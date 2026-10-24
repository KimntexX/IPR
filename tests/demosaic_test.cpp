#include "Demosaic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

const Demosaic::Pattern Rggb{{{0, 1}, {1, 2}}};

// LibRaw's layout before interpolation: only the pattern's colour of each pixel set.
std::vector<std::array<std::uint16_t, 4>> mosaic(int w, int h, const Demosaic::Pattern &p,
                                                 double (*value)(int x, int y, int colour))
{
    std::vector<std::array<std::uint16_t, 4>> image(size_t(w) * size_t(h), {0, 0, 0, 0});
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int c = p[size_t(y & 1)][size_t(x & 1)];
            image[size_t(y) * size_t(w) + size_t(x)][size_t(c)] =
                std::uint16_t(std::lround(std::clamp(value(x, y, c), 0.0, 1.0) * 65535.0));
        }
    return image;
}

Demosaic::Pixel *raw(std::vector<std::array<std::uint16_t, 4>> &image)
{
    return reinterpret_cast<Demosaic::Pixel *>(image.data());
}

double maxError(const std::vector<std::array<std::uint16_t, 4>> &image, int w, int h,
                double (*value)(int x, int y, int colour), int border)
{
    double worst = 0;
    for (int y = border; y < h - border; ++y)
        for (int x = border; x < w - border; ++x)
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(image[size_t(y) * size_t(w) + size_t(x)][size_t(c)] / 65535.0 -
                                                 value(x, y, c)));
    return worst;
}

double flat(int, int, int c) { return c == 0 ? 0.40 : c == 1 ? 0.30 : 0.20; }
double gradient(int x, int y, int c) { return 0.1 + 0.6 * (x / 700.0) + 0.1 * (y / 600.0) + 0.05 * c; }
double greyEdge(int x, int, int) { return x < 300 ? 0.15 : 0.6; }

} // namespace

int main()
{
    const int threads = Demosaic::workerThreads();
    if (!check(threads >= 1 && threads <= 8, "worker threads bounded to 1..8")) return 1;

    // Spans several 512-pixel tiles with partial ones, on every Bayer layout.
    const int w = 700, h = 600;
    for (const Demosaic::Pattern &p : {Rggb, Demosaic::Pattern{{{2, 1}, {1, 0}}},
                                       Demosaic::Pattern{{{1, 0}, {2, 1}}}, Demosaic::Pattern{{{1, 2}, {0, 1}}}}) {
        auto image = mosaic(w, h, p, flat);
        if (!check(Demosaic::interpolate(raw(image), w, h, p, Demosaic::ChromaMedian::Off, {}), "interpolation completes")) return 1;
        if (!check(maxError(image, w, h, flat, 0) < 2.0 / 65535.0,
                   "a uniform colour is reconstructed exactly, borders and tile seams included")) return 1;
    }
    {
        auto image = mosaic(w, h, Rggb, gradient);
        Demosaic::interpolate(raw(image), w, h, Rggb, Demosaic::ChromaMedian::FlatOnly, {});
        if (!check(maxError(image, w, h, gradient, 8) < 0.002, "a smooth gradient stays within 0.2 %")) return 1;
    }
    {
        // A grey step must not gain colour beyond the two pixels next to it.
        auto image = mosaic(w, h, Rggb, greyEdge);
        Demosaic::interpolate(raw(image), w, h, Rggb, Demosaic::ChromaMedian::Off, {});
        double worst = 0;
        for (int y = 8; y < h - 8; ++y)
            for (int x = 8; x < w - 8; ++x) {
                if (std::abs(x - 300) <= 2) continue;
                const auto &px = image[size_t(y) * size_t(w) + size_t(x)];
                worst = std::max({worst, std::abs(px[0] - px[1]) / 65535.0, std::abs(px[2] - px[1]) / 65535.0});
            }
        if (!check(worst < 1e-3, "no false colour away from a grey edge")) return 1;
    }
    {
        auto image = mosaic(w, h, Rggb, flat);
        if (!check(!Demosaic::interpolate(raw(image), w, h, Rggb, Demosaic::ChromaMedian::Off, [] { return true; }),
                   "cancellation is reported")) return 1;
        if (!check(!Demosaic::interpolate(nullptr, w, h, Rggb, Demosaic::ChromaMedian::Off, {}) &&
                       !Demosaic::interpolate(raw(image), 1, 1, Rggb, Demosaic::ChromaMedian::Off, {}),
                   "missing or degenerate images are refused")) return 1;
    }
    return 0;
}
