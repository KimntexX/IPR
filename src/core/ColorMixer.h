#ifndef IPR_CORE_COLORMIXER_H
#define IPR_CORE_COLORMIXER_H

#include "Types.h"

#include <array>
#include "Image.h"

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

// True when a slider is not at zero.
bool active(const Parameters &parameters);

// One Oklab colour, in place, given the linear ProPhoto saturation of its pixel;
// slider values in [-100, 100] (clamped). Greys are left unchanged; no clipping.
// The image loop is PerceptualColour's.
void adjust(ipr::Vec3d &lab, double proPhotoSaturation, const Parameters &parameters);

// Input/output: linear Rec.2020 D65 RGB: adjust() with its conversions. Zero
// settings and greys are returned unchanged.
ipr::Vec3f apply(const ipr::Vec3f &rgb, const Parameters &parameters);

// Encoded sRGB colour (channels in [0, 1]) that one slider (component 0 hue,
// 1 saturation, 2 luminance) of a band reaches at a value, applied by apply()
// to the band's reference colour at its response peak; the chroma is reduced
// to fit sRGB, the hue kept. For the UI slider tracks; neutral grey when the
// band or component is invalid.
ipr::Vec3f trackColour(int band, int component, int value);
}

#endif // IPR_CORE_COLORMIXER_H
