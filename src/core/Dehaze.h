#ifndef IPR_CORE_DEHAZE_H
#define IPR_CORE_DEHAZE_H

#include <opencv2/core.hpp>

// Dehaze following Lightroom's response, fitted on its neutral -> state pairs.
// Tone (tests/study_dehaze_law.py, etude-loi-voile-v007): the luminance Y gains
// f(log2(Y / A), h) EV, A the airlight (99th percentile of max RGB), h the dark
// channel smoothed at about 128 pixels over A; that change is smoothed into a base
// at 64 pixels, Lightroom's change being a smooth local map. Detail
// (tests/study_dehaze_detail.py, etude-detail-voile-v001): around the base, the
// log2 luminance gains G at every scale, as Lightroom's band gains are nearly flat;
// G follows the haze and the local tone. Colour, in linear sRGB as fitted: adding
// haze turns part of the change into light of the airlight's colour; removing it
// takes the airlight's cast away and saturates where there is haze.
// Study: docs/validation/analyses/analyse-effets.md.
namespace Dehaze {

// Linear RGB image (CV_32FC3), in place; slider value in [-100, 100]. RGB stays nonnegative.
// pixelScale: pixels of this image per pixel of the full image (1 for the master,
// below 1 for the preview proxy); radii are defined on the full image.
void apply(cv::Mat &linearRgb, int amount, double pixelScale = 1.0);

} // namespace Dehaze

#endif // IPR_CORE_DEHAZE_H
