#ifndef IPR_CORE_DEHAZE_H
#define IPR_CORE_DEHAZE_H

#include <opencv2/core.hpp>

// Dehaze following Lightroom's response, fitted on its neutral -> state pairs
// (tests/study_dehaze_law.py, etude-loi-voile-v007). The luminance Y gains
// f(log2(Y / A), h) EV, where A is the airlight (99th percentile of max RGB) and h
// the dark channel smoothed at about 128 pixels over A; f is a bilinear table per
// slider value, bounded by Lightroom's observed change. Colour, in linear sRGB as
// fitted: adding haze turns part of the change into light of the airlight's colour
// (mean of the haziest pixels); removing it takes the airlight's cast away and
// saturates where there is haze, faint colours more than strong ones. Never negative.
// Study: docs/validation/analyses/analyse-effets.md.
namespace Dehaze {

// Linear RGB image (CV_32FC3), in place; slider value in [-100, 100]. RGB stays nonnegative.
// pixelScale: pixels of this image per pixel of the full image (1 for the master,
// below 1 for the preview proxy); radii are defined on the full image.
void apply(cv::Mat &linearRgb, int amount, double pixelScale = 1.0);

} // namespace Dehaze

#endif // IPR_CORE_DEHAZE_H
