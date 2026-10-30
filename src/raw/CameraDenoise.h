#ifndef IPR_RAW_CAMERADENOISE_H
#define IPR_RAW_CAMERADENOISE_H

#include <opencv2/core.hpp>
#include <functional>
#include <utility>
#include <vector>

// Experimental, profile-independent denoising of signed camera RGB, after the
// opening's RCD/median and BEFORE optical corrections and nonlinear DCP rendering.
namespace CameraDenoise {

struct Settings
{
    int luminance = 0;
    int chrominance = 0;
};

// Optional caller-thread diagnostics. Coefficient sigmas contain texture as well
// as noise; these are not a calibrated sensor model or a clean-noise oracle.
struct NoiseModelSummary
{
    int level = 0;
    int candidates = 0;
    int flatCandidates = 0;
    bool supported = false;
    bool darkFloorApplied = false;
    float low = 0, high = 0, globalSignal = 0;
    cv::Vec3f globalSigma{};
    std::vector<std::pair<float, cv::Vec3f>> nodes;
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

bool identity(const Settings &settings);

// Input: finite CV_32FC3, white 1, signed/extended range, with white balance
// ALREADY applied. Do not undo WB: both estimation and correction operate in
// these balanced camera axes. Camera intensity is an
// opponent-axis proxy, not photometric Rec.2020 luminance. Noise is measured per
// scale and witnessed signal range after RCD/median, with conditional covariance;
// this is NOT a calibrated sensor a*signal+b model. Unsupported signals abstain.
// With multiple witnessed bands, the darkest knot has an empirical pooled-
// variance floor; the existing interpolation fades it toward the next knot.
// Three intensity scales; five chroma scales when chroma is enabled. Chroma-only
// preserves camera intensity, NOT rendered Y or the mean of encoded RGB.
// Chroma structure protection uses Co/Cg only: achromatic luminance texture
// must not act as a veto on colour-noise correction. Intensity keeps its 3D gate.
// Intensity-only adds the SAME delta to R/G/B, preserving their differences
// before DCP (not necessarily encoded/rendered chroma after nonlinear mapping).
// Zero forces preserve the buffer exactly, without scanning pixels/using Context.
// Invalid shape/type/settings fail even on that path. Nonzero forces validate
// finitude/Context. Success publishes a NEW buffer, never edits aliased source
// pixels; on failure/cancellation the input is unchanged. No lower/upper clamp.
bool apply(cv::Mat &cameraRgb, const Settings &settings, const Context &context,
           const std::function<bool()> &cancelled = {});

} // namespace CameraDenoise

#endif // IPR_RAW_CAMERADENOISE_H
