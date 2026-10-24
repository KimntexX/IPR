#include "LocalContrast.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace LocalContrast {
namespace {

constexpr int BandCount = 7;   // fine (x - G1), then G1 - G2, ... G32 - G64
constexpr float LogFloor = 1e-3f;   // soft floor of the log: deep shadows are not stretched without bound

using Gains = std::array<float, BandCount>;
struct Row { int value; Gains gains; };

// Lightroom's band gains, median of four scenes (6V1A0043, 6V1A0327, 6V1A9947,
// _MG_7580), etude-reponse-effets-v001-20261024T151007Z.
constexpr std::array<Row, 6> TextureTable{{
    {-100, {0.495f, 0.572f, 0.615f, 0.703f, 0.831f, 0.937f, 0.982f}},
    {-50, {0.695f, 0.742f, 0.768f, 0.820f, 0.898f, 0.962f, 0.989f}},
    {-25, {0.832f, 0.857f, 0.872f, 0.901f, 0.944f, 0.979f, 0.994f}},
    {25, {1.205f, 1.175f, 1.158f, 1.123f, 1.070f, 1.026f, 1.008f}},
    {50, {1.358f, 1.305f, 1.276f, 1.215f, 1.123f, 1.046f, 1.014f}},
    {100, {1.559f, 1.476f, 1.432f, 1.338f, 1.193f, 1.073f, 1.022f}},
}};
constexpr std::array<Row, 6> ClarityTable{{
    {-100, {0.919f, 0.879f, 0.864f, 0.850f, 0.845f, 0.856f, 0.882f}},
    {-50, {0.954f, 0.929f, 0.925f, 0.921f, 0.918f, 0.923f, 0.934f}},
    {-25, {0.976f, 0.963f, 0.961f, 0.960f, 0.958f, 0.960f, 0.966f}},
    {25, {1.247f, 1.236f, 1.215f, 1.177f, 1.133f, 1.095f, 1.069f}},
    {50, {1.476f, 1.455f, 1.414f, 1.342f, 1.257f, 1.184f, 1.133f}},
    {100, {1.894f, 1.846f, 1.774f, 1.643f, 1.481f, 1.345f, 1.248f}},
}};

// Gains at a slider value: linear between the two measured values around it,
// the identity (gain 1) standing for 0.
Gains gainsAt(const std::array<Row, 6> &table, int value)
{
    std::array<Row, 7> rows{};
    size_t n = 0;
    for (const Row &row : table) {
        if (n == 3) rows[n++] = Row{0, {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f}};
        rows[n++] = row;
    }
    value = std::clamp(value, rows.front().value, rows.back().value);
    size_t i = 1;
    while (i + 1 < rows.size() && rows[i].value < value) ++i;
    const Row &low = rows[i - 1], &high = rows[i];
    const float t = float(value - low.value) / float(high.value - low.value);
    Gains result;
    for (int b = 0; b < BandCount; ++b)
        result[size_t(b)] = low.gains[size_t(b)] + t * (high.gains[size_t(b)] - low.gains[size_t(b)]);
    return result;
}

// Gaussian blur of sigma 2 * factor, computed at 1/factor resolution (area
// reduction, then sigma 2, then bilinear enlargement) for the large scales.
cv::Mat blurAtScale(const cv::Mat &plane, int factor)
{
    cv::Mat result;
    if (factor <= 1) {
        cv::GaussianBlur(plane, result, cv::Size(0, 0), 2.0 * factor, 2.0 * factor, cv::BORDER_REFLECT);
        return result;
    }
    cv::Mat small;
    cv::resize(plane, small, cv::Size(std::max(1, plane.cols / factor), std::max(1, plane.rows / factor)), 0, 0,
               cv::INTER_AREA);
    cv::GaussianBlur(small, small, cv::Size(0, 0), 2.0, 2.0, cv::BORDER_REFLECT);
    cv::resize(small, result, plane.size(), 0, 0, cv::INTER_LINEAR);
    return result;
}

} // namespace

void apply(cv::Mat &linearRgb, int texture, int clarity)
{
    if ((texture == 0 && clarity == 0) || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    const Gains textureGains = gainsAt(TextureTable, texture), clarityGains = gainsAt(ClarityTable, clarity);
    Gains gains;
    for (int b = 0; b < BandCount; ++b) gains[size_t(b)] = textureGains[size_t(b)] * clarityGains[size_t(b)];

    cv::Mat luminance(linearRgb.size(), CV_32F), logLuminance(linearRgb.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const auto *p = linearRgb.ptr<cv::Vec3f>(y);
            float *l = luminance.ptr<float>(y), *g = logLuminance.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                l[x] = WorkingColor::nonNegativeLuminance(p[x]);
                g[x] = std::log2(l[x] + LogFloor);
            }
        }
    });
    // Bands of the log: G0 is the image, G1 sigma 1, then sigma 2, 4, ... 64.
    cv::Mat delta = cv::Mat::zeros(linearRgb.size(), CV_32F);
    cv::Mat previous = logLuminance, current;
    cv::GaussianBlur(logLuminance, current, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    for (int b = 0; b < BandCount; ++b) {
        if (b > 0) current = blurAtScale(logLuminance, 1 << (b - 1));   // sigma 2, 4, ... 64
        if (gains[size_t(b)] != 1.f) delta += (gains[size_t(b)] - 1.f) * (previous - current);
        previous = current;
    }
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *p = linearRgb.ptr<cv::Vec3f>(y);
            const float *l = luminance.ptr<float>(y), *d = delta.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const float target = (l[x] + LogFloor) * std::exp2(d[x]) - LogFloor;
                WorkingColor::scaleToLuminance(p[x], l[x], target);
            }
        }
    });
}

} // namespace LocalContrast
