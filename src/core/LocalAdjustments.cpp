#include "LocalAdjustments.h"

#include "ImageMath.h"
#include "WorkingColorSpace.h"

#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace LocalAdjustments {
namespace {

constexpr int largestMaskCount = 64;
constexpr int largestShapeCount = 64;
constexpr int largestStrokeCount = 4096;
constexpr int largestPointCount = 65536;

// In the sliders' units; the modules bound the sums with the global values.
constexpr std::array<float, LocalValues::ParameterCount> valueLimit{
    5.0f,    // Exposure
    100.0f,  // Contrast
    5000.0f, // Temperature
    150.0f,  // Tint
    100.0f, 100.0f,                  // Saturation, Vibrance
    100.0f, 100.0f, 100.0f, 100.0f,  // Shadows, Blacks, Highlights, Whites
    100.0f, 100.0f, 100.0f,          // Texture, Clarity, Dehaze
    100.0f, 100.0f,                  // DenoiseLuminance, DenoiseChrominance
    200.0f                           // Sharpening
};

bool finite(const cv::Point2f &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

float fadeInside(float position, float feather)
{
    // 1 up to 1 - feather, falling to 0 at 1.
    return feather <= 0.0f ? (position <= 1.0f ? 1.0f : 0.0f)
                           : 1.0f - ImageMath::smoothStep(1.0f - feather, 1.0f, position);
}

cv::Point2f pixelOf(const cv::Point2f &point, const cv::Size &canvas)
{
    return {point.x * float(canvas.width) - 0.5f, point.y * float(canvas.height) - 0.5f};
}

void paintLinear(const Shape &shape, cv::Mat &out)
{
    const cv::Point2f from = pixelOf(shape.start, out.size()), to = pixelOf(shape.end, out.size());
    const cv::Point2f axis = to - from;
    const float lengthSquared = axis.dot(axis);
    cv::parallel_for_(cv::Range(0, out.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            float *row = out.ptr<float>(y);
            for (int x = 0; x < out.cols; ++x) {
                if (lengthSquared <= 0.0f) {
                    // No length: a sharp edge at the point, along no direction: all of it.
                    row[x] = 1.0f;
                    continue;
                }
                const float t = (float(x) - from.x) * axis.x / lengthSquared + (float(y) - from.y) * axis.y / lengthSquared;
                row[x] = 1.0f - ImageMath::smoothStep(0.0f, 1.0f, t);
            }
        }
    });
}

void paintRadial(const Shape &shape, cv::Mat &out)
{
    const cv::Point2f centre = pixelOf(shape.center, out.size());
    const float radiusX = std::max(1e-3f, shape.radiusX * float(out.cols));
    const float radiusY = std::max(1e-3f, shape.radiusY * float(out.rows));
    const float radians = shape.angle * float(CV_PI) / 180.0f;
    const float c = std::cos(radians), s = std::sin(radians);
    cv::parallel_for_(cv::Range(0, out.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            float *row = out.ptr<float>(y);
            const float dy = float(y) - centre.y;
            for (int x = 0; x < out.cols; ++x) {
                const float dx = float(x) - centre.x;
                const float u = (dx * c + dy * s) / radiusX, v = (dy * c - dx * s) / radiusY;
                row[x] = fadeInside(std::sqrt(u * u + v * v), shape.feather);
            }
        }
    });
}

