#include "DetailProcessing.h"
#include "WorkingColorSpace.h"

#include "Filters.h"
#include "Neighbourhood.h"
#include "Parallel.h"
#include "Pixels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace DetailProcessing {
namespace {

constexpr float kNoiseFloor = 0.00025f;
// Denoising thresholds are in units of measured input noise, not absolute RGB.
// Keep the legacy estimator/floor for sharpening until its separate validation.
constexpr float kDenoiseSigmaEpsilon = 1e-7f;
constexpr double kFineSigma = 0.8;
constexpr double kCoarseSigma = 1.8;
constexpr float kLumaThresholdAtMaximum = 2.55f;
constexpr float kMediumThresholdRatio = 0.52f;
// Colour noise: chroma smoothed by a guided filter on log luminance. Fitted on
// Lightroom's colour states of six base scenes (2026-10-31).
// Radius in full-resolution pixels: colour blotches of high ISOs span 10 to 20 px.
constexpr double kChromaRadiusAtZero = 2.0, kChromaRadiusAtMaximum = 24.0;
constexpr float kChromaGuideNoise = 3.0f;        // guided filter eps, in guide noise units
// Guide smoothing and colour-edge scale, in full-resolution pixels: a finer guide
// copies the luminance noise of high ISOs into the colour.
constexpr double kChromaGuideSigma = 2.0, kChromaEdgeSigma = 4.0;
// Luminance below which colour is denoised as absolute chroma, not relative.
constexpr double kChromaLumaFloor = 1e-3;
constexpr float kChromaLogFloor = 1e-5f;  // log2 guide floor, linear Y
// Brightness level for the colour-edge reference, in pixels of the processed
// image (not scaled with the preview): only bins the guide into 1 EV levels.
constexpr double kChromaLevelSigma = 4.0;
constexpr float kChromaEdgeLow = 3.0f, kChromaEdgeHigh = 8.0f;  // in noise units of the level

float luminanceOf(const ipr::Vec3f &rgb)
{
    return std::max(0.0f, WorkingColor::luminance(rgb));
}

// The sharpening works on the luminance encoded with the sRGB curve, close to how the eye
// grades contrast: an unsharp mask of linear light barely shows in the shadows.
float encodeLuminance(float linear)
{
    return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

float decodeLuminance(float encoded)
{
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

ipr::Image encodedLuminance(const ipr::Image &linearRgb)
{
    ipr::Image result = ipr::Image::create(linearRgb.size(), ipr::Depth::F32, 1);
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const ipr::Vec3f *source = linearRgb.row<ipr::Vec3f>(y);
            float *target = result.mutableRow<float>(y);
            for (int x = 0; x < linearRgb.width(); ++x)
                target[x] = encodeLuminance(luminanceOf(source[x]));
        }
    });
    return result;
}

// Calibrated on Lightroom's exports (Camera Neutral, radius 1, detail 25, masking 0) of four
// RAWs at 0, 25, 50, 75, 100 and 150, on their edges (15 November 2026): a Gaussian of 0.7
// radius, a gain of 1.9 (amount / 50)^1.5, and an overshoot past the 5 x 5 local range (for
// radius 1) compressed softly within 30 % of that range.
// Radius and masking, calibrated on the Lightroom factors of the 31 October study (28 scenes,
// radius 0.5 to 3 and masking 25 to 75 at amount 75, detail 25; 16 November 2026): the Gaussian
// grows as the square root of the radius, the gain falls as radius^-0.8 and the grain threshold
// follows the radius; the masking raises that threshold from 0.35-1.05 noise sigma at 0 to
// 4.35-7.45 at 100, keeping texture as Lightroom's does while sparing the grain.
constexpr double kSharpenSigmaPerRadius = 0.7, kSharpenSigmaRadiusPower = 0.5;
constexpr double kSharpenGainAt50 = 1.9, kSharpenGainExponent = 1.5, kSharpenGainRadiusPower = -0.8;
constexpr float kSharpenOvershootRoom = 0.3f;
constexpr float kSharpenThresholdLow = 0.35f, kSharpenThresholdHigh = 1.05f;
constexpr float kSharpenMaskingLow = 4.0f, kSharpenMaskingHigh = 6.4f;

float sharpenGain(float amount)
{
    return amount <= 0.0f ? 0.0f
        : float(kSharpenGainAt50 * std::pow(double(amount) / 50.0, kSharpenGainExponent));
}

double sharpenRadius(const DetailProcessing::SharpenSettings &settings)
{
    return std::clamp(static_cast<double>(settings.radius), 0.3, 3.0);
}

double sharpenSigma(const DetailProcessing::SharpenSettings &settings, double pixelScale)
{
    return kSharpenSigmaPerRadius * std::pow(sharpenRadius(settings), kSharpenSigmaRadiusPower) * pixelScale;
}

