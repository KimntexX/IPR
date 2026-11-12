#ifndef IPR_PANORAMA_PANORAMAFILL_H
#define IPR_PANORAMA_PANORAMAFILL_H

#include "Image.h"

#include <functional>

// Completes the empty corners of a panorama from what the image shows elsewhere: on a pyramid,
// each missing pixel looks for a patch of the known image like its surroundings (the randomized
// correspondence search of Barnes et al.) and takes the mean of what those patches propose; the
// fine detail of the same places then comes back at whole resolution. Nothing is learned, and
// a known pixel is never changed. What comes out is invented: credible where the surroundings
// repeat, as in a sky, and only a guess elsewhere. Deterministic, whatever the threads.
namespace PanoramaFill {

enum class Outcome {
    Filled,
    NothingToFill,
    TooLittleKnown,  // not enough of the image to take patches from
    Cancelled
};

struct Options
{
    int workingSide = 4800;       // long side the search runs on, pixels
    int transitionPixels = 64;    // over which the real image's own texture hands over to the invented one
    double minimumKnown = 0.2;    // share of the image that must be known
};

struct Report
{
    Outcome outcome = Outcome::NothingToFill;
    double filledShare = 0.0;     // of the image's pixels
    int levels = 0;
};

// image: F32 of three channels, linear, possibly signed, changed in place where known is 0.
// known: bytes of one channel of the same size, non-zero where the pixel is real.
Report fill(ipr::Image &image, const ipr::Image &known, const Options &options = {}, const std::function<bool()> &cancelled = {},
            const std::function<void(int done, int total)> &progress = {});

} // namespace PanoramaFill

#endif // IPR_PANORAMA_PANORAMAFILL_H
