#include "ColorMixer.h"

#include <algorithm>
#include <cmath>

namespace {
float luminance(const cv::Vec3f &rgb)
{
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}
}

cv::Vec3f ColorMixer::apply(const cv::Vec3f &rgb, const Parameters &parameters)
{
    const float maximum = std::max({rgb[0], rgb[1], rgb[2]});
    const float minimum = std::min({rgb[0], rgb[1], rgb[2]});
    const float delta = maximum - minimum;
    const float sourceY = luminance(rgb);
    // Hue is undefined for achromatics; never treat the conventional H=0 as red.
    // Keep negative/out-of-gamut working pixels intact rather than clamp them.
    if (delta <= 0.0f || minimum < 0.0f || sourceY <= 0.0f)
        return rgb;

    const float sourceSaturation = delta / maximum;
    // Provisional protection ramp: full response above 5% relative chroma.
    // Smoothstep avoids a discontinuity at the achromatic boundary.
    constexpr float fullChromaResponse = 0.05f;
    const float t = std::clamp(sourceSaturation / fullChromaResponse, 0.0f, 1.0f);
    const float chromaWeight = t * t * (3.0f - 2.0f * t);
    float sourceHue;
    if (maximum == rgb[0])
        sourceHue = 60.0f * std::fmod((rgb[1] - rgb[2]) / delta, 6.0f);
    else if (maximum == rgb[1])
        sourceHue = 60.0f * ((rgb[2] - rgb[0]) / delta + 2.0f);
    else
        sourceHue = 60.0f * ((rgb[0] - rgb[1]) / delta + 4.0f);
    if (sourceHue < 0.0f)
        sourceHue += 360.0f;

    float hueAmount = 0.0f, saturationAmount = 0.0f, luminanceAmount = 0.0f;
    for (size_t band = 0; band < BandCount; ++band) {
        const float distance = std::remainder(sourceHue - Centers[band], 360.0f);
        const size_t neighbour = distance < 0.0f ? (band + BandCount - 1) % BandCount
                                               : (band + 1) % BandCount;
        const float width = std::abs(std::remainder(Centers[neighbour] - Centers[band], 360.0f));
        const float weight = std::clamp(1.0f - std::abs(distance) / width, 0.0f, 1.0f)
                             * chromaWeight;
        // Accumulate on the immutable source hue: adjacent masks partition unity,
        // so their effects neither multiply nor depend on iteration order.
        hueAmount += std::clamp(parameters.hue[band], -100, 100) * 0.01f * weight;
        saturationAmount += std::clamp(parameters.saturation[band], -100, 100) * 0.01f * weight;
        luminanceAmount += std::clamp(parameters.luminance[band], -100, 100) * 0.01f * weight;
    }
    if (hueAmount == 0.0f && saturationAmount == 0.0f && luminanceAmount == 0.0f)
        return rgb;

    // Preserve the existing +/-18 degree hue control; saturation uses one gain.
    const float hue = std::fmod(sourceHue + 18.0f * hueAmount + 360.0f, 360.0f);
    const float saturation = std::clamp(sourceSaturation * (1.0f + saturationAmount), 0.0f, 1.0f);
    const float chroma = maximum * saturation;
    const float secondary = chroma * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    cv::Vec3f result;
    if (hue < 60.0f) result = {chroma, secondary, 0.0f};
    else if (hue < 120.0f) result = {secondary, chroma, 0.0f};
    else if (hue < 180.0f) result = {0.0f, chroma, secondary};
    else if (hue < 240.0f) result = {0.0f, secondary, chroma};
    else if (hue < 300.0f) result = {secondary, 0.0f, chroma};
    else result = {chroma, 0.0f, secondary};
    const float offset = maximum - chroma;
    result += cv::Vec3f(offset, offset, offset);

    // Correct the HSV V/Y coupling before applying selective exposure. The
    // provisional +/-0.5 EV bound prevents V->1 from blowing up dark colors.
    // Preserve hue/RGB ratios, leave HDR headroom to the downstream gamut mapper.
    constexpr float maximumSelectiveEv = 0.5f;
    const float targetY = sourceY * std::exp2(maximumSelectiveEv * luminanceAmount);
    return result * (targetY / luminance(result));
}
