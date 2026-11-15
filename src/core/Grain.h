#ifndef IPR_CORE_GRAIN_H
#define IPR_CORE_GRAIN_H

#include "Image.h"

// Film grain following Lightroom's (size 25, roughness 50), measured on its
// neutral -> state pairs: monochrome, added to the display-encoded values with a
// standard deviation of 0.062 at 100, the same at every tone, and fine (white noise
// under a Gaussian of 0.75 pixel: correlation 0.64 at one pixel, none at four).
// Deterministic: the same pixel gets the same grain at every render. On a reduced
// image the grain is what the reduction leaves of it: white, slightly weaker.
// Study: docs/validation/analyses/analyse-effets.md.
namespace Grain {

// Linear RGB image (F32 of three channels), in place; slider value in [0, 100].
// pixelScale: pixels of this image per pixel of the full image (1 for the master,
// below 1 for the preview proxy); radii are defined on the full image.
void apply(ipr::Image &linearRgb, int amount, double pixelScale = 1.0);

} // namespace Grain

#endif // IPR_CORE_GRAIN_H