// The points that shape a line, within a distance below what a coverage can show: each span
// keeps its furthest point while it lies too far from the span's straight line.
std::vector<cv::Point2f> shapingPoints(const std::vector<cv::Point2f> &line)
{
    constexpr float furthestFromTheLine = 0.05f;   // pixels
    if (line.size() <= 2)
        return line;
    std::vector<char> kept(line.size(), 0);
    kept.front() = kept.back() = 1;
    std::vector<std::pair<size_t, size_t>> spans{{0, line.size() - 1}};
    while (!spans.empty()) {
        const auto [first, last] = spans.back();
        spans.pop_back();
        const cv::Point2f a = line[first], ab = line[last] - a;
        const float squared = ab.dot(ab);
        float furthest = 0.0f;
        size_t at = first;
        for (size_t i = first + 1; i < last; ++i) {
            const float t = squared > 0.0f ? std::clamp((line[i] - a).dot(ab) / squared, 0.0f, 1.0f) : 0.0f;
            const cv::Point2f to = line[i] - (a + ab * t);
            const float distance = to.dot(to);
            if (distance > furthest) {
                furthest = distance;
                at = i;
            }
        }
        if (furthest > furthestFromTheLine * furthestFromTheLine) {
            kept[at] = 1;
            spans.emplace_back(first, at);
            spans.emplace_back(at, last);
        }
    }
    std::vector<cv::Point2f> shaping;
    for (size_t i = 0; i < line.size(); ++i)
        if (kept[i])
            shaping.push_back(line[i]);
    return shaping;
}

// One stroke at a time: within a stroke the coverage is that of its nearest point, so that
// its own overlaps do not build up; strokes then build up as paint does. The distances stay
// exact, at any resolution. The cost stays low because a hand's dense points are first reduced
// to the few that shape the line, and each row only looks at the segments within reach of it.
void paintStroke(const BrushStroke &stroke, cv::Mat &out)
{
    if (stroke.points.empty())
        return;
    const float radius = std::max(0.5f, stroke.size * float(std::max(out.cols, out.rows)));
    std::vector<cv::Point2f> dense;
    dense.reserve(stroke.points.size());
    for (const cv::Point2f &point : stroke.points)
        dense.push_back(pixelOf(point, out.size()));
    std::vector<cv::Point2f> points = shapingPoints(dense);
    if (points.size() == 1)
        points.push_back(points.front());

    cv::Point2f lowest = points.front(), highest = lowest;
    for (const cv::Point2f &point : points) {
        lowest = {std::min(lowest.x, point.x), std::min(lowest.y, point.y)};
        highest = {std::max(highest.x, point.x), std::max(highest.y, point.y)};
    }
    const int reach = int(std::ceil(radius)) + 1;
    const cv::Rect area = cv::Rect(cv::Point(int(std::floor(lowest.x)) - reach, int(std::floor(lowest.y)) - reach),
                                   cv::Point(int(std::ceil(highest.x)) + reach + 1, int(std::ceil(highest.y)) + reach + 1))
                          & cv::Rect(0, 0, out.cols, out.rows);
    if (area.empty())
        return;
    const float reachSquared = radius * radius;
    cv::parallel_for_(cv::Range(area.y, area.y + area.height), [&](const cv::Range &range) {
        std::vector<size_t> near;
        near.reserve(points.size());
        for (int y = range.start; y < range.end; ++y) {
            near.clear();
            const float py = float(y);
            for (size_t i = 0; i + 1 < points.size(); ++i)
                if (std::min(points[i].y, points[i + 1].y) - radius <= py && py <= std::max(points[i].y, points[i + 1].y) + radius)
                    near.push_back(i);
            if (near.empty())
                continue;
            float *row = out.ptr<float>(y);
            for (int x = area.x; x < area.x + area.width; ++x) {
                const cv::Point2f pixel{float(x), py};
                float nearest = reachSquared;
                for (const size_t i : near) {
                    const cv::Point2f &a = points[i], &b = points[i + 1];
                    if (std::min(a.x, b.x) - radius > pixel.x || pixel.x > std::max(a.x, b.x) + radius)
                        continue;
                    const cv::Point2f ab = b - a;
                    const float squared = ab.dot(ab);
                    const float t = squared > 0.0f ? std::clamp((pixel - a).dot(ab) / squared, 0.0f, 1.0f) : 0.0f;
                    const cv::Point2f to = pixel - (a + ab * t);
                    nearest = std::min(nearest, to.dot(to));
                }
                if (nearest >= reachSquared)
                    continue;
                const float own = stroke.flow * fadeInside(std::sqrt(nearest) / radius, stroke.feather);
                row[x] = row[x] + own - row[x] * own;
            }
        }
    });
}

