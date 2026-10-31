#ifndef IPR_CORE_COLORADJUSTMENTS_H
#define IPR_CORE_COLORADJUSTMENTS_H

#include <opencv2/core.hpp>

// Colour adjustments on a linear Rec.2020 RGB image (CV_32FC3), in place.
namespace ColorAdjustments {
// Replaces each pixel by its luminance.
void toGrayscale(cv::Mat &linearRgb);

// Keeps each pixel's colour in proportion to its weight (CV_32FC1, [0, 1]) and
// shows the rest as its luminance: the point colour range overlay. Linear RGBA
// (CV_32FC4), alpha kept; nothing done when the sizes differ.
void showWeights(cv::Mat &linearRgba, const cv::Mat &weights);
}

#endif // IPR_CORE_COLORADJUSTMENTS_H
