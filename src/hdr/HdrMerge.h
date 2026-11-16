#ifndef IPR_HDR_HDRMERGE_H
#define IPR_HDR_HDRMERGE_H

#include "Image.h"
#include "Types.h"

#include <QtGlobal>

#include <optional>
#include <vector>

// The radiometric merge of registered exposures of a same scene (plan B9,
// docs/architecture/fusion-hdr.md): each frame is brought to the reference's exposure, and
// each pixel is the average of the frames weighted by the inverse of the noise expected
// there, nothing where a frame is clipped. The result is a camera signal in the reference's
// units: what the reference shows well keeps its level, the highlights go above white.
// What every frame clipped stays clipped, and is counted: nothing is invented.
namespace HdrMerge {

// The variance of a frame's value v (white 1): shot * v + read. Same units for every frame.
struct NoiseModel
{
    double shot = 1.0 / 4000.0;
    double read = 1e-7;
};

struct Frame
{
    ipr::Image pixels;                    // F32, three channels, registered on the reference, white 1
    ipr::Vec3d exposure{1.0, 1.0, 1.0};   // per channel, relative to the reference: pixels ~ exposure * scene
    NoiseModel noise;
};

struct Options
{
    int referenceIndex = 0;
    double clipLevel = 1.0;     // the decoder's white: a channel there has lost its value
    double knee = 0.9;          // of clipLevel: the frame's weight fades from here to the clip
    // Motion: a frame whose smoothed luminance lies further than this many expected standard
    // deviations from the pivot's fades out. 0 keeps every frame (a tripod and a still scene).
    double motionSigma = 4.0;
    // What the measured exposures may still be wrong by, relative: a floor of the tolerance.
    double exposureTolerance = 0.03;
};

struct Statistics
{
    quint64 pixels = 0;
    quint64 clippedEverywhere = 0;    // pixels no frame saw below its clip
    quint64 motionRejected = 0;       // samples faded or left out as moving
    std::vector<double> share;        // per frame, its share of the total weight
};

// A pixel depends on this neighbourhood (clip dilation, smoothed decisions): a caller that
// cuts an image in parts gives them (window - 1) / 2 pixels of context all around.
constexpr int decisionWindow = 3;

// frames: at least one, same size, the reference among them with exposure 1. merged: F32 of
// three channels, of their size. False on invalid frames or options.
bool merge(const std::vector<Frame> &frames, const Options &options, ipr::Image &merged,
           Statistics *statistics = nullptr);

struct Ratio
{
    ipr::Vec3d exposure{1.0, 1.0, 1.0};   // of the frame to the base, per channel
    int samples = 0;                      // pixels both showed well
};

// The exposure of frame relative to base, measured as the median ratio where both show the
// scene well: above the noise, below the knee of the clip. Both of the same size, small
// proxies are enough. nullopt when too few pixels qualify (exposures too far apart, flat
// frames): the caller then chains through a frame in between, or says why.
std::optional<Ratio> measureRatio(const ipr::Image &base, const ipr::Image &frame, const Options &options = {});

} // namespace HdrMerge

#endif // IPR_HDR_HDRMERGE_H
