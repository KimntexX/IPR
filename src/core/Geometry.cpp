#include "Geometry.h"

#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Geometry {
namespace {

constexpr double Pi = 3.14159265358979323846;
// Perspective strength at 100, in units of half the long side. The weight 1 + px x +
// py y stays above 0.2 across any image, so no source corner reaches the horizon.
constexpr double PerspectiveAt100 = 0.4;
constexpr double MinCropSide = 1e-3;

using Matrix = cv::Matx33d;

// Centred coordinates where a unit is half the image's long side: the same on the
// source and the canvas, whatever the turns, so that rotations stay rotations.
Matrix normalizedToCentred(const cv::Size &size)
{
    const double half = std::max(size.width, size.height) / 2.0;
    return {size.width / half, 0, -size.width / (2 * half),
            0, size.height / half, -size.height / (2 * half),
            0, 0, 1};
}

Matrix orientation(const Settings &s)
{
    Matrix matrix = Matrix::eye();
    for (int turn = 0; turn < s.quarterTurns; ++turn)
        matrix = Matrix(0, -1, 0, 1, 0, 0, 0, 0, 1) * matrix;   // y points down: clockwise
    if (s.flipHorizontal) matrix = Matrix(-1, 0, 0, 0, 1, 0, 0, 0, 1) * matrix;
    if (s.flipVertical) matrix = Matrix(1, 0, 0, 0, -1, 0, 0, 0, 1) * matrix;
    return matrix;
}

Matrix continuous(const Settings &s, const cv::Size &canvas)
{
    const Matrix perspective(1, 0, 0, 0, 1, 0,
                             -PerspectiveAt100 * s.horizontal / 100.0, PerspectiveAt100 * s.vertical / 100.0, 1);
    const double widen = std::pow(2.0, s.aspect / 200.0);
    const Matrix aspect(widen, 0, 0, 0, 1 / widen, 0, 0, 0, 1);
    const double angle = s.rotation * Pi / 180.0;
    const Matrix rotation(std::cos(angle), -std::sin(angle), 0, std::sin(angle), std::cos(angle), 0, 0, 0, 1);
    const double zoom = s.scale / 100.0;
    const double half = std::max(canvas.width, canvas.height) / 2.0;
    const Matrix placed(zoom, 0, s.offsetX / 100.0 * canvas.width / (2 * half),
                        0, zoom, s.offsetY / 100.0 * canvas.height / (2 * half),
                        0, 0, 1);
    return placed * rotation * aspect * perspective;
}

bool needsResampling(const Settings &s)
{
    return s.rotation != 0 || s.vertical != 0 || s.horizontal != 0 || s.aspect != 0 || s.scale != 100 ||
           s.offsetX != 0 || s.offsetY != 0;
}

cv::Mat turnedAndMirrored(const cv::Mat &image, const Settings &s)
{
    // A separate output each time: the source is never written.
    cv::Mat result = s.quarterTurns >= 1 && s.quarterTurns <= 3 ? ipr::turned(image, s.quarterTurns) : image;
    if (s.flipHorizontal || s.flipVertical)
        result = ipr::mirrored(result, s.flipHorizontal, s.flipVertical);
    return result;
}

// Reducing keeps details the output cannot hold, and they fold back as false patterns:
// blurred before the warp, by what the reduction needs, along each axis of the source.
// The scale and the aspect only: perspective reduces less, and varies across the image.
cv::Mat antialiased(const cv::Mat &image, const Settings &s)
{
    const auto spread = [](double zoom) {
        return zoom < 1 ? 0.7 * std::sqrt(1 / (zoom * zoom) - 1) : 0.0;
    };
    const double zoom = s.scale / 100.0, widen = std::pow(2.0, s.aspect / 200.0);
    double sigmaX = spread(zoom * widen), sigmaY = spread(zoom / widen);
    if (s.quarterTurns % 2)
        std::swap(sigmaX, sigmaY);
    if (sigmaX <= 0 && sigmaY <= 0)
        return image;
    return ipr::gaussianBlurred(image, sigmaX, sigmaY, ipr::BlurBorder::LastPixel);
}

cv::Mat resampled(const cv::Mat &image, const Settings &s, Quality quality, int border)
{
    const cv::Size canvas = canvasSize(s, image.size());
    // Pixel x is centred on (x + 0.5) / width: the same geometry at every resolution.
    const Matrix fromPixels(1.0 / image.cols, 0, 0.5 / image.cols, 0, 1.0 / image.rows, 0.5 / image.rows, 0, 0, 1);
    const Matrix toPixels(canvas.width, 0, -0.5, 0, canvas.height, -0.5, 0, 0, 1);
    const Matrix forward = toPixels * homography(s, image.size()) * fromPixels;
    return ipr::warpedPerspective(image, forward.inv(), canvas, quality == Quality::Export ? ipr::Resampling::Lanczos4 : ipr::Resampling::Cubic,
                                  border == cv::BORDER_CONSTANT ? ipr::Outside::Zero : ipr::Outside::LastPixel);
}

std::optional<cv::Point2d> mapped(const Matrix &matrix, const cv::Point2d &point)
{
    const cv::Vec3d result = matrix * cv::Vec3d(point.x, point.y, 1);
    if (!(result[2] > 1e-9))
        return std::nullopt;
    return cv::Point2d(result[0] / result[2], result[1] / result[2]);
}

// Whether a canvas point has source pixels under it.
bool onImage(const Settings &s, const cv::Size &source, const cv::Point2d &canvasPoint)
{
    const auto point = toSource(s, source, canvasPoint);
    constexpr double slack = 1e-9;
    return point && point->x >= -slack && point->x <= 1 + slack && point->y >= -slack && point->y <= 1 + slack;
}

// A ratio on the canvas's pixels, as a ratio of its normalized sides.
double normalizedRatio(double ratio, const cv::Size &canvas)
{
    return ratio * canvas.height / std::max(1, canvas.width);
}

void centredOn(Settings &s, double centreX, double centreY, double width, double height)
{
    s.cropWidth = std::clamp(width, MinCropSide, 1.0);
    s.cropHeight = std::clamp(height, MinCropSide, 1.0);
    s.cropX = std::clamp(centreX - s.cropWidth / 2, 0.0, 1.0 - s.cropWidth);
    s.cropY = std::clamp(centreY - s.cropHeight / 2, 0.0, 1.0 - s.cropHeight);
}

} // namespace

