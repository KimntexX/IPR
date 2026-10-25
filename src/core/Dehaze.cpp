#include "Dehaze.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace Dehaze {
namespace {

// The law was fitted at 1/4 of the full resolution: dark channel eroded over 15
// pixels, haze smoothed at 128 pixels of the full image.
constexpr double FittedReduction = 4.0;
constexpr double HazeSigma = 128.0;
constexpr double DarkWindow = 15.0;
constexpr double HaziestShare = 0.001;   // pixels giving the airlight colour
constexpr float RatioScale = 1.0f;       // R0 of the saturation, EV
constexpr double BaseSigma = 64.0;       // full-image pixels: the base of the tone change (256 mixed regions)
constexpr float LogFloor = 1e-3f;        // soft floor of the log: deep shadows are not stretched without bound

// Detail gain in log2 around the base, uniform over scales as Lightroom's band
// gains are: log2 G = c0 + ch h + cu u, u = log2 of the local mean (64 pixels)
// over A, per slider value, bounded to the observed range. Fitted on Lightroom's
// pairs with equal weight per scene (6V1A0032, 6V1A0043, 6V1A0327, 6V1A0544, 6V1A9112, 6V1A9153, 6V1A9539, IMG_4345, IMG_6954, IMG_7953, _MG_7580),
// etude-detail-voile-v003-20261025T124720Z: error of the scene median log2 G,
// leave-one-scene-out, 0.06 to 0.54 (0.11 to 0.75 for a constant gain).
struct DetailLaw { int value; float c0, ch, cu; };
constexpr std::array<DetailLaw, 7> DetailLaws{{
    {-100, -2.584f, -1.532f, -0.134f},
    {-50, -0.955f, -0.531f, -0.093f},
    {-25, -0.428f, -0.231f, -0.049f},
    {0, 0.f, 0.f, 0.f},
    {25, 0.213f, 0.298f, 0.022f},
    {50, 0.415f, 0.686f, 0.042f},
    {100, 0.698f, 2.006f, 0.079f},
}};
constexpr double DetailMeanSigma = 64.0;     // full-image pixels: the local mean of u
constexpr float MinLog2Gain = -4.f, MaxLog2Gain = 2.5f;
constexpr float HighlightRange = 0.5f;   // EV below the airlight where highlights are kept

DetailLaw detailLawAt(int value)
{
    value = std::clamp(value, DetailLaws.front().value, DetailLaws.back().value);
    size_t i = 1;
    while (i + 1 < DetailLaws.size() && DetailLaws[i].value < value) ++i;
    const DetailLaw &low = DetailLaws[i - 1], &high = DetailLaws[i];
    const float t = float(value - low.value) / float(high.value - low.value);
    const auto mix = [t](float x, float y) { return x + t * (y - x); };
    return {value, mix(low.c0, high.c0), mix(low.ch, high.ch), mix(low.cu, high.cu)};
}

constexpr std::array<float, 13> ToneKnots{-10.f, -8.f, -6.f, -5.f, -4.f, -3.f, -2.5f, -2.f, -1.5f, -1.f, -0.5f, 0.f, 0.6f};
constexpr std::array<float, 6> HazeKnots{0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f};
using Table = std::array<std::array<float, HazeKnots.size()>, ToneKnots.size()>;   // luminance gain in EV
// Gain clamped to [low, high]; beta: share of airlight-coloured light when adding
// haze; kappa: saturation where haze is removed.
struct Law { int value; float low, high, beta, kappa; Table gain; };

// Pooled over 6V1A0032, 6V1A0043, 6V1A0327, 6V1A0544, 6V1A9112, 6V1A9153, 6V1A9539, IMG_4345, IMG_6954, IMG_7953, _MG_7580, etude-loi-voile-v009-20261025T123104Z
// (leave-one-scene-out unexplained share of the luminance change in EV 0.07 to 0.29, of the
// colour change 0.34 to 0.46 below 0 and 0.75 to 0.81 above).
const std::array<Law, 6> Laws{{
    {-100, -0.05f, 5.78f, 0.90f, 0.00f,
        {{
         {4.46f, 6.70f, 8.14f, 9.02f, 9.65f, 10.21f},
         {3.50f, 5.88f, 6.59f, 7.21f, 7.83f, 8.43f},
         {3.27f, 4.41f, 4.93f, 5.52f, 6.13f, 6.72f},
         {2.75f, 3.55f, 3.75f, 4.14f, 4.64f, 5.16f},
         {2.05f, 2.90f, 2.94f, 3.11f, 3.44f, 3.84f},
         {1.51f, 2.26f, 2.31f, 2.37f, 2.55f, 2.80f},
         {1.25f, 2.01f, 1.85f, 1.84f, 1.94f, 2.06f},
         {1.04f, 1.59f, 1.49f, 1.50f, 1.53f, 1.55f},
         {0.77f, 0.95f, 1.22f, 1.28f, 1.24f, 1.16f},
         {0.41f, 0.54f, 0.79f, 1.00f, 0.93f, 0.81f},
         {0.14f, 0.24f, 0.23f, 0.49f, 0.51f, 0.43f},
         {-0.05f, -0.05f, -0.01f, 0.09f, 0.05f, 0.00f},
         {-0.23f, -0.25f, -0.27f, -0.32f, -0.38f, -0.42f}}}},
    {-50, -0.03f, 2.83f, 0.55f, 0.00f,
        {{
         {1.72f, 3.09f, 4.18f, 4.98f, 5.63f, 6.23f},
         {1.52f, 2.96f, 3.55f, 4.08f, 4.62f, 5.16f},
         {1.64f, 2.34f, 2.77f, 3.21f, 3.67f, 4.13f},
         {1.46f, 1.92f, 2.14f, 2.46f, 2.82f, 3.20f},
         {1.06f, 1.60f, 1.70f, 1.87f, 2.13f, 2.42f},
         {0.77f, 1.28f, 1.35f, 1.43f, 1.60f, 1.81f},
         {0.66f, 1.16f, 1.08f, 1.12f, 1.23f, 1.37f},
         {0.54f, 0.92f, 0.87f, 0.93f, 1.00f, 1.05f},
         {0.37f, 0.50f, 0.73f, 0.83f, 0.83f, 0.80f},
         {0.20f, 0.26f, 0.49f, 0.69f, 0.63f, 0.56f},
         {0.06f, 0.12f, 0.12f, 0.31f, 0.32f, 0.29f},
         {-0.04f, -0.02f, 0.00f, 0.04f, 0.01f, 0.00f},
         {-0.11f, -0.12f, -0.14f, -0.20f, -0.26f, -0.30f}}}},
    {-25, -0.02f, 1.38f, 0.35f, 0.00f,
        {{
         {0.77f, 1.32f, 1.85f, 2.29f, 2.66f, 3.00f},
         {0.70f, 1.37f, 1.68f, 1.95f, 2.23f, 2.51f},
         {0.80f, 1.18f, 1.41f, 1.61f, 1.82f, 2.04f},
         {0.73f, 1.01f, 1.13f, 1.27f, 1.44f, 1.61f},
         {0.54f, 0.82f, 0.90f, 0.99f, 1.12f, 1.25f},
         {0.38f, 0.67f, 0.72f, 0.77f, 0.86f, 0.97f},
         {0.33f, 0.63f, 0.58f, 0.61f, 0.68f, 0.76f},
         {0.27f, 0.50f, 0.47f, 0.51f, 0.56f, 0.60f},
         {0.19f, 0.26f, 0.39f, 0.46f, 0.47f, 0.46f},
         {0.10f, 0.13f, 0.26f, 0.39f, 0.36f, 0.32f},
         {0.03f, 0.06f, 0.06f, 0.17f, 0.18f, 0.16f},
         {-0.02f, -0.01f, 0.00f, 0.02f, 0.00f, 0.00f},
         {-0.06f, -0.06f, -0.07f, -0.11f, -0.14f, -0.17f}}}},
    {25, -0.81f, 0.01f, 0.00f, 1.08f,
        {{
         {-0.24f, -1.18f, -1.73f, -2.12f, -2.44f, -2.75f},
         {-0.20f, -0.90f, -1.29f, -1.65f, -1.97f, -2.28f},
         {-0.26f, -0.70f, -0.95f, -1.24f, -1.54f, -1.83f},
         {-0.27f, -0.60f, -0.74f, -0.93f, -1.17f, -1.42f},
         {-0.20f, -0.51f, -0.60f, -0.72f, -0.89f, -1.08f},
         {-0.13f, -0.41f, -0.49f, -0.57f, -0.69f, -0.84f},
         {-0.10f, -0.39f, -0.41f, -0.47f, -0.57f, -0.69f},
         {-0.10f, -0.29f, -0.33f, -0.41f, -0.51f, -0.61f},
         {-0.10f, -0.13f, -0.27f, -0.38f, -0.47f, -0.55f},
         {-0.05f, -0.05f, -0.16f, -0.33f, -0.40f, -0.46f},
         {-0.00f, -0.03f, -0.02f, -0.15f, -0.25f, -0.30f},
         {0.01f, 0.00f, 0.01f, -0.00f, -0.03f, -0.03f},
         {0.01f, 0.02f, 0.05f, 0.11f, 0.18f, 0.24f}}}},
    {50, -1.89f, 0.01f, 0.00f, 2.52f,
        {{
         {-0.54f, -3.00f, -4.35f, -5.25f, -6.02f, -6.76f},
         {-0.41f, -2.11f, -3.09f, -4.01f, -4.84f, -5.63f},
         {-0.55f, -1.51f, -2.18f, -2.96f, -3.75f, -4.53f},
         {-0.54f, -1.31f, -1.69f, -2.21f, -2.85f, -3.51f},
         {-0.42f, -1.15f, -1.40f, -1.71f, -2.16f, -2.68f},
         {-0.25f, -0.93f, -1.15f, -1.37f, -1.70f, -2.09f},
         {-0.19f, -0.88f, -0.95f, -1.13f, -1.41f, -1.73f},
         {-0.18f, -0.68f, -0.77f, -0.99f, -1.25f, -1.53f},
         {-0.18f, -0.29f, -0.65f, -0.92f, -1.15f, -1.38f},
         {-0.10f, -0.10f, -0.37f, -0.78f, -0.98f, -1.16f},
         {-0.01f, -0.05f, -0.04f, -0.35f, -0.62f, -0.76f},
         {0.02f, 0.01f, 0.02f, 0.00f, -0.05f, -0.07f},
         {0.01f, 0.04f, 0.12f, 0.29f, 0.46f, 0.63f}}}},
    {100, -6.04f, 0.06f, 0.00f, 7.53f,
        {{
         {-1.26f, -7.94f, -11.71f, -14.19f, -16.23f, -18.19f},
         {-0.82f, -5.81f, -8.41f, -10.84f, -13.10f, -15.26f},
         {-1.13f, -3.79f, -5.72f, -7.98f, -10.23f, -12.42f},
         {-1.15f, -3.20f, -4.37f, -5.96f, -7.87f, -9.86f},
         {-0.84f, -2.80f, -3.64f, -4.68f, -6.15f, -7.81f},
         {-0.46f, -2.42f, -3.15f, -3.90f, -5.05f, -6.44f},
         {-0.29f, -2.43f, -2.69f, -3.38f, -4.48f, -5.71f},
         {-0.24f, -1.84f, -2.24f, -3.18f, -4.27f, -5.37f},
         {-0.33f, -0.64f, -2.10f, -3.22f, -4.15f, -5.03f},
         {-0.24f, -0.12f, -1.21f, -2.88f, -3.61f, -4.29f},
         {-0.04f, -0.09f, -0.06f, -1.19f, -2.19f, -2.79f},
         {0.02f, 0.00f, 0.09f, 0.15f, 0.05f, -0.22f},
         {-0.06f, 0.08f, 0.44f, 1.18f, 1.91f, 2.50f}}}},
}};

// Law at a slider value: linear between the two measured values around it, zero at 0.
Law lawAt(int value)
{
    std::array<const Law *, Laws.size() + 1> rows{};
    static const Law zero{0, 0.f, 0.f, 0.f, 0.f, {}};
    size_t n = 0;
    for (const Law &law : Laws) {
        if (n == 3) rows[n++] = &zero;
        rows[n++] = &law;
    }
    value = std::clamp(value, rows.front()->value, rows.back()->value);
    size_t i = 1;
    while (i + 1 < rows.size() && rows[i]->value < value) ++i;
    const Law &low = *rows[i - 1], &high = *rows[i];
    const float t = float(value - low.value) / float(high.value - low.value);
    const auto mix = [t](float a, float b) { return a + t * (b - a); };
    Law result{value, mix(low.low, high.low), mix(low.high, high.high), mix(low.beta, high.beta),
               mix(low.kappa, high.kappa), {}};
    for (size_t k = 0; k < ToneKnots.size(); ++k)
        for (size_t j = 0; j < HazeKnots.size(); ++j)
            result.gain[k][j] = mix(low.gain[k][j], high.gain[k][j]);
    return result;
}

// Cell and position of a value on knots, clamped to their range.
template <size_t N>
std::pair<size_t, float> locate(const std::array<float, N> &knots, float v)
{
    v = std::clamp(v, knots.front(), knots.back());
    size_t i = 0;
    while (i + 2 < N && knots[i + 1] < v) ++i;
    return {i, (v - knots[i]) / (knots[i + 1] - knots[i])};
}

float gainAt(const Law &law, float tone, float haze)
{
    const auto [i, a] = locate(ToneKnots, tone);
    const auto [j, b] = locate(HazeKnots, haze);
    const float g = (1 - a) * ((1 - b) * law.gain[i][j] + b * law.gain[i][j + 1])
                  + a * ((1 - b) * law.gain[i + 1][j] + b * law.gain[i + 1][j + 1]);
    return std::clamp(g, law.low, law.high);
}

float srgbLuminance(const cv::Vec3f &s) { return 0.2126f * s[0] + 0.7152f * s[1] + 0.0722f * s[2]; }

// Airlight of a reduced linear sRGB image: level A = max(0.03, 99th percentile of
// the luminance; max RGB put saturated colours far above the brightest tones),
// colour = mean of the haziest pixels (brightest eroded dark channel, He et al.
// 2009) at luminance 1.
struct Airlight { float level; cv::Vec3f colour; };

Airlight airlightOf(const cv::Mat &srgb, const cv::Mat &dark)
{
    std::vector<float> maxima, darks;
    maxima.reserve(srgb.total());
    darks.reserve(srgb.total());
    for (int y = 0; y < srgb.rows; ++y) {
        const auto *row = srgb.ptr<cv::Vec3f>(y);
        const float *d = dark.ptr<float>(y);
        for (int x = 0; x < srgb.cols; ++x) {
            maxima.push_back(std::max(0.f, srgbLuminance(row[x])));
            darks.push_back(d[x]);
        }
    }
    Airlight result{0.03f, cv::Vec3f(1.f, 1.f, 1.f)};
    if (maxima.empty()) return result;
    auto at = maxima.begin() + std::ptrdiff_t((maxima.size() - 1) * 99 / 100);
    std::nth_element(maxima.begin(), at, maxima.end());
    result.level = std::max(0.03f, *at);
    at = darks.begin() + std::ptrdiff_t(double(darks.size() - 1) * (1.0 - HaziestShare));
    std::nth_element(darks.begin(), at, darks.end());
    const float threshold = *at;
    cv::Vec3d sum(0, 0, 0);
    for (int y = 0; y < srgb.rows; ++y) {
        const auto *row = srgb.ptr<cv::Vec3f>(y);
        const float *d = dark.ptr<float>(y);
        for (int x = 0; x < srgb.cols; ++x)
            if (d[x] >= threshold) sum += cv::Vec3d(row[x]);
    }
    const double luminance = 0.2126 * sum[0] + 0.7152 * sum[1] + 0.0722 * sum[2];
    if (luminance > 1e-9) result.colour = cv::Vec3f(sum / luminance);
    return result;
}

// Log ratios to green, taken from the airlight's (centre), d, gain
// kappa h d / (1 + (d / R0)^2), luminance kept: the haze's cast is removed and
// its faint colours expanded, strong colours barely.
cv::Vec3f saturate(const cv::Vec3f &s, float gain, const cv::Vec2f &centre)
{
    if (gain <= 0.f || s[0] <= 1e-6f || s[1] <= 1e-6f || s[2] <= 1e-6f) return s;
    const auto expand = [gain](float r, float c) {
        const float d = r - c;
        return r + gain * d / (1.f + (d / RatioScale) * (d / RatioScale));
    };
    const float red = std::exp2(expand(std::log2(s[0] / s[1]), centre[0]));
    const float blue = std::exp2(expand(std::log2(s[2] / s[1]), centre[1]));
    const float green = srgbLuminance(s) / (0.2126f * red + 0.7152f + 0.0722f * blue);
    return {green * red, green, green * blue};
}

} // namespace