void paintRange(const Shape &shape, const cv::Mat &luminance, cv::Mat &out)
{
    if (luminance.empty() || luminance.size() != out.size() || luminance.type() != CV_32FC1)
        return;
    const float feather = std::max(1e-4f, shape.rangeFeather);
    cv::parallel_for_(cv::Range(0, out.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            float *row = out.ptr<float>(y);
            const float *in = luminance.ptr<float>(y);
            for (int x = 0; x < out.cols; ++x) {
                const float low = ImageMath::smoothStep(shape.rangeLow - feather, shape.rangeLow, in[x]);
                const float high = 1.0f - ImageMath::smoothStep(shape.rangeHigh, shape.rangeHigh + feather, in[x]);
                row[x] = low * high;
            }
        }
    });
}

cv::Mat shapeCoverage(const Shape &shape, const cv::Size &canvas, const cv::Mat &luminance)
{
    cv::Mat out(canvas, CV_32FC1, cv::Scalar(0));
    switch (shape.kind) {
    case Kind::Linear:
        paintLinear(shape, out);
        break;
    case Kind::Radial:
        paintRadial(shape, out);
        break;
    case Kind::Brush:
        for (const BrushStroke &stroke : shape.strokes)
            paintStroke(stroke, out);
        break;
    case Kind::LuminanceRange:
        paintRange(shape, luminance, out);
        break;
    }
    if (shape.inverted)
        out = ipr::subtractedFrom(1.0, out);
    return out;
}

template <typename Key>
void writeFloats(QDataStream &stream, const Key &values)
{
    for (const float value : values)
        stream << value;
}

constexpr std::array<const char *, LocalValues::ParameterCount> parameterNames{
    "exposure", "contrast", "temperature", "tint", "saturation", "vibrance",
    "shadows", "blacks", "highlights", "whites",
    "texture", "clarity", "dehaze", "denoiseLuminance", "denoiseChrominance", "sharpening"};

QVariantList pointValue(const cv::Point2f &point)
{
    return {double(point.x), double(point.y)};
}

bool readPoint(const QVariant &value, cv::Point2f &point)
{
    const QVariantList list = value.toList();
    if (list.size() != 2)
        return false;
    bool okX = false, okY = false;
    const float x = list.at(0).toFloat(&okX), y = list.at(1).toFloat(&okY);
    if (!okX || !okY)
        return false;
    point = {x, y};
    return true;
}

bool readFloat(const QVariant &value, float &number)
{
    bool ok = false;
    const float read = value.toFloat(&ok);
    if (ok)
        number = read;
    return ok;
}

} // namespace

bool operator==(const BrushStroke &a, const BrushStroke &b)
{
    return a.points == b.points && a.size == b.size && a.feather == b.feather && a.flow == b.flow;
}

bool operator==(const Shape &a, const Shape &b)
{
    return a.kind == b.kind && a.combine == b.combine && a.inverted == b.inverted && a.start == b.start && a.end == b.end &&
           a.center == b.center && a.radiusX == b.radiusX && a.radiusY == b.radiusY && a.angle == b.angle &&
           a.feather == b.feather && a.strokes == b.strokes && a.rangeLow == b.rangeLow &&
           a.rangeHigh == b.rangeHigh && a.rangeFeather == b.rangeFeather;
}

bool operator==(const Mask &a, const Mask &b)
{
    return a.name == b.name && a.visible == b.visible && a.opacity == b.opacity && a.shapes == b.shapes && a.values == b.values;
}

