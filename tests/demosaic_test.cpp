#include "Demosaic.h"
#include "Pixels.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

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
ipr::Image mosaic(int w, int h, const Demosaic::Pattern &p, double (*value)(int x, int y, int colour))
{
    ipr::Image cfa = ipr::Image::create({w, h}, ipr::Depth::F32, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            cfa.mutableRow<float>(y)[x] = float(std::lround(std::clamp(value(x, y, p[size_t(y & 1)][size_t(x & 1)]), 0.0, 1.0) * 65535.0) / 65535.0);
    return cfa;
}

ipr::Vec3f pixel(const ipr::Image &rgb, int y, int x)
{
    return rgb.row<ipr::Vec3f>(y)[x];
}

double maxError(const ipr::Image &rgb, double (*value)(int x, int y, int colour), int border)
{
    double worst = 0;
    for (int y = border; y < rgb.height() - border; ++y)
        for (int x = border; x < rgb.width() - border; ++x)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(double(pixel(rgb, y, x)[c]) - value(x, y, c)));
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
    ipr::Image rgb, before;
    for (const Demosaic::Pattern &p : {Rggb, Demosaic::Pattern{{{2, 1}, {1, 0}}},
                                       Demosaic::Pattern{{{1, 0}, {2, 1}}}, Demosaic::Pattern{{{1, 2}, {0, 1}}}}) {
        if (!check(Demosaic::interpolate(mosaic(w, h, p, flat), p, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}),
                   "interpolation completes")) return 1;
        if (!check(rgb.size() == ipr::Size(w, h) && rgb.depth() == ipr::Depth::F32 && rgb.channels() == 3 &&
                       maxError(rgb, flat, 0) < 2.0 / 65535.0,
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
                const ipr::Vec3f px = pixel(rgb, y, x);
                worst = std::max({worst, double(std::abs(px[0] - px[1])), double(std::abs(px[2] - px[1]))});
            }
        if (!check(worst < 1e-3, "no false colour away from a grey edge")) return 1;
    }
    // The interpolation before the median, for the development's captures.
    Demosaic::interpolate(mosaic(w, h, Rggb, noisy), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, &before, {});
    if (!check(ipr::largestDifference(rgb, before) == 0, "without median the capture before it is the output")) return 1;
    Demosaic::interpolate(mosaic(w, h, Rggb, noisy), Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::NonNegative, rgb, &before, {});
    if (!check(ipr::largestDifference(rgb, before) > 0, "the median changes the output, not the capture before it")) return 1;
    {
        // A signed mosaic keeps its negative values through RCD and the median; the
        // non-negative range bounds the interpolated ones at 0.
        const ipr::Image dark = ipr::filled({w, h}, ipr::Depth::F32, 1, -0.01f);
        Demosaic::interpolate(dark, Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::Signed, rgb, nullptr, {});
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for (const ipr::Image &plane : ipr::splitChannels(rgb)) {
            const ipr::Extremes found = ipr::extremesOf(plane);
            low = std::min(low, found.smallest);
            high = std::max(high, found.largest);
        }
        if (!check(std::abs(low + 0.01) < 1e-6 && std::abs(high + 0.01) < 1e-6, "a signed uniform mosaic stays signed")) return 1;
        Demosaic::interpolate(dark, Rggb, Demosaic::ChromaMedian::FlatOnly, Demosaic::Range::NonNegative, rgb, nullptr, {});
        if (!check(pixel(rgb, 100, 100)[1] == 0.f || pixel(rgb, 100, 101)[1] == 0.f,
                   "the non-negative range bounds the interpolated values")) return 1;
    }
    if (!check(!Demosaic::interpolate(mosaic(w, h, Rggb, flat), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr,
                                      [] { return true; }), "cancellation is reported")) return 1;
    if (!check(!Demosaic::interpolate(ipr::Image(), Rggb, Demosaic::ChromaMedian::Off, Demosaic::Range::NonNegative, rgb, nullptr, {}) &&
                   !Demosaic::interpolate(ipr::Image::create({1, 1}, ipr::Depth::F32, 1), Rggb, Demosaic::ChromaMedian::Off,
                                          Demosaic::Range::NonNegative, rgb, nullptr, {}) &&
                   !Demosaic::interpolate(ipr::Image::zeros({8, 8}, ipr::Depth::U16, 1), Rggb, Demosaic::ChromaMedian::Off,
                                          Demosaic::Range::NonNegative, rgb, nullptr, {}),
               "missing, degenerate or 16-bit mosaics are refused")) return 1;
    return 0;
}
