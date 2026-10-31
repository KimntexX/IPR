#include "DetailProcessing.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

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
constexpr int kChromaRadiusAtZero = 2, kChromaRadiusAtMaximum = 8;
constexpr float kChromaGuideNoise = 3.0f;        // guided filter eps, in guide noise units
constexpr float kChromaEdgeLow = 2.0f, kChromaEdgeHigh = 5.0f;  // in noise units of the level

float luminanceOf(const cv::Vec3f &rgb)
{
    return std::max(0.0f, WorkingColor::luminance(rgb));
}

cv::Mat extractLuminance(const cv::Mat &linearRgb)
{
    cv::Mat result(linearRgb.rows, linearRgb.cols, CV_32F);
    for (int y = 0; y < linearRgb.rows; ++y) {
        const cv::Vec3f *source = linearRgb.ptr<cv::Vec3f>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < linearRgb.cols; ++x)
            target[x] = luminanceOf(source[x]);
    }
    return result;
}

float estimateNoiseSigma(const cv::Mat &plane)
{
    cv::Mat base;
    cv::GaussianBlur(plane, base, cv::Size(0, 0), 0.8, 0.8, cv::BORDER_REPLICATE);
    const int step = std::max(1, static_cast<int>(std::sqrt(
        static_cast<double>(plane.total()) / 16384.0)));
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(plane.total() / (step * step)));
    for (int y = 0; y < plane.rows; y += step) {
        const float *values = plane.ptr<float>(y);
        const float *smoothed = base.ptr<float>(y);
        for (int x = 0; x < plane.cols; x += step)
            samples.push_back(std::abs(values[x] - smoothed[x]));
    }
    if (samples.empty())
        return kNoiseFloor;
    const size_t middle = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + middle, samples.end());
    return std::max(kNoiseFloor, samples[middle] / 0.67449f);
}

float smoothStep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / std::max(edge1 - edge0, 1e-6f),
                               0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float estimateDenoiseSigma(const cv::Mat &plane)
{
    // Unit-norm diagonal Haar filter. MAD estimates output high-frequency noise;
    // demosaicing can correlate it, so this is not a physical sensor-noise model.
    // Sample complete adjacent 2x2 blocks, never decimated individual pixels.
    const int pairRows = plane.rows / 2;
    const int pairColumns = plane.cols / 2;
    if (pairRows == 0 || pairColumns == 0)
        return 0.0f;
    const int stride = std::max(1, static_cast<int>(std::sqrt(
        static_cast<double>(pairRows) * pairColumns / 16384.0)));
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>((pairRows + stride - 1) / stride) *
                    ((pairColumns + stride - 1) / stride));
    for (int y = 0; y < pairRows; y += stride) {
        const float *first = plane.ptr<float>(2 * y);
        const float *second = plane.ptr<float>(2 * y + 1);
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

cv::Mat structureProtection(const cv::Mat &guide, float noiseSigma)
{
    cv::Mat gradientX, gradientY, magnitude;
    // Sobel's derivative scale is 8. Normalize it before comparing with sigma.
    cv::Sobel(guide, gradientX, CV_32F, 1, 0, 3, 1.0 / 8.0);
    cv::Sobel(guide, gradientY, CV_32F, 0, 1, 3, 1.0 / 8.0);
    cv::magnitude(gradientX, gradientY, magnitude);
    const float sigma = std::max(noiseSigma, kDenoiseSigmaEpsilon);
    for (int y = 0; y < magnitude.rows; ++y) {
        float *row = magnitude.ptr<float>(y);
        for (int x = 0; x < magnitude.cols; ++x)
            row[x] = smoothStep(0.5f * sigma, 2.0f * sigma, row[x]);
    }
    return magnitude;
}

float softThreshold(float value, float threshold)
{
    const float magnitude = std::max(0.0f, std::abs(value) - threshold);
    return std::copysign(magnitude, value);
}

cv::Mat denoiseLuminance(const cv::Mat &plane, const cv::Mat &protectionMap,
                         float noiseSigma, int amount)
{
    if (amount <= 0 || noiseSigma <= kDenoiseSigmaEpsilon)
        return plane;

    cv::Mat fineBase;
    cv::GaussianBlur(plane, fineBase, cv::Size(0, 0), kFineSigma, kFineSigma,
                     cv::BORDER_REPLICATE);
    cv::Mat coarseBase;
    cv::GaussianBlur(fineBase, coarseBase, cv::Size(0, 0), kCoarseSigma, kCoarseSigma,
                     cv::BORDER_REPLICATE);

    const float strength = static_cast<float>(std::clamp(amount, 0, 100)) / 100.0f;
    const float fineThreshold = noiseSigma * kLumaThresholdAtMaximum * strength;
    const float mediumThreshold = fineThreshold * kMediumThresholdRatio;
    cv::Mat result(plane.rows, plane.cols, CV_32F);

    for (int y = 0; y < plane.rows; ++y) {
        const float *original = plane.ptr<float>(y);
        const float *fine = fineBase.ptr<float>(y);
        const float *coarse = coarseBase.ptr<float>(y);
        const float *edge = protectionMap.ptr<float>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < plane.cols; ++x) {
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
    for (int y = 0; y < plane.rows; ++y) {
        const float *original = plane.ptr<float>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < plane.cols; ++x) {
            target[x] = std::max(0.0f, target[x]);
            const double change = static_cast<double>(target[x]) - original[x];
            positive += std::max(0.0, change);
            negative += std::max(0.0, -change);
        }
    }
    const float positiveScale = positive > 0.0 ? static_cast<float>(std::min(1.0, negative / positive)) : 1.0f;
    const float negativeScale = negative > 0.0 ? static_cast<float>(std::min(1.0, positive / negative)) : 1.0f;
    for (int y = 0; y < plane.rows; ++y) {
        const float *original = plane.ptr<float>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < plane.cols; ++x) {
            const float change = target[x] - original[x];
            // A strength-dependent blend also prevents early threshold plateaus
            // from making 25/50/75 indistinguishable on quiet fields.
            target[x] = original[x] + strength * change * (change > 0.f ? positiveScale : negativeScale);
        }
    }
    return result;
}

