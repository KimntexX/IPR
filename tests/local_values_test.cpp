#include "ColorPipeline.h"
#include <random>
#include "image_testing.h"
#include "Contrast.h"
#include "Dehaze.h"
#include "DetailProcessing.h"
#include "Exposure.h"
#include "LocalContrast.h"
#include "LocalValues.h"
#include "PerceptualColour.h"
#include "ToneRecovery.h"
#include "Pixels.h"

#include <functional>
#include <iostream>
#include <limits>

// Local values (ADR-0021): a module given a map of local amounts applies, at each
// pixel, its global value plus the local one. Without a map, or with a map of
// zeros, it renders exactly as before; inside a zone, a local +X renders as a
// global +X would.
namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

// Colours, greys and extended values over 24 x 16 pixels.
ipr::Image scene()
{
    ipr::Image image = ipr::Image::create({24, 16}, ipr::Depth::F32, 3);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            imagetest::at<ipr::Vec3f>(image, y, x) = (x + y) % 5 == 0
                ? ipr::Vec3f(0.02f * x, 0.02f * x, 0.02f * x)
                : ipr::Vec3f(0.01f + 0.04f * x, 0.03f + 0.05f * y, 0.2f + 0.01f * (x ^ y));
    return image;
}

// A noisy scene large enough for the neighbourhood modules: their noise and
// statistics estimators need samples.
ipr::Image noisyScene()
{
    ipr::Image image = ipr::Image::create({128, 96}, ipr::Depth::F32, 3);
    std::mt19937 noise(21);
    std::normal_distribution<float> common(0.f, 0.01f), own(0.f, 0.006f);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const ipr::Vec3f base(0.05f + 0.005f * x, 0.08f + 0.006f * y, 0.3f - 0.001f * x);
            const float shared = common(noise);
            const float r = own(noise), g = own(noise), b = own(noise);
            imagetest::at<ipr::Vec3f>(image, y, x) = ipr::Vec3f(
                std::max(0.001f, base[0] + shared + r),
                std::max(0.001f, base[1] + shared + g),
                std::max(0.001f, base[2] + shared + b));
        }
    return image;
}

// The local amount over the left half, nothing over the right half.
ipr::Image leftHalf(const ipr::Size &size, float amount)
{
    ipr::Image map = ipr::filled(size, ipr::Depth::F32, 1, 0);
    ipr::Image left = map.mutableView(ipr::Recti(0, 0, size.width / 2, size.height));
    ipr::fill(left, amount);
    return map;
}

double halfDifference(const ipr::Image &a, const ipr::Image &b, bool left)
{
    const ipr::Recti half(left ? 0 : a.width() / 2, 0, a.width() / 2, a.height());
    return ipr::largestDifference(a.copy(half), b.copy(half));
}

using Module = std::function<void(ipr::Image &, int global, const ipr::Image &local)>;