int sharpenWindowReach(const DetailProcessing::SharpenSettings &settings, double pixelScale)
{
    return std::max(1, static_cast<int>(std::lround(2.0 * sharpenSigma(settings, pixelScale) / kSharpenSigmaPerRadius)));
}

// Past the local extremes the overshoot is compressed: within room, never beyond it.
float softOvershoot(float value, float minimum, float maximum, float room)
{
    if (value > maximum)
        return maximum + room * std::tanh((value - maximum) / room);
    if (value < minimum)
        return minimum - room * std::tanh((minimum - value) / room);
    return value;
}

constexpr double kSharpenNoiseBlur = 0.8;

int sharpenNoiseReachOf()
{
    return int(ipr::gaussianHalfKernel(kSharpenNoiseBlur).size()) - 1;
}

// The samples of the sharpening's noise measure in rows of a plane of planeHeight rows,
// lumaBand holding the full width of rows bandTop onwards. Only the rows multiple of step
// whose blur the band holds whole (or reaching the plane's edge) are taken: the same values
// as in the whole plane, whatever band they come from.
void appendLumaNoiseSamples(const ipr::Image &lumaBand, int bandTop, int planeHeight, int step,
                            std::vector<float> &samples)
{
    if (lumaBand.empty() || step < 1)
        return;
    const ipr::Image base = ipr::gaussianBlurred(lumaBand, kSharpenNoiseBlur, ipr::BlurBorder::LastPixel);
    const int reach = sharpenNoiseReachOf(), bandEnd = bandTop + lumaBand.height();
    for (int y = (bandTop + step - 1) / step * step; y < bandEnd; y += step) {
        if ((y - reach < bandTop && bandTop > 0) || (y + reach >= bandEnd && bandEnd < planeHeight))
            continue;
        const float *values = lumaBand.row<float>(y - bandTop);
        const float *smoothed = base.row<float>(y - bandTop);
        for (int x = 0; x < lumaBand.width(); x += step)
            samples.push_back(std::abs(values[x] - smoothed[x]));
    }
}

float noiseSigmaOfSamples(std::vector<float> samples)
{
    if (samples.empty())
        return kNoiseFloor;
    const size_t middle = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + middle, samples.end());
    return std::max(kNoiseFloor, samples[middle] / 0.67449f);
}

float estimateNoiseSigma(const ipr::Image &plane)
{
    std::vector<float> samples;
    appendLumaNoiseSamples(plane, 0, plane.height(), sharpenNoiseStep(plane.size()), samples);
    return noiseSigmaOfSamples(std::move(samples));
}

float smoothStep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / std::max(edge1 - edge0, 1e-6f),
                               0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float estimateDenoiseSigma(const ipr::Image &plane)
{
    // Unit-norm diagonal Haar filter. MAD estimates output high-frequency noise;
    // demosaicing can correlate it, so this is not a physical sensor-noise model.
    // Sample complete adjacent 2x2 blocks, never decimated individual pixels.
    const int pairRows = plane.height() / 2;
    const int pairColumns = plane.width() / 2;
    if (pairRows == 0 || pairColumns == 0)
        return 0.0f;
    const int stride = std::max(1, static_cast<int>(std::sqrt(
        static_cast<double>(pairRows) * pairColumns / 16384.0)));
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>((pairRows + stride - 1) / stride) *
                    ((pairColumns + stride - 1) / stride));
    for (int y = 0; y < pairRows; y += stride) {
        const float *first = plane.row<float>(2 * y);
        const float *second = plane.row<float>(2 * y + 1);
        for (int x = 0; x < pairColumns; x += stride) {
            const int column = 2 * x;
            // Exact constant blocks carry no measurable noise. Letting a large
            // black border dominate the MAD would disable denoising elsewhere.
            if (first[column] == first[column + 1] && first[column] == second[column] &&
                first[column] == second[column + 1])
                continue;
            samples.push_back((first[column] - first[column + 1] -
                               second[column] + second[column + 1]) * 0.5f);
        }
    }
    if (samples.empty())
        return 0.0f;
    const auto middle = samples.begin() + samples.size() / 2;
    std::nth_element(samples.begin(), middle, samples.end());
    const float center = *middle;
    for (float &value : samples)
        value = std::abs(value - center);
    std::nth_element(samples.begin(), middle, samples.end());
    return *middle / 0.67448975f;
}

ipr::Image structureProtection(const ipr::Image &guide, float noiseSigma)
{
    // Sobel's derivative scale is 8. Normalize it before comparing with sigma.
    ipr::Image magnitude = ipr::magnitude(ipr::sobel(guide, true, 1.0 / 8.0), ipr::sobel(guide, false, 1.0 / 8.0));
    const float sigma = std::max(noiseSigma, kDenoiseSigmaEpsilon);
    for (int y = 0; y < magnitude.height(); ++y) {
        float *row = magnitude.mutableRow<float>(y);
        for (int x = 0; x < magnitude.width(); ++x)
            row[x] = smoothStep(0.5f * sigma, 2.0f * sigma, row[x]);
    }
    return magnitude;
}