cv::Mat boxMean(const cv::Mat &plane, int radius)
{
    cv::Mat mean;
    cv::boxFilter(plane, mean, CV_32F, cv::Size(2 * radius + 1, 2 * radius + 1),
                  cv::Point(-1, -1), true, cv::BORDER_REPLICATE);
    return mean;
}

// He's guided filter: the plane follows the guide's edges inside each window.
cv::Mat guidedFilter(const cv::Mat &guide, const cv::Mat &plane, int radius, float eps)
{
    const cv::Mat meanGuide = boxMean(guide, radius), meanPlane = boxMean(plane, radius);
    const cv::Mat covariance = boxMean(guide.mul(plane), radius) - meanGuide.mul(meanPlane);
    const cv::Mat variance = boxMean(guide.mul(guide), radius) - meanGuide.mul(meanGuide);
    const cv::Mat a = covariance / (variance + eps);
    const cv::Mat b = meanPlane - a.mul(meanGuide);
    return boxMean(a, radius).mul(guide) + boxMean(b, radius);
}

// Median of value per 1 EV bin of level, interpolated back to each pixel: the
// typical value at that brightness, sampled on a sparse grid.
cv::Mat levelMedian(const cv::Mat &value, const cv::Mat &level)
{
    double low = 0.0, high = 0.0;
    cv::minMaxLoc(level, &low, &high);
    const int first = static_cast<int>(std::floor(low));
    const int bins = std::max(1, static_cast<int>(std::ceil(high)) - first + 1);
    std::vector<std::vector<float>> samples(static_cast<size_t>(bins));
    const int stride = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(value.total()) / 262144.0)));
    for (int y = 0; y < value.rows; y += stride) {
        const float *v = value.ptr<float>(y);
        const float *l = level.ptr<float>(y);
        for (int x = 0; x < value.cols; x += stride) {
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
    cv::Mat result(value.rows, value.cols, CV_32F, cv::Scalar(0.0f));
    if (centers.empty())
        return result;
    for (int y = 0; y < value.rows; ++y) {
        const float *l = level.ptr<float>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < value.cols; ++x) {
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

// Colour noise: each chroma plane is smoothed by a guided filter on log
// luminance, so colour stays inside luminance edges; the radius and the weight
// grow with the amount (weight 1 - (1 - s)^4: most of the fine colour noise goes
// from the first settings, as in Lightroom). Colour edges that luminance does
// not show are kept where the smoothed chroma gradient stands well above its
// typical value at that brightness.
void denoiseChrominance(const cv::Mat &luma, cv::Mat *chromaOrange, cv::Mat *chromaGreen, int amount)
{
    const float strength = static_cast<float>(std::clamp(amount, 0, 100)) / 100.0f;
    const float weight = 1.0f - std::pow(1.0f - strength, 4.0f);
    const int radius = static_cast<int>(std::lround(
        kChromaRadiusAtZero + (kChromaRadiusAtMaximum - kChromaRadiusAtZero) * strength));

    cv::Mat logLuma(luma.rows, luma.cols, CV_32F);
    for (int y = 0; y < luma.rows; ++y) {
        const float *source = luma.ptr<float>(y);
        float *target = logLuma.ptr<float>(y);
        for (int x = 0; x < luma.cols; ++x)
            target[x] = std::log2(std::max(source[x], 1e-5f));
    }
    cv::Mat guide, level;
    cv::GaussianBlur(logLuma, guide, cv::Size(), kFineSigma, kFineSigma, cv::BORDER_REPLICATE);
    cv::GaussianBlur(guide, level, cv::Size(), 4.0, 4.0, cv::BORDER_REPLICATE);
    const float guideNoise = kChromaGuideNoise * estimateDenoiseSigma(guide);
    const float eps = guideNoise * guideNoise + 1e-12f;

    cv::Mat gradient = cv::Mat::zeros(luma.rows, luma.cols, CV_32F);
    for (const cv::Mat *plane : {chromaOrange, chromaGreen}) {
        cv::Mat smooth, gx, gy;
        cv::GaussianBlur(*plane, smooth, cv::Size(), kCoarseSigma, kCoarseSigma, cv::BORDER_REPLICATE);
        cv::Sobel(smooth, gx, CV_32F, 1, 0);
        cv::Sobel(smooth, gy, CV_32F, 0, 1);
        gradient += gx.mul(gx) + gy.mul(gy);
    }
    cv::sqrt(gradient, gradient);
    const cv::Mat typical = levelMedian(gradient, level);
    cv::Mat colourEdge(luma.rows, luma.cols, CV_32F);
    for (int y = 0; y < luma.rows; ++y) {
        const float *edge = gradient.ptr<float>(y);
        const float *noise = typical.ptr<float>(y);
        float *target = colourEdge.ptr<float>(y);
        for (int x = 0; x < luma.cols; ++x)
            target[x] = noise[x] > 0.0f ? smoothStep(kChromaEdgeLow, kChromaEdgeHigh, edge[x] / noise[x]) : 0.0f;
    }
    // The filter window must not reach across a colour edge either.
    cv::dilate(colourEdge, colourEdge, cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(2 * radius + 1, 2 * radius + 1)));

    for (cv::Mat *plane : {chromaOrange, chromaGreen}) {
        const cv::Mat smoothed = guidedFilter(guide, *plane, radius, eps);
        for (int y = 0; y < plane->rows; ++y) {
            float *values = plane->ptr<float>(y);
            const float *target = smoothed.ptr<float>(y);
            const float *kept = colourEdge.ptr<float>(y);
            for (int x = 0; x < plane->cols; ++x)
                values[x] += weight * (1.0f - kept[x]) * (target[x] - values[x]);
        }
    }
}

void splitLumaChroma(const cv::Mat &rgb, cv::Mat *luma, cv::Mat *chromaOrange,
                cv::Mat *chromaGreen)
{
    luma->create(rgb.rows, rgb.cols, CV_32F);
    chromaOrange->create(rgb.rows, rgb.cols, CV_32F);
    chromaGreen->create(rgb.rows, rgb.cols, CV_32F);
    for (int y = 0; y < rgb.rows; ++y) {
        const cv::Vec3f *source = rgb.ptr<cv::Vec3f>(y);
        float *yRow = luma->ptr<float>(y);
        float *coRow = chromaOrange->ptr<float>(y);
        float *cgRow = chromaGreen->ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const float red = source[x][0];
            const float green = source[x][1];
            const float blue = source[x][2];
            yRow[x] = WorkingColor::luminance(source[x]);
            coRow[x] = red - blue;
            cgRow[x] = green - 0.5f * (red + blue);
        }
    }
}

void mergeLumaChroma(const cv::Mat &luma, const cv::Mat &chromaOrange,
                const cv::Mat &chromaGreen, cv::Mat *rgb)
{
    for (int y = 0; y < rgb->rows; ++y) {
        cv::Vec3f *target = rgb->ptr<cv::Vec3f>(y);
        const float *yRow = luma.ptr<float>(y);
        const float *coRow = chromaOrange.ptr<float>(y);
        const float *cgRow = chromaGreen.ptr<float>(y);
        for (int x = 0; x < rgb->cols; ++x) {
            const float yValue = std::max(0.0f, yRow[x]);
            const float co = coRow[x];
            const float cg = cgRow[x];
            // Exact inverse of master Y, Co=R-B, Cg=G-(R+B)/2.
            const auto &w = WorkingColor::lumaWeights();
            const float k = w[0] + .5f * w[1];
            const cv::Vec3f deviation((1.f - k) * co - w[1] * cg,
                                     (.5f - k) * co + (1.f - w[1]) * cg,
                                     -k * co - w[1] * cg);
            const float minimum = std::min({deviation[0], deviation[1], deviation[2]});
            const float chromaScale = minimum < -yValue ? yValue / -minimum : 1.0f;
            // Compress toward a gray of the same luminance instead of clipping
            // channels independently (which lifts dark Y). No upper HDR clamp.
            for (int channel = 0; channel < 3; ++channel)
                target[x][channel] = std::max(0.0f, yValue + chromaScale * deviation[channel]);
        }
    }
}

}

