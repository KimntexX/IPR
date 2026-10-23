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

// Encoded sRGB colour (channels in [0, 1]) that one slider (component 0 hue,
// 1 saturation, 2 luminance) of a band reaches at a value, applied by apply()
// to the band's reference colour at its response peak; the chroma is reduced
// to fit sRGB, the hue kept. For the UI slider tracks; neutral grey when the
// band or component is invalid.
cv::Vec3f trackColour(int band, int component, int value);
}

#endif // IPR_CORE_COLORMIXER_H