float softThreshold(float value, float threshold)
{
    const float magnitude = std::max(0.0f, std::abs(value) - threshold);
    return std::copysign(magnitude, value);
}

ipr::Image denoiseLuminance(const ipr::Image &plane, const ipr::Image &protectionMap,
                            float noiseSigma, int amount, NoiseMeasures *record = nullptr,
                            const NoiseMeasures *given = nullptr)
{
    if (amount <= 0 || noiseSigma <= kDenoiseSigmaEpsilon)
        return plane.share();

    const ipr::Image fineBase = ipr::gaussianBlurred(plane, kFineSigma, ipr::BlurBorder::LastPixel);
    const ipr::Image coarseBase = ipr::gaussianBlurred(fineBase, kCoarseSigma, ipr::BlurBorder::LastPixel);

    const float strength = static_cast<float>(std::clamp(amount, 0, 100)) / 100.0f;
    const float fineThreshold = noiseSigma * kLumaThresholdAtMaximum * strength;
    const float mediumThreshold = fineThreshold * kMediumThresholdRatio;
    ipr::Image result = ipr::Image::create(plane.size(), ipr::Depth::F32, 1);

    for (int y = 0; y < plane.height(); ++y) {
        const float *original = plane.row<float>(y);
        const float *fine = fineBase.row<float>(y);
        const float *coarse = coarseBase.row<float>(y);
        const float *edge = protectionMap.row<float>(y);
        float *target = result.mutableRow<float>(y);
        for (int x = 0; x < plane.width(); ++x) {
            // Strong luminance edges protect real detail from the shrinkage.
            const float protection = edge[x] * 0.86f;
            const float fineDetail = original[x] - fine[x];
            const float mediumDetail = fine[x] - coarse[x];
            target[x] = coarse[x] +
                softThreshold(mediumDetail, mediumThreshold * (1.0f - protection)) +
                softThreshold(fineDetail, fineThreshold * (1.0f - protection));
        }
    }
    // Balance positive/negative changes, preserving DC without adding a global
    // offset to unchanged black areas. Corrections move toward the original,
    // never beyond the original/candidate range. Luminance is nonnegative.
    // This is not an exposure rescale.
    double positive = 0.0, negative = 0.0;
    for (int y = 0; y < plane.height(); ++y) {
        const float *original = plane.row<float>(y);
        float *target = result.mutableRow<float>(y);
        for (int x = 0; x < plane.width(); ++x) {
            target[x] = std::max(0.0f, target[x]);
            const double change = static_cast<double>(target[x]) - original[x];
            positive += std::max(0.0, change);
            negative += std::max(0.0, -change);
        }
    }
    // A part of the image takes the balance of the whole one (given).
    const float positiveScale = given ? given->positiveScale
        : positive > 0.0 ? static_cast<float>(std::min(1.0, negative / positive)) : 1.0f;
    const float negativeScale = given ? given->negativeScale
        : negative > 0.0 ? static_cast<float>(std::min(1.0, positive / negative)) : 1.0f;
    if (record) {
        record->positiveScale = positiveScale;
        record->negativeScale = negativeScale;
    }
    for (int y = 0; y < plane.height(); ++y) {
        const float *original = plane.row<float>(y);
        float *target = result.mutableRow<float>(y);
        for (int x = 0; x < plane.width(); ++x) {
            const float change = target[x] - original[x];
            // A strength-dependent blend also prevents early threshold plateaus
            // from making 25/50/75 indistinguishable on quiet fields.
            target[x] = original[x] + strength * change * (change > 0.f ? positiveScale : negativeScale);
        }
    }
    return result;
}

ipr::Image boxMean(const ipr::Image &plane, int radius)
{
    return ipr::boxMean(plane, ipr::Size(2 * radius + 1, 2 * radius + 1), ipr::BlurBorder::LastPixel);
}

// He's guided filter: the plane follows the guide's edges inside each window.
ipr::Image guidedFilter(const ipr::Image &guide, const ipr::Image &plane, int radius, float eps)
{
    const auto product = [](float p, float q) { return p * q; };
    const ipr::Image meanGuide = boxMean(guide, radius), meanPlane = boxMean(plane, radius);
    const ipr::Image meanProduct = boxMean(ipr::mapped(guide, plane, product), radius);
    const ipr::Image meanSquare = boxMean(ipr::mapped(guide, guide, product), radius);
    const ipr::Image a = ipr::mapped(meanProduct, meanGuide, meanPlane, [](float gp, float g, float p) { return gp - g * p; });
    const ipr::Image slope = ipr::mapped(a, meanSquare, meanGuide, [eps](float covariance, float gg, float g) {
        return covariance / ((gg - g * g) + eps);
    });
    const ipr::Image b = ipr::mapped(meanPlane, slope, meanGuide, [](float p, float s, float g) { return p - s * g; });
    return ipr::mapped(boxMean(slope, radius), guide, boxMean(b, radius), [](float s, float g, float q) { return s * g + q; });
}

