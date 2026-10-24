#include "Dehaze.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace Dehaze {
namespace {

constexpr int Reduction = 4;            // the law was fitted at 1/4 resolution
constexpr double HazeSigma = 128.0 / Reduction;
constexpr int DarkWindow = 15 / Reduction + 1;

constexpr std::array<float, 13> ToneKnots{-10.f, -8.f, -6.f, -5.f, -4.f, -3.f, -2.5f, -2.f, -1.5f, -1.f, -0.5f, 0.f, 0.6f};
constexpr std::array<float, 6> HazeKnots{0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f};
using Table = std::array<std::array<float, HazeKnots.size()>, ToneKnots.size()>;   // gain in EV
struct Law { int value; float low, high; Table gain; };   // gain clamped to [low, high]

// Pooled over 6V1A0043, 6V1A0327, IMG_4345 and _MG_7580, etude-loi-voile-v003-20261024T194506Z
// (leave-one-scene-out unexplained share of the change in EV 0.14 to 0.21 below 0, 0.44 to 0.59 above).
const std::array<Law, 6> Laws{{
    {-100, -0.28f, 5.37f,
        {{
         {3.14f, 6.43f, 9.33f, 11.89f, 14.27f, 16.59f},
         {3.09f, 5.81f, 8.00f, 10.09f, 12.16f, 14.22f},
         {3.31f, 4.87f, 6.56f, 8.32f, 10.11f, 11.91f},
         {2.60f, 3.89f, 5.22f, 6.68f, 8.20f, 9.73f},
         {1.72f, 3.04f, 4.07f, 5.26f, 6.49f, 7.73f},
         {1.12f, 2.59f, 3.27f, 4.11f, 5.02f, 5.95f},
         {0.94f, 2.20f, 2.68f, 3.18f, 3.78f, 4.42f},
         {0.86f, 1.69f, 2.02f, 2.36f, 2.75f, 3.16f},
         {0.53f, 1.02f, 1.35f, 1.64f, 1.89f, 2.14f},
         {0.10f, 0.38f, 0.83f, 1.02f, 1.16f, 1.30f},
         {-0.18f, -0.08f, 0.22f, 0.46f, 0.52f, 0.60f},
         {-0.30f, -0.24f, -0.14f, -0.03f, -0.02f, -0.01f},
         {-0.28f, -0.32f, -0.42f, -0.51f, -0.57f, -0.63f}}}},
    {-50, -0.11f, 2.67f,
        {{
         {1.36f, 2.90f, 4.44f, 5.86f, 7.19f, 8.48f},
         {1.40f, 2.90f, 4.05f, 5.15f, 6.26f, 7.37f},
         {1.81f, 2.67f, 3.52f, 4.41f, 5.34f, 6.29f},
         {1.45f, 2.20f, 2.92f, 3.69f, 4.48f, 5.27f},
         {0.94f, 1.73f, 2.35f, 3.01f, 3.67f, 4.33f},
         {0.58f, 1.50f, 1.94f, 2.44f, 2.95f, 3.47f},
         {0.50f, 1.32f, 1.63f, 1.95f, 2.31f, 2.70f},
         {0.46f, 1.01f, 1.25f, 1.50f, 1.75f, 2.02f},
         {0.27f, 0.60f, 0.85f, 1.08f, 1.25f, 1.42f},
         {0.04f, 0.22f, 0.53f, 0.69f, 0.78f, 0.88f},
         {-0.08f, -0.03f, 0.15f, 0.30f, 0.33f, 0.41f},
         {-0.10f, -0.11f, -0.09f, -0.04f, -0.04f, -0.02f},
         {-0.07f, -0.16f, -0.28f, -0.36f, -0.41f, -0.45f}}}},
    {-25, -0.05f, 1.36f,
        {{
         {0.68f, 1.43f, 2.20f, 2.92f, 3.60f, 4.27f},
         {0.64f, 1.42f, 2.02f, 2.59f, 3.16f, 3.74f},
         {0.92f, 1.36f, 1.79f, 2.25f, 2.73f, 3.21f},
         {0.75f, 1.15f, 1.52f, 1.91f, 2.31f, 2.72f},
         {0.50f, 0.91f, 1.24f, 1.58f, 1.92f, 2.26f},
         {0.29f, 0.79f, 1.03f, 1.30f, 1.57f, 1.84f},
         {0.26f, 0.72f, 0.88f, 1.05f, 1.25f, 1.45f},
         {0.24f, 0.55f, 0.68f, 0.82f, 0.96f, 1.10f},
         {0.14f, 0.32f, 0.47f, 0.60f, 0.70f, 0.79f},
         {0.02f, 0.12f, 0.30f, 0.39f, 0.44f, 0.49f},
         {-0.04f, -0.01f, 0.08f, 0.17f, 0.19f, 0.23f},
         {-0.04f, -0.06f, -0.05f, -0.03f, -0.03f, -0.01f},
         {-0.02f, -0.09f, -0.16f, -0.21f, -0.24f, -0.26f}}}},
    {25, -1.37f, 0.02f,
        {{
         {-0.51f, -1.67f, -2.79f, -3.84f, -4.82f, -5.79f},
         {-0.22f, -1.43f, -2.40f, -3.27f, -4.12f, -4.96f},
         {-0.42f, -1.23f, -1.96f, -2.68f, -3.42f, -4.16f},
         {-0.23f, -0.88f, -1.48f, -2.11f, -2.76f, -3.41f},
         {-0.17f, -0.63f, -1.08f, -1.62f, -2.17f, -2.73f},
         {-0.10f, -0.56f, -0.85f, -1.24f, -1.68f, -2.12f},
         {-0.05f, -0.51f, -0.72f, -0.97f, -1.28f, -1.62f},
         {-0.04f, -0.34f, -0.54f, -0.74f, -0.96f, -1.21f},
         {-0.03f, -0.17f, -0.33f, -0.52f, -0.70f, -0.89f},
         {0.01f, -0.05f, -0.18f, -0.32f, -0.47f, -0.61f},
         {0.02f, 0.01f, -0.04f, -0.15f, -0.26f, -0.33f},
         {0.01f, 0.03f, 0.04f, 0.03f, 0.01f, -0.03f},
         {-0.02f, 0.05f, 0.13f, 0.20f, 0.26f, 0.30f}}}},
    {50, -2.76f, 0.04f,
        {{
         {-1.11f, -3.32f, -5.51f, -7.61f, -9.63f, -11.61f},
         {-0.38f, -2.81f, -4.80f, -6.60f, -8.35f, -10.10f},
         {-0.79f, -2.49f, -4.03f, -5.55f, -7.08f, -8.63f},
         {-0.56f, -1.94f, -3.20f, -4.52f, -5.87f, -7.23f},
         {-0.36f, -1.46f, -2.45f, -3.60f, -4.78f, -5.95f},
         {-0.21f, -1.37f, -2.04f, -2.90f, -3.84f, -4.79f},
         {-0.06f, -1.27f, -1.80f, -2.36f, -3.05f, -3.78f},
         {-0.03f, -0.82f, -1.36f, -1.84f, -2.37f, -2.95f},
         {-0.04f, -0.37f, -0.81f, -1.29f, -1.76f, -2.24f},
         {0.03f, -0.09f, -0.41f, -0.79f, -1.20f, -1.58f},
         {0.05f, 0.03f, -0.09f, -0.36f, -0.67f, -0.87f},
         {0.01f, 0.06f, 0.11f, 0.10f, 0.04f, -0.06f},
         {-0.07f, 0.11f, 0.31f, 0.52f, 0.69f, 0.80f}}}},
    {100, -9.46f, 0.08f,
        {{
         {-1.85f, -6.51f, -10.98f, -15.25f, -19.40f, -23.53f},
         {-0.95f, -5.77f, -9.82f, -13.74f, -17.73f, -21.77f},
         {-1.43f, -5.03f, -8.53f, -12.25f, -16.12f, -20.03f},
         {-1.25f, -4.15f, -7.37f, -11.01f, -14.67f, -18.28f},
         {-0.75f, -3.99f, -6.98f, -10.29f, -13.43f, -16.45f},
         {-0.40f, -6.39f, -8.03f, -10.07f, -12.27f, -14.52f},
         {0.70f, -7.35f, -8.78f, -9.65f, -11.01f, -12.62f},
         {1.15f, -3.23f, -6.56f, -8.12f, -9.41f, -10.78f},
         {0.33f, -0.91f, -3.34f, -5.55f, -7.27f, -8.82f},
         {0.10f, -0.09f, -1.27f, -3.03f, -4.84f, -6.42f},
         {0.03f, 0.11f, -0.17f, -1.23f, -2.48f, -3.48f},
         {-0.14f, 0.16f, 0.44f, 0.52f, 0.39f, -0.10f},
         {-0.41f, 0.30f, 1.18f, 2.13f, 2.93f, 3.50f}}}},
}};

// Law at a slider value: linear between the two measured values around it, zero at 0.
Law lawAt(int value)
{
    std::array<const Law *, Laws.size() + 1> rows{};
    static const Law zero{0, 0.f, 0.f, {}};
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
    Law result{value, low.low + t * (high.low - low.low), low.high + t * (high.high - low.high), {}};
    for (size_t k = 0; k < ToneKnots.size(); ++k)
        for (size_t j = 0; j < HazeKnots.size(); ++j)
            result.gain[k][j] = low.gain[k][j] + t * (high.gain[k][j] - low.gain[k][j]);
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

// 99th percentile of max(R, G, B), on a sample of about 16k pixels.
float airlightOf(const cv::Mat &rgb)
{
    const int step = std::max(1, int(std::sqrt(double(rgb.total()) / 16384.0)));
    std::vector<float> samples;
    for (int y = 0; y < rgb.rows; y += step) {
        const auto *row = rgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < rgb.cols; x += step)
            samples.push_back(std::max({row[x][0], row[x][1], row[x][2], 0.f}));
    }
    if (samples.empty()) return 0.1f;
    const auto at = samples.begin() + std::ptrdiff_t((samples.size() - 1) * 99 / 100);
    std::nth_element(samples.begin(), at, samples.end());
    return std::max(0.10f, *at);
}

} // namespace

