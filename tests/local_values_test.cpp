#include "ColorPipeline.h"
#include "Contrast.h"
#include "Dehaze.h"
#include "DetailProcessing.h"
#include "Exposure.h"
#include "LocalContrast.h"
#include "LocalValues.h"
#include "PerceptualColour.h"
#include "ToneRecovery.h"

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
cv::Mat scene()
{
    cv::Mat image(16, 24, CV_32FC3);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            image.at<cv::Vec3f>(y, x) = (x + y) % 5 == 0
                ? cv::Vec3f(0.02f * x, 0.02f * x, 0.02f * x)
                : cv::Vec3f(0.01f + 0.04f * x, 0.03f + 0.05f * y, 0.2f + 0.01f * (x ^ y));
    return image;
}

// A noisy scene large enough for the neighbourhood modules: their noise and
// statistics estimators need samples.
cv::Mat noisyScene()
{
    cv::Mat image(96, 128, CV_32FC3);
    cv::RNG noise(21);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x) {
            const cv::Vec3f base(0.05f + 0.005f * x, 0.08f + 0.006f * y, 0.3f - 0.001f * x);
            const float shared = float(noise.gaussian(0.01));
            image.at<cv::Vec3f>(y, x) = cv::Vec3f(
                std::max(0.001f, base[0] + shared + float(noise.gaussian(0.006))),
                std::max(0.001f, base[1] + shared + float(noise.gaussian(0.006))),
                std::max(0.001f, base[2] + shared + float(noise.gaussian(0.006))));
        }
    return image;
}

// The local amount over the left half, nothing over the right half.
cv::Mat leftHalf(const cv::Size &size, float amount)
{
    cv::Mat map(size, CV_32FC1, cv::Scalar(0));
    map(cv::Rect(0, 0, size.width / 2, size.height)).setTo(amount);
    return map;
}

double halfDifference(const cv::Mat &a, const cv::Mat &b, bool left)
{
    const cv::Rect half(left ? 0 : a.cols / 2, 0, a.cols / 2, a.rows);
    return cv::norm(a(half), b(half), cv::NORM_INF);
}

using Module = std::function<void(cv::Mat &, int global, const cv::Mat &local)>;