bool operator==(const Settings &a, const Settings &b)
{
    return a.quarterTurns == b.quarterTurns && a.flipHorizontal == b.flipHorizontal &&
           a.flipVertical == b.flipVertical && a.rotation == b.rotation && a.vertical == b.vertical &&
           a.horizontal == b.horizontal && a.aspect == b.aspect && a.scale == b.scale &&
           a.offsetX == b.offsetX && a.offsetY == b.offsetY && a.cropX == b.cropX && a.cropY == b.cropY &&
           a.cropWidth == b.cropWidth && a.cropHeight == b.cropHeight && a.cropRatio == b.cropRatio &&
           a.constrain == b.constrain;
}

bool transformsPixels(const Settings &s)
{
    return s.quarterTurns != 0 || s.flipHorizontal || s.flipVertical || needsResampling(s);
}

bool hasCrop(const Settings &s)
{
    return s.cropX != 0 || s.cropY != 0 || s.cropWidth != 1 || s.cropHeight != 1;
}

bool isIdentity(const Settings &s)
{
    return !transformsPixels(s) && !hasCrop(s);
}

bool bound(Settings &s)
{
    if (!std::isfinite(s.rotation) || !std::isfinite(s.cropX) || !std::isfinite(s.cropY) ||
        !std::isfinite(s.cropWidth) || !std::isfinite(s.cropHeight) || !std::isfinite(s.cropRatio))
        return false;
    s.quarterTurns = std::clamp(s.quarterTurns, 0, 3);
    s.rotation = std::clamp(s.rotation, -45.0f, 45.0f);
    s.vertical = std::clamp(s.vertical, -100, 100);
    s.horizontal = std::clamp(s.horizontal, -100, 100);
    s.aspect = std::clamp(s.aspect, -100, 100);
    s.scale = std::clamp(s.scale, 50, 150);
    s.offsetX = std::clamp(s.offsetX, -100, 100);
    s.offsetY = std::clamp(s.offsetY, -100, 100);
    s.cropRatio = std::clamp(s.cropRatio, 0.0, 100.0);
    s.cropWidth = std::clamp(s.cropWidth, MinCropSide, 1.0);
    s.cropHeight = std::clamp(s.cropHeight, MinCropSide, 1.0);
    s.cropX = std::clamp(s.cropX, 0.0, 1.0 - s.cropWidth);
    s.cropY = std::clamp(s.cropY, 0.0, 1.0 - s.cropHeight);
    return true;
}

cv::Size canvasSize(const Settings &s, const cv::Size &source)
{
    return s.quarterTurns % 2 ? cv::Size(source.height, source.width) : source;
}

