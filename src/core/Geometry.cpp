#include "Geometry.h"

#include "Filters.h"
#include "Warp.h"
#include "Arrange.h"
#include "Pixels.h"


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

using Matrix = ipr::Matrix33d;

// Centred coordinates where a unit is half the image's long side: the same on the
// source and the canvas, whatever the turns, so that rotations stay rotations.
Matrix normalizedToCentred(const ipr::Size &size)
{
    const double half = std::max(size.width, size.height) / 2.0;
    return {size.width / half, 0, -size.width / (2 * half),
            0, size.height / half, -size.height / (2 * half),
            0, 0, 1};
}

Matrix orientation(const Settings &s)
{
    Matrix matrix = Matrix::identity();
    for (int turn = 0; turn < s.quarterTurns; ++turn)
        matrix = Matrix(0, -1, 0, 1, 0, 0, 0, 0, 1) * matrix;   // y points down: clockwise
    if (s.flipHorizontal) matrix = Matrix(-1, 0, 0, 0, 1, 0, 0, 0, 1) * matrix;
    if (s.flipVertical) matrix = Matrix(1, 0, 0, 0, -1, 0, 0, 0, 1) * matrix;
    return matrix;
}

Matrix continuous(const Settings &s, const ipr::Size &canvas)
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

ipr::Image turnedAndMirrored(ipr::Image image, const Settings &s)
{
    // A separate output each time: the source is never written.
    if (s.quarterTurns >= 1 && s.quarterTurns <= 3)
        image = ipr::turned(image, s.quarterTurns);
    if (s.flipHorizontal || s.flipVertical)
        image = ipr::mirrored(image, s.flipHorizontal, s.flipVertical);
    return image;
}

// Reducing keeps details the output cannot hold, and they fold back as false patterns:
// blurred before the warp, by what the reduction needs, along each axis of the source.
// The scale and the aspect only: perspective reduces less, and varies across the image.
// Empty when nothing is reduced.
ipr::Image antialiased(const ipr::Image &image, const Settings &s)
{
    const auto spread = [](double zoom) {
        return zoom < 1 ? 0.7 * std::sqrt(1 / (zoom * zoom) - 1) : 0.0;
    };
    const double zoom = s.scale / 100.0, widen = std::pow(2.0, s.aspect / 200.0);
    double sigmaX = spread(zoom * widen), sigmaY = spread(zoom / widen);
    if (s.quarterTurns % 2)
        std::swap(sigmaX, sigmaY);
    if (sigmaX <= 0 && sigmaY <= 0)
        return {};
    return ipr::gaussianBlurred(image, sigmaX, sigmaY, ipr::BlurBorder::LastPixel);
}

ipr::Image resampled(const ipr::Image &image, const Settings &s, Quality quality, ipr::Outside outside)
{
    const ipr::Size canvas = canvasSize(s, image.size());
    // Pixel x is centred on (x + 0.5) / width: the same geometry at every resolution.
    const Matrix fromPixels(1.0 / image.width(), 0, 0.5 / image.width(), 0, 1.0 / image.height(), 0.5 / image.height(), 0, 0, 1);
    const Matrix toPixels(canvas.width, 0, -0.5, 0, canvas.height, -0.5, 0, 0, 1);
    const Matrix forward = toPixels * homography(s, image.size()) * fromPixels;
    return ipr::warpedPerspective(image, forward.inv(), canvas, quality == Quality::Export ? ipr::Resampling::Lanczos4 : ipr::Resampling::Cubic,
                                  outside);
}

std::optional<ipr::Point2d> mapped(const Matrix &matrix, const ipr::Point2d &point)
{
    const ipr::Vec3d result = matrix * ipr::Vec3d(point.x, point.y, 1);
    if (!(result[2] > 1e-9))
        return std::nullopt;
    return ipr::Point2d(result[0] / result[2], result[1] / result[2]);
}

// Whether a canvas point has source pixels under it.
bool onImage(const Settings &s, const ipr::Size &source, const ipr::Point2d &canvasPoint)
{
    const auto point = toSource(s, source, canvasPoint);
    constexpr double slack = 1e-9;
    return point && point->x >= -slack && point->x <= 1 + slack && point->y >= -slack && point->y <= 1 + slack;
}

// A ratio on the canvas's pixels, as a ratio of its normalized sides.
double normalizedRatio(double ratio, const ipr::Size &canvas)
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

ipr::Size canvasSize(const Settings &s, const ipr::Size &source)
{
    return s.quarterTurns % 2 ? ipr::Size(source.height, source.width) : source;
}

ipr::Recti cropRect(const Settings &s, const ipr::Size &canvas)
{
    const int left = std::clamp(int(std::lround(s.cropX * canvas.width)), 0, std::max(0, canvas.width - 1));
    const int top = std::clamp(int(std::lround(s.cropY * canvas.height)), 0, std::max(0, canvas.height - 1));
    const int right = std::clamp(int(std::lround((s.cropX + s.cropWidth) * canvas.width)), left + 1, canvas.width);
    const int bottom = std::clamp(int(std::lround((s.cropY + s.cropHeight) * canvas.height)), top + 1, canvas.height);
    return {left, top, right - left, bottom - top};
}

