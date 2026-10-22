#include "DetailProcessing.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

float median(std::vector<float> values)
{
    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

cv::Mat component(const cv::Mat &rgb, int kind = 0)
{
    cv::Mat result(rgb.rows, rgb.cols, CV_32F);
    for (int y = 0; y < rgb.rows; ++y) {
        const auto *source = rgb.ptr<cv::Vec3f>(y);
        float *row = result.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const auto &p = source[x];
            row[x] = kind == 0 ? WorkingColor::luminance(p) :
                     kind == 1 ? p[0] - p[2] : p[1] - 0.5f*(p[0]+p[2]);
        }
    }
    return result;
}

double sigma(const cv::Mat &plane)
{
    std::vector<float> values;
    for (int y = 0; y + 1 < plane.rows; y += 2) {
        const float *first = plane.ptr<float>(y), *second = plane.ptr<float>(y + 1);
        for (int x = 0; x + 1 < plane.cols; x += 2)
            values.push_back((first[x]-first[x+1]-second[x]+second[x+1])*0.5f);
    }
    const float center = median(values);
    for (float &v : values)
        v = std::abs(v - center);
    return median(values) / 0.67448975;
}

double chromaSigma(const cv::Mat &image)
{
    return std::hypot(sigma(component(image, 1)), sigma(component(image, 2))) / std::sqrt(2.);
}

cv::Mat noisy(float signal, float noise, bool colored)
{
    cv::RNG rng(812);
    cv::Mat result(256, 256, CV_32FC3);
    for (int y = 0; y < result.rows; ++y) {
        auto *row = result.ptr<cv::Vec3f>(y);
        for (int x = 0; x < result.cols; ++x) {
            const float v = std::max(0.f, signal + static_cast<float>(rng.gaussian(noise)));
            row[x] = colored ? cv::Vec3f(v,
                std::max(0.f, signal + static_cast<float>(rng.gaussian(noise))),
                std::max(0.f, signal + static_cast<float>(rng.gaussian(noise)))) : cv::Vec3f(v, v, v);
        }
    }
    return result;
}

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

}

