#ifndef IPR_RAW_CAMERADENOISE_H
#define IPR_RAW_CAMERADENOISE_H

#include <opencv2/core.hpp>
#include <functional>

// Experimental, profile-independent denoising of signed camera RGB, after the
// opening's RCD/median and BEFORE optical corrections and nonlinear DCP rendering.
namespace CameraDenoise {

struct Settings
{
    int luminance = 0;
    int chrominance = 0;
};

struct Context
{
    double iso = 0;                        // 0 unknown; NOT a noise-strength formula.
    double pixelScale = 1;                 // Input/full-resolution extent, in (0, 1].
};

// At most eight threads INCLUDING the caller. OpenCV's global thread settings
// are not changed. Only the caller invokes cancellation; all workers are joined.
int workerThreads();

bool identity(const Settings &settings);

// Input: finite CV_32FC3, white 1, signed/extended range, with white balance
// ALREADY applied. Do not undo WB: both estimation and correction operate in
// these balanced camera axes. Camera intensity is an
// opponent-axis proxy, not photometric Rec.2020 luminance. Noise is measured per
// scale after RCD/median; this is NOT a calibrated sensor a*signal+b model.
// Three intensity scales; five chroma scales when chroma is enabled. Chroma-only
// preserves camera intensity, NOT rendered Y or the mean of encoded RGB.
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
