#ifndef IPR_STACK_STACKMERGE_H
#define IPR_STACK_STACKMERGE_H

#include <QtGlobal>
#include "Image.h"

#include <vector>

// The per-pixel fusion of registered frames of a same scene: at each pixel, the frames
// whose locally smoothed luminance lies far from the median, in units of the spread
// measured there, fade out (what moved, what flashed), and the others are averaged, colour
// included.
namespace StackMerge {

struct Statistics
{
    quint64 samples = 0;
    quint64 rejected = 0;   // samples beyond the limit, faded or out
};

// Below this many frames a median and a spread say nothing: plain average.
constexpr int minimumFramesForRejection = 5;
// The side of the window of the decisions. A pixel depends on this neighbourhood: a caller
// that cuts an image in parts gives them (window - 1) / 2 pixels of context all around, and
// keeps the middle of the result.
constexpr int decisionWindow = 3;

// frames: at least one, F32 of three channels, same size. merged: the same of this size, reallocated
// if needed. rejectionSigma <= 0 keeps every sample. False on invalid frames.
bool merge(const std::vector<ipr::Image> &frames, double rejectionSigma, ipr::Image &merged,
           Statistics *statistics = nullptr);

} // namespace StackMerge

#endif // IPR_STACK_STACKMERGE_H
