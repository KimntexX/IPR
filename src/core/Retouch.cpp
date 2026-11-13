#include "Retouch.h"

#include "PanoramaFill.h"

#include "Filters.h"
#include "Pixels.h"
#include "Parallel.h"

#include <QStringLiteral>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Retouch {
namespace {

constexpr int mostOperations = 1000;
constexpr int mostStrokesPerOperation = 2000;
constexpr int mostPointsPerStroke = 20000;
constexpr float zoneEdge = 0.02f;          // coverage from which a pixel counts as in the zone
constexpr int previewLongSide = 2048;      // a fill searches at this scale, so that preview and export agree
constexpr double contextOfZone = 1.0;      // around a zone, what it is invented from: this many of its sides
constexpr int smallestContext = 48;        // pixels
constexpr double healBlurOfZone = 0.35;    // of a zone's long side: how slowly a heal's level varies
constexpr int suggestionSteps = 12;        // of the search for a source, per direction
constexpr int suggestionSamples = 400;     // pixels around a zone compared per candidate

inline bool emptyPixel(const ipr::Vec3f &pixel) { return pixel[0] == 0.0f && pixel[1] == 0.0f && pixel[2] == 0.0f; }

ipr::Recti zoneOf(const ipr::Image &zone)
{
    int left = zone.width(), right = -1, top = zone.height(), bottom = -1;
    for (int y = 0; y < zone.height(); ++y) {
        const float *row = zone.row<float>(y);
        for (int x = 0; x < zone.width(); ++x) {
            if (row[x] > zoneEdge) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = y;
            }
        }
    }
    return right < 0 ? ipr::Recti() : ipr::Recti(left, top, right - left + 1, bottom - top + 1);
}

void blendIn(ipr::Image &rgb, const ipr::Image &zone, const ipr::Recti &area, const ipr::Image &result, float opacity)
{
    for (int y = 0; y < area.height; ++y) {
        const float *weight = zone.row<float>(y + area.y) + area.x;
        const ipr::Vec3f *in = result.row<ipr::Vec3f>(y);
        ipr::Vec3f *out = rgb.mutableRow<ipr::Vec3f>(y + area.y) + area.x;
        for (int x = 0; x < area.width; ++x) {
            const float amount = weight[x] * opacity;
            if (amount > 0.0f && !emptyPixel(out[x]))
                out[x] = out[x] * (1.0f - amount) + in[x] * amount;
        }
    }
}

bool removeZone(ipr::Image &rgb, const ipr::Image &zone, const ipr::Recti &box, float opacity)
{
    const int reach = std::max(smallestContext, int(contextOfZone * std::max(box.width, box.height)));
    const ipr::Recti area = ipr::Recti(box.x - reach, box.y - reach, box.width + 2 * reach, box.height + 2 * reach) &
                          ipr::Recti(0, 0, rgb.width(), rgb.height());
    ipr::Image region = rgb.copy(area);
    ipr::Image known = ipr::Image::create(area.size(), ipr::Depth::U8, 1);
    for (int y = 0; y < area.height; ++y) {
        const float *weight = zone.row<float>(y + area.y) + area.x;
        const ipr::Vec3f *pixel = region.row<ipr::Vec3f>(y);
        std::uint8_t *out = known.mutableRow<std::uint8_t>(y);
        for (int x = 0; x < area.width; ++x)
            out[x] = weight[x] > zoneEdge || emptyPixel(pixel[x]) ? 0 : 255;
    }
    PanoramaFill::Options options;
    // The same search at any resolution: an export does not find something else than its preview.
    const double toPreview = std::min(1.0, double(previewLongSide) / std::max(rgb.width(), rgb.height()));
    options.workingSide = std::max(64, int(std::ceil(std::max(area.width, area.height) * toPreview)));
    options.transitionPixels = std::max(4, std::min(box.width, box.height) / 3);
    const PanoramaFill::Report report = PanoramaFill::fill(region, known, options);
    if (report.outcome != PanoramaFill::Outcome::Filled)
        return false;
    blendIn(rgb, zone, area, region, opacity);
    return true;
}

