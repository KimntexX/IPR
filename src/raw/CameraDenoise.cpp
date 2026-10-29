#include "CameraDenoise.h"

namespace CameraDenoise {

bool identity(const Settings &settings)
{
    return settings.luminance == 0 && settings.chrominance == 0;
}

bool apply(cv::Mat &cameraRgb, const Settings &settings, const Context &context,
           const std::function<bool()> &cancelled)
{
    (void)context;
    if (cameraRgb.empty() || cameraRgb.type() != CV_32FC3) return false;
    if (identity(settings)) return true;
    if (cancelled && cancelled()) return false;
    // Placeholder until the algorithm (Codex, N2): the raccordement is tested with
    // an identity.
    return true;
}

} // namespace CameraDenoise