ipr::Image levelMap(const ipr::Image &level, const std::vector<float> &centers, const std::vector<float> &medians);

// Median of value per 1 EV bin of level, interpolated back to each pixel: the
// typical value at that brightness, sampled on a sparse grid.
ipr::Image levelMedian(const ipr::Image &value, const ipr::Image &level, NoiseMeasures *record = nullptr,
                       const NoiseMeasures *given = nullptr)
{
    if (given)
        return levelMap(level, given->levelCenters, given->levelMedians);
    const ipr::Extremes range = ipr::extremesOf(level);
    const double low = range.smallest, high = range.largest;
    const int first = static_cast<int>(std::floor(low));
    const int bins = std::max(1, static_cast<int>(std::ceil(high)) - first + 1);
    std::vector<std::vector<float>> samples(static_cast<size_t>(bins));
    const int stride = std::max(1, static_cast<int>(std::sqrt(double(value.width()) * double(value.height()) / 262144.0)));
    for (int y = 0; y < value.height(); y += stride) {
        const float *v = value.row<float>(y);
        const float *l = level.row<float>(y);
        for (int x = 0; x < value.width(); x += stride) {
            const int bin = std::clamp(static_cast<int>(std::floor(l[x])) - first, 0, bins - 1);
            samples[static_cast<size_t>(bin)].push_back(v[x]);
        }
    }
    std::vector<float> centers, medians;
    for (int i = 0; i < bins; ++i) {
        std::vector<float> &bin = samples[static_cast<size_t>(i)];
        if (bin.size() < 64)
            continue;
        const auto middle = bin.begin() + static_cast<std::ptrdiff_t>(bin.size() / 2);
        std::nth_element(bin.begin(), middle, bin.end());
        centers.push_back(static_cast<float>(first + i) + 0.5f);
        medians.push_back(*middle);
    }
    if (record) {
        record->levelCenters = centers;
        record->levelMedians = medians;
    }
    return levelMap(level, centers, medians);
}

// The typical value at each pixel's level, from the medians per level (empty: zero everywhere).
ipr::Image levelMap(const ipr::Image &level, const std::vector<float> &centers, const std::vector<float> &medians)
{
    ipr::Image result = ipr::filled(level.size(), ipr::Depth::F32, 1, 0.0);
    if (centers.empty())
        return result;
    for (int y = 0; y < level.height(); ++y) {
        const float *l = level.row<float>(y);
        float *target = result.mutableRow<float>(y);
        for (int x = 0; x < level.width(); ++x) {
            const auto upper = std::upper_bound(centers.begin(), centers.end(), l[x]);
            if (upper == centers.begin()) {
                target[x] = medians.front();
            } else if (upper == centers.end()) {
                target[x] = medians.back();
            } else {
                const size_t i = static_cast<size_t>(upper - centers.begin());
                const float t = (l[x] - centers[i - 1]) / (centers[i] - centers[i - 1]);
                target[x] = medians[i - 1] + t * (medians[i] - medians[i - 1]);
            }
        }
    }
    return result;
}

// Colour edges that luminance does not show: 1 where the smoothed chroma
// gradient stands well above its typical value at that brightness, dilated over
// the filter window so the window does not reach across the edge either.
ipr::Image colourEdgeProtection(const ipr::Image &chromaOrange, const ipr::Image &chromaGreen,
                                const ipr::Image &level, double edgeSigma, int radius, NoiseMeasures *record,
                                const NoiseMeasures *given)
{
    ipr::Image gradient = ipr::Image::zeros(chromaOrange.size(), ipr::Depth::F32, 1);
    for (const ipr::Image *plane : {&chromaOrange, &chromaGreen}) {
        const ipr::Image smooth = ipr::gaussianBlurred(*plane, edgeSigma, ipr::BlurBorder::LastPixel);
        const ipr::Image gx = ipr::sobel(smooth, true), gy = ipr::sobel(smooth, false);
        gradient = ipr::mapped(gradient, gx, gy, [](float sum, float x, float y) { return sum + (x * x + y * y); });
    }
    gradient = ipr::squareRoot(gradient);
    const ipr::Image typical = levelMedian(gradient, level, record, given);
    ipr::Image protection = ipr::Image::create(gradient.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < gradient.height(); ++y) {
        const float *edge = gradient.row<float>(y);
        const float *noise = typical.row<float>(y);
        float *target = protection.mutableRow<float>(y);
        for (int x = 0; x < gradient.width(); ++x)
            target[x] = noise[x] > 0.0f ? smoothStep(kChromaEdgeLow, kChromaEdgeHigh, edge[x] / noise[x]) : 0.0f;
    }
    return ipr::maximum(protection, ipr::Size(2 * radius + 1, 2 * radius + 1), ipr::Beyond::Nothing);
}

