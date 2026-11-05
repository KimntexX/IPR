#ifndef IPR_STACK_STACKTURBULENCE_H
#define IPR_STACK_STACKTURBULENCE_H

#include "StackAlignment.h"
#include "Types.h"

#include <QString>
#include <opencv2/core.hpp>

#include <functional>
#include <vector>

// A second pass over a small subject, as the Moon through the atmosphere: each view is
// deformed differently, and some are sharper than others. The first stack averages the
// deformations, so it serves as the geometric reference: every view is registered on it by
// small blocks, from its original pixels, so that they are resampled only once. The sharpest
// views are then merged, and the result fades into the stack around the subject.
namespace StackTurbulence {

// The subject's pixels in one frame, as decoded.
struct View
{
    cv::Mat pixels;                 // CV_32FC3
    ipr::Point2i origin;               // of pixels in the frame
    ipr::Matrix33d referenceToFrame = ipr::Matrix33d::identity();
    ipr::Vec3d gains{1.0, 1.0, 1.0}; // brings it to the reference's level
};

struct Options
{
    StackAlignment::LocalOptions local{16, 12, 6, 0.5, 0.8};
    double keepShare = 0.5;         // the sharpest views kept
    int minimumKept = 3;
    double rejectionSigma = 3.0;
    int feather = 32;               // of the fade into the stack, pixels
};

struct Report
{
    bool applied = false;
    int views = 0;
    int kept = 0;
    int measuredBlocks = 0;         // median per view
    double largestDisplacement = 0.0;
    std::vector<double> sharpness;  // per view, in the order given
    QString reason;                 // why it was not applied
};

// merged: the first stack, CV_32FC3, refined in place. subject: its area in merged, which
// the frame coordinates reach by adding origin to it.
Report refine(cv::Mat &merged, const ipr::Recti &subject, const ipr::Point2i &origin, const std::vector<View> &views,
              const Options &options = {}, const std::function<bool()> &cancelled = {});

} // namespace StackTurbulence

#endif // IPR_STACK_STACKTURBULENCE_H
