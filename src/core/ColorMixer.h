#ifndef IPR_CORE_COLORMIXER_H
#define IPR_CORE_COLORMIXER_H

#include <array>
#include <opencv2/core.hpp>

namespace ColorMixer {
inline constexpr size_t BandCount = 8;
inline constexpr std::array<float, BandCount> Centers{
    0.0f, 30.0f, 60.0f, 120.0f, 180.0f, 240.0f, 270.0f, 300.0f};

struct Parameters {
    std::array<int, BandCount> hue{};
    std::array<int, BandCount> saturation{};
    std::array<int, BandCount> luminance{};
};

// Input/output: linear Rec.2020 D65 RGB, not gamma-encoded RGB. No output clipping.
// HSV band centres refer to these working primaries, not to sRGB channel angles.
cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters);
}

#endif // IPR_CORE_COLORMIXER_H
