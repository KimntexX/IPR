#include "ColorGrading.h"
#include "WorkingColorSpace.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>

namespace ColorGrading {
namespace {

constexpr int Knots = 21;                        // lightness 0, 0.05, ... 1
constexpr int Hues = 12;                         // 0, 30, ... 330 degrees
constexpr std::array<float, 6> SaturationKnots{0.f, 10.f, 25.f, 50.f, 75.f, 100.f};

// etude-grading-v001-20261025T173113Z, 28 scenes, R6 Mark III, R7 and 60D
// (leave-one-scene-out unexplained share of Lightroom's change in Oklab: shadows
// 0.057, midtones 0.039, highlights 0.053).
// Zone weight over the pixel's Oklab lightness, peak 1.
constexpr std::array<std::array<float, Knots>, 3> Curves{{
    {{0.000f, 0.217f, 0.373f, 0.592f, 0.742f, 0.881f, 0.981f, 1.000f, 0.927f, 0.763f, 0.575f, 0.434f, 0.339f, 0.289f, 0.224f, 0.171f, 0.116f, 0.076f, 0.036f, 0.006f, 0.000f}},
    {{0.000f, 0.000f, 0.000f, 0.004f, 0.028f, 0.068f, 0.135f, 0.244f, 0.402f, 0.617f, 0.798f, 0.918f, 1.000f, 0.980f, 0.834f, 0.639f, 0.428f, 0.247f, 0.106f, 0.016f, 0.000f}},
    {{0.000f, 0.000f, 0.000f, 0.002f, 0.010f, 0.022f, 0.039f, 0.066f, 0.101f, 0.158f, 0.205f, 0.302f, 0.380f, 0.435f, 0.623f, 0.791f, 0.972f, 1.000f, 0.918f, 0.594f, 0.015f}}
}};
// Change in Oklab (dL, da, db) of each hue at saturation 50.
constexpr std::array<std::array<std::array<float, 3>, Hues>, 3> Colours{{
    {{{0.0080f, 0.0615f, 0.0119f}, {0.0028f, 0.0277f, 0.0284f}, {0.0099f, -0.0073f, 0.0420f}, {-0.0052f, -0.0412f, 0.0381f}, {-0.0046f, -0.0698f, 0.0340f}, {-0.0024f, -0.0635f, 0.0119f}, {0.0002f, -0.0586f, -0.0078f}, {-0.0015f, -0.0321f, -0.0240f}, {0.0023f, 0.0020f, -0.0371f}, {0.0061f, 0.0353f, -0.0321f}, {0.0103f, 0.0662f, -0.0268f}, {0.0091f, 0.0646f, -0.0084f}}},
    {{{-0.0086f, 0.0681f, 0.0102f}, {0.0154f, 0.0326f, 0.0250f}, {0.0409f, -0.0035f, 0.0402f}, {0.0255f, -0.0371f, 0.0348f}, {0.0116f, -0.0696f, 0.0299f}, {0.0130f, -0.0674f, 0.0114f}, {0.0145f, -0.0659f, -0.0077f}, {-0.0137f, -0.0335f, -0.0254f}, {-0.0400f, -0.0002f, -0.0423f}, {-0.0237f, 0.0352f, -0.0356f}, {-0.0062f, 0.0703f, -0.0283f}, {-0.0075f, 0.0696f, -0.0087f}}},
    {{{0.0081f, 0.0621f, 0.0116f}, {0.0027f, 0.0297f, 0.0233f}, {-0.0019f, -0.0027f, 0.0351f}, {-0.0046f, -0.0327f, 0.0309f}, {-0.0062f, -0.0621f, 0.0270f}, {-0.0053f, -0.0609f, 0.0093f}, {-0.0043f, -0.0602f, -0.0097f}, {-0.0014f, -0.0300f, -0.0252f}, {-0.0147f, 0.0009f, -0.0408f}, {-0.0002f, 0.0320f, -0.0354f}, {0.0108f, 0.0636f, -0.0294f}, {0.0094f, 0.0632f, -0.0080f}}}
}};
// Strength of the saturation slider over its value at 50 (mean of 30 and 210 degrees).
constexpr std::array<std::array<float, 6>, 3> Strengths{{
    {{0.000f, 0.203f, 0.506f, 1.000f, 1.423f, 1.829f}},
    {{0.000f, 0.201f, 0.502f, 1.000f, 1.482f, 1.952f}},
    {{0.000f, 0.199f, 0.498f, 1.000f, 1.525f, 2.101f}}
}};

float curveAt(size_t zone, float lightness)
{
    const float position = std::clamp(lightness, 0.f, 1.f) * float(Knots - 1);
    const int i = std::min(int(position), Knots - 2);
    const float t = position - float(i);
    return Curves[zone][size_t(i)] * (1.f - t) + Curves[zone][size_t(i + 1)] * t;
}

// Change of a zone at full weight: the colour of its hue, interpolated around the
// circle, times the strength of its saturation.
cv::Vec3f changeOf(size_t zone, int hue, int saturation)
{
    const float h = float(((hue % 360) + 360) % 360) / 30.f;
    const int i = int(h) % Hues, j = (i + 1) % Hues;
    const float t = h - std::floor(h);
    const auto &a = Colours[zone][size_t(i)], &b = Colours[zone][size_t(j)];
    const float s = float(std::clamp(saturation, 0, 100));
    size_t k = 1;
    while (k + 1 < SaturationKnots.size() && SaturationKnots[k] < s) ++k;
    const float u = (s - SaturationKnots[k - 1]) / (SaturationKnots[k] - SaturationKnots[k - 1]);
    const float strength = Strengths[zone][k - 1] + u * (Strengths[zone][k] - Strengths[zone][k - 1]);
    return cv::Vec3f(a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])) * strength;
}

} // namespace

void apply(cv::Mat &linearRgb, const std::array<int, 3> &hue, const std::array<int, 3> &saturation)
{
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3
        || std::all_of(saturation.cbegin(), saturation.cend(), [](int s) { return s == 0; }))
        return;
    std::array<cv::Vec3f, 3> change{};
    for (size_t zone = 0; zone < 3; ++zone)
        if (saturation[zone] != 0) change[zone] = changeOf(zone, hue[zone], saturation[zone]);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                cv::Vec3d lab = WorkingColor::workingToOklab(cv::Vec3d(row[x]));
                const float lightness = float(lab[0]);
                for (size_t zone = 0; zone < 3; ++zone)
                    if (saturation[zone] != 0) lab += cv::Vec3d(change[zone]) * double(curveAt(zone, lightness));
                row[x] = cv::Vec3f(WorkingColor::oklabToWorking(lab));
            }
        }
    });
}

} // namespace ColorGrading