// Heal and Clone: the source's pixels, at the zone's level for a heal. False when the source
// falls outside the image.
bool copyZone(ipr::Image &rgb, const ipr::Image &zone, const ipr::Recti &box, const Operation &operation)
{
    const int shiftX = int(std::lround(operation.source.x * rgb.width()));
    const int shiftY = int(std::lround(operation.source.y * rgb.height()));
    const ipr::Recti image(0, 0, rgb.width(), rgb.height());
    if ((shiftX == 0 && shiftY == 0) || (box + ipr::Point2i(shiftX, shiftY) & image) != box + ipr::Point2i(shiftX, shiftY))
        return false;
    ipr::Image result = rgb.copy(box + ipr::Point2i(shiftX, shiftY));
    // Reprojection can leave the sampled area outside the available canvas.
    // Refuse that correction instead of copying its empty pixels as black.
    for (int y = 0; y < box.height; ++y) {
        const auto *pixels = result.row<ipr::Vec3f>(y);
        const auto *weights = zone.row<float>(box.y + y) + box.x;
        for (int x = 0; x < box.width; ++x)
            if (weights[x] > 0 && emptyPixel(pixels[x])) return false;
    }
    if (operation.kind == Kind::Heal) {
        // What separates this place from the source, measured around the zone, and carried
        // smoothly across it.
        const int reach = std::max(8, int(healBlurOfZone * std::max(box.width, box.height)) * 2);
        const ipr::Recti area = ipr::Recti(box.x - reach, box.y - reach, box.width + 2 * reach, box.height + 2 * reach) & image;
        const ipr::Recti shifted = (area + ipr::Point2i(shiftX, shiftY)) & image;
        const ipr::Recti measured = (shifted - ipr::Point2i(shiftX, shiftY)) & area;
        ipr::Image difference = ipr::Image::zeros(measured.size(), ipr::Depth::F32, 3);
        ipr::Image weight = ipr::Image::zeros(measured.size(), ipr::Depth::F32, 1);
        for (int y = 0; y < measured.height; ++y) {
            const float *in = zone.row<float>(y + measured.y) + measured.x;
            const float *inSource = zone.row<float>(y + measured.y + shiftY) + measured.x + shiftX;
            const ipr::Vec3f *here = rgb.row<ipr::Vec3f>(y + measured.y) + measured.x;
            const ipr::Vec3f *there = rgb.row<ipr::Vec3f>(y + measured.y + shiftY) + measured.x + shiftX;
            ipr::Vec3f *outDifference = difference.mutableRow<ipr::Vec3f>(y);
            float *outWeight = weight.mutableRow<float>(y);
            for (int x = 0; x < measured.width; ++x) {
                if (in[x] <= zoneEdge && inSource[x] <= zoneEdge && !emptyPixel(here[x]) && !emptyPixel(there[x])) {
                    outDifference[x] = here[x] - there[x];
                    outWeight[x] = 1.0f;
                }
            }
        }
        const double sigma = std::max(2.0, healBlurOfZone * std::max(box.width, box.height));
        difference = ipr::gaussianBlurred(difference, sigma, ipr::BlurBorder::LastPixel);
        weight = ipr::gaussianBlurred(weight, sigma, ipr::BlurBorder::LastPixel);
        for (int y = 0; y < box.height; ++y) {
            ipr::Vec3f *out = result.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < box.width; ++x) {
                const ipr::Point2i p(box.x + x - measured.x, box.y + y - measured.y);
                if (p.x < 0 || p.y < 0 || p.x >= measured.width || p.y >= measured.height)
                    continue;
                const float amount = weight.row<float>(p.y)[p.x];
                if (amount > 1e-4f)
                    out[x] += difference.row<ipr::Vec3f>(p.y)[p.x] * (1.0f / amount);
            }
        }
    }
    blendIn(rgb, zone, box, result, operation.opacity);
    return true;
}

} // namespace

