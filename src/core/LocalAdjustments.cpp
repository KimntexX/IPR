#include "LocalAdjustments.h"

#include "ImageMath.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

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

// One stroke at a time: within a stroke the coverage is that of its nearest point, so that
// its own overlaps do not build up; strokes then build up as paint does.
void paintStroke(const BrushStroke &stroke, cv::Mat &out)
{
    if (stroke.points.empty())
        return;
    const float radius = std::max(0.5f, stroke.size * float(std::max(out.cols, out.rows)));
    const cv::Rect bounds(0, 0, out.cols, out.rows);
    cv::Point2f lowest = pixelOf(stroke.points.front(), out.size()), highest = lowest;
    std::vector<cv::Point2f> points;
    points.reserve(stroke.points.size());
    for (const cv::Point2f &point : stroke.points) {
        const cv::Point2f pixel = pixelOf(point, out.size());
        points.push_back(pixel);
        lowest = {std::min(lowest.x, pixel.x), std::min(lowest.y, pixel.y)};
        highest = {std::max(highest.x, pixel.x), std::max(highest.y, pixel.y)};
    }
    const int reach = int(std::ceil(radius)) + 1;
    const cv::Rect area = cv::Rect(cv::Point(int(lowest.x) - reach, int(lowest.y) - reach),
                                   cv::Point(int(highest.x) + reach + 2, int(highest.y) + reach + 2)) & bounds;
    if (area.empty())
        return;
    cv::parallel_for_(cv::Range(area.y, area.y + area.height), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            float *row = out.ptr<float>(y);
            for (int x = area.x; x < area.x + area.width; ++x) {
                const cv::Point2f pixel{float(x), float(y)};
                float nearest = std::numeric_limits<float>::max();
                for (size_t i = 0; i < points.size(); ++i) {
                    const cv::Point2f a = points[i], b = points[std::min(i + 1, points.size() - 1)];
                    const cv::Point2f ab = b - a;
                    const float squared = ab.dot(ab);
                    const float t = squared > 0.0f ? std::clamp((pixel - a).dot(ab) / squared, 0.0f, 1.0f) : 0.0f;
                    const cv::Point2f to = pixel - (a + ab * t);
                    nearest = std::min(nearest, to.dot(to));
                }
                const float own = stroke.flow * fadeInside(std::sqrt(nearest) / radius, stroke.feather);
                row[x] = row[x] + own - row[x] * own;
            }
        }
    });
}

cv::Mat shapeCoverage(const Shape &shape, const cv::Size &canvas)
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
    }
    if (shape.inverted)
        cv::subtract(cv::Scalar(1.0), out, out);
    return out;
}

template <typename Key>
void writeFloats(QDataStream &stream, const Key &values)
{
    for (const float value : values)
        stream << value;
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
           a.feather == b.feather && a.strokes == b.strokes;
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
                int(shape.strokes.size()) > largestStrokeCount)
                return false;
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

cv::Mat coverage(const Mask &mask, const cv::Size &canvas)
{
    cv::Mat combined(canvas, CV_32FC1, cv::Scalar(0));
    for (const Shape &shape : mask.shapes) {
        const cv::Mat own = shapeCoverage(shape, canvas);
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

LocalValues::Maps valueMaps(const Masks &masks, const cv::Size &canvas)
{
    LocalValues::Maps maps;
    if (!active(masks) || canvas.empty())
        return maps;
    for (const Mask &mask : masks) {
        if (!mask.visible || mask.opacity <= 0.0f || mask.shapes.empty() ||
            std::all_of(mask.values.cbegin(), mask.values.cend(), [](float value) { return value == 0.0f; }))
            continue;
        const cv::Mat weight = coverage(mask, canvas) * double(mask.opacity);
        for (size_t i = 0; i < mask.values.size(); ++i) {
            if (mask.values[i] == 0.0f)
                continue;
            cv::Mat &map = maps.values[i];
            if (map.empty())
                map = cv::Mat(canvas, CV_32FC1, cv::Scalar(0));
            cv::scaleAdd(weight, double(mask.values[i]), map, map);
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
    cv::Mat out;
    cv::warpPerspective(canvasMap, out, sourceToCanvas, source, cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
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
                   << shape.angle << shape.feather << qint32(shape.strokes.size());
            for (const BrushStroke &stroke : shape.strokes) {
                stream << stroke.size << stroke.feather << stroke.flow << qint32(stroke.points.size());
                for (const cv::Point2f &point : stroke.points)
                    stream << point.x << point.y;
            }
        }
    }
}

} // namespace LocalAdjustments
