#ifndef IPR_CORE_COLORADJUSTMENTS_H
#define IPR_CORE_COLORADJUSTMENTS_H

#include <opencv2/core.hpp>

// Colour adjustments on a linear Rec.2020 RGB image (CV_32FC3), in place.
namespace ColorAdjustments {
// Replaces each pixel by its luminance.
void toGrayscale(cv::Mat &linearRgb);
}

#endif // IPR_CORE_COLORADJUSTMENTS_H