void apply(cv::Mat &linearRgb, int amount)
{
    if (amount == 0 || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    const Law law = lawAt(amount);
    const float airlight = airlightOf(linearRgb);
    // Smoothed haze h at 1/4 resolution, as fitted, then enlarged.
    cv::Mat small;
    cv::resize(linearRgb, small, cv::Size(std::max(1, linearRgb.cols / Reduction), std::max(1, linearRgb.rows / Reduction)),
               0, 0, cv::INTER_AREA);
    cv::Mat dark(small.size(), CV_32F);
    for (int y = 0; y < small.rows; ++y) {
        const auto *row = small.ptr<cv::Vec3f>(y);
        float *d = dark.ptr<float>(y);
        for (int x = 0; x < small.cols; ++x) d[x] = std::max(0.f, std::min({row[x][0], row[x][1], row[x][2]}));
    }
    cv::erode(dark, dark, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(DarkWindow, DarkWindow)));
    cv::GaussianBlur(dark, dark, cv::Size(0, 0), HazeSigma, HazeSigma, cv::BORDER_REFLECT);
    cv::Mat haze;
    cv::resize(dark, haze, linearRgb.size(), 0, 0, cv::INTER_LINEAR);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            const float *hr = haze.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const float h = std::clamp(hr[x] / airlight, 0.f, 1.f);
                for (int c = 0; c < 3; ++c) {
                    const float v = row[x][c];
                    if (v <= 0.f) continue;
                    row[x][c] = v * std::exp2(gainAt(law, std::log2(v / airlight), h));
                }
            }
        }
    });
}

} // namespace Dehaze