// Colour noise. A 5 x 5 median first removes isolated colour dots, which an
// edge-aware filter would keep as structure. Each chroma plane is then smoothed
// by a guided filter on smoothed log luminance, so colour stays inside
// luminance edges. Weight 1 - (1 - s)^8: nearly all the colour noise goes by
// 25, as in Lightroom; the radius follows the weight. Colour edges that
// luminance does not show are kept (colourEdgeProtection).
void denoiseChrominance(const ipr::Image &luma, ipr::Image *chromaOrange, ipr::Image *chromaGreen, int amount,
                        double pixelScale, NoiseMeasures *record = nullptr, const NoiseMeasures *given = nullptr)
{
    const float strength = static_cast<float>(std::clamp(amount, 0, 100)) / 100.0f;
    const float weight = 1.0f - std::pow(1.0f - strength, 8.0f);
    const int radius = std::max(1, static_cast<int>(std::lround(
        (kChromaRadiusAtZero + (kChromaRadiusAtMaximum - kChromaRadiusAtZero) * weight) * pixelScale)));
    const auto scaled = [pixelScale](double sigma) { return std::max(0.8, sigma * pixelScale); };

    for (ipr::Image *plane : {chromaOrange, chromaGreen}) {
        const ipr::Image median = ipr::median(*plane, pixelScale >= 0.6 ? 5 : 3);
        *plane = ipr::blended(*plane, 1.0 - weight, median, weight, 0.0);
    }

    ipr::Image logLuma = ipr::Image::create(luma.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < luma.height(); ++y) {
        const float *source = luma.row<float>(y);
        float *target = logLuma.mutableRow<float>(y);
        for (int x = 0; x < luma.width(); ++x)
            target[x] = std::log2(std::max(source[x], kChromaLogFloor));
    }
    const ipr::Image fineGuide = ipr::gaussianBlurred(logLuma, kFineSigma, ipr::BlurBorder::LastPixel);
    const double guideSigma = scaled(kChromaGuideSigma);
    const ipr::Image guide = ipr::gaussianBlurred(logLuma, guideSigma, ipr::BlurBorder::LastPixel);
    const ipr::Image level = ipr::gaussianBlurred(guide, kChromaLevelSigma, ipr::BlurBorder::LastPixel);
    const float guideSigma0 = given ? given->guideSigma : estimateDenoiseSigma(fineGuide);
    if (record)
        record->guideSigma = guideSigma0;
    const float guideNoise = kChromaGuideNoise * guideSigma0;
    const float eps = guideNoise * guideNoise + 1e-12f;
    const ipr::Image colourEdge = colourEdgeProtection(*chromaOrange, *chromaGreen, level,
                                                       scaled(kChromaEdgeSigma), radius, record, given);

    for (ipr::Image *plane : {chromaOrange, chromaGreen}) {
        const ipr::Image smoothed = guidedFilter(guide, *plane, radius, eps);
        for (int y = 0; y < plane->height(); ++y) {
            float *values = plane->mutableRow<float>(y);
            const float *target = smoothed.row<float>(y);
            const float *kept = colourEdge.row<float>(y);
            for (int x = 0; x < plane->width(); ++x)
                values[x] += weight * (1.0f - kept[x]) * (target[x] - values[x]);
        }
    }
}

void splitLumaChroma(const ipr::Image &rgb, ipr::Image *luma, ipr::Image *chromaOrange,
                ipr::Image *chromaGreen)
{
    *luma = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
    *chromaOrange = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
    *chromaGreen = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < rgb.height(); ++y) {
        const ipr::Vec3f *source = rgb.row<ipr::Vec3f>(y);
        float *yRow = luma->mutableRow<float>(y);
        float *coRow = chromaOrange->mutableRow<float>(y);
        float *cgRow = chromaGreen->mutableRow<float>(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const float red = source[x][0];
            const float green = source[x][1];
            const float blue = source[x][2];
            yRow[x] = WorkingColor::luminance(source[x]);
            coRow[x] = red - blue;
            cgRow[x] = green - 0.5f * (red + blue);
        }
    }
}

inline ipr::Vec3f mergedPixel(float luma, float co, float cg)
{
    const float yValue = std::max(0.0f, luma);
    // Exact inverse of master Y, Co=R-B, Cg=G-(R+B)/2.
    const auto &w = WorkingColor::lumaWeights();
    const float k = w[0] + .5f * w[1];
    const ipr::Vec3f deviation((1.f - k) * co - w[1] * cg,
                             (.5f - k) * co + (1.f - w[1]) * cg,
                             -k * co - w[1] * cg);
    const float minimum = std::min({deviation[0], deviation[1], deviation[2]});
    const float chromaScale = minimum < -yValue ? yValue / -minimum : 1.0f;
    // Compress toward a gray of the same luminance instead of clipping
    // channels independently (which lifts dark Y). No upper HDR clamp.
    ipr::Vec3f merged;
    for (int channel = 0; channel < 3; ++channel)
        merged[channel] = std::max(0.0f, yValue + chromaScale * deviation[channel]);
    return merged;
}

