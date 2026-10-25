#include "Grain.h"

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

double encode(double v)
{
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

struct Statistics { double mean, deviation, lag1, lag4, chroma; };

// Grain of a uniform field, in display-encoded green, as measured on Lightroom.
Statistics measure(const cv::Mat &rgb, double base)
{
    cv::Mat g(rgb.size(), CV_64F), chroma(rgb.size(), CV_64F);
    for (int y = 0; y < rgb.rows; ++y)
        for (int x = 0; x < rgb.cols; ++x) {
            const cv::Vec3f p = rgb.at<cv::Vec3f>(y, x);
            g.at<double>(y, x) = encode(p[1]) - base;
            chroma.at<double>(y, x) = encode(p[0]) - encode(p[2]);
        }
    cv::Scalar mean, deviation;
    cv::meanStdDev(g, mean, deviation);
    auto correlation = [&](int lag) {
        double sum = 0;
        int n = 0;
        for (int y = 0; y < g.rows; ++y)
            for (int x = 0; x + lag < g.cols; ++x, ++n)
                sum += (g.at<double>(y, x) - mean[0]) * (g.at<double>(y, x + lag) - mean[0]);
        return sum / n / (deviation[0] * deviation[0]);
    };
    cv::Scalar chromaMean, chromaDeviation;
    cv::meanStdDev(chroma, chromaMean, chromaDeviation);
    return {mean[0], deviation[0], correlation(1), correlation(4), chromaDeviation[0]};
}

} // namespace

int main()
{
    const float grey = 0.18f;
    const cv::Mat field(400, 400, CV_32FC3, cv::Scalar(grey, grey, grey));
    const double base = encode(grey);

    cv::Mat same = field.clone();
    Grain::apply(same, 0);
    if (!check(cv::norm(same, field, cv::NORM_INF) == 0, "zero leaves the image unchanged")) return 1;

    cv::Mat full = field.clone();
    Grain::apply(full, 100);
    const Statistics s = measure(full, base);
    if (!check(std::abs(s.deviation - 0.062) < 0.006, "+100: deviation 0.062 of the encoded values, as Lightroom")) return 1;
    if (!check(std::abs(s.mean) < 0.005, "the grain leaves the mean encoded value in place")) return 1;
    if (!check(s.lag1 > 0.5 && s.lag1 < 0.78 && std::abs(s.lag4) < 0.08,
               "fine grain: correlated at one pixel, not at four, as Lightroom")) return 1;
    if (!check(s.chroma < 1e-4, "monochrome grain")) return 1;

    cv::Mat half = field.clone();
    Grain::apply(half, 50);
    if (!check(std::abs(measure(half, base).deviation - 0.031) < 0.004, "+50: half the deviation")) return 1;

    cv::Mat again = field.clone();
    Grain::apply(again, 100);
    if (!check(cv::norm(again, full, cv::NORM_INF) == 0, "the same grain at every render")) return 1;

    cv::Mat dark(64, 64, CV_32FC3, cv::Scalar(0, 0, 0));
    Grain::apply(dark, 100);
    double lo;
    cv::minMaxLoc(dark.reshape(1), &lo);
    if (!check(lo >= 0, "black stays nonnegative")) return 1;

    // On a 1/3 proxy, the grain is what the reduction leaves of the full image's.
    cv::Mat reducedFull, proxy(field.rows / 3, field.cols / 3, CV_32FC3, cv::Scalar(grey, grey, grey));
    cv::resize(full, reducedFull, proxy.size(), 0, 0, cv::INTER_AREA);
    Grain::apply(proxy, 100, 1.0 / 3.0);
    const double expected = measure(reducedFull, base).deviation, proxyDeviation = measure(proxy, base).deviation;
    if (!check(std::abs(proxyDeviation - expected) < 0.15 * expected,
               "a 1/3 proxy shows the deviation of the reduced full-resolution grain")) return 1;

    cv::Mat empty;
    Grain::apply(empty, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
