#ifndef IPR_CORE_COLORADJUSTMENTS_H
#define IPR_CORE_COLORADJUSTMENTS_H

#include "ColorMixer.h"

#include <opencv2/core.hpp>

// Colour adjustments on a linear Rec.2020 RGB image (CV_32FC3), in place.
namespace ColorAdjustments {
// Replaces each pixel by its luminance.
void toGrayscale(cv::Mat &linearRgb);

// Colour mixer (eight hue bands).
void applyMixer(cv::Mat &linearRgb, const ColorMixer::Parameters &mixer);
}

#endif // IPR_CORE_COLORADJUSTMENTS_H
