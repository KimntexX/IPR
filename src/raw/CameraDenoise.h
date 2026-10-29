#ifndef IPR_RAW_CAMERADENOISE_H
#define IPR_RAW_CAMERADENOISE_H

#include <opencv2/core.hpp>

#include <array>
#include <functional>

// Noise reduction of the developed camera RGB, before the lens and colour profiles
// (experimental, PLAN-SEPARATION-LIBRAW interface I4): the signed linear signal,
// values below the black included, white 1. Strength 0 leaves the image untouched.
namespace CameraDenoise {

struct Settings
{
    int luminance = 0;     // 0..100, the editor's sliders
    int chrominance = 0;   // 0..100
};

struct Context
{
    std::array<double, 4> gains{};   // white-balance gains already applied (smallest 1), per RawInput colour
    double iso = 0.0;                // 0 when unknown
    double pixelScale = 1.0;         // 1 at full definition, below on the preview proxy
};

bool identity(const Settings &settings);

// Denoises cameraRgb (CV_32FC3, signed, finite) in place. Returns false when
// cancelled or on an invalid image; the image is then left as it was.
bool apply(cv::Mat &cameraRgb, const Settings &settings, const Context &context,
           const std::function<bool()> &cancelled = {});

} // namespace CameraDenoise

#endif // IPR_RAW_CAMERADENOISE_H
