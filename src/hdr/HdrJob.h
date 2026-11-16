#ifndef IPR_HDR_HDRJOB_H
#define IPR_HDR_HDRJOB_H

#include "HdrMerge.h"
#include "StackAlignment.h"
#include "Types.h"

#include "Image.h"

#include <QString>

#include <functional>
#include <vector>

// One merge of a bracketed series (plan B9, docs/architecture/fusion-hdr.md, steps H2 and
// H3): each frame is decoded once, registered on the reference after both are brought to
// the same exposure and to the range they both see, written to a scratch folder owned by
// the job, then the exposures are measured on the registered frames and the common area is
// merged by bands, so that the memory does not grow with the frame count. The scratch
// folder is removed on every path. The sources are only read, through the provider.
namespace HdrJob {

struct Frame
{
    ipr::Image cameraRgb;          // F32, three channels, linear, white 1 at the decoder's clip
    // Time x ISO / aperture squared, any unit common to the series; 0 when unknown. It only
    // guides the registration and checks the measure: the exposures are measured.
    double nominalExposure = 0.0;
    HdrMerge::NoiseModel noise;
    QString error;
};

struct Request
{
    int frameCount = 0;
    int referenceIndex = 0;
    // Called at most once per index, one at a time, on the job's thread.
    std::function<Frame(int index)> frame;
    QString scratchRoot;                              // the system's temporary folder when empty
    quint64 memoryBudgetBytes = quint64(512) << 20;   // of the frames' bands held at once
    quint64 minimumFreeDiskBytes = quint64(2) << 30;  // left free once every frame is written
    bool localRefinement = true;
    StackAlignment::Options alignment;
    StackAlignment::LocalOptions local;
    HdrMerge::Options merge;                          // its reference is the request's
    std::function<bool()> cancelled;
    std::function<void(int done, int total)> progress;
};

// A measured exposure further than this from the nominal one is reported: the light may
// have changed during the series.
constexpr double largestNominalGapEv = 1.0 / 3.0;

struct FrameReport
{
    int index = 0;
    bool used = false;
    bool reference = false;
    QString exclusion;                     // why it was left out, empty when used
    QString warning;                       // what the user should know about a used frame
    StackAlignment::Result alignment;
    ipr::Vec3d exposure{1.0, 1.0, 1.0};    // measured, relative to the reference, per channel
    double nominalExposure = 0.0;          // as given
    double nominalGapEv = 0.0;             // measured to nominal, 0 without a nominal value
    int measuredThrough = -1;              // the frame its ratio was measured against
    int localBlocks = 0;
    double localLargest = 0.0;
};

struct Result
{
    bool success = false;
    bool cancelled = false;
    QString error;
    ipr::Image cameraRgb;                  // merged common area, F32, three channels, the reference's units
    ipr::Recti commonArea;                 // of the reference's frame
    ipr::Size frameSize;
    std::vector<FrameReport> frames;
    int usedFrames = 0;
    HdrMerge::Statistics merge;            // share per frame index of the series
    quint64 scratchBytes = 0;
    int bandRows = 0;
};

Result run(const Request &request);

} // namespace HdrJob

#endif // IPR_HDR_HDRJOB_H
