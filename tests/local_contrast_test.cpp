#include "LocalContrast.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// Grey image whose log luminance carries random detail at every scale.
cv::Mat texturedGrey(int size)
{
    cv::Mat detail(size, size, CV_32F);
    cv::randn(detail, 0.0, 1.0);
    cv::Mat log2Luminance = cv::Mat::zeros(size, size, CV_32F) - 3.0f;   // around 0.125
    for (double sigma : {0.7, 3.0, 24.0}) {
        cv::Mat band;
        cv::GaussianBlur(detail, band, cv::Size(0, 0), sigma);
        cv::Scalar mean, deviation;
        cv::meanStdDev(band, mean, deviation);
        log2Luminance += band * (0.25 / deviation[0]);
    }
    cv::Mat rgb(size, size, CV_32FC3);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float v = std::exp2(log2Luminance.at<float>(y, x));
            rgb.at<cv::Vec3f>(y, x) = cv::Vec3f(v, v, v);
        }
    return rgb;
}

// Least-squares gain of the variant's band (G_a - G_b of the log luminance) on the original's.
double bandGain(const cv::Mat &original, const cv::Mat &variant, double a, double b)
{
    const auto band = [&](const cv::Mat &rgb) {
        cv::Mat l(rgb.size(), CV_32F);
        for (int y = 0; y < rgb.rows; ++y)
            for (int x = 0; x < rgb.cols; ++x)
                l.at<float>(y, x) = std::log2(WorkingColor::nonNegativeLuminance(rgb.at<cv::Vec3f>(y, x)) + 1e-3f);
        cv::Mat ga, gb;
        if (a > 0) cv::GaussianBlur(l, ga, cv::Size(0, 0), a, a, cv::BORDER_REFLECT); else ga = l;
        cv::GaussianBlur(l, gb, cv::Size(0, 0), b, b, cv::BORDER_REFLECT);
        return cv::Mat(ga - gb);
    };
    const cv::Mat o = band(original), v = band(variant);
    return o.dot(v) / o.dot(o);
}

} // namespace

int main()
{
    const cv::Mat original = texturedGrey(512);

    cv::Mat same = original.clone();
    LocalContrast::apply(same, 0, 0);
    if (!check(cv::norm(same, original, cv::NORM_INF) == 0, "zero texture and clarity leave the image unchanged")) return 1;

    // Lightroom's measured gains (etude-reponse-effets-v001): texture +100 is strong
    // at the pixel scale and fades by 16-32 pixels; clarity +100 lifts every scale.
    cv::Mat texture = original.clone();
    LocalContrast::apply(texture, 100, 0);
    const double fine = bandGain(original, texture, 0, 1), wide = bandGain(original, texture, 16, 32);
    if (!check(std::abs(fine - 1.56) < 0.12 && std::abs(wide - 1.07) < 0.06,
               "texture +100 follows the measured gains at fine and wide scales")) return 1;
    cv::Mat clarity = original.clone();
    LocalContrast::apply(clarity, 0, 100);
    const double clarityWide = bandGain(original, clarity, 16, 32);
    if (!check(std::abs(clarityWide - 1.345) < 0.1, "clarity +100 lifts the 16-32 pixel band")) return 1;
    cv::Mat smoother = original.clone();
    LocalContrast::apply(smoother, -100, 0);
    if (!check(std::abs(bandGain(original, smoother, 0, 1) - 0.495) < 0.08, "texture -100 halves the fine band")) return 1;

    // Colours keep their channel ratios and stay finite and nonnegative.
    cv::Mat colour = original.clone();
    for (int y = 0; y < colour.rows; ++y)
        for (int x = 0; x < colour.cols; ++x) {
            auto &p = colour.at<cv::Vec3f>(y, x);
            p = cv::Vec3f(p[0] * 1.4f, p[1], p[2] * 0.5f);
        }
    const cv::Mat before = colour.clone();
    LocalContrast::apply(colour, 60, 60);
    bool ratios = true, finite = true;
    for (int y = 0; y < colour.rows; ++y)
        for (int x = 0; x < colour.cols; ++x) {
            const auto &a = before.at<cv::Vec3f>(y, x), &b = colour.at<cv::Vec3f>(y, x);
            finite = finite && std::isfinite(b[0]) && std::isfinite(b[1]) && std::isfinite(b[2]) && b[0] >= 0 && b[2] >= 0;
            ratios = ratios && std::abs(b[0] / b[1] - a[0] / a[1]) < 1e-3f && std::abs(b[2] / b[1] - a[2] / a[1]) < 1e-3f;
        }
    if (!check(finite && ratios, "colours keep their ratios and stay finite and nonnegative")) return 1;

    cv::Mat empty;
    LocalContrast::apply(empty, 50, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
