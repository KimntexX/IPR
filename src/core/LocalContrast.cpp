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
// 6V1A0327, 6V1A9947, _MG_7580): each band d becomes d + k d / (1 + (|d| / A)^2).
// Small variations are amplified by 1 + k, variations well above A (edges) are left
// as they are. Texture acts on the cube root of the luminance, where Lightroom's
// response is the same at every level (etude-amplitude-effets-v002-20261024T172402Z,
// k then scaled band by band by the ratio of Lightroom's effect to IPR's rendered
// one, measured before this calibration; result in etude-reponse-effets-v002);
// clarity on its log2, where the band model explains it best
// (etude-amplitude-effets-v001-20261024T165011Z).
constexpr std::array<Row, 6> TextureTable{{
    {-100, {-0.597f, -0.562f, -0.555f, -0.566f, -0.325f, -0.067f, -0.010f, -0.005f, 0.000f}},
    {-50, {-0.360f, -0.337f, -0.333f, -0.339f, -0.195f, -0.040f, -0.006f, -0.003f, 0.000f}},
    {-25, {-0.198f, -0.184f, -0.182f, -0.186f, -0.107f, -0.022f, -0.007f, -0.002f, 0.000f}},
    {25, {0.238f, 0.234f, 0.233f, 0.225f, 0.129f, 0.027f, 0.009f, 0.002f, -0.000f}},
    {50, {0.414f, 0.403f, 0.404f, 0.389f, 0.225f, 0.046f, 0.007f, 0.004f, -0.001f}},
    {100, {0.643f, 0.619f, 0.622f, 0.601f, 0.348f, 0.071f, 0.011f, 0.006f, -0.001f}},
}};
constexpr std::array<Row, 6> ClarityTable{{
    {-100, {-0.100f, -0.109f, -0.119f, -0.138f, -0.151f, -0.146f, -0.117f, -0.063f, -0.018f}},
    {-50, {-0.057f, -0.062f, -0.067f, -0.074f, -0.080f, -0.078f, -0.064f, -0.035f, -0.010f}},
    {-25, {-0.030f, -0.032f, -0.035f, -0.038f, -0.041f, -0.040f, -0.033f, -0.018f, -0.005f}},
    {25, {0.253f, 0.231f, 0.231f, 0.233f, 0.234f, 0.214f, 0.174f, 0.152f, 0.131f}},
    {50, {0.488f, 0.445f, 0.450f, 0.453f, 0.455f, 0.415f, 0.337f, 0.295f, 0.256f}},
    {100, {0.917f, 0.829f, 0.842f, 0.855f, 0.863f, 0.789f, 0.634f, 0.557f, 0.484f}},
}};
// A per band, in the units of the effect's domain, by sign of the slider:
// Lightroom's negative clarity does not depend on the amplitude (A at the top of
// the fitted range).
constexpr Bands TextureReduce{0.040f, 0.040f, 0.040f, 0.030f, 0.035f, 0.060f, 0.118f, 0.068f, 0.400f};
constexpr Bands TextureEnhance{0.040f, 0.035f, 0.035f, 0.030f, 0.035f, 0.060f, 0.103f, 0.052f, 0.400f};
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

// Gaussian blur of a sigma in pixels of this image; large sigmas at reduced
// resolution (area reduction by a power of 2 leaving a sigma of 2 to 4, then
// bilinear enlargement). A sigma far below a pixel leaves the plane: that band
// is finer than this image can show.
cv::Mat blurSigma(const cv::Mat &plane, double sigma)
{
    if (sigma < 0.25) return plane.clone();
    int factor = 1;
    while (sigma / (2 * factor) >= 2.0) factor *= 2;
    cv::Mat result;
    if (factor == 1) {
        cv::GaussianBlur(plane, result, cv::Size(0, 0), sigma, sigma, cv::BORDER_REFLECT);
        return result;
    }
    cv::Mat small;
    cv::resize(plane, small, cv::Size(std::max(1, plane.cols / factor), std::max(1, plane.rows / factor)), 0, 0,
               cv::INTER_AREA);
    cv::GaussianBlur(small, small, cv::Size(0, 0), sigma / factor, sigma / factor, cv::BORDER_REFLECT);
    cv::resize(small, result, plane.size(), 0, 0, cv::INTER_LINEAR);
    return result;
}

enum class Domain { Log2, CubeRoot };

float forward(Domain domain, float luminance)
{
    return domain == Domain::Log2 ? std::log2(luminance + LogFloor) : std::cbrt(luminance);
}

float inverse(Domain domain, float value)
{
    return domain == Domain::Log2 ? std::exp2(value) - LogFloor : value * value * value;
}

// Bands of the luminance in the domain: G0 is the image, G1 sigma 1, then sigma
// 2, 4, ... 256 pixels of the full image; each band d gains k d / (1 + (|d| / A)^2); colours keep their
// channel ratios.
void applyBands(cv::Mat &linearRgb, Domain domain, const Bands &k, const Bands &a, double pixelScale)
{
    cv::Mat luminance(linearRgb.size(), CV_32F), value(linearRgb.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const auto *p = linearRgb.ptr<cv::Vec3f>(y);
            float *l = luminance.ptr<float>(y), *v = value.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                l[x] = WorkingColor::nonNegativeLuminance(p[x]);
                v[x] = forward(domain, l[x]);
            }
        }
    });
    cv::Mat delta = cv::Mat::zeros(linearRgb.size(), CV_32F);
    cv::Mat previous = value, current;
    for (int b = 0; b < BandCount; ++b) {
        current = blurSigma(value, pixelScale * double(b == 0 ? 1 : 2 << (b - 1)));   // sigma 1, 2, 4, ... 256
        const float gain = k[size_t(b)], inverseA2 = 1.f / (a[size_t(b)] * a[size_t(b)]);
        if (gain != 0.f) {
            cv::parallel_for_(cv::Range(0, delta.rows), [&](const cv::Range &rows) {
                for (int y = rows.start; y < rows.end; ++y) {
                    const float *p = previous.ptr<float>(y), *c = current.ptr<float>(y);
                    float *o = delta.ptr<float>(y);
                    for (int x = 0; x < delta.cols; ++x) {
                        const float d = p[x] - c[x];
                        o[x] += gain * d / (1.f + d * d * inverseA2);
                    }
                }
            });
        }
        previous = current;
    }
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *p = linearRgb.ptr<cv::Vec3f>(y);
            const float *l = luminance.ptr<float>(y), *v = value.ptr<float>(y), *d = delta.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x)
                WorkingColor::scaleToLuminance(p[x], l[x], std::max(0.f, inverse(domain, v[x] + d[x])));
        }
    });
}

} // namespace

void apply(cv::Mat &linearRgb, int texture, int clarity, double pixelScale)
{
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    if (texture != 0)
        applyBands(linearRgb, Domain::CubeRoot, detailGainAt(TextureTable, texture),
                   texture < 0 ? TextureReduce : TextureEnhance, pixelScale);
    if (clarity != 0)
        applyBands(linearRgb, Domain::Log2, detailGainAt(ClarityTable, clarity),
                   clarity < 0 ? ClarityReduce : ClarityEnhance, pixelScale);
}

} // namespace LocalContrast
