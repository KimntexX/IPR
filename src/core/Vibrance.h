#ifndef IPR_CORE_VIBRANCE_H
#define IPR_CORE_VIBRANCE_H

#include <opencv2/core.hpp>

// Vibrance and saturation in Oklab. Vibrance follows the Lightroom response
// tables (VibranceModel.h): chroma and lightness gains and hue rotation by the pixel's linear
// ProPhoto saturation, Oklab lightness and hue, interpolated between the
// measured slider values, scaled down on near-greys as Lightroom does.
// Saturation scales the Oklab chroma uniformly.
// No clipping here: the shared output handles gamut.
namespace Vibrance {
// Linear Rec.2020 image (CV_32FC3), in place; slider values in [-100, 100].
void apply(cv::Mat &linearRgb, int saturation, int vibrance);

// One linear sRGB D65 pixel; amounts in [-1, 1], clamped. Zero amounts and
// greys are returned unchanged.
cv::Vec3f applyPixel(const cv::Vec3f &linearSrgb, double vibrance, double saturation);

// Ottosson Oklab (2021) from linear sRGB, signed cube roots for HDR/signed values.
cv::Vec3d toOklab(const cv::Vec3d &linearSrgb);
}

#endif // IPR_CORE_VIBRANCE_H
