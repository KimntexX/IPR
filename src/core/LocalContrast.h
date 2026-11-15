#ifndef IPR_CORE_LOCALCONTRAST_H
#define IPR_CORE_LOCALCONTRAST_H

#include "LocalValues.h"

#include "Image.h"

// Texture and clarity, following Lightroom's response measured by scale and by
// amplitude (tests/study_effects_response.py, tests/study_effects_amplitude.py):
// the luminance is split into difference-of-Gaussian bands from 1 to 256 pixels,
// in its cube root for texture and its logarithm for clarity (the domains where
// Lightroom's response is best reproduced); in each band, small variations are amplified (or reduced) by
// the gain Lightroom gives them at the slider value, large ones (edges) much less,
// which avoids halos. Texture acts mostly up to 8-16 pixels, clarity at every
// scale. Colours keep their channel ratios.
// Study: docs/validation/analyses/analyse-effets.md.
namespace LocalContrast {

// Linear RGB image (F32 of three channels), in place; slider values in [-100, 100].
// pixelScale: pixels of this image per pixel of the full image (1 for the master,
// below 1 for the preview proxy); radii are defined on the full image.
// local: per-pixel Texture and Clarity added to the sliders (LocalValues), the
// sums within [-100, 100]. The bands do not depend on the slider: a pixel renders
// as the global value it reaches would, and a pixel at zero stays untouched.
void apply(ipr::Image &linearRgb, int texture, int clarity, double pixelScale = 1.0,
           const LocalValues::Maps &local = {});

} // namespace LocalContrast

#endif // IPR_CORE_LOCALCONTRAST_H