// The three properties every module with local values keeps.
bool keepsContract(const char *name, const Module &apply, int global, int local, const ipr::Image &source = scene())
{
    ipr::Image plain = source.clone(), zeros = source.clone(), withMap = source.clone(), shifted = source.clone();
    apply(plain, global, {});
    apply(zeros, global, ipr::filled(source.size(), ipr::Depth::F32, 1, 0));
    apply(withMap, global, leftHalf(source.size(), float(local)));
    apply(shifted, global + local, {});
    bool ok = check(ipr::largestDifference(plain, zeros) == 0, "a map of zeros renders exactly as no map");
    ok &= check(ipr::largestDifference(shifted, plain) > 1e-3, "the tested value has an effect on the scene");
    ok &= check(halfDifference(withMap, shifted, true) < 1e-5, "inside the zone, a local value acts as a global one");
    ok &= check(halfDifference(withMap, plain, false) == 0, "outside the zone, nothing changes");
    if (!ok) std::cerr << "  in " << name << '\n';
    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= check(LocalValues::fits({}, {4, 3}) && LocalValues::fits(ipr::Image::create({4, 3}, ipr::Depth::F32, 1), {4, 3}) &&
                !LocalValues::fits(ipr::Image::create({4, 4}, ipr::Depth::F32, 1), {4, 3}) && !LocalValues::fits(ipr::Image::create({4, 3}, ipr::Depth::U8, 1), {4, 3}),
                "a map fits when empty or one float per pixel of the image");
    ok &= keepsContract("exposure", [](ipr::Image &image, int global, const ipr::Image &local) {
        Exposure::apply(image, global / 10.0f, local.empty() ? ipr::Image() : ipr::mapped(local, [](float v) { return v * float(1.0 / 10.0) + 0.0f; }));
    }, -5, 15);
    ok &= keepsContract("contrast", [](ipr::Image &image, int global, const ipr::Image &local) {
        Contrast::apply(image, global, local);
    }, 20, 35);
    ok &= keepsContract("negative contrast", [](ipr::Image &image, int global, const ipr::Image &local) {
        Contrast::apply(image, global, local);
    }, 0, -60);
    ok &= keepsContract("saturation", [](ipr::Image &image, int global, const ipr::Image &local) {
        PerceptualColour::Parameters parameters;
        parameters.saturation = global;
        LocalValues::Maps maps;
        maps[LocalValues::Saturation] = local.share();
        PerceptualColour::apply(image, parameters, maps);
    }, 10, 40);
    ok &= keepsContract("vibrance", [](ipr::Image &image, int global, const ipr::Image &local) {
        PerceptualColour::Parameters parameters;
        parameters.vibrance = global;
        parameters.mixer.hue[2] = 30;   // the mixer reads the pixel vibrance produced
        LocalValues::Maps maps;
        maps[LocalValues::Vibrance] = local.share();
        PerceptualColour::apply(image, parameters, maps);
    }, 0, 50);
    const ToneRecovery::Statistics statistics = ToneRecovery::typicalStatistics();
    const auto toneModule = [&](LocalValues::Parameter parameter, float ToneRecovery::Amounts::*amount) {
        return [&statistics, parameter, amount](ipr::Image &image, int global, const ipr::Image &local) {
            ToneRecovery::Amounts amounts;
            amounts.*amount = float(global) / 100.0f;
            LocalValues::Maps maps;
            maps[parameter] = local.share();
            ToneRecovery::apply(image, amounts, statistics, maps);
        };
    };
    ok &= keepsContract("shadows", toneModule(LocalValues::Shadows, &ToneRecovery::Amounts::shadows), 20, 30);
    ok &= keepsContract("blacks", toneModule(LocalValues::Blacks, &ToneRecovery::Amounts::blacks), -10, -40);
    ok &= keepsContract("highlights", toneModule(LocalValues::Highlights, &ToneRecovery::Amounts::highlights), -30, -50);
    ok &= keepsContract("whites", toneModule(LocalValues::Whites, &ToneRecovery::Amounts::whites), 0, 60);
    {
        // Two controls at once: the zone of each map follows its own value.
        const ipr::Image source = scene();
        ipr::Image both = source.clone(), expected = source.clone();
        LocalValues::Maps maps;
        maps[LocalValues::Shadows] = leftHalf(source.size(), 40);
        maps[LocalValues::Highlights] = leftHalf(source.size(), -60);
        ToneRecovery::apply(both, {}, statistics, maps);
        ToneRecovery::Amounts amounts;
        amounts.shadows = 0.4f;
        amounts.highlights = -0.6f;
        ToneRecovery::apply(expected, amounts, statistics);
        ok &= check(halfDifference(both, expected, true) < 1e-5 && halfDifference(both, source, false) == 0,
                    "local tone controls combine as the global ones do");
    }
    ok &= keepsContract("temperature", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Temperature] = local.empty() ? ipr::Image() : ipr::mapped(local, [](float v) { return v * 10.0f + 0.0f; });
        ColorPipeline::applyTemperatureTint(image, 5000 + 10 * global, 5000.0, 0, nullptr, maps);
    }, 20, 150);
    ok &= keepsContract("tint", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Tint] = local.share();
        ColorPipeline::applyTemperatureTint(image, 6000, 5000.0, global, nullptr, maps);
    }, 10, -40);
    // Neighbourhood modules: the image-wide analysis does not move with the local value.
    const ipr::Image noisy = noisyScene();
    const auto localContrastModule = [](LocalValues::Parameter parameter) {
        return [parameter](ipr::Image &image, int global, const ipr::Image &local) {
            LocalValues::Maps maps;
            maps[parameter] = local.share();
            LocalContrast::apply(image, parameter == LocalValues::Texture ? global : 0,
                                 parameter == LocalValues::Clarity ? global : 0, 1.0, maps);
        };
    };
    ok &= keepsContract("texture", localContrastModule(LocalValues::Texture), 20, 30, noisy);
    ok &= keepsContract("negative texture", localContrastModule(LocalValues::Texture), 0, -60, noisy);
    ok &= keepsContract("clarity", localContrastModule(LocalValues::Clarity), -10, 45, noisy);
    ok &= keepsContract("dehaze", [](ipr::Image &image, int global, const ipr::Image &local) {
        Dehaze::apply(image, global, 0.25, local);
    }, 20, 30, noisy);
    ok &= keepsContract("added haze", [](ipr::Image &image, int global, const ipr::Image &local) {
        Dehaze::apply(image, global, 0.25, local);
    }, 0, -50, noisy);
    ok &= keepsContract("sharpening", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Sharpening] = local.share();
        DetailProcessing::sharpen(image, {global, 1.0f, 0}, 1.0, maps);
    }, 40, 60, noisy);
    ok &= keepsContract("luminance noise", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = local.share();
        DetailProcessing::reduceNoise(image, {global, 0}, 1.0, maps);
    }, 20, 40, noisy);
    ok &= keepsContract("colour noise", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseChrominance] = local.share();
        DetailProcessing::reduceNoise(image, {0, global}, 1.0, maps);
    }, 25, 25, noisy);
    ok &= keepsContract("noise from zero", [](ipr::Image &image, int global, const ipr::Image &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = local.share();
        maps[LocalValues::DenoiseChrominance] = local.share();
        DetailProcessing::reduceNoise(image, {global, global}, 1.0, maps);
    }, 0, 50, noisy);
    {
        // A soft transition mixes between the values, never outside the two renders.
        ipr::Image ramp = ipr::Image::create(noisy.size(), ipr::Depth::F32, 1);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x) imagetest::at<float>(ramp, y, x) = 100.0f * float(x) / float(ramp.width() - 1);
        ipr::Image soft = noisy.clone(), none = noisy.clone(), full = noisy.clone();
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = ramp.share();
        DetailProcessing::reduceNoise(soft, {}, 1.0, maps);
        DetailProcessing::reduceNoise(full, {100, 0}, 1.0);
        const auto bandDifference = [&](const ipr::Image &a, const ipr::Image &b, int column) {
            return ipr::largestDifference(imagetest::columns(a, column, column + 1), imagetest::columns(b, column, column + 1));
        };
        ok &= check(bandDifference(soft, none, 0) == 0 && bandDifference(soft, full, soft.width() - 1) == 0,
                    "a noise ramp reaches each end's global render exactly");
        const int middle = soft.width() / 2;
        ok &= check(bandDifference(soft, none, middle) > 0 && bandDifference(soft, full, middle) > 0,
                    "between its ends a noise ramp differs from both");
    }
    {
        // Half-way values of the dehaze mix: the ramp stays finite and reaches its ends.
        ipr::Image ramp = ipr::Image::create(noisy.size(), ipr::Depth::F32, 1);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x) imagetest::at<float>(ramp, y, x) = 100.0f * float(x) / float(ramp.width() - 1);
        ipr::Image soft = noisy.clone();
        Dehaze::apply(soft, 0, 0.25, ramp);
        ok &= check(ipr::allFinite(soft) && ipr::largestDifference(imagetest::columns(soft, 0, 0 + 1), imagetest::columns(noisy, 0, 0 + 1)) == 0,
                    "a dehaze ramp stays finite and leaves its zero end untouched");
    }
    {
        // Enumeration of the values present: exact where the local value is zero, and bounded.
        ipr::Image ramp = ipr::Image::create({64, 8}, ipr::Depth::F32, 1);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x) imagetest::at<float>(ramp, y, x) = x == 0 ? 0.0f : float(y * 64 + x);
        const LocalValues::Combinations few({{0.37f, &ramp, 0.0f, -1.0f, 1.0f, 0.5f}}, ramp.size(), 16);
        ok &= check(!few.active() && few.count() == 1 && few.values(0)[0] == 0.37f,
                    "a scale of zero leaves the global value alone");
        const LocalValues::Combinations bounded({{20.0f, &ramp, 1.0f, -1000.0f, 1000.0f, 1.0f}}, ramp.size(), 16);
        auto row = bounded.row(0);
        ok &= check(bounded.active() && bounded.count() <= 16 && bounded.values(row.at(0))[0] == 20.0f,
                    "too many values widen the step, and a zero local value stays exactly the global one");
        ipr::Image misfit = ipr::filled({3, 3}, ipr::Depth::F32, 1, 1);
        ok &= check(!LocalValues::Combinations({{1.0f, &misfit, 1.0f, -9.0f, 9.0f, 1.0f}}, ramp.size(), 16).active() &&
                    !LocalValues::Combinations({{1.0f, nullptr, 1.0f, -9.0f, 9.0f, 1.0f}}, ramp.size(), 16).active(),
                    "absent or misfit maps leave nothing to enumerate");
        ipr::Image invalid = ipr::Image::create({2, 1}, ipr::Depth::F32, 1);
        imagetest::at<float>(invalid, 0, 0) = std::numeric_limits<float>::quiet_NaN();
        imagetest::at<float>(invalid, 0, 1) = std::numeric_limits<float>::infinity();
        const LocalValues::Combinations finite({{3.0f, &invalid, 1.0f, -9.0f, 9.0f, 1.0f}}, invalid.size(), 16);
        ok &= check(finite.count() == 1 && finite.values(0)[0] == 3.0f, "a non-finite local value counts as none");
    }
    {
        // A combined value stays within the slider's range.
        const ipr::Image source = scene();
        ipr::Image beyond = source.clone(), limit = source.clone();
        Contrast::apply(beyond, 80, ipr::filled(source.size(), ipr::Depth::F32, 1, 70));
        Contrast::apply(limit, 100, {});
        ok &= check(ipr::largestDifference(beyond, limit) == 0, "global plus local contrast is bounded by the slider");
    }
    std::cout << (ok ? "local values: ok\n" : "local values: FAILED\n");
    return ok ? 0 : 1;
}