void reduceNoise(cv::Mat &linearRgb, const DenoiseSettings &settings)
{
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3 ||
        (settings.luminance <= 0 && settings.chrominance <= 0))
        return;

    cv::Mat luma, chromaOrange, chromaGreen;
    splitLumaChroma(linearRgb, &luma, &chromaOrange, &chromaGreen);

    // Chroma is smoothed against the luminance before its own denoise: the guide
    // keeps the colour edges the photo shows.
    if (settings.chrominance > 0)
        denoiseChrominance(luma, &chromaOrange, &chromaGreen, settings.chrominance);
    if (settings.luminance > 0) {
        const float lumaSigma = estimateDenoiseSigma(luma);
        cv::Mat lumaGuide;
        cv::GaussianBlur(luma, lumaGuide, cv::Size(), kFineSigma, kFineSigma,
                         cv::BORDER_REPLICATE);
        luma = denoiseLuminance(luma, structureProtection(lumaGuide, lumaSigma),
                                lumaSigma, settings.luminance);
    }
    mergeLumaChroma(luma, chromaOrange, chromaGreen, &linearRgb);
}

void sharpen(cv::Mat &linearRgb, const SharpenSettings &settings)
{
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3 || settings.amount <= 0)
        return;

    const cv::Mat luma = extractLuminance(linearRgb);
    cv::Mat localBase;
    const double radius = std::clamp(static_cast<double>(settings.radius), 0.3, 3.0);
    cv::GaussianBlur(luma, localBase, cv::Size(0, 0), radius, radius,
                     cv::BORDER_REPLICATE);
    cv::Mat localMinimum, localMaximum;
    const int kernelSize = std::max(3, static_cast<int>(std::ceil(radius * 2.0)) * 2 + 1);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
                                                     cv::Size(kernelSize, kernelSize));
    cv::erode(luma, localMinimum, kernel);
    cv::dilate(luma, localMaximum, kernel);

    const float noiseSigma = estimateNoiseSigma(luma);
    const float threshold = noiseSigma *
        (0.35f + 5.5f * static_cast<float>(std::clamp(settings.masking, 0, 100)) / 100.0f);
    const float amount = static_cast<float>(std::clamp(settings.amount, 0, 200)) / 100.0f;
    for (int y = 0; y < linearRgb.rows; ++y) {
        cv::Vec3f *pixels = linearRgb.ptr<cv::Vec3f>(y);
        const float *sourceLuma = luma.ptr<float>(y);
        const float *base = localBase.ptr<float>(y);
        const float *minimum = localMinimum.ptr<float>(y);
        const float *maximum = localMaximum.ptr<float>(y);
        for (int x = 0; x < linearRgb.cols; ++x) {
            const float detail = sourceLuma[x] - base[x];
            const float edgeMask = smoothStep(threshold, threshold * 3.0f,
                                              std::abs(detail));
            if (edgeMask <= 0.0f || sourceLuma[x] <= 1e-6f)
                continue;
            const float localRange = std::max(0.0f, maximum[x] - minimum[x]);
            // Restrict overshoot to a small fraction of the local range. This
            // retains acutance while preventing the bright/dark halos of USM.
            const float targetLuma = std::clamp(
                sourceLuma[x] + detail * amount * edgeMask,
                minimum[x] - 0.15f * localRange, maximum[x] + 0.15f * localRange);
            pixels[x] *= std::max(0.0f, targetLuma) / sourceLuma[x];
        }
    }
}

}