// The three properties every module with local values keeps.
bool keepsContract(const char *name, const Module &apply, int global, int local, const cv::Mat &source = scene())
{
    cv::Mat plain = source.clone(), zeros = source.clone(), withMap = source.clone(), shifted = source.clone();
    apply(plain, global, {});
    apply(zeros, global, cv::Mat(source.size(), CV_32FC1, cv::Scalar(0)));
    apply(withMap, global, leftHalf(source.size(), float(local)));
    apply(shifted, global + local, {});
    bool ok = check(cv::norm(plain, zeros, cv::NORM_INF) == 0, "a map of zeros renders exactly as no map");
    ok &= check(cv::norm(shifted, plain, cv::NORM_INF) > 1e-3, "the tested value has an effect on the scene");
    ok &= check(halfDifference(withMap, shifted, true) < 1e-5, "inside the zone, a local value acts as a global one");
    ok &= check(halfDifference(withMap, plain, false) == 0, "outside the zone, nothing changes");
    if (!ok) std::cerr << "  in " << name << '\n';
    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= check(LocalValues::fits({}, {4, 3}) && LocalValues::fits(cv::Mat(3, 4, CV_32FC1), {4, 3}) &&
                !LocalValues::fits(cv::Mat(4, 4, CV_32FC1), {4, 3}) && !LocalValues::fits(cv::Mat(3, 4, CV_8UC1), {4, 3}),
                "a map fits when empty or one float per pixel of the image");
    ok &= keepsContract("exposure", [](cv::Mat &image, int global, const cv::Mat &local) {
        Exposure::apply(image, global / 10.0f, local.empty() ? local : cv::Mat(local / 10.0f));
    }, -5, 15);
    ok &= keepsContract("contrast", [](cv::Mat &image, int global, const cv::Mat &local) {
        Contrast::apply(image, global, local);
    }, 20, 35);
    ok &= keepsContract("negative contrast", [](cv::Mat &image, int global, const cv::Mat &local) {
        Contrast::apply(image, global, local);
    }, 0, -60);
    ok &= keepsContract("saturation", [](cv::Mat &image, int global, const cv::Mat &local) {
        PerceptualColour::Parameters parameters;
        parameters.saturation = global;
        LocalValues::Maps maps;
        maps[LocalValues::Saturation] = local;
        PerceptualColour::apply(image, parameters, maps);
    }, 10, 40);
    ok &= keepsContract("vibrance", [](cv::Mat &image, int global, const cv::Mat &local) {
        PerceptualColour::Parameters parameters;
        parameters.vibrance = global;
        parameters.mixer.hue[2] = 30;   // the mixer reads the pixel vibrance produced
        LocalValues::Maps maps;
        maps[LocalValues::Vibrance] = local;
        PerceptualColour::apply(image, parameters, maps);
    }, 0, 50);
    const ToneRecovery::Statistics statistics = ToneRecovery::typicalStatistics();
    const auto toneModule = [&](LocalValues::Parameter parameter, float ToneRecovery::Amounts::*amount) {
        return [&statistics, parameter, amount](cv::Mat &image, int global, const cv::Mat &local) {
            ToneRecovery::Amounts amounts;
            amounts.*amount = float(global) / 100.0f;
            LocalValues::Maps maps;
            maps[parameter] = local;
            ToneRecovery::apply(image, amounts, statistics, maps);
        };
    };
    ok &= keepsContract("shadows", toneModule(LocalValues::Shadows, &ToneRecovery::Amounts::shadows), 20, 30);
    ok &= keepsContract("blacks", toneModule(LocalValues::Blacks, &ToneRecovery::Amounts::blacks), -10, -40);
    ok &= keepsContract("highlights", toneModule(LocalValues::Highlights, &ToneRecovery::Amounts::highlights), -30, -50);
    ok &= keepsContract("whites", toneModule(LocalValues::Whites, &ToneRecovery::Amounts::whites), 0, 60);
    {
        // Two controls at once: the zone of each map follows its own value.
        const cv::Mat source = scene();
        cv::Mat both = source.clone(), expected = source.clone();
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
    ok &= keepsContract("temperature", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Temperature] = local.empty() ? local : cv::Mat(local * 10.0f);
        ColorPipeline::applyTemperatureTint(image, 5000 + 10 * global, 5000.0, 0, nullptr, maps);
    }, 20, 150);
    ok &= keepsContract("tint", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Tint] = local;
        ColorPipeline::applyTemperatureTint(image, 6000, 5000.0, global, nullptr, maps);
    }, 10, -40);
    // Neighbourhood modules: the image-wide analysis does not move with the local value.
    const cv::Mat noisy = noisyScene();
    const auto localContrastModule = [](LocalValues::Parameter parameter) {
        return [parameter](cv::Mat &image, int global, const cv::Mat &local) {
            LocalValues::Maps maps;
            maps[parameter] = local;
            LocalContrast::apply(image, parameter == LocalValues::Texture ? global : 0,
                                 parameter == LocalValues::Clarity ? global : 0, 1.0, maps);
        };
    };
    ok &= keepsContract("texture", localContrastModule(LocalValues::Texture), 20, 30, noisy);
    ok &= keepsContract("negative texture", localContrastModule(LocalValues::Texture), 0, -60, noisy);
    ok &= keepsContract("clarity", localContrastModule(LocalValues::Clarity), -10, 45, noisy);
    ok &= keepsContract("dehaze", [](cv::Mat &image, int global, const cv::Mat &local) {
        Dehaze::apply(image, global, 0.25, local);
    }, 20, 30, noisy);
    ok &= keepsContract("added haze", [](cv::Mat &image, int global, const cv::Mat &local) {
        Dehaze::apply(image, global, 0.25, local);
    }, 0, -50, noisy);
    ok &= keepsContract("sharpening", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::Sharpening] = local;
        DetailProcessing::sharpen(image, {global, 1.0f, 0}, 1.0, maps);
    }, 40, 60, noisy);
    ok &= keepsContract("luminance noise", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = local;
        DetailProcessing::reduceNoise(image, {global, 0}, 1.0, maps);
    }, 20, 40, noisy);
    ok &= keepsContract("colour noise", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseChrominance] = local;
        DetailProcessing::reduceNoise(image, {0, global}, 1.0, maps);
    }, 25, 25, noisy);
    ok &= keepsContract("noise from zero", [](cv::Mat &image, int global, const cv::Mat &local) {
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = local;
        maps[LocalValues::DenoiseChrominance] = local;
        DetailProcessing::reduceNoise(image, {global, global}, 1.0, maps);
    }, 0, 50, noisy);
    {
        // A soft transition mixes between the values, never outside the two renders.
        cv::Mat ramp(noisy.size(), CV_32FC1);
        for (int y = 0; y < ramp.rows; ++y)
            for (int x = 0; x < ramp.cols; ++x) ramp.at<float>(y, x) = 100.0f * float(x) / float(ramp.cols - 1);
        cv::Mat soft = noisy.clone(), none = noisy.clone(), full = noisy.clone();
        LocalValues::Maps maps;
        maps[LocalValues::DenoiseLuminance] = ramp;
        DetailProcessing::reduceNoise(soft, {}, 1.0, maps);
        DetailProcessing::reduceNoise(full, {100, 0}, 1.0);
        const auto bandDifference = [&](const cv::Mat &a, const cv::Mat &b, int column) {
            return cv::norm(a.col(column), b.col(column), cv::NORM_INF);
        };
        ok &= check(bandDifference(soft, none, 0) == 0 && bandDifference(soft, full, soft.cols - 1) == 0,
                    "a noise ramp reaches each end's global render exactly");
        const int middle = soft.cols / 2;
        ok &= check(bandDifference(soft, none, middle) > 0 && bandDifference(soft, full, middle) > 0,
                    "between its ends a noise ramp differs from both");
    }
    {
        // Half-way values of the dehaze mix: the ramp stays finite and reaches its ends.
        cv::Mat ramp(noisy.size(), CV_32FC1);
        for (int y = 0; y < ramp.rows; ++y)
            for (int x = 0; x < ramp.cols; ++x) ramp.at<float>(y, x) = 100.0f * float(x) / float(ramp.cols - 1);
        cv::Mat soft = noisy.clone();
        Dehaze::apply(soft, 0, 0.25, ramp);
        ok &= check(cv::checkRange(soft) && cv::norm(soft.col(0), noisy.col(0), cv::NORM_INF) == 0,
                    "a dehaze ramp stays finite and leaves its zero end untouched");
    }
    {
        // Enumeration of the values present: exact where the local value is zero, and bounded.
        cv::Mat ramp(8, 64, CV_32FC1);
        for (int y = 0; y < ramp.rows; ++y)
            for (int x = 0; x < ramp.cols; ++x) ramp.at<float>(y, x) = x == 0 ? 0.0f : float(y * 64 + x);
        const LocalValues::Combinations few({{0.37f, &ramp, 0.0f, -1.0f, 1.0f, 0.5f}}, ramp.size(), 16);
        ok &= check(!few.active() && few.count() == 1 && few.values(0)[0] == 0.37f,
                    "a scale of zero leaves the global value alone");
        const LocalValues::Combinations bounded({{20.0f, &ramp, 1.0f, -1000.0f, 1000.0f, 1.0f}}, ramp.size(), 16);
        auto row = bounded.row(0);
        ok &= check(bounded.active() && bounded.count() <= 16 && bounded.values(row.at(0))[0] == 20.0f,
                    "too many values widen the step, and a zero local value stays exactly the global one");
        const cv::Mat misfit(3, 3, CV_32FC1, cv::Scalar(1));
        ok &= check(!LocalValues::Combinations({{1.0f, &misfit, 1.0f, -9.0f, 9.0f, 1.0f}}, ramp.size(), 16).active() &&
                    !LocalValues::Combinations({{1.0f, nullptr, 1.0f, -9.0f, 9.0f, 1.0f}}, ramp.size(), 16).active(),
                    "absent or misfit maps leave nothing to enumerate");
        cv::Mat invalid(1, 2, CV_32FC1);
        invalid.at<float>(0, 0) = std::numeric_limits<float>::quiet_NaN();
        invalid.at<float>(0, 1) = std::numeric_limits<float>::infinity();
        const LocalValues::Combinations finite({{3.0f, &invalid, 1.0f, -9.0f, 9.0f, 1.0f}}, invalid.size(), 16);
        ok &= check(finite.count() == 1 && finite.values(0)[0] == 3.0f, "a non-finite local value counts as none");
    }
    {
        // A combined value stays within the slider's range.
        const cv::Mat source = scene();
        cv::Mat beyond = source.clone(), limit = source.clone();
        Contrast::apply(beyond, 80, cv::Mat(source.size(), CV_32FC1, cv::Scalar(70)));
        Contrast::apply(limit, 100, {});
        ok &= check(cv::norm(beyond, limit, cv::NORM_INF) == 0, "global plus local contrast is bounded by the slider");
    }
    std::cout << (ok ? "local values: ok\n" : "local values: FAILED\n");
    return ok ? 0 : 1;
}
