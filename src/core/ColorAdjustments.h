#ifndef IPR_CORE_COLORADJUSTMENTS_H
#define IPR_CORE_COLORADJUSTMENTS_H

#include "Image.h"

// Colour adjustments on a linear Rec.2020 RGB image (F32 of three channels), in place.
namespace ColorAdjustments {
// Replaces each pixel by its luminance.
void toGrayscale(ipr::Image &linearRgb);

// Keeps each pixel's colour in proportion to its weight (F32 of one channel, [0, 1]) and
// shows the rest as its luminance: the point colour range overlay. Linear RGBA
// (F32 of four channels), alpha kept; nothing done when the sizes differ.
void showWeights(ipr::Image &linearRgba, const ipr::Image &weights);
}

#endif // IPR_CORE_COLORADJUSTMENTS_H
