#ifndef IPR_CORE_DEHAZE_H
#define IPR_CORE_DEHAZE_H

#include "Image.h"
#include "Types.h"

#include <memory>

// Dehaze following Lightroom's response, fitted on its neutral -> state pairs.
// Tone (tests/study_dehaze_law.py, etude-loi-voile-v009): the luminance Y gains
// f(log2(Y / A), h) EV, A the airlight (99th percentile of the luminance), h the dark
// channel smoothed at about 128 pixels over A; that change is smoothed into a base
// at 64 pixels, Lightroom's change being a smooth local map. Detail
// (tests/study_dehaze_detail.py, etude-detail-voile-v003): around the base, the
// log2 luminance gains G at every scale, as Lightroom's band gains are nearly flat;
// G follows the haze and the local tone. Colour, in linear sRGB as fitted: adding
// haze turns part of the change into light of the airlight's colour; removing it
// takes the airlight's cast away and saturates where there is haze.
// Study: docs/validation/analyses/analyse-effets.md.
namespace Dehaze {

// Linear RGB image (F32 of three channels), in place; slider value in [-100, 100]. RGB stays nonnegative.
// pixelScale: pixels of this image per pixel of the full image (1 for the master,
// below 1 for the preview proxy); radii are defined on the full image.
// local: per-pixel value added to the slider (LocalValues), the sum within
// [-100, 100]; empty for none. The smooth tone change is computed at a few values
// of the map (at most 8, one 64-pixel blur each) and mixed per pixel; a pixel at
// zero stays untouched.
// The reduced maps of a whole canvas at full resolution (without local values), kept so that a
// part of it renders alone: they do not depend on the slider; the tone change of each slider value
// is made from them once, when a part first asks for it. Shared by every copy.
struct Layers
{
    struct State;
    ipr::Size canvas;
    std::shared_ptr<State> state;
};

// record: when given, receives the layers of this render, only at pixelScale 1 without local values.
void apply(ipr::Image &linearRgb, int amount, double pixelScale = 1.0, const ipr::Image &local = {},
           Layers *record = nullptr);
// A part of apply on the whole canvas, the same pixels: linearRgb holds the part at origin of the
// canvas (every stage before it at full resolution); layers recorded by apply on the whole canvas
// with the same input, at any slider value. Reads no pixel around the part. False without layers.
bool applyRegion(ipr::Image &linearRgb, int amount, ipr::Point2i origin, const Layers &layers);

} // namespace Dehaze

#endif // IPR_CORE_DEHAZE_H
