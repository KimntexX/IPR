#include "Contrast.h"
#include "LocalValues.h"

#include <algorithm>
#include <cmath>

namespace Contrast {
namespace {
constexpr float floorEv = -24.0f;

// Position in the 16 EV log domain below white, 0 at the floor, 1 at white.
float encode(float value) { return (std::log2(value) - floorEv) / -floorEv; }

float curve(float norm, float amount)
{
    constexpr float pivot = 0.225f;
    const float floorValue = std::exp2(floorEv);
    if (norm <= floorValue || norm >= 1.0f) return norm;
    const float v0 = encode(pivot), v = encode(norm);
    const float g = std::exp(amount > 0.0f ? (0.47f + 0.07f * amount) * amount
                                           : (0.44f + 0.01f * amount) * amount);
    const float out = v <= v0
        ? v0 * std::pow(v / v0, g)
        : 1.0f - (1.0f - v0) * std::pow((1.0f - v) / (1.0f - v0), g);
    return std::exp2(floorEv + out * -floorEv);
}
}

void apply(cv::Mat &linearRgb, int contrast, const cv::Mat &local)
{
    if (local.empty() || !LocalValues::fits(local, linearRgb.size())) {
        if (contrast == 0) return;
        const float amount = static_cast<float>(contrast) / 100.0f;
        for (int y = 0; y < linearRgb.rows; ++y) {
            cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x)
                for (int c = 0; c < 3; ++c)
                    row[x][c] = curve(row[x][c], amount);
        }
        return;
    }
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
            const float *values = local.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                // Global plus local, within the slider's range the curve was fitted on.
                const float value = std::clamp(static_cast<float>(contrast) + values[x], -100.0f, 100.0f);
                if (value == 0.0f) continue;
                const float amount = value / 100.0f;
                for (int c = 0; c < 3; ++c)
                    row[x][c] = curve(row[x][c], amount);
            }
        }
    });
}
}
