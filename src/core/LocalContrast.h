#ifndef IPR_CORE_LOCALCONTRAST_H
#define IPR_CORE_LOCALCONTRAST_H

#include <opencv2/core.hpp>

// Texture and clarity, following Lightroom's response measured by scale and by
// amplitude (tests/study_effects_response.py, tests/study_effects_amplitude.py):
// the logarithm of the luminance is split into difference-of-Gaussian bands from
// 1 to 256 pixels; in each band, small variations are amplified (or reduced) by
// the gain Lightroom gives them at the slider value, large ones (edges) much less,
// which avoids halos. Texture acts mostly up to 8-16 pixels, clarity at every
// scale. Colours keep their channel ratios.
// Study: docs/validation/analyses/analyse-effets.md.
namespace LocalContrast {

// Linear RGB image (CV_32FC3), in place; slider values in [-100, 100].
void apply(cv::Mat &linearRgb, int texture, int clarity);

} // namespace LocalContrast

#endif // IPR_CORE_LOCALCONTRAST_H
