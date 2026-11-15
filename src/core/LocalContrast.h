#ifndef IPR_CORE_LOCALCONTRAST_H
#define IPR_CORE_LOCALCONTRAST_H

#include "LocalValues.h"

#include "Image.h"
#include "Types.h"

#include <vector>

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
// The coarse bands of a whole canvas at full resolution, kept so that a part of it renders alone:
// per effect and band, the blurred reduced plane that band goes through (empty for the bands
// blurred at full resolution, and for an effect at zero). They do not depend on the sliders.
struct Layers
{
    bool valid = false;
    ipr::Size canvas;
    std::vector<ipr::Image> texture;
    std::vector<ipr::Image> clarity;
};

// record: when given, receives the layers of this render, valid only at pixelScale 1 without
// local values (the region render's case).
void apply(ipr::Image &linearRgb, int texture, int clarity, double pixelScale = 1.0,
           const LocalValues::Maps &local = {}, Layers *record = nullptr);
// How far around a pixel a part reads, its coarse bands coming from the layers.
int regionReach(int texture, int clarity);
// A part of apply on the whole canvas (pixelScale 1, no local value), the same pixels: linearRgb
// holds the part at origin of the canvas, grown by regionReach where the canvas allows (its pixels
// within that reach of an inner edge are wrong); layers recorded by apply on the whole canvas with
// the same input and sliders. False when the layers lack a band the sliders need.
bool applyRegion(ipr::Image &linearRgb, int texture, int clarity, ipr::Point2i origin, const Layers &layers);

} // namespace LocalContrast

#endif // IPR_CORE_LOCALCONTRAST_H
