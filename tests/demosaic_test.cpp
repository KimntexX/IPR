#include "Demosaic.h"

#include <algorithm>
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

const Demosaic::Pattern Rggb{{{0, 1}, {1, 2}}};

// A white-balanced mosaic, white 1: the pattern's colour of each site, on the 16-bit grid.
cv::Mat mosaic(int w, int h, const Demosaic::Pattern &p, double (*value)(int x, int y, int colour))
{
    cv::Mat cfa(h, w, CV_32FC1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            cfa.at<float>(y, x) = float(std::lround(std::clamp(value(x, y, p[size_t(y & 1)][size_t(x & 1)]), 0.0, 1.0) * 65535.0) / 65535.0);
    return cfa;
}

double maxError(const cv::Mat &rgb, double (*value)(int x, int y, int colour), int border)
{
    double worst = 0;
    for (int y = border; y < rgb.rows - border; ++y)
        for (int x = border; x < rgb.cols - border; ++x)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(double(rgb.at<cv::Vec3f>(y, x)[c]) - value(x, y, c)));
    return worst;
}

double flat(int, int, int c) { return c == 0 ? 0.40 : c == 1 ? 0.30 : 0.20; }
double gradient(int x, int y, int c) { return 0.1 + 0.6 * (x / 700.0) + 0.1 * (y / 600.0) + 0.05 * c; }
double greyEdge(int x, int, int) { return x < 300 ? 0.15 : 0.6; }
double noisy(int x, int y, int c) { return 0.3 + 0.02 * c + 0.01 * std::sin(x * 12.9898 + y * 78.233) * ((x * 7 + y * 13) % 5 - 2); }

} // namespace

int main()
{
    const int threads = Demosaic::workerThreads();
    if (!check(threads >= 1 && threads <= 8, "worker threads bounded to 1..8")) return 1;

    // Spans several 512-pixel tiles with partial ones, on every Bayer layout.
    const int w = 700, h = 600;
    cv::Mat rgb, before;
    for (const Demosaic::Pattern &p : {Rggb, Demosaic::Pattern{{{2, 1}, {1, 0}}},
                                       Demosaic::Pattern{{{1, 0}, {2, 1}}}, Demosaic::Pattern{{{1, 2}, {0, 1}}}}) {
        if (!check(Demosaic::interpolate(mosaic(w, h, p, flat), p, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}),
                   "interpolation completes")) return 1;
        if (!check(rgb.size() == cv::Size(w, h) && rgb.type() == CV_32FC3 && maxError(rgb, flat, 0) < 2.0 / 65535.0,
                   "a uniform colour is reconstructed exactly, borders and tile seams included")) return 1;
    }
    Demosaic::interpolate(mosaic(w, h, Rggb, gradient), Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::NonNegative, rgb, nullptr, {});
    if (!check(maxError(rgb, gradient, 8) < 0.002, "a smooth gradient stays within 0.2 %")) return 1;
    {
        // A grey step must not gain colour beyond the two pixels next to it.
        Demosaic::interpolate(mosaic(w, h, Rggb, greyEdge), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {});
        double worst = 0;
        for (int y = 8; y < h - 8; ++y)
            for (int x = 8; x < w - 8; ++x) {
                if (std::abs(x - 300) <= 2) continue;
                const cv::Vec3f px = rgb.at<cv::Vec3f>(y, x);
                worst = std::max({worst, double(std::abs(px[0] - px[1])), double(std::abs(px[2] - px[1]))});
            }
        if (!check(worst < 1e-3, "no false colour away from a grey edge")) return 1;
    }
    // The interpolation before the median, for the development's captures.
    Demosaic::interpolate(mosaic(w, h, Rggb, noisy), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, &before, {});
    if (!check(cv::norm(rgb, before, cv::NORM_INF) == 0, "without median the capture before it is the output")) return 1;
    Demosaic::interpolate(mosaic(w, h, Rggb, noisy), Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::NonNegative, rgb, &before, {});
    if (!check(cv::norm(rgb, before, cv::NORM_INF) > 0, "the median changes the output, not the capture before it")) return 1;
    {
        // A signed mosaic keeps its negative values through RCD and the median; the
        // non-negative range bounds the interpolated ones at 0.
        cv::Mat dark(h, w, CV_32FC1, cv::Scalar(-0.01f));
        Demosaic::interpolate(dark, Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::Signed, rgb, nullptr, {});
        double low = 0, high = 0;
        cv::minMaxLoc(rgb.reshape(1), &low, &high);
        if (!check(std::abs(low + 0.01) < 1e-6 && std::abs(high + 0.01) < 1e-6, "a signed uniform mosaic stays signed")) return 1;
        Demosaic::interpolate(dark, Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::NonNegative, rgb, nullptr, {});
        if (!check(rgb.at<cv::Vec3f>(100, 100)[1] == 0.f || rgb.at<cv::Vec3f>(100, 101)[1] == 0.f,
                   "the non-negative range bounds the interpolated values")) return 1;
    }
    if (!check(!Demosaic::interpolate(mosaic(w, h, Rggb, flat), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr,
                                      [] { return true; }), "cancellation is reported")) return 1;
    if (!check(!Demosaic::interpolate(cv::Mat(), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}) &&
                   !Demosaic::interpolate(cv::Mat(1, 1, CV_32FC1), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}) &&
                   !Demosaic::interpolate(cv::Mat(8, 8, CV_16UC1), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}),
               "missing, degenerate or 16-bit mosaics are refused")) return 1;
    return 0;
}
