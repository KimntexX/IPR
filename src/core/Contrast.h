#ifndef IPR_CORE_CONTRAST_H
#define IPR_CORE_CONTRAST_H

#include <opencv2/core.hpp>

// Contrast: endpoint-preserving S curve in a log domain (16 EV below white),
// pivot 0.32, applied to each RGB channel. Black and white stay fixed, so no
// channel goes above 1. Per channel, positive contrast also saturates bright
// colours as Lightroom does; a common RGB scale washed them out (bright-tone
// saturation -0.085 vs LR at +100, now +0.003). Fitted on the six Lumières
// scenes against Lightroom (median |gap| 0.08 EV); the negative strength is
// the fitted 0.45 raised by 8 % after the user's visual check.
namespace Contrast {
// Slider value in [-100, 100]; linear Rec.2020 RGB (CV_32FC3), in place.
void apply(cv::Mat &linearRgb, int contrast);
}

#endif // IPR_CORE_CONTRAST_H
