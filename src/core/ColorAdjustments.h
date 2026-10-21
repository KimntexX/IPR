#ifndef IPR_CORE_COLORADJUSTMENTS_H
#define IPR_CORE_COLORADJUSTMENTS_H

#include "ColorMixer.h"

#include <opencv2/core.hpp>

#include <array>

// Colour adjustments on a linear Rec.2020 RGB image (CV_32FC3), in place.
namespace ColorAdjustments {
// Replaces each pixel by its luminance.
void toGrayscale(cv::Mat &linearRgb);

// Saturation and vibrance (A1/A2 Oklab model, src/core/Vibrance.h),
// slider values in [-100, 100].
void applySaturationVibrance(cv::Mat &linearRgb, int saturation, int vibrance);

// Colour mixer (eight hue bands), then colour grading of shadows, midtones
// and highlights weighted by the luminance before mixing.
void applyMixerAndGrading(cv::Mat &linearRgb, const ColorMixer::Parameters &mixer,
                          const std::array<int, 3> &gradingHue,
                          const std::array<int, 3> &gradingSaturation);
}

#endif // IPR_CORE_COLORADJUSTMENTS_H