bool operator==(const Operation &a, const Operation &b)
{
    return a.frame == b.frame && a.kind == b.kind && a.visible == b.visible && a.opacity == b.opacity && a.source == b.source && a.strokes == b.strokes;
}

bool bound(Operations &operations)
{
    if (int(operations.size()) > mostOperations)
        return false;
    for (Operation &operation : operations) {
        if (!Geometry::bound(operation.frame) || !std::isfinite(operation.opacity) || !std::isfinite(operation.source.x) || !std::isfinite(operation.source.y) ||
            int(operation.strokes.size()) > mostStrokesPerOperation)
            return false;
        operation.opacity = std::clamp(operation.opacity, 0.0f, 1.0f);
        operation.source.x = std::clamp(operation.source.x, -1.0f, 1.0f);
        operation.source.y = std::clamp(operation.source.y, -1.0f, 1.0f);
        for (LocalAdjustments::BrushStroke &stroke : operation.strokes) {
            if (!std::isfinite(stroke.size) || !std::isfinite(stroke.feather) || !std::isfinite(stroke.flow) ||
                int(stroke.points.size()) > mostPointsPerStroke)
                return false;
            stroke.size = std::clamp(stroke.size, 0.0005f, 1.0f);
            stroke.feather = std::clamp(stroke.feather, 0.0f, 1.0f);
            stroke.flow = 1.0f;
            for (ipr::Point2f &point : stroke.points) {
                if (!std::isfinite(point.x) || !std::isfinite(point.y))
                    return false;
                point.x = std::clamp(point.x, -1.0f, 2.0f);
                point.y = std::clamp(point.y, -1.0f, 2.0f);
            }
        }
    }
    return true;
}

bool active(const Operations &operations)
{
    return std::any_of(operations.cbegin(), operations.cend(), [](const Operation &operation) {
        return operation.visible && operation.opacity > 0.0f && !operation.strokes.empty();
    });
}

ipr::Image coverage(const Operation &operation, const ipr::Size &canvas)
{
    LocalAdjustments::Shape shape;
    shape.kind = LocalAdjustments::Kind::Brush;
    shape.strokes = operation.strokes;
    LocalAdjustments::Mask mask;
    mask.shapes.push_back(std::move(shape));
    return LocalAdjustments::coverage(mask, canvas);
}

namespace {
LocalAdjustments::Mask asMask(const Operation &operation)
{
    LocalAdjustments::Mask mask;
    mask.frame = operation.frame;
    LocalAdjustments::Shape shape;
    shape.kind = LocalAdjustments::Kind::Brush;
    shape.strokes = operation.strokes;
    mask.shapes.push_back(std::move(shape));
    return mask;
}
}

ipr::Image canvasCoverage(const Operation &operation, const Geometry::Settings &geometry, const ipr::Size &source)
{
    return LocalAdjustments::canvasCoverage(asMask(operation), geometry, source);
}

int applyOnCanvas(ipr::Image &rgb, const Operations &operations, const Geometry::Settings &geometry, const ipr::Size &source)
{
    if (rgb.empty() || rgb.depth() != ipr::Depth::F32 || rgb.channels() != 3 || source.empty()) return 0;
    int notDone = 0;
    const auto current = Geometry::homography(geometry, source);
    for (const auto &operation : operations) {
        if (!operation.visible || operation.opacity <= 0 || operation.strokes.empty()) continue;
        if (current == Geometry::homography(operation.frame, source)) {
            notDone += apply(rgb, {operation});
            continue;
        }
        const auto before = Geometry::reframe(rgb, geometry, operation.frame, source);
        if (before.empty()) { ++notDone; continue; }
        auto corrected = before.clone();
        const int failed = apply(corrected, {operation});
        if (failed) { notDone += failed; continue; }
        const auto delta = ipr::mapped(corrected, before, [](float a, float b) { return a-b; });
        const auto projected = Geometry::reframe(delta, operation.frame, geometry, source);
        if (projected.empty() || projected.size() != rgb.size()) { ++notDone; continue; }
        ipr::parallelRows(rgb.height(), [&](int first, int last) {
            for (int y=first; y<last; ++y) {
                auto *out = rgb.mutableRow<ipr::Vec3f>(y);
                const auto *change = projected.row<ipr::Vec3f>(y);
                for (int x=0; x<rgb.width(); ++x)
                    if (change[x] != ipr::Vec3f(0,0,0) && !emptyPixel(out[x])) out[x] += change[x];
            }
        });
    }
    return notDone;
}

