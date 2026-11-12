#ifndef IPR_STACK_STACKJOB_H
#define IPR_STACK_STACKJOB_H

#include "StackAlignment.h"
#include "StackMerge.h"
#include "StackTurbulence.h"
#include "Types.h"

#include <QString>
#include "Image.h"

#include <array>
#include <functional>
#include <vector>

// One noise-reduction stack of a same scene at constant exposure (plan M1 of
// docs/architecture/empilement-et-panorama.md): each frame is decoded once, registered on
// the reference, written to a scratch folder owned by the job, and the common area is
// merged by bands, so that the memory does not grow with the frame count. The scratch
// folder is removed on every path. The sources are only read, through the provider.
namespace StackJob {

struct Frame
{
    ipr::Image cameraRgb;   // F32, three channels, linear, white 1, the same balance intended across the series
    QString error;
};

struct Request
{
    int frameCount = 0;
    int referenceIndex = 0;
    // Called at most once per index, one at a time, on the job's thread.
    std::function<Frame(int index)> frame;
    // Where the job creates its own folder; the system's temporary folder when empty.
    QString scratchRoot;
    quint64 memoryBudgetBytes = quint64(512) << 20; // of the frames' bands held at once
    quint64 minimumFreeDiskBytes = quint64(2) << 30; // left free once every frame is written
    double rejectionSigma = 3.0;
    // False registers on the reference without merging it: stacks independent of its
    // noise, which is what a measurement against it needs.
    bool mergeReference = true;
    // Corrects what one transform leaves over a scene with depth.
    bool localRefinement = true;
    // A second pass on a small subject, which only a similarity can register: see StackTurbulence.
    bool correctTurbulence = true;
    StackTurbulence::Options turbulence;
    StackAlignment::Options alignment;
    StackAlignment::LocalOptions local;
    std::function<bool()> cancelled;
    std::function<void(int done, int total)> progress;
};

struct FrameReport
{
    int index = 0;
    bool used = false;
    bool reference = false;
    QString exclusion;                                // why it was left out, empty when used
    StackAlignment::Result alignment;
    ipr::Vec3d gains{1.0, 1.0, 1.0};                   // what brought it to the reference's level
    int localBlocks = 0;                              // blocks that measured the local field
    double localLargest = 0.0;                        // longest local displacement, pixels
};

struct Result
{
    bool success = false;
    bool cancelled = false;
    QString error;
    ipr::Image cameraRgb;                                // merged common area, F32, three channels
    ipr::Recti commonArea;                              // of the reference's frame
    ipr::Size frameSize;
    std::vector<FrameReport> frames;
    int usedFrames = 0;
    StackMerge::Statistics merge;
    quint64 scratchBytes = 0;                         // written to the scratch folder
    int bandRows = 0;
    StackTurbulence::Report turbulence;
    ipr::Recti subject;                                 // of the reference's frame, when a pass ran
};

// The largest upright rectangle in a convex area given by its span on each row: spans[y]
// is [left, right), empty when right <= left. Empty rectangle when there is none.
ipr::Recti largestRectangle(const std::vector<ipr::Vec2i> &spans);

// Bytes the scratch folder needs for this series.
quint64 scratchBytesNeeded(int frameCount, const ipr::Size &size);

Result run(const Request &request);

} // namespace StackJob

#endif // IPR_STACK_STACKJOB_H