void mergeLumaChroma(const ipr::Image &luma, const ipr::Image &chromaOrange,
                const ipr::Image &chromaGreen, ipr::Image *rgb)
{
    for (int y = 0; y < rgb->height(); ++y) {
        ipr::Vec3f *target = rgb->mutableRow<ipr::Vec3f>(y);
        const float *yRow = luma.row<float>(y);
        const float *coRow = chromaOrange.row<float>(y);
        const float *cgRow = chromaGreen.row<float>(y);
        for (int x = 0; x < rgb->width(); ++x)
            target[x] = mergedPixel(yRow[x], coRow[x], cgRow[x]);
    }
}

ipr::Image clampedLuma(const ipr::Image &plane)
{
    return ipr::atLeast(plane, kChromaLumaFloor);
}

float mixed(const std::vector<const float *> &rows, const LocalValues::Bracket &at, int x)
{
    const float low = rows[at.low][x];
    return at.t == 0.0f ? low : low + at.t * (rows[at.high][x] - low);
}

// The global filters at the values the maps need, mixed per pixel. On the lattice
// every plane is the global path's own, so a pixel renders as that global value.
void reduceNoiseLocally(ipr::Image &linearRgb, const DenoiseSettings &settings, double pixelScale,
                        const ipr::Image &localLuminance, const ipr::Image &localChrominance)
{
    constexpr int MaxLatticeValues = 5;
    const float globalLuminance = float(std::clamp(settings.luminance, 0, 100));
    const float globalChrominance = float(std::clamp(settings.chrominance, 0, 100));
    const std::vector<float> luminanceValues = LocalValues::latticeOf(
        globalLuminance, localLuminance, 0.f, 100.f, 5.f, MaxLatticeValues);
    const std::vector<float> chrominanceValues = LocalValues::latticeOf(
        globalChrominance, localChrominance, 0.f, 100.f, 5.f, MaxLatticeValues);

    ipr::Image luma, chromaOrange, chromaGreen;
    splitLumaChroma(linearRgb, &luma, &chromaOrange, &chromaGreen);

    std::vector<ipr::Image> lumaPlanes;
    if (std::any_of(luminanceValues.cbegin(), luminanceValues.cend(), [](float v) { return v > 0.f; })) {
        const float lumaSigma = estimateDenoiseSigma(luma);
        const ipr::Image lumaGuide = ipr::gaussianBlurred(luma, kFineSigma, ipr::BlurBorder::LastPixel);
        const ipr::Image protection = structureProtection(lumaGuide, lumaSigma);
        for (float value : luminanceValues)
            lumaPlanes.push_back(denoiseLuminance(luma, protection, lumaSigma, int(std::lround(value))));
    } else {
        for (size_t i = 0; i < luminanceValues.size(); ++i)
            lumaPlanes.push_back(luma.share());
    }

    // Colour relative to the luminance before its denoise, as the global path.
    std::vector<ipr::Image> orangePlanes, greenPlanes;
    if (std::any_of(chrominanceValues.cbegin(), chrominanceValues.cend(), [](float v) { return v > 0.f; })) {
        const ipr::Image divisor = clampedLuma(luma);
        const ipr::Image orangeRatio = ipr::divided(chromaOrange, divisor);
        const ipr::Image greenRatio = ipr::divided(chromaGreen, divisor);
        for (float value : chrominanceValues) {
            ipr::Image orange = orangeRatio.clone(), green = greenRatio.clone();
            if (value > 0.f)
                denoiseChrominance(luma, &orange, &green, int(std::lround(value)), pixelScale);
            orangePlanes.push_back(std::move(orange));
            greenPlanes.push_back(std::move(green));
        }
    }

    const auto rowsOf = [](const std::vector<ipr::Image> &planes, int y) {
        std::vector<const float *> rows;
        for (const ipr::Image &plane : planes) rows.push_back(plane.row<float>(y));
        return rows;
    };
    const auto valueAt = [](float global, const float *localRow, int x) {
        return localRow && std::isfinite(localRow[x]) ? std::clamp(global + localRow[x], 0.f, 100.f) : global;
    };
    const float *const noRow = nullptr;
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *pixels = linearRgb.mutableRow<ipr::Vec3f>(y);
            const float *luminanceRow = localLuminance.empty() ? noRow : localLuminance.row<float>(y);
            const float *chrominanceRow = localChrominance.empty() ? noRow : localChrominance.row<float>(y);
            const std::vector<const float *> lumaRows = rowsOf(lumaPlanes, y);
            const std::vector<const float *> orangeRows = rowsOf(orangePlanes, y);
            const std::vector<const float *> greenRows = rowsOf(greenPlanes, y);
            const float *orangeSource = chromaOrange.row<float>(y), *greenSource = chromaGreen.row<float>(y);
            for (int x = 0; x < linearRgb.width(); ++x) {
                const float luminance = valueAt(globalLuminance, luminanceRow, x);
                const float chrominance = valueAt(globalChrominance, chrominanceRow, x);
                if (luminance == 0.f && chrominance == 0.f) continue;
                const float yValue = mixed(lumaRows, LocalValues::bracketOf(luminanceValues, luminance), x);
                float co = orangeSource[x], cg = greenSource[x];
                if (chrominance > 0.f) {
                    const LocalValues::Bracket at = LocalValues::bracketOf(chrominanceValues, chrominance);
                    const float factor = float(std::max(double(yValue), kChromaLumaFloor));
                    co = mixed(orangeRows, at, x) * factor;
                    cg = mixed(greenRows, at, x) * factor;
                }
                pixels[x] = mergedPixel(yValue, co, cg);
            }
        }
    });
}

}