cv::Rect cropRect(const Settings &s, const cv::Size &canvas)
{
    const int left = std::clamp(int(std::lround(s.cropX * canvas.width)), 0, std::max(0, canvas.width - 1));
    const int top = std::clamp(int(std::lround(s.cropY * canvas.height)), 0, std::max(0, canvas.height - 1));
    const int right = std::clamp(int(std::lround((s.cropX + s.cropWidth) * canvas.width)), left + 1, canvas.width);
    const int bottom = std::clamp(int(std::lround((s.cropY + s.cropHeight) * canvas.height)), top + 1, canvas.height);
    return {left, top, right - left, bottom - top};
}

cv::Matx33d homography(const Settings &s, const cv::Size &source)
{
    const cv::Size canvas = canvasSize(s, source);
    return normalizedToCentred(canvas).inv() * continuous(s, canvas) * orientation(s) * normalizedToCentred(source);
}

std::optional<cv::Point2d> toCanvas(const Settings &s, const cv::Size &source, const cv::Point2d &point)
{
    return mapped(homography(s, source), point);
}

std::optional<cv::Point2d> toSource(const Settings &s, const cv::Size &source, const cv::Point2d &point)
{
    return mapped(homography(s, source).inv(), point);
}

cv::Point2d viewToCanvas(const Settings &s, const cv::Point2d &point)
{
    return {s.cropX + point.x * s.cropWidth, s.cropY + point.y * s.cropHeight};
}

cv::Mat warpRgb(const cv::Mat &rgb, const Settings &s, Quality quality)
{
    if (rgb.empty() || !transformsPixels(s))
        return rgb;
    if (!needsResampling(s))
        return turnedAndMirrored(rgb, s);
    cv::Mat result = resampled(antialiased(rgb, s), s, quality, cv::BORDER_REPLICATE);
    // The filter's lobes must not push a pixel below what the source holds.
    std::vector<cv::Mat> channels;
    channels = ipr::splitChannels(rgb);
    std::vector<float> floor(size_t(rgb.channels()));
    for (int channel = 0; channel < rgb.channels(); ++channel)
        floor[size_t(channel)] = float(std::min(0.0, ipr::extremesOf(channels[size_t(channel)]).smallest));
    return ipr::atLeast(result, floor);
}

cv::Mat warpCoverage(const cv::Mat &alpha, const Settings &s, Quality quality)
{
    if (alpha.empty() || !transformsPixels(s))
        return alpha;
    if (!needsResampling(s))
        return turnedAndMirrored(alpha, s);
    cv::Mat result = resampled(alpha, s, quality, cv::BORDER_CONSTANT);
    // The filter's rounding must not make the image's inside faintly transparent,
    // nor leave a haze beyond it.
    result.setTo(1, result >= 0.999);
    result.setTo(0, result <= 0.001);
    return result;
}

void clearTransparent(cv::Mat &rgba)
{
    for (int y = 0; y < rgba.rows; ++y) {
        auto *row = rgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < rgba.cols; ++x)
            if (row[x][3] <= 0)
                row[x] = cv::Vec4f(0, 0, 0, 0);
    }
}

cv::Mat warpRgba(const cv::Mat &rgba, const Settings &s, Quality quality)
{
    if (rgba.empty() || !transformsPixels(s))
        return rgba;
    if (!needsResampling(s))
        return turnedAndMirrored(rgba, s);
    cv::Mat rgb, alpha;
    rgb = ipr::withoutAlpha(rgba);
    alpha = ipr::channel(rgba, 3);
    std::vector<cv::Mat> channels;
    channels = ipr::splitChannels(warpRgb(rgb, s, quality));
    channels.push_back(warpCoverage(alpha, s, quality));
    cv::Mat result;
    result = ipr::merged(channels);
    clearTransparent(result);
    return result;
}

cv::Mat crop(const cv::Mat &canvasImage, const Settings &s)
{
    if (canvasImage.empty() || !hasCrop(s))
        return canvasImage;
    return canvasImage(cropRect(s, canvasImage.size()));
}

void turn(Settings &s, const cv::Size &source)
{
    // A single mirror reverses the sense of the turn that comes before it.
    const bool mirrored = s.flipHorizontal != s.flipVertical;
    s.quarterTurns = (s.quarterTurns + (mirrored ? 3 : 1)) % 4;
    const int offsetX = s.offsetX;
    s.offsetX = -s.offsetY;
    s.offsetY = offsetX;
    const int horizontal = s.horizontal;
    s.horizontal = s.vertical;
    s.vertical = -horizontal;
    s.aspect = -s.aspect;
    const double cropX = s.cropX, cropWidth = s.cropWidth;
    s.cropX = 1 - s.cropY - s.cropHeight;
    s.cropY = cropX;
    s.cropWidth = s.cropHeight;
    s.cropHeight = cropWidth;
    if (s.cropRatio > 0)
        s.cropRatio = 1 / s.cropRatio;
    applyConstraint(s, source);
}