bool bound(Masks &masks)
{
    if (int(masks.size()) > largestMaskCount)
        return false;
    // Coordinates may leave the canvas (a gradient can start beyond it), not without limit.
    const auto place = [](cv::Point2f &point) {
        if (!finite(point))
            return false;
        point = {std::clamp(point.x, -4.0f, 5.0f), std::clamp(point.y, -4.0f, 5.0f)};
        return true;
    };
    for (Mask &mask : masks) {
        if (!std::isfinite(mask.opacity) || int(mask.shapes.size()) > largestShapeCount)
            return false;
        mask.opacity = std::clamp(mask.opacity, 0.0f, 1.0f);
        for (size_t i = 0; i < mask.values.size(); ++i) {
            if (!std::isfinite(mask.values[i]))
                return false;
            mask.values[i] = std::clamp(mask.values[i], -valueLimit[i], valueLimit[i]);
        }
        for (Shape &shape : mask.shapes) {
            if (!place(shape.start) || !place(shape.end) || !place(shape.center) || !std::isfinite(shape.radiusX) ||
                !std::isfinite(shape.radiusY) || !std::isfinite(shape.angle) || !std::isfinite(shape.feather) ||
                !std::isfinite(shape.rangeLow) || !std::isfinite(shape.rangeHigh) || !std::isfinite(shape.rangeFeather) ||
                int(shape.strokes.size()) > largestStrokeCount)
                return false;
            shape.rangeLow = std::clamp(shape.rangeLow, 0.0f, 1.0f);
            shape.rangeHigh = std::clamp(shape.rangeHigh, shape.rangeLow, 1.0f);
            shape.rangeFeather = std::clamp(shape.rangeFeather, 0.0f, 1.0f);
            shape.radiusX = std::clamp(shape.radiusX, 0.0f, 8.0f);
            shape.radiusY = std::clamp(shape.radiusY, 0.0f, 8.0f);
            shape.angle = std::clamp(shape.angle, -360.0f, 360.0f);
            shape.feather = std::clamp(shape.feather, 0.0f, 1.0f);
            for (BrushStroke &stroke : shape.strokes) {
                if (!std::isfinite(stroke.size) || !std::isfinite(stroke.feather) || !std::isfinite(stroke.flow) ||
                    int(stroke.points.size()) > largestPointCount)
                    return false;
                stroke.size = std::clamp(stroke.size, 0.0f, 1.0f);
                stroke.feather = std::clamp(stroke.feather, 0.0f, 1.0f);
                stroke.flow = std::clamp(stroke.flow, 0.0f, 1.0f);
                for (cv::Point2f &point : stroke.points)
                    if (!place(point))
                        return false;
            }
        }
    }
    return true;
}

bool active(const Masks &masks)
{
    return std::any_of(masks.cbegin(), masks.cend(), [](const Mask &mask) {
        return mask.visible && mask.opacity > 0.0f && !mask.shapes.empty() &&
               std::any_of(mask.values.cbegin(), mask.values.cend(), [](float value) { return value != 0.0f; });
    });
}

bool usesLuminance(const Masks &masks)
{
    for (const Mask &mask : masks)
        for (const Shape &shape : mask.shapes)
            if (shape.kind == Kind::LuminanceRange)
                return true;
    return false;
}

cv::Mat rangeLuminance(const cv::Mat &linearRgb)
{
    cv::Mat luminance(linearRgb.size(), CV_32FC1);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            const cv::Vec3f *in = linearRgb.ptr<cv::Vec3f>(y);
            float *out = luminance.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x)
                out[x] = WorkingColor::srgbEncode(std::clamp(WorkingColor::nonNegativeLuminance(in[x]), 0.0f, 1.0f));
        }
    });
    return luminance;
}

cv::Mat coverage(const Mask &mask, const cv::Size &canvas, const cv::Mat &luminance)
{
    cv::Mat combined(canvas, CV_32FC1, cv::Scalar(0));
    for (const Shape &shape : mask.shapes) {
        const cv::Mat own = shapeCoverage(shape, canvas, luminance);
        switch (shape.combine) {
        case Combine::Add:
            combined = combined + own - combined.mul(own);
            break;
        case Combine::Subtract:
            combined = combined.mul(1.0 - own);
            break;
        case Combine::Intersect:
            combined = combined.mul(own);
            break;
        }
    }
    return combined;
}

