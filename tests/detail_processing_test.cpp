#include "DetailProcessing.h"
#include "WorkingColorSpace.h"
#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
        const auto *source = rgb.ptr<ipr::Vec3f>(y);
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
        auto *row = result.ptr<ipr::Vec3f>(y);
        for (int x = 0; x < result.cols; ++x) {
            const float v = std::max(0.f, signal + static_cast<float>(rng.gaussian(noise)));
            row[x] = colored ? ipr::Vec3f(v,
                std::max(0.f, signal + static_cast<float>(rng.gaussian(noise))),
                std::max(0.f, signal + static_cast<float>(rng.gaussian(noise)))) : ipr::Vec3f(v, v, v);
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

    for (const ipr::Vec3f color : {ipr::Vec3f(0, 0, 0), ipr::Vec3f(.02f, .1f, .3f), ipr::Vec3f(2.f, 1.5f, .75f)}) {
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
    const ipr::Vec3f firstColor(.4f, .05f, .05f);
    const float edgeY = WorkingColor::luminance(firstColor);
    const auto &weights = WorkingColor::lumaWeights();
    const ipr::Vec3f secondColor(.05f, (edgeY - weights[0]*.05f - weights[2]*.1f)/weights[1], .1f);
    for (int y = 0; y < colorEdge.rows; ++y) {
        auto *row = colorEdge.ptr<ipr::Vec3f>(y);
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

    // Luminance grain on a saturated colour must stay grain, not become
    // saturation speckle: the colour denoise works on chroma relative to Y.
    cv::Mat grain(256, 256, CV_32FC3);
    {
        cv::RNG rng(9);
        const ipr::Vec3f orange(.40f, .20f, .05f);
        for (int y = 0; y < grain.rows; ++y)
            for (int x = 0; x < grain.cols; ++x)
                ipr::at<ipr::Vec3f>(grain, y, x) = orange * std::max(.05f, 1.f + static_cast<float>(rng.gaussian(.25)));
    }
    auto grainFiltered = grain.clone();
    DetailProcessing::reduceNoise(grainFiltered, {0, 100});
    const auto relativeChromaSpread = [&](const cv::Mat &image) {
        cv::Mat ratio;
        cv::divide(component(image, 1), component(image, 0), ratio);
        cv::Scalar mean, deviation;
        cv::meanStdDev(ratio, mean, deviation);
        return deviation[0] / std::abs(mean[0]);
    };
    std::cout << "luminance_grain_relative_chroma_spread=" << relativeChromaSpread(grainFiltered) << '\n';
    passed &= check(relativeChromaSpread(grainFiltered) < .01,
                    "luminance grain must keep its hue and saturation after colour denoise");
    passed &= check(cv::norm(component(grainFiltered), component(grain), cv::NORM_INF) < 2e-6,
                    "colour denoise must leave luminance grain untouched");

    // Colour denoise strength follows the setting, at full and preview scales
    // (the preview uses a 3 x 3 median and scaled filters).
    for (const double scale : {1.0, 0.25}) {
        double previousChroma = 1.;
        for (int value : {1, 10, 25, 50, 100}) {
            auto filtered = original.clone();
            DetailProcessing::reduceNoise(filtered, {0, value}, scale);
            const double ratio = chromaSigma(filtered) / chromaSigma(original);
            std::cout << "chroma scale=" << scale << " amount=" << value << " sigma_ratio=" << ratio << '\n';
            passed &= check(ratio <= previousChroma + .003, "colour denoise should progress with the setting");
            if (value == 1)
                passed &= check(ratio > .7, "colour denoise at 1 must stay gentle");
            if (value == 100)
                passed &= check(ratio < .3, "colour denoise at 100 should remove flat-field colour noise");
            passed &= check(cv::norm(component(filtered), component(original), cv::NORM_INF) < 2e-7,
                            "colour denoise must preserve per-pixel Y at every scale");
            previousChroma = ratio;
        }
    }

    // Images smaller than the filters must stay finite and keep their luminance.
    for (const ipr::Size size : {ipr::Size(1, 1), ipr::Size(2, 3), ipr::Size(7, 5)}) {
        for (const double scale : {1.0, 0.25}) {
            cv::Mat tiny = noisy(.05f, .005f, true)(ipr::toCv(ipr::Recti(ipr::Point2i(), size))).clone();
            const auto tinyOriginal = tiny.clone();
            DetailProcessing::reduceNoise(tiny, {100, 100}, scale);
            passed &= check(ipr::fromCv(tiny.size()) == size && cv::checkRange(tiny, true, nullptr, 0., 10.),
                            "tiny images must stay finite and nonnegative");
            passed &= check(std::abs(cv::mean(component(tiny))[0] / cv::mean(component(tinyOriginal))[0] - 1.) < .05,
                            "tiny images must keep their mean luminance");
        }
    }

    // The preview sharpens the reduced image: with its radius scaled to the
    // reduction, it must approach the export sharpened then reduced. Chart: a
    // slightly noisy flat field, isolated lines 3 to 24 px wide and a hard edge
    // (audit C-01).
    {
        cv::Mat chart(1200, 1200, CV_32FC3);
        cv::RNG rng(41);
        rng.fill(chart, cv::RNG::NORMAL, cv::Scalar(.1, .1, .1), cv::Scalar(.002, .002, .002));
        int x = 120;
        for (int width : {3, 6, 12, 24}) {
            chart.colRange(x, x + width) += cv::Scalar(.35f, .3f, .25f);
            x += 180;
        }
        chart.colRange(1020, 1200) += cv::Scalar(.5f, .5f, .5f);
        for (const float radius : {0.5f, 1.0f, 2.0f}) {
            for (const int reduction : {2, 3}) {
                const DetailProcessing::SharpenSettings settings{100, radius, 0};
                const ipr::Size reduced(chart.cols / reduction, chart.rows / reduction);
                auto full = chart.clone();
                DetailProcessing::sharpen(full, settings);
                cv::Mat reference, proxy;
                cv::resize(full, reference, ipr::toCv(reduced), 0, 0, cv::INTER_AREA);
                cv::resize(chart, proxy, ipr::toCv(reduced), 0, 0, cv::INTER_AREA);
                const auto distance = [&](const cv::Mat &image) {
                    return cv::norm(image, reference, cv::NORM_L1) / double(image.total());
                };
                auto unscaled = proxy.clone(), scaled = proxy.clone();
                DetailProcessing::sharpen(unscaled, settings);
                DetailProcessing::sharpen(scaled, settings, 1.0 / reduction);
                const double before = distance(unscaled), after = distance(scaled), none = distance(proxy);
                std::cout << "preview_sharpening radius=" << radius << " reduction=" << reduction
                          << " unscaled=" << before << " scaled=" << after << " unsharpened=" << none << '\n';
                passed &= check(after < before, "a scaled radius must bring the preview closer to the export");
                passed &= check(after <= none, "the preview's sharpening must never move it away from the export");
            }
        }
        const DetailProcessing::SharpenSettings settings{100, 1.0f, 0};
        auto full = chart.clone();
        DetailProcessing::sharpen(full, settings);
        auto identity = chart.clone();
        DetailProcessing::sharpen(identity, settings, 1.0);
        passed &= check(cv::norm(identity, full, cv::NORM_INF) == 0., "full resolution is unchanged");
    }

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

    // The law of the single slider.
    DetailProcessing::DenoiseSettings mapped{42, 43}, previous;
    bool lawValid = true;
    for (int amount = 0; amount <= 100; ++amount) {
        lawValid = DetailProcessing::denoiseFromAmount(amount, mapped) && lawValid;
        // Independent reference, including integer quantization.
        lawValid = lawValid && mapped.luminance == amount && mapped.chrominance >= amount &&
                   mapped.chrominance >= previous.chrominance &&
                   mapped.chrominance == int(std::lround(100. * (1. - std::pow(1. - double(amount) / 100., 6))));
        previous = mapped;
    }
    passed &= check(lawValid, "the slider law is bounded, monotone and matches all 101 states");
    passed &= check(DetailProcessing::denoiseFromAmount(0, mapped) && mapped.luminance == 0 && mapped.chrominance == 0,
                    "the slider at zero is exact identity");
    passed &= check(DetailProcessing::denoiseFromAmount(100, mapped) && mapped.luminance == 100 && mapped.chrominance == 100,
                    "the slider maximum reaches both maxima without extrapolation");
    passed &= check(DetailProcessing::denoiseFromAmount(25, mapped) && mapped.luminance == 25 && mapped.chrominance == 82,
                    "the slider raises the colour correction faster at small amounts");
    const DetailProcessing::DenoiseSettings saved = mapped;
    passed &= check(!DetailProcessing::denoiseFromAmount(-1, mapped) && !DetailProcessing::denoiseFromAmount(101, mapped) &&
                    !DetailProcessing::denoiseFromAmount(std::numeric_limits<int>::min(), mapped) &&
                    !DetailProcessing::denoiseFromAmount(std::numeric_limits<int>::max(), mapped) &&
                    mapped.luminance == saved.luminance && mapped.chrominance == saved.chrominance,
                    "an invalid amount is rejected and leaves the settings unchanged");
    return passed ? 0 : 1;
}