ipr::Point2f suggestSourceOnCanvas(const ipr::Image &rgb, const Operation &operation,
                                  const Geometry::Settings &geometry, const ipr::Size &source)
{
    return suggestSource(Geometry::reframe(rgb, geometry, operation.frame, source), operation);
}

int apply(ipr::Image &rgb, const Operations &operations)
{
    if (rgb.empty() || !(rgb.depth() == ipr::Depth::F32 && rgb.channels() == 3))
        return 0;
    int notDone = 0;
    for (const Operation &operation : operations) {
        if (!operation.visible || operation.opacity <= 0.0f || operation.strokes.empty())
            continue;
        const ipr::Image zone = coverage(operation, rgb.size());
        const ipr::Recti box = zoneOf(zone);
        if (box.empty())
            continue;
        const bool done = operation.kind == Kind::Remove ? removeZone(rgb, zone, box, operation.opacity)
                                                         : copyZone(rgb, zone, box, operation);
        if (!done)
            ++notDone;
    }
    return notDone;
}

ipr::Point2f suggestSource(const ipr::Image &rgb, const Operation &operation)
{
    if (rgb.empty() || !(rgb.depth() == ipr::Depth::F32 && rgb.channels() == 3) || operation.strokes.empty())
        return {0.0f, 0.0f};
    const ipr::Image zone = coverage(operation, rgb.size());
    const ipr::Recti box = zoneOf(zone);
    if (box.empty())
        return {0.0f, 0.0f};

    // The zone's surroundings: what the source must match.
    const int ring = std::max(4, std::max(box.width, box.height) / 4);
    const ipr::Recti around = ipr::Recti(box.x - ring, box.y - ring, box.width + 2 * ring, box.height + 2 * ring) & ipr::Recti(0, 0, rgb.width(), rgb.height());
    const ipr::PixelReader<ipr::Vec3f> pixel(rgb);
    const ipr::PixelReader<float> zoneAt(zone);
    std::vector<ipr::Point2i> samples;
    const int stride = std::max(1, int(std::sqrt(double(around.area()) / suggestionSamples)));
    for (int y = around.y; y < around.y + around.height; y += stride)
        for (int x = around.x; x < around.x + around.width; x += stride)
            if (zoneAt(y, x) <= zoneEdge && !emptyPixel(pixel(y, x)))
                samples.emplace_back(x, y);
    if (samples.empty())
        return {0.0f, 0.0f};

    const int reachStep = std::max(2, std::max(box.width, box.height) / 2);
    const ipr::Recti whole(0, 0, rgb.width(), rgb.height());
    float bestCost = std::numeric_limits<float>::max();
    ipr::Point2i best(0, 0);
    for (int stepY = -suggestionSteps; stepY <= suggestionSteps; ++stepY) {
        for (int stepX = -suggestionSteps; stepX <= suggestionSteps; ++stepX) {
            const ipr::Point2i shift(stepX * reachStep, stepY * reachStep);
            const ipr::Recti moved = box + shift;
            // The source must be real, and not the zone itself.
            if ((moved & whole) != moved || (moved & box).area() > 0)
                continue;
            float sum = 0.0f;
            int count = 0;
            bool usable = true;
            for (const ipr::Point2i &sample : samples) {
                const ipr::Point2i there = sample + shift;
                if (!whole.contains(there))
                    continue;
                const ipr::Vec3f &value = pixel(there.y, there.x);
                if (emptyPixel(value) || zoneAt(there.y, there.x) > zoneEdge) {
                    usable = false;
                    break;
                }
                const ipr::Vec3f &here = pixel(sample.y, sample.x);
                sum += std::abs(std::sqrt(std::max(0.0f, here[1])) - std::sqrt(std::max(0.0f, value[1])));
                ++count;
            }
            if (!usable || count * 2 < int(samples.size()))
                continue;
            // Among equals, the closest: the light is likelier to be the same.
            const float distance = float(std::hypot(shift.x, shift.y)) / float(std::max(rgb.width(), rgb.height()));
            const float cost = sum / float(count) + 0.02f * distance;
            if (cost < bestCost) {
                bestCost = cost;
                best = shift;
            }
        }
    }
    return {float(best.x) / float(rgb.width()), float(best.y) / float(rgb.height())};
}

