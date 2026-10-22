#ifndef IPR_CORE_COLORMIXER_H
#define IPR_CORE_COLORMIXER_H

#include <array>
#include <opencv2/core.hpp>

// Colour mixer following the Lightroom response tables (ColorMixerModel.h):
// for each band (red, orange, yellow, green, cyan, blue, violet, magenta) and
// component, a hue rotation, a chroma gain and a lightness gain in Oklab, by
// the pixel's Oklab hue, linear ProPhoto saturation and Oklab lightness,
// interpolated between the measured slider values. Active sliders add up.
namespace ColorMixer {
inline constexpr size_t BandCount = 8;

struct Parameters {
    std::array<int, BandCount> hue{};
    std::array<int, BandCount> saturation{};
    std::array<int, BandCount> luminance{};
};

// Input/output: linear Rec.2020 D65 RGB, slider values in [-100, 100]
// (clamped). Zero settings and greys are returned unchanged; no clipping.
cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters);
}

#endif // IPR_CORE_COLORMIXER_H