int reduceNoiseReach(const DenoiseSettings &settings, double pixelScale)
{
    // How far each filter reads, added along the chain: what a part needs around it.
    const auto reach = [](double sigma) { return int(ipr::gaussianHalfKernel(sigma).size()) - 1; };
    int total = 0;
    if (settings.chrominance > 0) {
        const float strength = static_cast<float>(std::clamp(settings.chrominance, 0, 100)) / 100.0f;
        const float weight = 1.0f - std::pow(1.0f - strength, 8.0f);
        const int radius = std::max(1, static_cast<int>(std::lround(
            (kChromaRadiusAtZero + (kChromaRadiusAtMaximum - kChromaRadiusAtZero) * weight) * pixelScale)));
        const auto scaled = [pixelScale](double sigma) { return std::max(0.8, sigma * pixelScale); };
        const int median = (pixelScale >= 0.6 ? 5 : 3) / 2;
        // The guide (blurred log luminance) and its level, the colour edges (blur, Sobel, maximum),
        // the guided filter (two box means): the largest chain from the input to the output.
        const int guide = reach(scaled(kChromaGuideSigma));
        const int edge = median + reach(scaled(kChromaEdgeSigma)) + 1 + radius;
        const int filter = std::max(median, guide) + 2 * radius;
        total += std::max({guide + reach(kChromaLevelSigma) + 1 + radius, edge, filter}) + 1;
    }
    if (settings.luminance > 0)
        total += reach(kFineSigma) + std::max(reach(kCoarseSigma), 1) + 1;
    return total;
}

int sharpenNoiseStep(const ipr::Size &plane)
{
    const double total = double(plane.width) * double(plane.height);
    return std::max(1, static_cast<int>(std::sqrt(total / 16384.0)));
}

int sharpenNoiseReach()
{
    return sharpenNoiseReachOf();
}

void appendSharpenNoiseSamples(const ipr::Image &linearRgbBand, int bandTop, int planeHeight, int step,
                               std::vector<float> &samples)
{
    if (linearRgbBand.empty() || linearRgbBand.depth() != ipr::Depth::F32 || linearRgbBand.channels() != 3)
        return;
    appendLumaNoiseSamples(encodedLuminance(linearRgbBand), bandTop, planeHeight, step, samples);
}

float sharpenNoiseSigma(std::vector<float> samples)
{
    return noiseSigmaOfSamples(std::move(samples));
}

int sharpenReach(const SharpenSettings &settings, double pixelScale)
{
    return std::max(int(ipr::gaussianHalfKernel(sharpenSigma(settings, pixelScale)).size()) - 1,
                    sharpenWindowReach(settings, pixelScale));
}

bool denoiseFromAmount(int amount, DenoiseSettings &settings)
{
    if (amount < 0 || amount > 100)
        return false;
    const double remaining = 1. - double(amount) / 100.;
    const double squared = remaining * remaining;
    settings = {amount, int(std::lround(100. * (1. - squared * squared * squared)))};
    return true;
}