void appendKey(QDataStream &stream, const Operations &operations)
{
    if (!active(operations))
        return;
    stream << qint32(operations.size());
    for (const Operation &operation : operations) {
        if (!operation.visible || operation.opacity <= 0.0f)
            continue;
        const auto &g = operation.frame;
        stream << qint32(g.quarterTurns) << g.flipHorizontal << g.flipVertical << g.rotation
               << qint32(g.vertical) << qint32(g.horizontal) << qint32(g.aspect) << qint32(g.scale)
               << qint32(g.offsetX) << qint32(g.offsetY);
        stream << qint32(operation.kind) << operation.opacity << operation.source.x << operation.source.y << qint32(operation.strokes.size());
        for (const LocalAdjustments::BrushStroke &stroke : operation.strokes) {
            stream << stroke.size << stroke.feather << qint32(stroke.points.size());
            for (const ipr::Point2f &point : stroke.points)
                stream << point.x << point.y;
        }
    }
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Remove: return QStringLiteral("remove");
    case Kind::Heal: return QStringLiteral("heal");
    case Kind::Clone: return QStringLiteral("clone");
    }
    return {};
}

bool kindNamed(const QString &name, Kind &kind)
{
    for (const Kind candidate : {Kind::Remove, Kind::Heal, Kind::Clone}) {
        if (kindName(candidate) == name) {
            kind = candidate;
            return true;
        }
    }
    return false;
}

QVariantList describe(const Operations &operations)
{
    QVariantList list;
    for (const Operation &operation : operations) {
        QVariantMap entry;
        entry.insert(QStringLiteral("kind"), kindName(operation.kind));
        entry.insert(QStringLiteral("visible"), operation.visible);
        entry.insert(QStringLiteral("opacity"), double(operation.opacity));
        entry.insert(QStringLiteral("strokes"), int(operation.strokes.size()));
        entry.insert(QStringLiteral("sourceX"), double(operation.source.x));
        entry.insert(QStringLiteral("sourceY"), double(operation.source.y));
        ipr::Point2f centre(0.5f, 0.5f);
        qint64 points = 0;
        ipr::Point2d sum(0.0, 0.0);
        for (const LocalAdjustments::BrushStroke &stroke : operation.strokes) {
            for (const ipr::Point2f &point : stroke.points) {
                sum += ipr::Point2d(point);
                ++points;
            }
        }
        if (points > 0)
            centre = ipr::Point2f(float(sum.x / double(points)), float(sum.y / double(points)));
        entry.insert(QStringLiteral("centerX"), double(centre.x));
        entry.insert(QStringLiteral("centerY"), double(centre.y));
        list.append(entry);
    }
    return list;
}

QVariantList describeOnCanvas(const Operations &operations, const Geometry::Settings &geometry, const ipr::Size &source)
{
    auto list = describe(operations);
    LocalAdjustments::Masks masks;
    for (const auto &operation : operations) masks.push_back(asMask(operation));
    const auto frames = LocalAdjustments::describeOnCanvas(masks, geometry, source);
    for (qsizetype i=0; i<list.size(); ++i) {
        auto entry = list[i].toMap(); const auto frame = frames[i].toMap();
        for (const auto &key : {QStringLiteral("frameToCanvas"),QStringLiteral("canvasToFrame"),QStringLiteral("frameAspect")})
            if (frame.contains(key)) entry.insert(key, frame.value(key));
        list[i] = entry;
    }
    return list;
}

} // namespace Retouch
