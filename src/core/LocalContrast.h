#ifndef IPR_CORE_LOCALCONTRAST_H
#define IPR_CORE_LOCALCONTRAST_H

#include <opencv2/core.hpp>

// Texture and clarity, following Lightroom's response measured by scale
// (tests/study_effects_response.py, etude-reponse-effets-v001): the logarithm
// of the luminance is split into difference-of-Gaussian bands from 1 to 64
// pixels, and each band is multiplied by the gain Lightroom gives it at the
// slider value (interpolated between the measured values). Texture acts mostly
// up to 8-16 pixels, clarity at every scale. Colours keep their channel ratios.
// Study: docs/validation/analyses/analyse-effets.md.
namespace LocalContrast {

// Linear RGB image (CV_32FC3), in place; slider values in [-100, 100].
void apply(cv::Mat &linearRgb, int texture, int clarity);

} // namespace LocalContrast

#endif // IPR_CORE_LOCALCONTRAST_H