int main()
{
    bool passed = true;
    const cv::Mat original = noisy(.05f, .005f, true);
    auto zero = original.clone();
    DetailProcessing::reduceNoise(zero, {0, 0});
    passed &= check(cv::norm(zero, original, cv::NORM_INF) == 0., "zero must be bit-exact");
    auto sharpZero = original.clone();
    DetailProcessing::sharpen(sharpZero, {0, 1.f, 0});
    passed &= check(cv::norm(sharpZero, original, cv::NORM_INF) == 0., "zero sharpening must be bit-exact");

    for (const cv::Vec3f color : {cv::Vec3f(0, 0, 0), cv::Vec3f(.02f, .1f, .3f), cv::Vec3f(2.f, 1.5f, .75f)}) {
        cv::Mat flat(64, 64, CV_32FC3, cv::Scalar(color[0], color[1], color[2]));
        auto filtered = flat.clone();
        DetailProcessing::reduceNoise(filtered, {100, 100});
        passed &= check(cv::norm(filtered, flat, cv::NORM_INF) < 5e-7, "constants and HDR must be preserved");
    }

    for (float level : {.0001f, .005f}) {
        const auto image = noisy(.05f, level, false);
        const double baseline = sigma(component(image));
        double previous = 1.;
        for (int value : {1, 5, 10, 25, 50, 75, 100}) {
            auto filtered = image.clone();
            DetailProcessing::reduceNoise(filtered, {value, 0});
            const double ratio = sigma(component(filtered)) / baseline;
            std::cout << "gray noise=" << level << " amount=" << value << " sigma_ratio=" << ratio << '\n';
            passed &= check(ratio <= previous + .003, "noise reduction should progress on an unstructured gray field");
            if (value == 1)
                passed &= check(ratio > .9 && ratio < 1., "activation at 1 must be gentle, including low-noise inputs");
            if (value == 100)
                passed &= check(ratio < .35, "high strength should denoise gray fields instead of protecting their grain");
            previous = ratio;
        }
    }

    auto chroma = original.clone();
    DetailProcessing::reduceNoise(chroma, {0, 100});
    const double cRatio = chromaSigma(chroma) / chromaSigma(original);
    std::cout << "chroma sigma_ratio=" << cRatio << '\n';
    passed &= check(cRatio < .3, "chroma noise on a flat field should be reduced");
    passed &= check(cv::norm(component(chroma), component(original), cv::NORM_INF) < 2e-7,
                    "color-only processing must preserve Rec.709 luminance per pixel");

    const auto dark = noisy(.005f, .006f, true);
    for (const auto settings : {DetailProcessing::DenoiseSettings{100, 0}, {0, 100}, {100, 100}}) {
        auto filtered = dark.clone();
        DetailProcessing::reduceNoise(filtered, settings);
        const double drift = cv::mean(component(filtered))[0] / cv::mean(component(dark))[0] - 1.;
        std::cout << "dark luma=" << settings.luminance << " color=" << settings.chrominance
                  << " relative_mean_drift=" << drift << '\n';
        passed &= check(std::abs(drift) < .001, "dark-field mean luminance should be stable within 0.1 percent");
        passed &= check(cv::checkRange(filtered, true, nullptr, 0., 10.), "denoised pixels must be finite and nonnegative");
    }

    // A genuine strong edge must survive even though flat-field grain does not.
    auto edge = noisy(.08f, .003f, false);
    edge.colRange(128, 256) += cv::Scalar(.6f, .6f, .6f);
    auto filteredEdge = edge.clone();
    DetailProcessing::reduceNoise(filteredEdge, {100, 0});
    const double before = cv::mean(component(edge).colRange(129, 135))[0] -
                          cv::mean(component(edge).colRange(121, 127))[0];
    const double after = cv::mean(component(filteredEdge).colRange(129, 135))[0] -
                         cv::mean(component(filteredEdge).colRange(121, 127))[0];
    std::cout << "strong_edge_contrast_ratio=" << after / before << '\n';
    passed &= check(after / before > .98, "strong edge contrast must remain above 98 percent");

    auto mixed = noisy(.005f, .006f, true);
    mixed.colRange(0, 128).setTo(cv::Scalar(0, 0, 0));
    auto mixedFiltered = mixed.clone();
    DetailProcessing::reduceNoise(mixedFiltered, {100, 100});
    passed &= check(cv::norm(mixedFiltered.colRange(0, 64), cv::NORM_INF) < 1e-8,
                    "a black area must not acquire a pedestal from DC restoration");
    passed &= check(sigma(component(mixedFiltered.colRange(144, 256))) <
                    .5 * sigma(component(mixed.colRange(144, 256))),
                    "a large constant black area must not disable denoising in noisy regions");
    passed &= check(std::abs(cv::mean(component(mixedFiltered))[0] / cv::mean(component(mixed))[0] - 1.) < .001,
                    "mixed black/noisy fields must preserve mean luminance");

    // Same Y on both sides: a chromatic border must not require a luma edge.
    auto colorEdge = noisy(.02f, .002f, true);
    const cv::Vec3f firstColor(.4f, .05f, .05f);
    const float edgeY = WorkingColor::luminance(firstColor);
    const auto &weights = WorkingColor::lumaWeights();
    const cv::Vec3f secondColor(.05f, (edgeY - weights[0]*.05f - weights[2]*.1f)/weights[1], .1f);
    for (int y = 0; y < colorEdge.rows; ++y) {
        auto *row = colorEdge.ptr<cv::Vec3f>(y);
        for (int x = 0; x < colorEdge.cols; ++x)
            row[x] += x < 128 ? firstColor : secondColor;
    }
    auto colorEdgeFiltered = colorEdge.clone();
    DetailProcessing::reduceNoise(colorEdgeFiltered, {0, 100});
    const double colorBefore = cv::mean(component(colorEdge, 1).colRange(121, 127))[0] -
                               cv::mean(component(colorEdge, 1).colRange(129, 135))[0];
    const double colorAfter = cv::mean(component(colorEdgeFiltered, 1).colRange(121, 127))[0] -
                              cv::mean(component(colorEdgeFiltered, 1).colRange(129, 135))[0];
    std::cout << "isoluminant_color_edge_ratio=" << colorAfter / colorBefore << '\n';
    passed &= check(colorAfter / colorBefore > .98, "isoluminant color borders must be preserved");
    passed &= check(cv::norm(component(colorEdgeFiltered), component(colorEdge), cv::NORM_INF) < 2e-7,
                    "color-border processing must preserve per-pixel Y");

    // Coloured HDR noise must not be clipped to SDR by reconstruction.
    auto hdr = noisy(2.f, .02f, true);
    DetailProcessing::reduceNoise(hdr, {100, 100});
    passed &= check(cv::mean(component(hdr))[0] > 1.9 && cv::checkRange(hdr), "no upper HDR clamp");
    cv::Mat invalid(4, 4, CV_8UC3, cv::Scalar(23, 44, 51));
    const auto invalidCopy = invalid.clone();
    DetailProcessing::reduceNoise(invalid, {100, 100});
    passed &= check(cv::norm(invalid, invalidCopy, cv::NORM_INF) == 0., "unsupported types must be ignored safely");
    cv::Mat empty;
    DetailProcessing::reduceNoise(empty, {100, 100});
    passed &= check(empty.empty(), "empty inputs must be safe");
    return passed ? 0 : 1;
}