void reduceNoise(ipr::Image &linearRgb, const DenoiseSettings &settings, double pixelScale,
                 const LocalValues::Maps &local, NoiseMeasures *record, const NoiseMeasures *given)
{
    if (record)
        *record = {};
    if (linearRgb.empty() || linearRgb.depth() != ipr::Depth::F32 || linearRgb.channels() != 3) return;
    const ipr::Image none;
    const auto usable = [&](LocalValues::Parameter parameter) -> const ipr::Image & {
        const ipr::Image &map = local[parameter];
        return !map.empty() && LocalValues::fits(map, linearRgb.size()) ? map : none;
    };
    const ipr::Image &localLuminance = usable(LocalValues::DenoiseLuminance);
    const ipr::Image &localChrominance = usable(LocalValues::DenoiseChrominance);
    if (!localLuminance.empty() || !localChrominance.empty()) {
        reduceNoiseLocally(linearRgb, settings, pixelScale, localLuminance, localChrominance);
        return;
    }
    if (settings.luminance <= 0 && settings.chrominance <= 0)
        return;

    ipr::Image luma, chromaOrange, chromaGreen;
    splitLumaChroma(linearRgb, &luma, &chromaOrange, &chromaGreen);

    // Colour is denoised relative to luminance (Co/Y, Cg/Y), then rebuilt on the
    // final luminance: absolute chroma on a noisy luminance would turn the
    // luminance grain into saturation speckle. The guide is the luminance before
    // its own denoise: it keeps the colour edges the photo shows.
    if (settings.chrominance > 0) {
        const ipr::Image divisor = clampedLuma(luma);
        chromaOrange = ipr::divided(chromaOrange, divisor);
        chromaGreen = ipr::divided(chromaGreen, divisor);
        denoiseChrominance(luma, &chromaOrange, &chromaGreen, settings.chrominance, pixelScale, record, given);
    }
    if (settings.luminance > 0) {
        const float lumaSigma = given ? given->lumaSigma : estimateDenoiseSigma(luma);
        if (record)
            record->lumaSigma = lumaSigma;
        const ipr::Image lumaGuide = ipr::gaussianBlurred(luma, kFineSigma, ipr::BlurBorder::LastPixel);
        luma = denoiseLuminance(luma, structureProtection(lumaGuide, lumaSigma),
                                lumaSigma, settings.luminance, record, given);
    }
    if (record)
        record->valid = true;
    if (settings.chrominance > 0) {
        const ipr::Image factor = clampedLuma(luma);
        chromaOrange = ipr::multiplied(chromaOrange, factor);
        chromaGreen = ipr::multiplied(chromaGreen, factor);
    }
    mergeLumaChroma(luma, chromaOrange, chromaGreen, &linearRgb);
}

void sharpen(ipr::Image &linearRgb, const SharpenSettings &settings, double pixelScale,
             const LocalValues::Maps &local, const float *noiseSigma)
{
    if (linearRgb.empty() || linearRgb.depth() != ipr::Depth::F32 || linearRgb.channels() != 3) return;
    const ipr::Image &map = local[LocalValues::Sharpening];
    const bool hasLocal = !map.empty() && LocalValues::fits(map, linearRgb.size());
    if (settings.amount <= 0 && !hasLocal)
        return;

    const ipr::Image encoded = encodedLuminance(linearRgb);
    const ipr::Image localBase = ipr::gaussianBlurred(encoded, sharpenSigma(settings, pixelScale),
                                                      ipr::BlurBorder::LastPixel);
    const int window = 2 * sharpenWindowReach(settings, pixelScale) + 1;
    const ipr::Image localMinimum = ipr::minimum(encoded, ipr::Size(window, window), ipr::Beyond::Nothing);
    const ipr::Image localMaximum = ipr::maximum(encoded, ipr::Size(window, window), ipr::Beyond::Nothing);

    // Grain protection, kept at masking 0 (user's choice of 15 November 2026, unlike
    // Lightroom): a detail at the level of the noise is not sharpened.
    const float masking = static_cast<float>(std::clamp(settings.masking, 0, 100)) / 100.0f;
    const float noise = (noiseSigma ? *noiseSigma : estimateNoiseSigma(encoded)) * float(sharpenRadius(settings));
    const float thresholdLow = noise * (kSharpenThresholdLow + kSharpenMaskingLow * masking);
    const float thresholdHigh = noise * (kSharpenThresholdHigh + kSharpenMaskingHigh * masking);
    const float radiusGain = float(std::pow(sharpenRadius(settings), kSharpenGainRadiusPower));
    const float gain = radiusGain * sharpenGain(float(std::clamp(settings.amount, 0, 200)));
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            ipr::Vec3f *pixels = linearRgb.mutableRow<ipr::Vec3f>(y);
            const float *source = encoded.row<float>(y);
            const float *base = localBase.row<float>(y);
            const float *minimum = localMinimum.row<float>(y);
            const float *maximum = localMaximum.row<float>(y);
            const float *localAmount = hasLocal ? map.row<float>(y) : nullptr;
            for (int x = 0; x < linearRgb.width(); ++x) {
                float pixelGain = gain;
                if (localAmount && std::isfinite(localAmount[x]))
                    pixelGain = radiusGain * sharpenGain(std::clamp(float(std::clamp(settings.amount, 0, 200)) +
                                                                        localAmount[x], 0.0f, 200.0f));
                if (pixelGain <= 0.0f)
                    continue;
                const float detail = source[x] - base[x];
                const float edgeMask = smoothStep(thresholdLow, thresholdHigh, std::abs(detail));
                const float linear = decodeLuminance(source[x]);
                if (edgeMask <= 0.0f || linear <= 1e-6f)
                    continue;
                const float room = std::max(kSharpenOvershootRoom * (maximum[x] - minimum[x]), 1e-4f);
                const float target = softOvershoot(source[x] + detail * pixelGain * edgeMask,
                                                   minimum[x], maximum[x], room);
                pixels[x] *= decodeLuminance(std::max(0.0f, target)) / linear;
            }
        }
    });
}

}
