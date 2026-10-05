#include "DetailProcessing.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
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
constexpr float kChromaThresholdAtMaximum = 3.15f;
constexpr float kMediumThresholdRatio = 0.52f;

float luminanceOf(const cv::Vec3f &rgb)
{
    return std::max(0.0f, 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]);
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

cv::Mat denoisePlane(const cv::Mat &plane, const cv::Mat &lumaProtection,
                     float noiseSigma, int amount, bool isChrominance)
{
    if (amount <= 0 || noiseSigma <= kDenoiseSigmaEpsilon)
        return plane;

    cv::Mat fineBase;
    cv::GaussianBlur(plane, fineBase, cv::Size(0, 0), kFineSigma, kFineSigma,
                     cv::BORDER_REPLICATE);
    cv::Mat coarseBase;
    cv::GaussianBlur(fineBase, coarseBase, cv::Size(0, 0), kCoarseSigma, kCoarseSigma,
                     cv::BORDER_REPLICATE);

    cv::Mat protectionMap;
    if (isChrominance) {
        // Also protect isoluminant colour borders. Reuse the already filtered
        // plane instead of treating raw chroma noise as a contour.
        cv::max(lumaProtection, structureProtection(fineBase, noiseSigma), protectionMap);
    } else {
        protectionMap = lumaProtection;
    }
    const float strength = static_cast<float>(std::clamp(amount, 0, 100)) / 100.0f;
    const float multiplier = isChrominance ? kChromaThresholdAtMaximum : kLumaThresholdAtMaximum;
    const float fineThreshold = noiseSigma * multiplier * strength;
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
            // Chroma is allowed slightly more smoothing while still following
            // the same structure map, preventing colour bleeding over edges.
            const float protection = edge[x] * (isChrominance ? 0.72f : 0.86f);
            const float fineDetail = original[x] - fine[x];
            const float mediumDetail = fine[x] - coarse[x];
            target[x] = coarse[x] +
                softThreshold(mediumDetail, mediumThreshold * (1.0f - protection)) +
                softThreshold(fineDetail, fineThreshold * (1.0f - protection));
        }
    }
    // Balance positive/negative changes, preserving DC without adding a global
    // offset to unchanged black areas. Corrections move toward the original,
    // never beyond the original/candidate range. Luminance is nonnegative;
    // signed chroma must not be clamped. This is not an exposure rescale.
    double positive = 0.0, negative = 0.0;
    for (int y = 0; y < plane.rows; ++y) {
        const float *original = plane.ptr<float>(y);
        float *target = result.ptr<float>(y);
        for (int x = 0; x < plane.cols; ++x) {
            if (!isChrominance)
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
            yRow[x] = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
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
            // Inverse of Rec.709 Y with Co=R-B, Cg=G-(R+B)/2.
            const cv::Vec3f deviation(0.4298f * co - 0.7152f * cg,
                                     -0.0702f * co + 0.2848f * cg,
                                     -0.5702f * co - 0.7152f * cg);
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

    const float lumaSigma = estimateDenoiseSigma(luma);
    cv::Mat lumaGuide;
    cv::GaussianBlur(luma, lumaGuide, cv::Size(), kFineSigma, kFineSigma,
                     cv::BORDER_REPLICATE);
    const cv::Mat protection = structureProtection(lumaGuide, lumaSigma);

    if (settings.luminance > 0)
        luma = denoisePlane(luma, protection, lumaSigma,
                            settings.luminance, false);
    if (settings.chrominance > 0) {
        chromaOrange = denoisePlane(chromaOrange, protection,
                                    estimateDenoiseSigma(chromaOrange),
                                    settings.chrominance, true);
        chromaGreen = denoisePlane(chromaGreen, protection,
                                   estimateDenoiseSigma(chromaGreen),
                                   settings.chrominance, true);
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