LocalValues::Maps valueMaps(const Masks &masks, const cv::Size &canvas, const cv::Mat &luminance)
{
    LocalValues::Maps maps;
    if (!active(masks) || canvas.empty())
        return maps;
    for (const Mask &mask : masks) {
        if (!mask.visible || mask.opacity <= 0.0f || mask.shapes.empty() ||
            std::all_of(mask.values.cbegin(), mask.values.cend(), [](float value) { return value == 0.0f; }))
            continue;
        const cv::Mat weight = coverage(mask, canvas, luminance) * double(mask.opacity);
        for (size_t i = 0; i < mask.values.size(); ++i) {
            if (mask.values[i] == 0.0f)
                continue;
            cv::Mat &map = maps.values[i];
            if (map.empty())
                map = cv::Mat(canvas, CV_32FC1, cv::Scalar(0));
            map = ipr::addScaled(weight, double(mask.values[i]), map);
        }
    }
    return maps;
}

cv::Mat toSource(const cv::Mat &canvasMap, const Geometry::Settings &geometry, const cv::Size &source)
{
    if (canvasMap.empty() || !Geometry::transformsPixels(geometry))
        return canvasMap;
    const cv::Size canvas = canvasMap.size();
    // From a source pixel's index to a canvas pixel's: normalized at their outer edges in between.
    const cv::Matx33d sourceToNormal(1.0 / source.width, 0, 0.5 / source.width, 0, 1.0 / source.height, 0.5 / source.height, 0, 0, 1);
    const cv::Matx33d normalToCanvas(canvas.width, 0, -0.5, 0, canvas.height, -0.5, 0, 0, 1);
    const cv::Matx33d sourceToCanvas = normalToCanvas * Geometry::homography(geometry, source) * sourceToNormal;
    cv::Mat out = ipr::warpedPerspective(canvasMap, sourceToCanvas, source, ipr::Resampling::Linear, ipr::Outside::LastPixel);
    return out;
}

void appendKey(QDataStream &stream, const Masks &masks)
{
    if (!active(masks))
        return;
    stream << qint32(masks.size());
    for (const Mask &mask : masks) {
        stream << mask.visible << mask.opacity << qint32(mask.shapes.size());
        writeFloats(stream, mask.values);
        for (const Shape &shape : mask.shapes) {
            stream << qint32(shape.kind) << qint32(shape.combine) << shape.inverted << shape.start.x << shape.start.y
                   << shape.end.x << shape.end.y << shape.center.x << shape.center.y << shape.radiusX << shape.radiusY
                   << shape.angle << shape.feather << shape.rangeLow << shape.rangeHigh << shape.rangeFeather
                   << qint32(shape.strokes.size());
            for (const BrushStroke &stroke : shape.strokes) {
                stream << stroke.size << stroke.feather << stroke.flow << qint32(stroke.points.size());
                for (const cv::Point2f &point : stroke.points)
                    stream << point.x << point.y;
            }
        }
    }
}

QString parameterName(int parameter)
{
    return parameter >= 0 && parameter < LocalValues::ParameterCount ? QString::fromLatin1(parameterNames[size_t(parameter)]) : QString();
}

int parameterNamed(const QString &name)
{
    for (size_t i = 0; i < parameterNames.size(); ++i)
        if (name == QLatin1String(parameterNames[i]))
            return int(i);
    return -1;
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Linear: return QStringLiteral("linear");
    case Kind::Radial: return QStringLiteral("radial");
    case Kind::Brush: return QStringLiteral("brush");
    case Kind::LuminanceRange: return QStringLiteral("luminanceRange");
    }
    return {};
}

