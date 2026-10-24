#include "LocalContrast.h"
#include "WorkingColorSpace.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

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

    // Lightroom's fitted response (etude-amplitude-effets-v001): small variations of
    // the fine bands are strongly amplified by texture, which fades by 16-32 pixels.
    cv::Mat texture = original.clone();
    LocalContrast::apply(texture, 100, 0);
    const double fine = bandGain(original, texture, 0, 1), wide = bandGain(original, texture, 16, 32);
    if (!(fine > 1.3 && fine < 1.65 && wide > 1.0 && wide < fine - 0.3))
        std::cerr << "texture +100: fine " << fine << ", 16-32 px " << wide << "\n";
    if (!check(fine > 1.3 && fine < 1.65 && wide > 1.0 && wide < fine - 0.3,
               "texture +100 amplifies fine detail and fades at wide scales")) return 1;
    cv::Mat smoother = original.clone();
    LocalContrast::apply(smoother, -100, 0);
    const double reduced = bandGain(original, smoother, 0, 1);
    if (!check(reduced > 0.4 && reduced < 0.7, "texture -100 reduces the fine band")) return 1;

    // A 4 EV step with faint detail: clarity +100 amplifies the detail but
    // preserves the edge (no strong halo, flat sides kept).
    cv::Mat step(256, 512, CV_32FC3);
    for (int y = 0; y < step.rows; ++y)
        for (int x = 0; x < step.cols; ++x) {
            const float base = x < 256 ? 0.05f : 0.8f;
            const float v = base * std::exp2(0.04f * std::sin(0.4f * float(y)));
            step.at<cv::Vec3f>(y, x) = cv::Vec3f(v, v, v);
        }
    cv::GaussianBlur(step, step, cv::Size(0, 0), 1.5, 0.0, cv::BORDER_REFLECT);   // edge softened as by optics
    cv::Mat clear = step.clone();
    LocalContrast::apply(clear, 0, 100);
    const auto stripe = [](const cv::Mat &m, int x0, int x1) {
        double lo = 1e9, hi = -1e9;
        for (int y = 64; y < 192; ++y)
            for (int x = x0; x < x1; ++x) {
                const double l = std::log2(WorkingColor::nonNegativeLuminance(m.at<cv::Vec3f>(y, x)));
                lo = std::min(lo, l);
                hi = std::max(hi, l);
            }
        return std::make_pair(lo, hi);
    };
    const auto [stepLo, stepHi] = stripe(step, 60, 120);
    const auto [clearLo, clearHi] = stripe(clear, 60, 120);
    if (!check((clearHi - clearLo) > 1.5 * (stepHi - stepLo), "clarity +100 amplifies faint detail")) return 1;
    // wide halo: change of the flat sides from 8 to 56 pixels off the edge
    double halo = 0;
    for (int x = 200; x < 312; ++x) {
        if (std::abs(x - 256) < 8) continue;
        const double before = std::log2(WorkingColor::nonNegativeLuminance(step.at<cv::Vec3f>(128, x)));
        const double after = std::log2(WorkingColor::nonNegativeLuminance(clear.at<cv::Vec3f>(128, x)));
        halo = std::max(halo, std::abs(after - before));
    }
    if (!(halo < 1.2)) std::cerr << "clarity +100: largest change 8-56 px off the edge " << halo << " EV\n";
    // Lightroom's own rings at +100 reach 0.7-1.0 EV beside strong edges (6V1A9947):
    // the bound guards against a regression, not against Lightroom's behaviour.
    if (!check(halo < 1.2, "clarity +100 keeps a 4 EV edge without a strong halo")) return 1;

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