ipr::Image reframe(const ipr::Image &image, const Geometry::Settings &from,
                   const Geometry::Settings &to, const ipr::Size &source)
{
    if (image.empty() || source.empty()) return {};
    const auto output = Geometry::canvasSize(to, source);
    const auto input = image.size();
    const auto a = Geometry::homography(from, source);
    const auto b = Geometry::homography(to, source);
    if (a == b && output == input) return image.share();
    bool invertible = false;
    const auto inverse = b.inv(&invertible);
    if (!invertible) return {};
    const ipr::Matrix33d pixelToNormal(1.0/output.width, 0, .5/output.width,
                                     0, 1.0/output.height, .5/output.height, 0, 0, 1);
    const ipr::Matrix33d normalToPixel(input.width, 0, -.5, 0, input.height, -.5, 0, 0, 1);
    return ipr::warpedPerspective(image, normalToPixel * a * inverse * pixelToNormal,
                                 output, ipr::Resampling::Linear, ipr::Outside::Zero);
}

ipr::Matrix33d homography(const Settings &s, const ipr::Size &source)
{
    const ipr::Size canvas = canvasSize(s, source);
    return normalizedToCentred(canvas).inv() * continuous(s, canvas) * orientation(s) * normalizedToCentred(source);
}

std::optional<ipr::Point2d> toCanvas(const Settings &s, const ipr::Size &source, const ipr::Point2d &point)
{
    return mapped(homography(s, source), point);
}

std::optional<ipr::Point2d> toSource(const Settings &s, const ipr::Size &source, const ipr::Point2d &point)
{
    return mapped(homography(s, source).inv(), point);
}

ipr::Point2d viewToCanvas(const Settings &s, const ipr::Point2d &point)
{
    return {s.cropX + point.x * s.cropWidth, s.cropY + point.y * s.cropHeight};
}

ipr::Image warpRgb(ipr::Image rgb, const Settings &s, Quality quality)
{
    if (rgb.empty() || !transformsPixels(s))
        return rgb;
    if (!needsResampling(s))
        return turnedAndMirrored(std::move(rgb), s);
    const ipr::Image blurred = antialiased(rgb, s);
    ipr::Image result = resampled(blurred.empty() ? rgb : blurred, s, quality, ipr::Outside::LastPixel);
    // The filter's lobes must not push a pixel below what the source holds.
    std::vector<ipr::Image> channels;
    channels = ipr::splitChannels(rgb);
    std::vector<float> floor(size_t(rgb.channels()));
    for (int channel = 0; channel < rgb.channels(); ++channel)
        floor[size_t(channel)] = float(std::min(0.0, ipr::extremesOf(channels[size_t(channel)]).smallest));
    return ipr::atLeast(result, floor);
}

ipr::Image warpCoverage(ipr::Image alpha, const Settings &s, Quality quality)
{
    if (alpha.empty() || !transformsPixels(s))
        return alpha;
    if (!needsResampling(s))
        return turnedAndMirrored(std::move(alpha), s);
    ipr::Image result = resampled(alpha, s, quality, ipr::Outside::Zero);
    // The filter's rounding must not make the image's inside faintly transparent,
    // nor leave a haze beyond it.
    ipr::mapInPlace(result, [](float v) { return v >= 0.999f ? 1.0f : v <= 0.001f ? 0.0f : v; });
    return result;
}

void clearTransparent(ipr::Image &rgba)
{
    for (int y = 0; y < rgba.height(); ++y) {
        auto *row = rgba.mutableRow<ipr::Vec4f>(y);
        for (int x = 0; x < rgba.width(); ++x)
            if (row[x][3] <= 0)
                row[x] = ipr::Vec4f(0, 0, 0, 0);
    }
}

ipr::Image warpRgba(ipr::Image rgba, const Settings &s, Quality quality)
{
    if (rgba.empty() || !transformsPixels(s))
        return rgba;
    if (!needsResampling(s))
        return turnedAndMirrored(std::move(rgba), s);
    std::vector<ipr::Image> channels = ipr::splitChannels(warpRgb(ipr::withoutAlpha(rgba), s, quality));
    channels.push_back(warpCoverage(ipr::channel(rgba, 3), s, quality));
    ipr::Image result = ipr::merged(channels);
    clearTransparent(result);
    return result;
}

ipr::Image crop(ipr::Image canvasImage, const Settings &s)
{
    if (canvasImage.empty() || !hasCrop(s))
        return canvasImage;
    return canvasImage.copy(cropRect(s, canvasImage.size()));
}

void turn(Settings &s, const ipr::Size &source)
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

void mirror(Settings &s, bool horizontal, const ipr::Size &source)
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

bool cropInsideImage(const Settings &s, const ipr::Size &source)
{
    const double right = s.cropX + s.cropWidth, bottom = s.cropY + s.cropHeight;
    // The image's outline is a convex quadrilateral: the corners are enough.
    for (const ipr::Point2d &corner : {ipr::Point2d(s.cropX, s.cropY), ipr::Point2d(right, s.cropY),
                                      ipr::Point2d(right, bottom), ipr::Point2d(s.cropX, bottom)})
        if (!onImage(s, source, corner))
            return false;
    return true;
}

void applyConstraint(Settings &s, const ipr::Size &source)
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

void setCrop(Settings &s, const ipr::Size &source, double x, double y, double width, double height)
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

void setCropRatio(Settings &s, const ipr::Size &source, double ratio)
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
