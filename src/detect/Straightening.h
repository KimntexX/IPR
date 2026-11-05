#ifndef IPR_DETECT_STRAIGHTENING_H
#define IPR_DETECT_STRAIGHTENING_H

#include "Geometry.h"
#include "LineDetection.h"

#include <vector>

// A proposal for the geometry, from the straight lines of the image: what levels it and, when
// the lines allow it, what brings its verticals or its horizontals back parallel. A proposal
// and nothing more: the image's own lines may be slanted on purpose. It refuses rather than
// guesses when the lines do not agree.
namespace Straightening {

enum class Mode {
    Level,      // rotation only
    Vertical,   // and the verticals made parallel
    Full        // and the horizontals too
};

enum class Refusal {
    None,
    InvalidInput,
    NotEnoughLines,      // too little length near the axes of the image
    NoAgreement,         // lines near the axes, but no direction they share
    NotEnoughEvidence    // they share one, but too few and too short for a correction this large
};

struct Options
{
    float levelTilt = 12.0f;            // degrees from an axis, of a line that can level
    float perspectiveTilt = 20.0f;      // degrees from an axis, of a line that can show a perspective
    float agreement = 1.5f;             // degrees around the shared direction, of a line that agrees
    float minimumVoteShare = 0.12f;     // of the long side, under which a line does not count: waves, leaves
    float minimumLengthShare = 0.25f;   // of the long side, length that must agree for a small correction
    float degreesPerEvidence = 3.0f;    // this many more degrees ask for that length once more
    float minimumLongestShare = 0.15f;  // of the long side, the longest line that agrees
    float minimumAgreementShare = 0.25f;  // of the length near the axes
    int minimumPerspectiveLines = 5;    // that agree once corrected
    float minimumPerspectiveLengthShare = 0.8f;  // of the long side, their length
    float minimumPerspectiveSpread = 0.35f;  // of the side across them, between the farthest two
    float minimumPerspectiveGain = 0.4f;     // share of the misalignment a perspective must remove
};

struct Proposal
{
    Refusal refusal = Refusal::InvalidInput;
    float rotation = 0;       // as in Geometry::Settings
    int vertical = 0;
    int horizontal = 0;
    int supportingLines = 0;  // that agree with the result
    float misalignmentBefore = 0;   // degrees, mean over the lines used, each capped at the agreement's double
    float misalignmentAfter = 0;

    bool valid() const { return refusal == Refusal::None; }
};

// The lines of a linear image, CV_32FC3 or CV_32FC4 (its opacity ignored), read on the square
// root of the luminance so that a step weighs the same in shadows and in lights. Empty on any
// other type.
std::vector<LineDetection::Segment> linesOf(const cv::Mat &image);

// segments: in pixels of a source of this size. base: the geometry in place, whose turns and
// mirrors stay; only the rotation and the two perspectives are worked out.
Proposal propose(const std::vector<LineDetection::Segment> &segments, const cv::Size &source,
                 const Geometry::Settings &base, Mode mode, const Options &options = {});

// The base with the proposal in it, within Geometry's ranges.
Geometry::Settings applied(const Geometry::Settings &base, const Proposal &proposal);

} // namespace Straightening

#endif // IPR_DETECT_STRAIGHTENING_H
