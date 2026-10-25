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
// One Oklab colour, in place, given the linear ProPhoto saturation of its pixel;
// amounts in [-1, 1], clamped. Zero amounts and greys are left unchanged. The image
// loop is PerceptualColour's, shared with the mixer and the grading.
void adjust(cv::Vec3d &lab, double proPhotoSaturation, double vibrance, double saturation);

// One linear Rec.2020 pixel: adjust() with its conversions.
cv::Vec3f applyPixel(const cv::Vec3f &linearRgb, double vibrance, double saturation);
}

#endif // IPR_CORE_VIBRANCE_H