void apply(cv::Mat &linearRgb, int amount, double pixelScale)
{
    if (amount == 0 || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    const Law law = lawAt(amount);
    const cv::Matx33f toSrgb(WorkingColor::toSrgb()), fromSrgb(WorkingColor::fromSrgb());
    // Haze measured in linear sRGB at 1/4 of the full resolution, as fitted, then enlarged.
    const int reduction = std::max(1, int(std::lround(FittedReduction * pixelScale)));
    const double scale = pixelScale / reduction;   // pixels of the reduced image per full-image pixel
    cv::Mat small;
    cv::resize(linearRgb, small, cv::Size(std::max(1, linearRgb.cols / reduction), std::max(1, linearRgb.rows / reduction)),
               0, 0, cv::INTER_AREA);
    cv::Mat dark(small.size(), CV_32F);
    for (int y = 0; y < small.rows; ++y) {
        auto *row = small.ptr<cv::Vec3f>(y);
        float *d = dark.ptr<float>(y);
        for (int x = 0; x < small.cols; ++x) {
            row[x] = toSrgb * row[x];
            d[x] = std::max(0.f, std::min({row[x][0], row[x][1], row[x][2]}));
        }
    }
    const int window = int(DarkWindow * scale) + 1;
    cv::erode(dark, dark, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(window, window)));
    const Airlight airlight = airlightOf(small, dark);
    const cv::Vec2f centre(std::log2(std::max(airlight.colour[0], 1e-6f) / std::max(airlight.colour[1], 1e-6f)),
                           std::log2(std::max(airlight.colour[2], 1e-6f) / std::max(airlight.colour[1], 1e-6f)));
    cv::GaussianBlur(dark, dark, cv::Size(0, 0), HazeSigma * scale, HazeSigma * scale, cv::BORDER_REFLECT);
    // Tone change of the law, smoothed into a base: Lightroom's change is a smooth
    // local map, not a per-pixel curve. On the reduced image: log2 luminance before
    // and after the law, both blurred at BaseSigma.
    cv::Mat before(small.size(), CV_32F), after(small.size(), CV_32F);
    for (int y = 0; y < small.rows; ++y) {
        const auto *row = small.ptr<cv::Vec3f>(y);
        const float *d = dark.ptr<float>(y);
        float *b = before.ptr<float>(y), *a = after.ptr<float>(y);
        for (int x = 0; x < small.cols; ++x) {
            const float luminance = std::max(0.f, srgbLuminance(row[x]));
            const float h = std::clamp(d[x] / airlight.level, 0.f, 1.f);
            const float target = luminance > 1e-6f
                ? luminance * std::exp2(gainAt(law, std::log2(luminance / airlight.level), h)) : luminance;
            b[x] = std::log2(luminance + LogFloor);
            a[x] = std::log2(target + LogFloor);
        }
    }
    const double baseSigma = BaseSigma * scale, meanSigma = DetailMeanSigma * scale;
    cv::Mat tone;
    cv::GaussianBlur(before, tone, cv::Size(0, 0), meanSigma, meanSigma, cv::BORDER_REFLECT);
    tone -= std::log2(airlight.level + LogFloor);
    cv::GaussianBlur(before, before, cv::Size(0, 0), baseSigma, baseSigma, cv::BORDER_REFLECT);
    cv::GaussianBlur(after, after, cv::Size(0, 0), baseSigma, baseSigma, cv::BORDER_REFLECT);
    cv::Mat shift = after - before;
    cv::Mat haze, base, toneShift, localTone;
    cv::resize(dark, haze, linearRgb.size(), 0, 0, cv::INTER_LINEAR);
    cv::resize(tone, localTone, linearRgb.size(), 0, 0, cv::INTER_LINEAR);
    cv::resize(before, base, linearRgb.size(), 0, 0, cv::INTER_LINEAR);
    cv::resize(shift, toneShift, linearRgb.size(), 0, 0, cv::INTER_LINEAR);
    const DetailLaw detail = detailLawAt(amount);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            const float *hr = haze.ptr<float>(y), *br = base.ptr<float>(y), *tr = toneShift.ptr<float>(y);
            const float *ur = localTone.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const float luminance = WorkingColor::luminance(row[x]);
                if (luminance <= 1e-6f) continue;
                const float h = std::clamp(hr[x] / airlight.level, 0.f, 1.f);
                // log2 Y' = base + shift + G (log2 Y - base): the detail gains G at every scale.
                const float u = std::clamp(ur[x], -10.f, 0.6f);
                const float gain = std::exp2(std::clamp(detail.c0 + detail.ch * h + detail.cu * u, MinLog2Gain, MaxLog2Gain));
                const float level = std::log2(luminance + LogFloor);
                // Removing haze, Lightroom keeps its highlights: a pixel brighter than its base
                // gains less as it nears the airlight (from 0.5 EV below it), else the brightest
                // tones rose 0.5 EV (6V1A9539) and turned white (IMG_7953).
                const float deviation = level - br[x];
                const float own = std::log2(luminance / airlight.level);
                const float detailGain = amount > 0 && deviation > 0.f
                    ? 1.f + (gain - 1.f) * std::clamp(-own / HighlightRange, 0.f, 1.f) : gain;
                const float target = std::max(0.f, std::exp2(br[x] + tr[x] + detailGain * deviation) - LogFloor);
                const cv::Vec3f s = toSrgb * row[x];
                cv::Vec3f out = s * (target / luminance);
                if (law.beta > 0.f) {
                    // Part of the change is light of the airlight's colour.
                    cv::Vec3f z = s + airlight.colour * (target - luminance);
                    for (int c = 0; c < 3; ++c) z[c] = std::max(0.f, z[c]);
                    const float lz = srgbLuminance(z);
                    if (lz > 1e-9f) out = (1.f - law.beta) * out + law.beta * z * (target / lz);
                }
                row[x] = fromSrgb * saturate(out, law.kappa * h, centre);
            }
        }
    });
}

} // namespace Dehaze
