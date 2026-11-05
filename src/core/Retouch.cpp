#include "Retouch.h"

#include "PanoramaFill.h"

#include <QStringLiteral>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
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

inline bool emptyPixel(const cv::Vec3f &pixel) { return pixel[0] == 0.0f && pixel[1] == 0.0f && pixel[2] == 0.0f; }

cv::Rect zoneOf(const cv::Mat &zone)
{
    int left = zone.cols, right = -1, top = zone.rows, bottom = -1;
    for (int y = 0; y < zone.rows; ++y) {
        const float *row = zone.ptr<float>(y);
        for (int x = 0; x < zone.cols; ++x) {
            if (row[x] > zoneEdge) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = y;
            }
        }
    }
    return right < 0 ? cv::Rect() : cv::Rect(left, top, right - left + 1, bottom - top + 1);
}

void blendIn(cv::Mat &rgb, const cv::Mat &zone, const cv::Rect &area, const cv::Mat &result, float opacity)
{
    for (int y = 0; y < area.height; ++y) {
        const float *weight = zone.ptr<float>(y + area.y) + area.x;
        const cv::Vec3f *in = result.ptr<cv::Vec3f>(y);
        cv::Vec3f *out = rgb.ptr<cv::Vec3f>(y + area.y) + area.x;
        for (int x = 0; x < area.width; ++x) {
            const float amount = weight[x] * opacity;
            if (amount > 0.0f && !emptyPixel(out[x]))
                out[x] = out[x] * (1.0f - amount) + in[x] * amount;
        }
    }
}

bool removeZone(cv::Mat &rgb, const cv::Mat &zone, const cv::Rect &box, float opacity)
{
    const int reach = std::max(smallestContext, int(contextOfZone * std::max(box.width, box.height)));
    const cv::Rect area = cv::Rect(box.x - reach, box.y - reach, box.width + 2 * reach, box.height + 2 * reach) &
                          cv::Rect(0, 0, rgb.cols, rgb.rows);
    cv::Mat region = rgb(area).clone();
    cv::Mat known(area.size(), CV_8UC1);
    for (int y = 0; y < area.height; ++y) {
        const float *weight = zone.ptr<float>(y + area.y) + area.x;
        const cv::Vec3f *pixel = region.ptr<cv::Vec3f>(y);
        uchar *out = known.ptr<uchar>(y);
        for (int x = 0; x < area.width; ++x)
            out[x] = weight[x] > zoneEdge || emptyPixel(pixel[x]) ? 0 : 255;
    }
    PanoramaFill::Options options;
    // The same search at any resolution: an export does not find something else than its preview.
    const double toPreview = std::min(1.0, double(previewLongSide) / std::max(rgb.cols, rgb.rows));
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
bool copyZone(cv::Mat &rgb, const cv::Mat &zone, const cv::Rect &box, const Operation &operation)
{
    const int shiftX = int(std::lround(operation.source.x * rgb.cols));
    const int shiftY = int(std::lround(operation.source.y * rgb.rows));
    const cv::Rect image(0, 0, rgb.cols, rgb.rows);
    if ((shiftX == 0 && shiftY == 0) || (box + cv::Point(shiftX, shiftY) & image) != box + cv::Point(shiftX, shiftY))
        return false;
    cv::Mat result = rgb(box + cv::Point(shiftX, shiftY)).clone();
    if (operation.kind == Kind::Heal) {
        // What separates this place from the source, measured around the zone, and carried
        // smoothly across it.
        const int reach = std::max(8, int(healBlurOfZone * std::max(box.width, box.height)) * 2);
        const cv::Rect area = cv::Rect(box.x - reach, box.y - reach, box.width + 2 * reach, box.height + 2 * reach) & image;
        const cv::Rect shifted = (area + cv::Point(shiftX, shiftY)) & image;
        const cv::Rect measured = (shifted - cv::Point(shiftX, shiftY)) & area;
        cv::Mat difference = cv::Mat::zeros(measured.size(), CV_32FC3), weight = cv::Mat::zeros(measured.size(), CV_32FC1);
        for (int y = 0; y < measured.height; ++y) {
            const float *in = zone.ptr<float>(y + measured.y) + measured.x;
            const float *inSource = zone.ptr<float>(y + measured.y + shiftY) + measured.x + shiftX;
            const cv::Vec3f *here = rgb.ptr<cv::Vec3f>(y + measured.y) + measured.x;
            const cv::Vec3f *there = rgb.ptr<cv::Vec3f>(y + measured.y + shiftY) + measured.x + shiftX;
            cv::Vec3f *outDifference = difference.ptr<cv::Vec3f>(y);
            float *outWeight = weight.ptr<float>(y);
            for (int x = 0; x < measured.width; ++x) {
                if (in[x] <= zoneEdge && inSource[x] <= zoneEdge && !emptyPixel(here[x]) && !emptyPixel(there[x])) {
                    outDifference[x] = here[x] - there[x];
                    outWeight[x] = 1.0f;
                }
            }
        }
        const double sigma = std::max(2.0, healBlurOfZone * std::max(box.width, box.height));
        cv::GaussianBlur(difference, difference, cv::Size(), sigma, sigma, cv::BORDER_REPLICATE);
        cv::GaussianBlur(weight, weight, cv::Size(), sigma, sigma, cv::BORDER_REPLICATE);
        for (int y = 0; y < box.height; ++y) {
            cv::Vec3f *out = result.ptr<cv::Vec3f>(y);
            for (int x = 0; x < box.width; ++x) {
                const cv::Point p(box.x + x - measured.x, box.y + y - measured.y);
                if (p.x < 0 || p.y < 0 || p.x >= measured.width || p.y >= measured.height)
                    continue;
                const float amount = weight.at<float>(p);
                if (amount > 1e-4f)
                    out[x] += difference.at<cv::Vec3f>(p) * (1.0f / amount);
            }
        }
    }
    blendIn(rgb, zone, box, result, operation.opacity);
    return true;
}

} // namespace

