#include "LocalContrast.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace LocalContrast {
namespace {

constexpr int BandCount = 9;   // fine (x - G1), then G1 - G2, ... G128 - G256
constexpr float LogFloor = 1e-3f;   // soft floor of the log: deep shadows are not stretched without bound

using Bands = std::array<float, BandCount>;
struct Row { int value; Bands detailGain; };   // k: extra gain of small variations, per band

// Lightroom's response, fitted on its neutral -> state pairs of four scenes (6V1A0043,
// 6V1A0327, 6V1A9947, _MG_7580), etude-amplitude-effets-v001-20261024T165011Z: each band d (log2
// units) becomes d + k d / (1 + (|d| / A)^2). Small variations are amplified by
// 1 + k, variations well above A (edges) are left as they are.
constexpr std::array<Row, 6> TextureTable{{
    {-100, {-0.550f, -0.473f, -0.469f, -0.461f, -0.303f, -0.107f, -0.022f, -0.004f, -0.001f}},
    {-50, {-0.333f, -0.285f, -0.283f, -0.279f, -0.183f, -0.065f, -0.013f, -0.002f, -0.000f}},
    {-25, {-0.184f, -0.158f, -0.157f, -0.154f, -0.101f, -0.036f, -0.007f, -0.001f, -0.000f}},
    {25, {0.226f, 0.199f, 0.193f, 0.191f, 0.126f, 0.045f, 0.009f, 0.002f, 0.000f}},
    {50, {0.397f, 0.348f, 0.338f, 0.335f, 0.221f, 0.079f, 0.017f, 0.003f, 0.000f}},
    {100, {0.624f, 0.547f, 0.531f, 0.527f, 0.349f, 0.124f, 0.026f, 0.005f, 0.001f}},
}};
constexpr std::array<Row, 6> ClarityTable{{
    {-100, {-0.100f, -0.109f, -0.119f, -0.138f, -0.151f, -0.146f, -0.117f, -0.063f, -0.018f}},
    {-50, {-0.057f, -0.062f, -0.067f, -0.074f, -0.080f, -0.078f, -0.064f, -0.035f, -0.010f}},
    {-25, {-0.030f, -0.032f, -0.035f, -0.038f, -0.041f, -0.040f, -0.033f, -0.018f, -0.005f}},
    {25, {0.253f, 0.231f, 0.231f, 0.233f, 0.234f, 0.214f, 0.174f, 0.152f, 0.131f}},
    {50, {0.488f, 0.445f, 0.450f, 0.453f, 0.455f, 0.415f, 0.337f, 0.295f, 0.256f}},
    {100, {0.917f, 0.829f, 0.842f, 0.855f, 0.863f, 0.789f, 0.634f, 0.557f, 0.484f}},
}};
// A (log2 units) per band, by sign of the slider: Lightroom's negative clarity does
// not depend on the amplitude (A at the top of the fitted range).
constexpr Bands TextureReduce{0.90f, 0.90f, 0.68f, 0.52f, 0.60f, 0.90f, 1.77f, 4.00f, 4.00f};
constexpr Bands TextureEnhance{0.90f, 0.78f, 0.68f, 0.52f, 0.60f, 0.90f, 1.77f, 4.00f, 4.00f};
constexpr Bands ClarityReduce{1.03f, 3.05f, 4.00f, 4.00f, 4.00f, 4.00f, 4.00f, 4.00f, 4.00f};
constexpr Bands ClarityEnhance{1.77f, 1.35f, 0.78f, 0.60f, 0.52f, 0.52f, 0.52f, 0.52f, 0.52f};

// k at a slider value: linear between the two measured values around it, 0 at 0.
Bands detailGainAt(const std::array<Row, 6> &table, int value)
{
    std::array<Row, 7> rows{};
    size_t n = 0;
    for (const Row &row : table) {
        if (n == 3) rows[n++] = Row{};
        rows[n++] = row;
    }
    value = std::clamp(value, rows.front().value, rows.back().value);
    size_t i = 1;
    while (i + 1 < rows.size() && rows[i].value < value) ++i;
    const Row &low = rows[i - 1], &high = rows[i];
    const float t = float(value - low.value) / float(high.value - low.value);
    Bands result;
    for (int b = 0; b < BandCount; ++b)
        result[size_t(b)] = low.detailGain[size_t(b)] + t * (high.detailGain[size_t(b)] - low.detailGain[size_t(b)]);
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
    const Bands textureGain = detailGainAt(TextureTable, texture), clarityGain = detailGainAt(ClarityTable, clarity);
    const Bands &textureA = texture < 0 ? TextureReduce : TextureEnhance;
    const Bands &clarityA = clarity < 0 ? ClarityReduce : ClarityEnhance;

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
    // Bands of the log: G0 is the image, G1 sigma 1, then sigma 2, 4, ... 256.
    cv::Mat delta = cv::Mat::zeros(linearRgb.size(), CV_32F);
    cv::Mat previous = logLuminance, current;
    cv::GaussianBlur(logLuminance, current, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    for (int b = 0; b < BandCount; ++b) {
        if (b > 0) current = blurAtScale(logLuminance, 1 << (b - 1));   // sigma 2, 4, ... 256
        const float kt = textureGain[size_t(b)], kc = clarityGain[size_t(b)];
        if (kt != 0.f || kc != 0.f) {
            const float it = 1.f / (textureA[size_t(b)] * textureA[size_t(b)]);
            const float ic = 1.f / (clarityA[size_t(b)] * clarityA[size_t(b)]);
            cv::parallel_for_(cv::Range(0, delta.rows), [&](const cv::Range &rows) {
                for (int y = rows.start; y < rows.end; ++y) {
                    const float *p = previous.ptr<float>(y), *c = current.ptr<float>(y);
                    float *o = delta.ptr<float>(y);
                    for (int x = 0; x < delta.cols; ++x) {
                        const float d = p[x] - c[x], d2 = d * d;
                        o[x] += d * (kt / (1.f + d2 * it) + kc / (1.f + d2 * ic));
                    }
                }
            });
        }
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
