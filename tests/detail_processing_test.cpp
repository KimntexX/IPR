#include "DetailProcessing.h"
#include "Pixels.h"
#include "image_testing.h"
#include "WorkingColorSpace.h"
#include "Resize.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {

float median(std::vector<float> values)
{
    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

ipr::Image component(const ipr::Image &rgb, int kind = 0)
{
    ipr::Image result = ipr::Image::create({rgb.width(), rgb.height()}, ipr::Depth::F32, 1);
    for (int y = 0; y < rgb.height(); ++y) {
        const auto *source = rgb.row<ipr::Vec3f>(y);
        float *row = result.mutableRow<float>(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const auto &p = source[x];
            row[x] = kind == 0 ? WorkingColor::luminance(p) :
                     kind == 1 ? p[0] - p[2] : p[1] - 0.5f*(p[0]+p[2]);
        }
    }
    return result;
}

double sigma(const ipr::Image &plane)
{
    std::vector<float> values;
    for (int y = 0; y + 1 < plane.height(); y += 2) {
        const float *first = plane.row<float>(y), *second = plane.row<float>(y + 1);
        for (int x = 0; x + 1 < plane.width(); x += 2)
            values.push_back((first[x]-first[x+1]-second[x]+second[x+1])*0.5f);
    }
    const float center = median(values);
    for (float &v : values)
        v = std::abs(v - center);
    return median(values) / 0.67448975;
}

double chromaSigma(const ipr::Image &image)
{
    return std::hypot(sigma(component(image, 1)), sigma(component(image, 2))) / std::sqrt(2.);
}

ipr::Image noisy(float signal, float noise, bool colored)
{
    std::mt19937 rng(812);
    std::normal_distribution<float> gaussian(0.f, noise);
    ipr::Image result = ipr::Image::create({256, 256}, ipr::Depth::F32, 3);
    for (int y = 0; y < result.height(); ++y) {
        auto *row = result.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < result.width(); ++x) {
            const float v = std::max(0.f, signal + gaussian(rng));
            if (colored) {
                const float g = std::max(0.f, signal + gaussian(rng));
                row[x] = ipr::Vec3f(v, g, std::max(0.f, signal + gaussian(rng)));
            } else {
                row[x] = ipr::Vec3f(v, v, v);
            }
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
    const ipr::Image original = noisy(.05f, .005f, true);
    auto zero = original.clone();
    DetailProcessing::reduceNoise(zero, {0, 0});
    passed &= check(ipr::largestDifference(zero, original) == 0., "zero must be bit-exact");
    auto sharpZero = original.clone();
    DetailProcessing::sharpen(sharpZero, {0, 1.f, 0});
    passed &= check(ipr::largestDifference(sharpZero, original) == 0., "zero sharpening must be bit-exact");

    for (const ipr::Vec3f color : {ipr::Vec3f(0, 0, 0), ipr::Vec3f(.02f, .1f, .3f), ipr::Vec3f(2.f, 1.5f, .75f)}) {
        ipr::Image flat = imagetest::filledWith({64, 64}, {color[0], color[1], color[2]});
        auto filtered = flat.clone();
        DetailProcessing::reduceNoise(filtered, {100, 100});
        passed &= check(ipr::largestDifference(filtered, flat) < 5e-7, "constants and HDR must be preserved");
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
    passed &= check(ipr::largestDifference(component(chroma), component(original)) < 2e-7,
                    "color-only processing must preserve Rec.709 luminance per pixel");

    const auto dark = noisy(.005f, .006f, true);
    for (const auto settings : {DetailProcessing::DenoiseSettings{100, 0}, {0, 100}, {100, 100}}) {
        auto filtered = dark.clone();
        DetailProcessing::reduceNoise(filtered, settings);
        const double drift = ipr::meanOf(component(filtered))[0] / ipr::meanOf(component(dark))[0] - 1.;
        std::cout << "dark luma=" << settings.luminance << " color=" << settings.chrominance
                  << " relative_mean_drift=" << drift << '\n';
        passed &= check(std::abs(drift) < .001, "dark-field mean luminance should be stable within 0.1 percent");
        passed &= check(imagetest::within(filtered, 0., 10.), "denoised pixels must be finite and nonnegative");
    }

    // A genuine strong edge must survive even though flat-field grain does not.
    auto edge = noisy(.08f, .003f, false);
    imagetest::addTo(edge, {128, 0, 128, edge.height()}, {.6f, .6f, .6f});
    auto filteredEdge = edge.clone();
    DetailProcessing::reduceNoise(filteredEdge, {100, 0});
    const double before = ipr::meanOf(imagetest::columns(component(edge), 129, 135))[0] -
                          ipr::meanOf(imagetest::columns(component(edge), 121, 127))[0];
    const double after = ipr::meanOf(imagetest::columns(component(filteredEdge), 129, 135))[0] -
                         ipr::meanOf(imagetest::columns(component(filteredEdge), 121, 127))[0];
    std::cout << "strong_edge_contrast_ratio=" << after / before << '\n';
    passed &= check(after / before > .98, "strong edge contrast must remain above 98 percent");

    auto mixed = noisy(.005f, .006f, true);
    {
        ipr::Image black = mixed.mutableView({0, 0, 128, mixed.height()});
        ipr::fill(black, 0.0);
    }
    auto mixedFiltered = mixed.clone();
    DetailProcessing::reduceNoise(mixedFiltered, {100, 100});
    passed &= check(imagetest::largestAbsolute(imagetest::columns(mixedFiltered, 0, 64)) < 1e-8,
                    "a black area must not acquire a pedestal from DC restoration");
    passed &= check(sigma(component(imagetest::columns(mixedFiltered, 144, 256))) <
                    .5 * sigma(component(imagetest::columns(mixed, 144, 256))),
                    "a large constant black area must not disable denoising in noisy regions");
    passed &= check(std::abs(ipr::meanOf(component(mixedFiltered))[0] / ipr::meanOf(component(mixed))[0] - 1.) < .001,
                    "mixed black/noisy fields must preserve mean luminance");

    // Same Y on both sides: a chromatic border must not require a luma edge.
    auto colorEdge = noisy(.02f, .002f, true);
    const ipr::Vec3f firstColor(.4f, .05f, .05f);
    const float edgeY = WorkingColor::luminance(firstColor);
    const auto &weights = WorkingColor::lumaWeights();
    const ipr::Vec3f secondColor(.05f, (edgeY - weights[0]*.05f - weights[2]*.1f)/weights[1], .1f);
    for (int y = 0; y < colorEdge.height(); ++y) {
        auto *row = colorEdge.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < colorEdge.width(); ++x)
            row[x] += x < 128 ? firstColor : secondColor;
    }
    auto colorEdgeFiltered = colorEdge.clone();
    DetailProcessing::reduceNoise(colorEdgeFiltered, {0, 100});
    const double colorBefore = ipr::meanOf(imagetest::columns(component(colorEdge, 1), 121, 127))[0] -
                               ipr::meanOf(imagetest::columns(component(colorEdge, 1), 129, 135))[0];
    const double colorAfter = ipr::meanOf(imagetest::columns(component(colorEdgeFiltered, 1), 121, 127))[0] -
                              ipr::meanOf(imagetest::columns(component(colorEdgeFiltered, 1), 129, 135))[0];
    std::cout << "isoluminant_color_edge_ratio=" << colorAfter / colorBefore << '\n';
    passed &= check(colorAfter / colorBefore > .98, "isoluminant color borders must be preserved");
    passed &= check(ipr::largestDifference(component(colorEdgeFiltered), component(colorEdge)) < 2e-7,
                    "color-border processing must preserve per-pixel Y");

    // Luminance grain on a saturated colour must stay grain, not become
    // saturation speckle: the colour denoise works on chroma relative to Y.
    ipr::Image grain = ipr::Image::create({256, 256}, ipr::Depth::F32, 3);
    {
        std::mt19937 rng(9);
        std::normal_distribution<float> gaussian(0.f, .25f);
        const ipr::Vec3f orange(.40f, .20f, .05f);
        for (int y = 0; y < grain.height(); ++y)
            for (int x = 0; x < grain.width(); ++x)
                imagetest::at<ipr::Vec3f>(grain, y, x) = orange * std::max(.05f, 1.f + gaussian(rng));
    }
    auto grainFiltered = grain.clone();
    DetailProcessing::reduceNoise(grainFiltered, {0, 100});
    const auto relativeChromaSpread = [&](const ipr::Image &image) {
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(ipr::divided(component(image, 1), component(image, 0)), mean, deviation);
        return deviation[0] / std::abs(mean[0]);
    };
    std::cout << "luminance_grain_relative_chroma_spread=" << relativeChromaSpread(grainFiltered) << '\n';
    passed &= check(relativeChromaSpread(grainFiltered) < .01,
                    "luminance grain must keep its hue and saturation after colour denoise");
    passed &= check(ipr::largestDifference(component(grainFiltered), component(grain)) < 2e-6,
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
            passed &= check(ipr::largestDifference(component(filtered), component(original)) < 2e-7,
                            "colour denoise must preserve per-pixel Y at every scale");
            previousChroma = ratio;
        }
    }

    // Images smaller than the filters must stay finite and keep their luminance.
    for (const ipr::Size size : {ipr::Size(1, 1), ipr::Size(2, 3), ipr::Size(7, 5)}) {
        for (const double scale : {1.0, 0.25}) {
            ipr::Image tiny = noisy(.05f, .005f, true).copy(ipr::Recti(ipr::Point2i(), size));
            const auto tinyOriginal = tiny.clone();
            DetailProcessing::reduceNoise(tiny, {100, 100}, scale);
            passed &= check(tiny.size() == size && imagetest::within(tiny, 0., 10.),
                            "tiny images must stay finite and nonnegative");
            passed &= check(std::abs(ipr::meanOf(component(tiny))[0] / ipr::meanOf(component(tinyOriginal))[0] - 1.) < .05,
                            "tiny images must keep their mean luminance");
        }
    }

    // The preview sharpens the reduced image: with its radius scaled to the
    // reduction, it must approach the export sharpened then reduced. Chart: a
    // slightly noisy flat field, isolated lines 3 to 24 px wide and a hard edge
    // (audit C-01).
    {
        ipr::Image chart = ipr::Image::create({1200, 1200}, ipr::Depth::F32, 3);
        std::mt19937 rng(41);
        std::normal_distribution<float> gaussian(.1f, .002f);
        ipr::mapInPlace(chart, [&](float) { return gaussian(rng); });
        int x = 120;
        for (int width : {3, 6, 12, 24}) {
            imagetest::addTo(chart, {x, 0, width, chart.height()}, {.35f, .3f, .25f});
            x += 180;
        }
        imagetest::addTo(chart, {1020, 0, 180, chart.height()}, {.5f, .5f, .5f});
        for (const float radius : {0.5f, 1.0f, 2.0f}) {
            for (const int reduction : {2, 3}) {
                const DetailProcessing::SharpenSettings settings{100, radius, 0};
                const ipr::Size reduced(chart.width() / reduction, chart.height() / reduction);
                auto full = chart.clone();
                DetailProcessing::sharpen(full, settings);
                ipr::Image reference, proxy;
                reference = ipr::resized(full, reduced, ipr::Interpolation::Area);
                proxy = ipr::resized(chart, reduced, ipr::Interpolation::Area);
                const auto distance = [&](const ipr::Image &image) {
                    return imagetest::sumOfDifferences(image, reference) / (double(image.width()) * image.height());
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
        passed &= check(ipr::largestDifference(identity, full) == 0., "full resolution is unchanged");
    }

    // Sharpening calibrated on Lightroom (15 November 2026): on a hard edge, the contrast across
    // it grows with the amount, and past the two levels the overshoot stays within 30 % of the
    // step, in the sRGB-encoded luminance the sharpening works on.
    {
        const auto encoded = [](float linear) {
            return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        };
        const float dark = .05f, light = .4f;
        ipr::Image edge = imagetest::filledWith({64, 32}, {dark, dark, dark});
        imagetest::addTo(edge, {32, 0, 32, 32}, {light - dark, light - dark, light - dark});
        const float low = encoded(dark), high = encoded(light), step = high - low;
        double previous = 0.0;
        for (const int amount : {25, 50, 100, 150, 200}) {
            auto sharpened = edge.clone();
            DetailProcessing::sharpen(sharpened, {amount, 1.0f, 0});
            const ipr::Image luma = component(sharpened);
            float minimum = 10.f, maximum = -10.f;
            for (int x = 0; x < luma.width(); ++x) {
                const float value = encoded(luma.row<float>(16)[x]);
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
            const double across = encoded(luma.row<float>(16)[32]) - encoded(luma.row<float>(16)[31]);
            passed &= check(across > previous, "the contrast across an edge grows with the amount");
            passed &= check(maximum <= high + .3f * step + 1e-4f && minimum >= low - .3f * step - 1e-4f,
                            "the overshoot stays within 30 % of the step");
            passed &= check(maximum > high && minimum < low, "the sharpening overshoots on both sides of an edge");
            previous = across;
        }
        // Radius and masking calibrated on Lightroom (16 November 2026), on a fine texture a few times
        // the grain, in a lightly grained flat field the noise measure sees: a radius of 0.5 still
        // sharpens it (it did not before), a radius of 3 no more than 1 (it did twice as much), and
        // masking 25 keeps part of it (it kept almost nothing).
        ipr::Image texture = ipr::Image::create({256, 64}, ipr::Depth::F32, 3);
        std::mt19937 textureRng(17);
        std::normal_distribution<float> flatGrain(0.f, .002f);
        for (int y = 0; y < texture.height(); ++y)
            for (int x = 0; x < texture.width(); ++x) {
                const float v = .2f + flatGrain(textureRng) + (x >= 112 && x < 144 ? .008f * float(std::sin(x * 0.9)) : 0.f);
                texture.mutableRow<ipr::Vec3f>(y)[x] = ipr::Vec3f(v, v, v);
            }
        // RMS of the encoded luminance step across the texture, against the unsharpened one, minus 1.
        const auto sharpened = [&](int masking, float radius) {
            auto image = texture.clone();
            if (masking >= 0)
                DetailProcessing::sharpen(image, {75, radius, masking});
            const ipr::Image luma = component(image);
            double sum = 0.0;
            int count = 0;
            for (int y = 8; y < 56; ++y)
                for (int x = 116; x < 140; ++x) {
                    const double d = encoded(luma.row<float>(y)[x]) - encoded(luma.row<float>(y)[x - 1]);
                    sum += d * d;
                    ++count;
                }
            return std::sqrt(sum / count);
        };
        const double unsharpened = sharpened(-1, 1.f);
        const auto effect = [&](int masking, float radius) { return sharpened(masking, radius) / unsharpened - 1.0; };
        const double atRadiusOne = effect(0, 1.f);
        passed &= check(atRadiusOne > .5, "the texture is sharpened at radius 1");
        passed &= check(effect(0, .5f) > .5 * atRadiusOne, "a radius of 0.5 still sharpens a fine texture");
        passed &= check(effect(0, 3.f) <= atRadiusOne, "a radius of 3 sharpens a fine texture no more than 1");
        passed &= check(effect(25, 1.f) > .2 * atRadiusOne && effect(75, 1.f) < .05 * atRadiusOne,
                        "masking 25 keeps part of a texture, 75 spares it with the grain");
        // Masking 100 spares a flat grain that masking 0 sharpens.
        const ipr::Image grain = noisy(.1f, .004f, false);
        auto unmasked = grain.clone(), masked = grain.clone();
        DetailProcessing::sharpen(unmasked, {100, 1.0f, 0});
        DetailProcessing::sharpen(masked, {100, 1.0f, 100});
        const double before = sigma(component(grain));
        passed &= check(sigma(component(unmasked)) > 1.1 * before, "masking 0 sharpens a strong grain");
        passed &= check(sigma(component(masked)) < 1.02 * before, "masking 100 spares a flat grain");
    }

    // Coloured HDR noise must not be clipped to SDR by reconstruction.
    auto hdr = noisy(2.f, .02f, true);
    DetailProcessing::reduceNoise(hdr, {100, 100});
    passed &= check(ipr::meanOf(component(hdr))[0] > 1.9 && ipr::allFinite(hdr), "no upper HDR clamp");
    ipr::Image invalid = ipr::Image::create({4, 4}, ipr::Depth::U8, 3);
    ipr::fill(invalid, std::vector<double>{23, 44, 51});
    const auto invalidCopy = invalid.clone();
    DetailProcessing::reduceNoise(invalid, {100, 100});
    passed &= check(ipr::largestDifference(invalid, invalidCopy) == 0., "unsupported types must be ignored safely");
    ipr::Image empty;
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
