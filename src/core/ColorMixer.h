#ifndef IPR_COLOR_MIXER_H
#define IPR_COLOR_MIXER_H

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

// Input/output: linear working RGB, not gamma-encoded RGB. No output clipping.
cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters);
}

#endif