void mirror(Settings &s, bool horizontal, const cv::Size &source)
{
    // Rotated by a quarter turn, the screen's horizontal is the source's vertical,
    // but the mirrors act after the turns, in the screen's frame already.
    (horizontal ? s.flipHorizontal : s.flipVertical) = !(horizontal ? s.flipHorizontal : s.flipVertical);
    s.rotation = -s.rotation;
    if (horizontal) {
        s.offsetX = -s.offsetX;
        s.horizontal = -s.horizontal;
        s.cropX = 1 - s.cropX - s.cropWidth;
    } else {
        s.offsetY = -s.offsetY;
        s.vertical = -s.vertical;
        s.cropY = 1 - s.cropY - s.cropHeight;
    }
    applyConstraint(s, source);
}

bool cropInsideImage(const Settings &s, const cv::Size &source)
{
    const double right = s.cropX + s.cropWidth, bottom = s.cropY + s.cropHeight;
    // The image's outline is a convex quadrilateral: the corners are enough.
    for (const cv::Point2d &corner : {cv::Point2d(s.cropX, s.cropY), cv::Point2d(right, s.cropY),
                                      cv::Point2d(right, bottom), cv::Point2d(s.cropX, bottom)})
        if (!onImage(s, source, corner))
            return false;
    return true;
}

void applyConstraint(Settings &s, const cv::Size &source)
{
    if (!s.constrain || cropInsideImage(s, source))
        return;
    const double width = s.cropWidth, height = s.cropHeight;
    double centreX = s.cropX + width / 2, centreY = s.cropY + height / 2;
    // The centre may lie off the image after a strong offset: start from the image's.
    if (!onImage(s, source, {centreX, centreY})) {
        const auto centre = toCanvas(s, source, {0.5, 0.5});
        if (!centre || !onImage(s, source, *centre))
            return;
        centreX = std::clamp(centre->x, 0.0, 1.0);
        centreY = std::clamp(centre->y, 0.0, 1.0);
    }
    Settings candidate = s;
    const auto fits = [&](double factor) {
        candidate.cropWidth = width * factor;
        candidate.cropHeight = height * factor;
        candidate.cropX = centreX - candidate.cropWidth / 2;
        candidate.cropY = centreY - candidate.cropHeight / 2;
        return candidate.cropX >= 0 && candidate.cropY >= 0 && candidate.cropX + candidate.cropWidth <= 1 &&
               candidate.cropY + candidate.cropHeight <= 1 && cropInsideImage(candidate, source);
    };
    double low = 0, high = 1;
    for (int step = 0; step < 40; ++step) {
        const double middle = (low + high) / 2;
        (fits(middle) ? low : high) = middle;
    }
    if (low > 0 && fits(low)) {
        s.cropX = candidate.cropX;
        s.cropY = candidate.cropY;
        s.cropWidth = std::max(candidate.cropWidth, MinCropSide);
        s.cropHeight = std::max(candidate.cropHeight, MinCropSide);
    }
}

void setCrop(Settings &s, const cv::Size &source, double x, double y, double width, double height)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height))
        return;
    width = std::clamp(width, MinCropSide, 1.0);
    height = std::clamp(height, MinCropSide, 1.0);
    if (s.cropRatio > 0) {
        const double ratio = normalizedRatio(s.cropRatio, canvasSize(s, source));
        // Keep the larger side the hand gave, within the canvas.
        if (width / height > ratio) height = width / ratio;
        else width = height * ratio;
        if (width > 1) { height /= width; width = 1; }
        if (height > 1) { width /= height; height = 1; }
    }
    s.cropWidth = width;
    s.cropHeight = height;
    s.cropX = std::clamp(x, 0.0, 1.0 - width);
    s.cropY = std::clamp(y, 0.0, 1.0 - height);
    applyConstraint(s, source);
}

void setCropRatio(Settings &s, const cv::Size &source, double ratio)
{
    if (!std::isfinite(ratio) || ratio < 0)
        return;
    s.cropRatio = ratio;
    if (ratio > 0) {
        const double target = normalizedRatio(ratio, canvasSize(s, source));
        const double centreX = s.cropX + s.cropWidth / 2, centreY = s.cropY + s.cropHeight / 2;
        // Shaped inside the present crop; from a whole canvas, as large as it allows.
        if (s.cropWidth / s.cropHeight > target) centredOn(s, centreX, centreY, s.cropHeight * target, s.cropHeight);
        else centredOn(s, centreX, centreY, s.cropWidth, s.cropWidth / target);
    }
    applyConstraint(s, source);
}

} // namespace Geometry