QString combineName(Combine combine)
{
    switch (combine) {
    case Combine::Add: return QStringLiteral("add");
    case Combine::Subtract: return QStringLiteral("subtract");
    case Combine::Intersect: return QStringLiteral("intersect");
    }
    return {};
}

bool kindNamed(const QString &name, Kind &kind)
{
    for (const Kind candidate : {Kind::Linear, Kind::Radial, Kind::Brush, Kind::LuminanceRange})
        if (name == kindName(candidate)) {
            kind = candidate;
            return true;
        }
    return false;
}

bool combineNamed(const QString &name, Combine &combine)
{
    for (const Combine candidate : {Combine::Add, Combine::Subtract, Combine::Intersect})
        if (name == combineName(candidate)) {
            combine = candidate;
            return true;
        }
    return false;
}

QVariantList describe(const Masks &masks)
{
    QVariantList list;
    for (const Mask &mask : masks) {
        QVariantMap values;
        for (size_t i = 0; i < mask.values.size(); ++i)
            values.insert(parameterName(int(i)), double(mask.values[i]));
        QVariantList shapes;
        for (const Shape &shape : mask.shapes)
            shapes.append(QVariantMap{
                {QStringLiteral("kind"), kindName(shape.kind)}, {QStringLiteral("combine"), combineName(shape.combine)},
                {QStringLiteral("inverted"), shape.inverted}, {QStringLiteral("start"), pointValue(shape.start)},
                {QStringLiteral("end"), pointValue(shape.end)}, {QStringLiteral("center"), pointValue(shape.center)},
                {QStringLiteral("radiusX"), double(shape.radiusX)}, {QStringLiteral("radiusY"), double(shape.radiusY)},
                {QStringLiteral("angle"), double(shape.angle)}, {QStringLiteral("feather"), double(shape.feather)},
                {QStringLiteral("strokes"), int(shape.strokes.size())},
                {QStringLiteral("rangeLow"), double(shape.rangeLow)}, {QStringLiteral("rangeHigh"), double(shape.rangeHigh)},
                {QStringLiteral("rangeFeather"), double(shape.rangeFeather)}});
        list.append(QVariantMap{{QStringLiteral("name"), mask.name}, {QStringLiteral("visible"), mask.visible},
                                {QStringLiteral("opacity"), double(mask.opacity)}, {QStringLiteral("values"), values},
                                {QStringLiteral("shapes"), shapes}});
    }
    return list;
}

bool setShapeFields(Shape &shape, const QVariantMap &values)
{
    for (auto field = values.cbegin(); field != values.cend(); ++field) {
        const QString &key = field.key();
        const QVariant &value = field.value();
        bool ok = true;
        if (key == QStringLiteral("start")) ok = readPoint(value, shape.start);
        else if (key == QStringLiteral("end")) ok = readPoint(value, shape.end);
        else if (key == QStringLiteral("center")) ok = readPoint(value, shape.center);
        else if (key == QStringLiteral("radiusX")) ok = readFloat(value, shape.radiusX);
        else if (key == QStringLiteral("radiusY")) ok = readFloat(value, shape.radiusY);
        else if (key == QStringLiteral("angle")) ok = readFloat(value, shape.angle);
        else if (key == QStringLiteral("feather")) ok = readFloat(value, shape.feather);
        else if (key == QStringLiteral("rangeLow")) ok = readFloat(value, shape.rangeLow);
        else if (key == QStringLiteral("rangeHigh")) ok = readFloat(value, shape.rangeHigh);
        else if (key == QStringLiteral("rangeFeather")) ok = readFloat(value, shape.rangeFeather);
        else if (key == QStringLiteral("inverted")) shape.inverted = value.toBool();
        else if (key == QStringLiteral("combine")) ok = combineNamed(value.toString(), shape.combine);
        else ok = false;
        if (!ok)
            return false;
    }
    return true;
}

} // namespace LocalAdjustments
