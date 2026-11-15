#ifndef IPR_RAW_CAMERADENOISE_H
#define IPR_RAW_CAMERADENOISE_H

#include "Image.h"
#include "Types.h"

#include <functional>
#include <utility>
#include <vector>

// Experimental, profile-independent intensity denoising of signed camera RGB, after
// the opening's RCD/median and BEFORE optical corrections and nonlinear DCP rendering.
// Colour noise is removed later, after the tones (DetailProcessing::reduceNoise).
namespace CameraDenoise {

// Optional caller-thread diagnostics. Coefficient sigmas contain texture as well
// as noise; these are not a calibrated sensor model or a clean-noise oracle.
struct NoiseModelSummary
{
    int level = 0;
    int candidates = 0;
    int flatCandidates = 0;
    bool supported = false;
    bool darkFloorApplied = false;
    bool globalFallbackAvailable = false;
    float low = 0, high = 0, globalSignal = 0;
    ipr::Vec3f globalSigma{};
    std::vector<std::pair<float, ipr::Vec3f>> nodes;
};

struct Context
{
    double iso = 0;                        // 0 unknown; NOT a noise-strength formula.
    double pixelScale = 1;                 // Input/full-resolution extent, in (0, 1].
    // Called before filtering on the caller thread, once per active scale.
    // An exception aborts atomically. Absent by default; no render change.
    std::function<void(const NoiseModelSummary &)> noiseModelObserver;
};

// At most eight threads INCLUDING the caller. OpenCV's global thread settings
// are not changed. Only the caller invokes cancellation; all workers are joined.
int workerThreads();

// Input: finite F32 of three channels, white 1, signed/extended range, with white balance
// ALREADY applied. Do not undo WB: both estimation and correction operate in
// these balanced camera axes. Camera intensity is an
// opponent-axis proxy, not photometric Rec.2020 luminance. Noise is measured per
// scale and witnessed signal range after RCD/median, with conditional covariance;
// this is NOT a calibrated sensor a*signal+b model. Unsupported signals fall
// back to the pooled model when available; without any usable witness, abstain.
// Outside a conditional range, a short exterior transition joins its boundary
// to the pooled model, without changing the conditional knots inside the range.
// With multiple witnessed bands, the darkest knot has an empirical pooled-
// variance floor; the existing interpolation fades it toward the next knot.
// Three intensity scales, with a structure gate over the three opponent axes. The
// SAME delta is added to R/G/B, preserving their differences before DCP (not
// necessarily encoded/rendered chroma after nonlinear mapping).
// Luminance is in [0,100]. Zero preserves the buffer exactly, without scanning
// pixels/using Context. An invalid shape/type/strength fails even on that path.
// Nonzero validates finitude/Context. Success publishes a NEW buffer, never edits
// aliased source pixels; on failure/cancellation the input is unchanged. No
// lower/upper clamp.
bool apply(ipr::Image &cameraRgb, int luminance, const Context &context,
           const std::function<bool()> &cancelled = {});

} // namespace CameraDenoise

#endif // IPR_RAW_CAMERADENOISE_H
