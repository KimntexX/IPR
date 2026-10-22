#include "ColorAdjustments.h"

#include "ImageMath.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace ColorAdjustments {
namespace {
cv::Vec3f hsvToRgb(float hue, float saturation, float value)
{
    hue = std::fmod(hue + 360.0f, 360.0f);
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    const float chroma = value * saturation;
    const float secondary = chroma * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float offset = value - chroma;
    cv::Vec3f rgb;
    if (hue < 60.0f) rgb = {chroma, secondary, 0.0f};
    else if (hue < 120.0f) rgb = {secondary, chroma, 0.0f};
    else if (hue < 180.0f) rgb = {0.0f, chroma, secondary};
    else if (hue < 240.0f) rgb = {0.0f, secondary, chroma};
    else if (hue < 300.0f) rgb = {secondary, 0.0f, chroma};
    else rgb = {chroma, 0.0f, secondary};
    return rgb + cv::Vec3f(offset, offset, offset);
}

template <typename Values>
bool anyNonZero(const Values &values)
{
    return std::any_of(values.cbegin(), values.cend(), [](int value) { return value != 0; });
}
}

void toGrayscale(cv::Mat &linearRgb)
{
    for (int y = 0; y < linearRgb.rows; ++y) {
        auto *row = linearRgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < linearRgb.cols; ++x) {
            const float value = WorkingColor::luminance(row[x]);
            row[x] = {value, value, value};
        }
    }
}

void applyMixerAndGrading(cv::Mat &linearRgb, const ColorMixer::Parameters &mixer,
                          const std::array<int, 3> &gradingHue,
                          const std::array<int, 3> &gradingSaturation)
{
    const bool hasColorMixer = anyNonZero(mixer.hue) || anyNonZero(mixer.saturation) || anyNonZero(mixer.luminance);
    const bool hasColorGrading = anyNonZero(gradingSaturation);
    if ((!hasColorMixer && !hasColorGrading) || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    // Pixels are independent: rows run in parallel.
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                cv::Vec3f pixel = row[x];
                const float originalLuminance = WorkingColor::nonNegativeLuminance(pixel);
                if (hasColorMixer) pixel = ColorMixer::apply(pixel, mixer);
                if (hasColorGrading) {
                    const float shadows = 1.0f - ImageMath::smoothStep(0.12f, 0.42f, originalLuminance);
                    const float highlights = ImageMath::smoothStep(0.58f, 0.88f, originalLuminance);
                    const float midtones = std::clamp(1.0f - shadows - highlights, 0.0f, 1.0f);
                    const std::array<float, 3> weights{shadows, midtones, highlights};
                    for (size_t zone = 0; zone < weights.size(); ++zone) {
                        if (gradingSaturation[zone] == 0 || weights[zone] == 0.0f)
                            continue;
                        const cv::Vec3f color = hsvToRgb(static_cast<float>(gradingHue[zone]), 1.0f, 1.0f);
                        const float colorLuminance = WorkingColor::nonNegativeLuminance(color);
                        pixel += (color - cv::Vec3f(colorLuminance, colorLuminance, colorLuminance)) *
                                 (gradingSaturation[zone] * 0.0035f * weights[zone]);
                    }
                }
                row[x] = pixel;
            }
        }
    });
}
}
