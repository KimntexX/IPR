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
// pairs with equal weight per scene (6V1A0043, 6V1A0327, IMG_4345, _MG_7580),
// etude-detail-voile-v001-20261025T104351Z: error of the scene median log2 G,
// leave-one-scene-out, 0.09 to 0.39 (0.13 to 1.27 for a constant gain).
struct DetailLaw { int value; float c0, ch, cu; };
constexpr std::array<DetailLaw, 7> DetailLaws{{
    {-100, -3.883f, 0.099f, -0.368f}, {-50, -1.335f, -0.115f, -0.144f}, {-25, -0.569f, -0.093f, -0.063f},
    {0, 0.f, 0.f, 0.f},
    {25, 0.339f, 0.085f, 0.039f}, {50, 0.707f, 0.208f, 0.083f}, {100, 1.228f, 1.247f, 0.147f},
}};
constexpr double DetailMeanSigma = 64.0;     // full-image pixels: the local mean of u
constexpr float MinLog2Gain = -4.f, MaxLog2Gain = 2.5f;

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

// Pooled over 6V1A0043, 6V1A0327, IMG_4345 and _MG_7580, etude-loi-voile-v007-20261024T222843Z
// (leave-one-scene-out unexplained share of the luminance change in EV 0.15 to 0.34, of the
// colour change 0.43 to 0.47 below 0 and 0.74 to 0.84 above).
const std::array<Law, 6> Laws{{
    {-100, -0.07f, 4.80f, 0.95f, 0.00f,
        {{
         {2.00f, 6.76f, 10.66f, 14.00f, 17.07f, 20.06f},
         {3.09f, 6.12f, 8.94f, 11.68f, 14.35f, 17.01f},
         {3.09f, 5.13f, 7.25f, 9.46f, 11.74f, 14.04f},
         {2.62f, 4.11f, 5.65f, 7.43f, 9.33f, 11.27f},
         {1.78f, 3.10f, 4.25f, 5.70f, 7.23f, 8.78f},
         {1.14f, 2.42f, 3.31f, 4.35f, 5.47f, 6.62f},
         {1.15f, 2.21f, 2.71f, 3.31f, 4.04f, 4.82f},
         {1.00f, 1.75f, 2.09f, 2.46f, 2.90f, 3.38f},
         {0.44f, 0.98f, 1.44f, 1.72f, 1.98f, 2.26f},
         {0.10f, 0.45f, 0.83f, 1.03f, 1.21f, 1.38f},
         {-0.07f, 0.07f, 0.26f, 0.46f, 0.56f, 0.66f},
         {-0.16f, -0.20f, -0.16f, -0.06f, -0.03f, 0.00f},
         {-0.23f, -0.39f, -0.52f, -0.59f, -0.63f, -0.67f}}}},
    {-50, -0.04f, 2.35f, 0.55f, 0.00f,
        {{
         {0.81f, 2.94f, 4.89f, 6.66f, 8.30f, 9.91f},
         {1.29f, 2.94f, 4.38f, 5.77f, 7.15f, 8.53f},
         {1.65f, 2.73f, 3.79f, 4.88f, 6.03f, 7.19f},
         {1.43f, 2.30f, 3.12f, 4.01f, 4.97f, 5.94f},
         {0.93f, 1.77f, 2.45f, 3.22f, 4.02f, 4.81f},
         {0.56f, 1.39f, 1.96f, 2.57f, 3.18f, 3.80f},
         {0.60f, 1.30f, 1.65f, 2.03f, 2.46f, 2.91f},
         {0.50f, 1.04f, 1.30f, 1.56f, 1.85f, 2.15f},
         {0.17f, 0.56f, 0.91f, 1.13f, 1.31f, 1.50f},
         {0.01f, 0.25f, 0.54f, 0.70f, 0.82f, 0.94f},
         {-0.05f, 0.04f, 0.17f, 0.30f, 0.36f, 0.45f},
         {-0.08f, -0.11f, -0.10f, -0.06f, -0.04f, -0.01f},
         {-0.10f, -0.22f, -0.33f, -0.40f, -0.45f, -0.47f}}}},
    {-25, -0.02f, 1.16f, 0.35f, 0.00f,
        {{
         {0.40f, 1.32f, 2.21f, 3.04f, 3.83f, 4.60f},
         {0.58f, 1.36f, 2.05f, 2.70f, 3.36f, 4.01f},
         {0.82f, 1.34f, 1.84f, 2.35f, 2.89f, 3.44f},
         {0.73f, 1.19f, 1.57f, 1.99f, 2.44f, 2.90f},
         {0.49f, 0.93f, 1.27f, 1.65f, 2.03f, 2.41f},
         {0.27f, 0.72f, 1.04f, 1.35f, 1.65f, 1.96f},
         {0.31f, 0.70f, 0.89f, 1.09f, 1.32f, 1.54f},
         {0.25f, 0.57f, 0.71f, 0.86f, 1.01f, 1.17f},
         {0.07f, 0.29f, 0.50f, 0.63f, 0.73f, 0.83f},
         {-0.01f, 0.13f, 0.30f, 0.39f, 0.46f, 0.53f},
         {-0.03f, 0.02f, 0.09f, 0.16f, 0.20f, 0.25f},
         {-0.04f, -0.06f, -0.06f, -0.03f, -0.03f, -0.00f},
         {-0.04f, -0.12f, -0.18f, -0.23f, -0.25f, -0.27f}}}},
    {25, -0.71f, 0.01f, 0.00f, 1.34f,
        {{
         {-0.18f, -1.28f, -2.25f, -3.13f, -3.97f, -4.80f},
         {-0.17f, -1.05f, -1.88f, -2.66f, -3.40f, -4.14f},
         {-0.25f, -0.91f, -1.55f, -2.19f, -2.84f, -3.49f},
         {-0.25f, -0.76f, -1.24f, -1.77f, -2.32f, -2.88f},
         {-0.18f, -0.60f, -0.96f, -1.40f, -1.86f, -2.33f},
         {-0.08f, -0.47f, -0.77f, -1.11f, -1.47f, -1.84f},
         {-0.10f, -0.42f, -0.64f, -0.88f, -1.15f, -1.43f},
         {-0.09f, -0.35f, -0.51f, -0.68f, -0.88f, -1.09f},
         {-0.00f, -0.17f, -0.34f, -0.50f, -0.65f, -0.81f},
         {0.03f, -0.06f, -0.19f, -0.32f, -0.44f, -0.56f},
         {0.03f, -0.00f, -0.06f, -0.15f, -0.24f, -0.31f},
         {0.01f, 0.03f, 0.03f, 0.02f, -0.01f, -0.03f},
         {-0.01f, 0.06f, 0.12f, 0.17f, 0.21f, 0.25f}}}},
    {50, -1.60f, 0.02f, 0.00f, 3.36f,
        {{
         {-0.38f, -3.10f, -5.42f, -7.51f, -9.51f, -11.47f},
         {-0.34f, -2.46f, -4.46f, -6.32f, -8.11f, -9.89f},
         {-0.51f, -2.03f, -3.59f, -5.17f, -6.75f, -8.34f},
         {-0.52f, -1.68f, -2.84f, -4.13f, -5.50f, -6.88f},
         {-0.37f, -1.34f, -2.19f, -3.26f, -4.40f, -5.56f},
         {-0.17f, -1.09f, -1.78f, -2.60f, -3.49f, -4.40f},
         {-0.19f, -0.97f, -1.52f, -2.09f, -2.74f, -3.42f},
         {-0.16f, -0.83f, -1.23f, -1.64f, -2.12f, -2.62f},
         {0.03f, -0.39f, -0.83f, -1.20f, -1.58f, -1.96f},
         {0.08f, -0.12f, -0.43f, -0.77f, -1.08f, -1.36f},
         {0.06f, -0.00f, -0.13f, -0.35f, -0.59f, -0.75f},
         {0.02f, 0.06f, 0.08f, 0.05f, -0.02f, -0.07f},
         {-0.03f, 0.12f, 0.28f, 0.42f, 0.54f, 0.63f}}}},
    {100, -4.25f, 0.04f, 0.00f, 10.48f,
        {{
         {-0.86f, -6.92f, -12.27f, -17.16f, -21.79f, -26.35f},
         {-0.72f, -5.80f, -10.39f, -14.70f, -18.92f, -23.11f},
         {-0.97f, -4.81f, -8.54f, -12.30f, -16.11f, -19.96f},
         {-1.09f, -3.94f, -6.83f, -10.09f, -13.52f, -17.00f},
         {-0.79f, -3.17f, -5.42f, -8.26f, -11.27f, -14.31f},
         {-0.34f, -2.77f, -4.68f, -6.95f, -9.41f, -11.94f},
         {-0.33f, -2.61f, -4.24f, -5.97f, -7.88f, -9.87f},
         {-0.20f, -2.34f, -3.66f, -5.04f, -6.53f, -8.06f},
         {0.28f, -1.11f, -2.71f, -4.02f, -5.20f, -6.36f},
         {0.35f, -0.32f, -1.48f, -2.76f, -3.71f, -4.57f},
         {0.21f, -0.00f, -0.42f, -1.19f, -1.98f, -2.55f},
         {-0.00f, 0.18f, 0.33f, 0.30f, 0.12f, -0.20f},
         {-0.24f, 0.40f, 1.07f, 1.66f, 2.07f, 2.31f}}}},
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

// Airlight of a reduced linear sRGB image: level A = max(0.10, 99th percentile of
// max(R, G, B)), colour = mean of the haziest pixels (brightest eroded dark
// channel, He et al. 2009) at luminance 1.
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
            maxima.push_back(std::max({row[x][0], row[x][1], row[x][2], 0.f}));
            darks.push_back(d[x]);
        }
    }
    Airlight result{0.10f, cv::Vec3f(1.f, 1.f, 1.f)};
    if (maxima.empty()) return result;
    auto at = maxima.begin() + std::ptrdiff_t((maxima.size() - 1) * 99 / 100);
    std::nth_element(maxima.begin(), at, maxima.end());
    result.level = std::max(0.10f, *at);
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

float srgbLuminance(const cv::Vec3f &s) { return 0.2126f * s[0] + 0.7152f * s[1] + 0.0722f * s[2]; }

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
                const float target = std::max(0.f, std::exp2(br[x] + tr[x] + gain * (level - br[x])) - LogFloor);
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