bool operator==(const Operation &a, const Operation &b)
{
    return a.kind == b.kind && a.visible == b.visible && a.opacity == b.opacity && a.source == b.source && a.strokes == b.strokes;
}

bool bound(Operations &operations)
{
    if (int(operations.size()) > mostOperations)
        return false;
    for (Operation &operation : operations) {
        if (!std::isfinite(operation.opacity) || !std::isfinite(operation.source.x) || !std::isfinite(operation.source.y) ||
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
            for (cv::Point2f &point : stroke.points) {
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

cv::Mat coverage(const Operation &operation, const cv::Size &canvas)
{
    LocalAdjustments::Shape shape;
    shape.kind = LocalAdjustments::Kind::Brush;
    shape.strokes = operation.strokes;
    LocalAdjustments::Mask mask;
    mask.shapes.push_back(std::move(shape));
    return LocalAdjustments::coverage(mask, canvas);
}

int apply(cv::Mat &rgb, const Operations &operations)
{
    if (rgb.empty() || rgb.type() != CV_32FC3)
        return 0;
    int notDone = 0;
    for (const Operation &operation : operations) {
        if (!operation.visible || operation.opacity <= 0.0f || operation.strokes.empty())
            continue;
        const cv::Mat zone = coverage(operation, rgb.size());
        const cv::Rect box = zoneOf(zone);
        if (box.empty())
            continue;
        const bool done = operation.kind == Kind::Remove ? removeZone(rgb, zone, box, operation.opacity)
                                                         : copyZone(rgb, zone, box, operation);
        if (!done)
            ++notDone;
    }
    return notDone;
}

cv::Point2f suggestSource(const cv::Mat &rgb, const Operation &operation)
{
    if (rgb.empty() || rgb.type() != CV_32FC3 || operation.strokes.empty())
        return {0.0f, 0.0f};
    const cv::Mat zone = coverage(operation, rgb.size());
    const cv::Rect box = zoneOf(zone);
    if (box.empty())
        return {0.0f, 0.0f};

    // The zone's surroundings: what the source must match.
    const int ring = std::max(4, std::max(box.width, box.height) / 4);
    const cv::Rect around = cv::Rect(box.x - ring, box.y - ring, box.width + 2 * ring, box.height + 2 * ring) & cv::Rect(0, 0, rgb.cols, rgb.rows);
    std::vector<cv::Point> samples;
    const int stride = std::max(1, int(std::sqrt(double(around.area()) / suggestionSamples)));
    for (int y = around.y; y < around.y + around.height; y += stride)
        for (int x = around.x; x < around.x + around.width; x += stride)
            if (zone.at<float>(y, x) <= zoneEdge && !emptyPixel(rgb.at<cv::Vec3f>(y, x)))
                samples.emplace_back(x, y);
    if (samples.empty())
        return {0.0f, 0.0f};

    const int reachStep = std::max(2, std::max(box.width, box.height) / 2);
    const cv::Rect whole(0, 0, rgb.cols, rgb.rows);
    float bestCost = std::numeric_limits<float>::max();
    cv::Point best(0, 0);
    for (int stepY = -suggestionSteps; stepY <= suggestionSteps; ++stepY) {
        for (int stepX = -suggestionSteps; stepX <= suggestionSteps; ++stepX) {
            const cv::Point shift(stepX * reachStep, stepY * reachStep);
            const cv::Rect moved = box + shift;
            // The source must be real, and not the zone itself.
            if ((moved & whole) != moved || (moved & box).area() > 0)
                continue;
            float sum = 0.0f;
            int count = 0;
            bool usable = true;
            for (const cv::Point &sample : samples) {
                const cv::Point there = sample + shift;
                if (!whole.contains(there))
                    continue;
                const cv::Vec3f &value = rgb.at<cv::Vec3f>(there);
                if (emptyPixel(value) || zone.at<float>(there) > zoneEdge) {
                    usable = false;
                    break;
                }
                const cv::Vec3f &here = rgb.at<cv::Vec3f>(sample);
                sum += std::abs(std::sqrt(std::max(0.0f, here[1])) - std::sqrt(std::max(0.0f, value[1])));
                ++count;
            }
            if (!usable || count * 2 < int(samples.size()))
                continue;
            // Among equals, the closest: the light is likelier to be the same.
            const float distance = float(std::hypot(shift.x, shift.y)) / float(std::max(rgb.cols, rgb.rows));
            const float cost = sum / float(count) + 0.02f * distance;
            if (cost < bestCost) {
                bestCost = cost;
                best = shift;
            }
        }
    }
    return {float(best.x) / float(rgb.cols), float(best.y) / float(rgb.rows)};
}

void appendKey(QDataStream &stream, const Operations &operations)
{
    if (!active(operations))
        return;
    stream << qint32(operations.size());
    for (const Operation &operation : operations) {
        if (!operation.visible || operation.opacity <= 0.0f)
            continue;
        stream << qint32(operation.kind) << operation.opacity << operation.source.x << operation.source.y << qint32(operation.strokes.size());
        for (const LocalAdjustments::BrushStroke &stroke : operation.strokes) {
            stream << stroke.size << stroke.feather << qint32(stroke.points.size());
            for (const cv::Point2f &point : stroke.points)
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
        cv::Point2f centre(0.5f, 0.5f);
        qint64 points = 0;
        cv::Point2d sum(0.0, 0.0);
        for (const LocalAdjustments::BrushStroke &stroke : operation.strokes) {
            for (const cv::Point2f &point : stroke.points) {
                sum += cv::Point2d(point);
                ++points;
            }
        }
        if (points > 0)
            centre = cv::Point2f(float(sum.x / double(points)), float(sum.y / double(points)));
        entry.insert(QStringLiteral("centerX"), double(centre.x));
        entry.insert(QStringLiteral("centerY"), double(centre.y));
        list.append(entry);
    }
    return list;
}

} // namespace Retouch
