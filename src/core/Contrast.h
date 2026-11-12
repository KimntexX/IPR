#ifndef IPR_CORE_CONTRAST_H
#define IPR_CORE_CONTRAST_H

#include "Image.h"

// Contrast: endpoint-preserving S curve in a log domain (24 EV below white),
// pivot 0.225, applied to each RGB channel. Black and white stay fixed, so no
// channel goes above 1. Per channel, positive contrast also saturates bright
// colours as Lightroom does; a common RGB scale washed them out. The strength
// grows faster than the slider towards +100, as Lightroom's deep shadows do.
// Range and pivot fitted on Lightroom's luminance transfer of the 28 base
// scenes, strength on its Oklab amplitude (2026-10-31): 12.1 % of Lightroom's
// change left unexplained against 24.2 % before. Lightroom's pivot depends on
// the image; this fixed one favours the low-pivot scenes.
namespace Contrast {
// Slider value in [-100, 100]; linear Rec.2020 RGB (F32 of three channels), in place. local:
// per-pixel values added to it (LocalValues), the sum kept within [-100, 100];
// empty for none. A map that does not fit the image is ignored.
void apply(ipr::Image &linearRgb, int contrast, const ipr::Image &local = {});
}

#endif // IPR_CORE_CONTRAST_H
