#include "Contrast.h"
#include "Exposure.h"
#include "LocalValues.h"
#include "PerceptualColour.h"

#include <iostream>

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
bool keepsContract(const char *name, const Module &apply, int global, int local)
{
    const cv::Mat source = scene();
    cv::Mat plain = source.clone(), zeros = source.clone(), withMap = source.clone(), shifted = source.clone();
    apply(plain, global, {});
    apply(zeros, global, cv::Mat(source.size(), CV_32FC1, cv::Scalar(0)));
    apply(withMap, global, leftHalf(source.size(), float(local)));
    apply(shifted, global + local, {});
    bool ok = check(cv::norm(plain, zeros, cv::NORM_INF) == 0, "a map of zeros renders exactly as no map");
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
