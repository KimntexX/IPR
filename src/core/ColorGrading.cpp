#include "ColorGrading.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace ColorGrading {
namespace {

constexpr int Knots = 21;                        // lightness 0, 0.05, ... 1
constexpr int Hues = 12;                         // 0, 30, ... 330 degrees
constexpr std::array<float, 6> SaturationKnots{0.f, 10.f, 25.f, 50.f, 75.f, 100.f};
constexpr double DisplayLightness = 0.72;       // the wheel's grey
constexpr double DisplayChroma = 0.17;          // its chroma at saturation 100, readable

// etude-grading-v001-20261025T173113Z, 28 scenes, R6 Mark III, R7 and 60D
// (leave-one-scene-out unexplained share of Lightroom's change in Oklab: shadows
// 0.057, midtones 0.039, highlights 0.053).
// Zone weight over the pixel's Oklab lightness: per zone, a curve for each measured
// blending (balance 0) and each measured balance (blending 50); the curve at
// blending 50 and balance 0, peak 1, is the default one. etude-grading-fusion-v001-20261025T202111Z:
// leave-one-scene-out unexplained 0.04 to 0.10 per zone and value; zones and the two
// sliders add up around the default curve (three zones together 0.049).
constexpr std::array<int, 5> BlendingValues{0, 25, 50, 75, 100};
constexpr std::array<int, 7> BalanceValues{-100, -50, -25, 0, 25, 50, 100};
using Curve = std::array<float, Knots>;
constexpr std::array<std::array<Curve, 5>, 3> BlendingCurves{{
    {{
        {{0.082f, 0.237f, 0.403f, 0.653f, 0.792f, 0.895f, 0.917f, 0.878f, 0.745f, 0.488f, 0.266f, 0.108f, 0.022f, 0.019f, 0.008f, 0.008f, 0.004f, 0.003f, -0.001f, 0.000f, -0.000f}},
        {{0.083f, 0.236f, 0.403f, 0.652f, 0.796f, 0.903f, 0.934f, 0.912f, 0.805f, 0.589f, 0.390f, 0.237f, 0.148f, 0.127f, 0.093f, 0.072f, 0.048f, 0.033f, 0.015f, 0.002f, -0.001f}},
        {{-0.032f, 0.217f, 0.373f, 0.592f, 0.742f, 0.881f, 0.981f, 1.000f, 0.927f, 0.763f, 0.575f, 0.434f, 0.339f, 0.289f, 0.224f, 0.171f, 0.116f, 0.076f, 0.036f, 0.006f, -0.002f}},
        {{0.091f, 0.252f, 0.425f, 0.689f, 0.852f, 0.984f, 1.047f, 1.087f, 1.070f, 0.988f, 0.861f, 0.724f, 0.616f, 0.528f, 0.409f, 0.310f, 0.212f, 0.143f, 0.070f, 0.011f, -0.001f}},
        {{0.084f, 0.264f, 0.446f, 0.725f, 0.907f, 1.067f, 1.165f, 1.273f, 1.355f, 1.417f, 1.369f, 1.249f, 1.123f, 0.963f, 0.754f, 0.570f, 0.392f, 0.263f, 0.126f, 0.021f, -0.003f}}
    }},
    {{
        {{-0.000f, 0.000f, -0.000f, -0.001f, 0.016f, 0.045f, 0.101f, 0.201f, 0.353f, 0.591f, 0.784f, 0.945f, 1.018f, 0.923f, 0.771f, 0.559f, 0.343f, 0.180f, 0.067f, 0.002f, 0.002f}},
        {{-0.001f, 0.000f, -0.000f, 0.001f, 0.023f, 0.058f, 0.118f, 0.222f, 0.374f, 0.608f, 0.794f, 0.950f, 1.021f, 0.928f, 0.784f, 0.578f, 0.366f, 0.200f, 0.081f, 0.007f, 0.000f}},
        {{-0.002f, 0.000f, -0.000f, 0.004f, 0.028f, 0.068f, 0.135f, 0.244f, 0.402f, 0.617f, 0.798f, 0.918f, 1.000f, 0.980f, 0.834f, 0.639f, 0.428f, 0.247f, 0.106f, 0.016f, -0.003f}},
        {{-0.001f, 0.000f, -0.000f, 0.010f, 0.046f, 0.100f, 0.178f, 0.295f, 0.448f, 0.667f, 0.829f, 0.970f, 1.034f, 0.946f, 0.829f, 0.645f, 0.448f, 0.271f, 0.130f, 0.024f, -0.006f}},
        {{-0.001f, 0.000f, 0.001f, 0.018f, 0.069f, 0.142f, 0.237f, 0.368f, 0.521f, 0.727f, 0.864f, 0.990f, 1.046f, 0.963f, 0.873f, 0.711f, 0.529f, 0.342f, 0.179f, 0.041f, -0.013f}}
    }},
    {{
        {{0.000f, -0.000f, 0.000f, -0.000f, -0.000f, -0.000f, 0.000f, 0.001f, 0.001f, 0.005f, -0.003f, 0.069f, 0.109f, 0.104f, 0.330f, 0.550f, 0.796f, 0.882f, 0.890f, 0.572f, -0.035f}},
        {{-0.000f, 0.000f, -0.000f, -0.000f, 0.004f, 0.009f, 0.016f, 0.027f, 0.041f, 0.070f, 0.084f, 0.181f, 0.240f, 0.240f, 0.457f, 0.650f, 0.865f, 0.922f, 0.905f, 0.572f, -0.033f}},
        {{-0.001f, 0.000f, -0.000f, 0.002f, 0.010f, 0.022f, 0.039f, 0.066f, 0.101f, 0.158f, 0.205f, 0.302f, 0.380f, 0.435f, 0.623f, 0.791f, 0.972f, 1.000f, 0.918f, 0.594f, 0.015f}},
        {{-0.002f, 0.000f, -0.000f, 0.003f, 0.021f, 0.043f, 0.073f, 0.123f, 0.191f, 0.312f, 0.406f, 0.595f, 0.730f, 0.750f, 0.946f, 1.051f, 1.167f, 1.121f, 1.008f, 0.603f, -0.035f}},
        {{-0.002f, 0.001f, -0.001f, 0.008f, 0.039f, 0.081f, 0.136f, 0.228f, 0.356f, 0.575f, 0.754f, 1.040f, 1.257f, 1.303f, 1.476f, 1.490f, 1.499f, 1.335f, 1.110f, 0.633f, -0.063f}}
    }}
}};
constexpr std::array<std::array<Curve, 7>, 3> BalanceCurves{{
    {{
        {{0.075f, 0.238f, 0.405f, 0.662f, 0.860f, 1.038f, 1.166f, 1.339f, 1.525f, 1.787f, 1.882f, 1.920f, 1.917f, 1.777f, 1.546f, 1.220f, 0.875f, 0.572f, 0.247f, 0.052f, -0.005f}},
        {{0.080f, 0.237f, 0.404f, 0.660f, 0.844f, 1.004f, 1.110f, 1.232f, 1.331f, 1.394f, 1.310f, 1.100f, 0.898f, 0.700f, 0.463f, 0.336f, 0.238f, 0.172f, 0.086f, 0.016f, -0.004f}},
        {{0.076f, 0.238f, 0.404f, 0.658f, 0.828f, 0.971f, 1.053f, 1.128f, 1.150f, 1.087f, 0.925f, 0.715f, 0.528f, 0.405f, 0.298f, 0.235f, 0.166f, 0.114f, 0.056f, 0.009f, -0.002f}},
        {{-0.032f, 0.217f, 0.373f, 0.592f, 0.742f, 0.881f, 0.981f, 1.000f, 0.927f, 0.763f, 0.575f, 0.434f, 0.339f, 0.289f, 0.224f, 0.171f, 0.116f, 0.076f, 0.036f, 0.006f, -0.002f}},
        {{0.085f, 0.236f, 0.403f, 0.642f, 0.753f, 0.816f, 0.793f, 0.705f, 0.567f, 0.415f, 0.328f, 0.270f, 0.224f, 0.187f, 0.140f, 0.103f, 0.068f, 0.046f, 0.022f, 0.003f, -0.000f}},
        {{0.097f, 0.233f, 0.401f, 0.616f, 0.661f, 0.635f, 0.530f, 0.398f, 0.299f, 0.246f, 0.211f, 0.172f, 0.139f, 0.113f, 0.081f, 0.059f, 0.038f, 0.025f, 0.012f, 0.002f, -0.000f}},
        {{0.127f, 0.218f, 0.346f, 0.289f, 0.160f, 0.127f, 0.101f, 0.082f, 0.065f, 0.051f, 0.040f, 0.030f, 0.022f, 0.018f, 0.012f, 0.009f, 0.005f, 0.004f, 0.002f, 0.000f, -0.000f}}
    }},
    {{
        {{0.000f, -0.000f, 0.000f, -0.000f, 0.001f, 0.004f, 0.008f, 0.015f, 0.025f, 0.045f, 0.067f, 0.124f, 0.174f, 0.208f, 0.302f, 0.381f, 0.478f, 0.505f, 0.511f, 0.267f, -0.096f}},
        {{-0.001f, 0.000f, -0.000f, 0.000f, 0.009f, 0.023f, 0.044f, 0.081f, 0.138f, 0.245f, 0.362f, 0.560f, 0.718f, 0.804f, 0.912f, 0.885f, 0.783f, 0.556f, 0.302f, 0.060f, -0.023f}},
        {{-0.001f, 0.000f, -0.000f, 0.002f, 0.018f, 0.042f, 0.081f, 0.146f, 0.246f, 0.418f, 0.585f, 0.795f, 0.944f, 0.972f, 0.959f, 0.816f, 0.614f, 0.378f, 0.178f, 0.029f, -0.009f}},
        {{-0.002f, 0.000f, -0.000f, 0.004f, 0.028f, 0.068f, 0.135f, 0.244f, 0.402f, 0.617f, 0.798f, 0.918f, 1.000f, 0.980f, 0.834f, 0.639f, 0.428f, 0.247f, 0.106f, 0.016f, -0.003f}},
        {{-0.002f, 0.000f, -0.000f, 0.011f, 0.059f, 0.134f, 0.247f, 0.411f, 0.601f, 0.824f, 0.924f, 0.942f, 0.885f, 0.700f, 0.530f, 0.365f, 0.221f, 0.125f, 0.054f, 0.007f, -0.001f}},
        {{-0.002f, 0.001f, 0.001f, 0.025f, 0.113f, 0.245f, 0.413f, 0.601f, 0.747f, 0.840f, 0.791f, 0.680f, 0.556f, 0.393f, 0.272f, 0.179f, 0.105f, 0.060f, 0.026f, 0.003f, -0.000f}},
        {{-0.020f, 0.008f, 0.035f, 0.226f, 0.406f, 0.447f, 0.379f, 0.292f, 0.216f, 0.154f, 0.111f, 0.078f, 0.056f, 0.039f, 0.026f, 0.018f, 0.011f, 0.007f, 0.003f, 0.001f, -0.000f}}
    }},
    {{
        {{0.000f, -0.000f, -0.000f, -0.000f, 0.000f, 0.001f, 0.003f, 0.006f, 0.009f, 0.017f, 0.023f, 0.042f, 0.057f, 0.059f, 0.083f, 0.099f, 0.119f, 0.129f, 0.135f, 0.150f, 0.167f}},
        {{0.000f, -0.000f, -0.000f, -0.000f, 0.003f, 0.008f, 0.014f, 0.024f, 0.039f, 0.067f, 0.092f, 0.148f, 0.191f, 0.200f, 0.255f, 0.289f, 0.345f, 0.413f, 0.554f, 0.491f, 0.026f}},
        {{-0.000f, 0.000f, -0.000f, 0.000f, 0.006f, 0.013f, 0.023f, 0.039f, 0.062f, 0.106f, 0.142f, 0.222f, 0.286f, 0.282f, 0.370f, 0.463f, 0.615f, 0.702f, 0.775f, 0.549f, -0.021f}},
        {{-0.001f, 0.000f, -0.000f, 0.002f, 0.010f, 0.022f, 0.039f, 0.066f, 0.101f, 0.158f, 0.205f, 0.302f, 0.380f, 0.435f, 0.623f, 0.791f, 0.972f, 1.000f, 0.918f, 0.594f, 0.015f}},
        {{-0.001f, 0.000f, -0.000f, 0.003f, 0.018f, 0.036f, 0.061f, 0.099f, 0.150f, 0.235f, 0.302f, 0.505f, 0.683f, 0.785f, 1.056f, 1.179f, 1.280f, 1.185f, 1.014f, 0.584f, -0.033f}},
        {{-0.002f, 0.001f, -0.001f, 0.007f, 0.031f, 0.063f, 0.102f, 0.160f, 0.232f, 0.380f, 0.557f, 0.888f, 1.169f, 1.289f, 1.500f, 1.521f, 1.512f, 1.316f, 1.061f, 0.590f, -0.036f}},
        {{0.002f, -0.000f, 0.008f, 0.049f, 0.148f, 0.290f, 0.520f, 0.835f, 1.156f, 1.514f, 1.719f, 1.956f, 2.091f, 1.986f, 1.993f, 1.854f, 1.711f, 1.420f, 1.096f, 0.596f, -0.039f}}
    }}
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

// Curve of a slider value between its two measured neighbours.
template <size_t N>
Curve curveAt(const std::array<Curve, N> &curves, const std::array<int, N> &values, int value)
{
    value = std::clamp(value, values.front(), values.back());
    size_t i = 1;
    while (i + 1 < N && values[i] < value) ++i;
    const float t = float(value - values[i - 1]) / float(values[i] - values[i - 1]);
    Curve out{};
    for (int k = 0; k < Knots; ++k) out[size_t(k)] = curves[i - 1][size_t(k)] + t * (curves[i][size_t(k)] - curves[i - 1][size_t(k)]);
    return out;
}

float weightAt(const Curve &curve, float lightness)
{
    const float position = std::clamp(lightness, 0.f, 1.f) * float(Knots - 1);
    const int i = std::min(int(position), Knots - 2);
    const float t = position - float(i);
    return curve[size_t(i)] * (1.f - t) + curve[size_t(i + 1)] * t;
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

Zones prepare(const std::array<int, 3> &hue, const std::array<int, 3> &saturation, int blending, int balance)
{
    Zones zones;
    for (size_t zone = 0; zone < 3; ++zone) {
        if (saturation[zone] == 0) continue;
        zones.change[zone] = changeOf(zone, hue[zone], saturation[zone]);
        zones.active[zone] = true;
        // Default curve plus the change each slider makes to it.
        const Curve &base = BlendingCurves[zone][2];
        const Curve b = curveAt(BlendingCurves[zone], BlendingValues, blending);
        const Curve v = curveAt(BalanceCurves[zone], BalanceValues, balance);
        for (int k = 0; k < Knots; ++k)
            zones.curve[zone][size_t(k)] = std::max(0.f, b[size_t(k)] + v[size_t(k)] - base[size_t(k)]);
    }
    return zones;
}

void adjust(cv::Vec3d &lab, const Zones &zones)
{
    const float lightness = float(lab[0]);
    for (size_t zone = 0; zone < 3; ++zone)
        if (zones.active[zone]) lab += cv::Vec3d(zones.change[zone]) * double(weightAt(zones.curve[zone], lightness));
}

cv::Vec3f displayColour(int zone, int hue, int saturation)
{
    if (zone < 0 || zone > 2) return {0.5f, 0.5f, 0.5f};
    const cv::Vec3f change = changeOf(size_t(zone), hue, 50);
    const double chroma = std::hypot(double(change[1]), double(change[2]));
    if (chroma <= 0) return WorkingColor::encodedInGamut({DisplayLightness, 0, 0});
    const double scale = DisplayChroma * std::clamp(saturation, 0, 100) / 100.0 / chroma;
    return WorkingColor::encodedInGamut({DisplayLightness + change[0] * scale, change[1] * scale, change[2] * scale});
}

} // namespace ColorGrading
