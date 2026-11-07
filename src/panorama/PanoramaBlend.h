#ifndef IPR_PANORAMA_PANORAMABLEND_H
#define IPR_PANORAMA_PANORAMABLEND_H

#include "Image.h"
#include "Types.h"

#include <functional>
#include <vector>

// Joining the warped frames, on a reduced copy of the canvas (one cell per scale * scale
// pixels). Where the seams run is decided there; each frame then contributes its fine detail
// up to its seam, and its slow variations over a wide transition, so that a difference of
// vignetting or of light does not show as a step and the detail does not double.
namespace PanoramaBlend {

// A frame on the reduced canvas.
struct View
{
    ipr::Recti area;       // cells of the reduced canvas
    ipr::Image colour;     // F32, three channels: the mean of each cell's valid pixels
    ipr::Image whole;      // bytes of one channel, 255 where all of the cell's pixels are valid
    ipr::Image any;        // bytes of one channel, 255 where one of them is
    ipr::Vec3d gains{1.0, 1.0, 1.0};
};

// One gain per view and channel, from the overlaps: the differences of level between views
// are shared out, with a weak pull towards 1 so that the series keeps its exposure.
void levelGains(std::vector<View> &views);

// The frame each cell comes from, S16 of one channel, -1 where none. Each view in turn takes its share
// by a minimal cut through the overlap with what is already there, along the smallest
// visible difference. Empty when cancelled.
ipr::Image seams(const std::vector<View> &views, const ipr::Size &canvas, const std::function<bool()> &cancelled = {});

// An upright rectangle full of the mask, as low as it can stand without giving up much of the
// largest area: a series leans on the bottom of its view, so a crop takes from the top.
ipr::Recti lowRectangle(const ipr::Image &mask);

// What a frame brings to the canvas.
struct Layer
{
    ipr::Recti area;       // cells of the reduced canvas
    ipr::Image low;        // F32, three channels: slow variations, gain included
    ipr::Image wide;       // F32, one channel: weight of the slow variations
    ipr::Image narrow;     // F32, one channel: weight of the detail
    ipr::Vec3f gains{1.0f, 1.0f, 1.0f};
};

// sigmas in cells.
Layer prepareLayer(const View &view, const ipr::Image &labels, int label, double narrowSigma, double wideSigma);

// One frame's pixels over a part of the canvas.
struct Pixels
{
    const Layer *layer = nullptr;
    ipr::Recti area;       // canvas pixels
    ipr::Image colour;     // F32, three channels
    ipr::Image valid;      // bytes of one channel
};

// Fills target (F32 of three channels, the size of band) from the frames that cover band, a part of the
// canvas in pixels. scale: pixels per cell. A pixel no frame covers is 0.
void blendBand(const std::vector<Pixels> &pixels, int scale, const ipr::Recti &band, ipr::Image &target);

} // namespace PanoramaBlend

#endif // IPR_PANORAMA_PANORAMABLEND_H
