#include "Demosaic.h"

#include "Arrange.h"
#include "Filters.h"
#include "Neighbourhood.h"
#include "Pixels.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace Demosaic {
namespace {

constexpr int MaximumThreads = 8;
constexpr int TileSize = 512;
constexpr int TileMargin = 12;   // even: keeps the pattern; covers the 9-pixel stencils and the medians
constexpr float Eps = 1e-5f;
constexpr float FlatGradient = 0.5f, DetailGradient = 1.0f;   // in luminance noise sigmas
constexpr float LaplacianWeight = 0.5f;   // green: share of the colour-difference estimate

// Direction weight of a pixel (0 vertical, 1 horizontal), pulled toward the mean
// of its diagonal neighbours when they are more decided.
float direction(const ipr::PixelReader<float> &vh, int y, int x)
{
    const float own = vh(y, x);
    const float n = 0.25f * (vh(y - 1, x - 1) + vh(y - 1, x + 1) + vh(y + 1, x - 1) + vh(y + 1, x + 1));
    return std::abs(0.5f - own) < std::abs(0.5f - n) ? n : own;
}

// Green at red and blue pixels: gradient-weighted directional estimates, each the
// even blend of the neighbour green scaled by the low-pass ratio (RCD) and of the
// neighbour green corrected by half the same-colour difference toward the centre
// (Laplacian). The ratio alone was softer than Lightroom at ISO 800 to 3200
// (0.89 to 0.92 of its fine-band energy), the Laplacian alone less accurate.
void interpolateGreen(const ipr::Image &cfa, const ipr::PixelReader<float> &vh, const ipr::Image &lowPass, const Pattern &p,
                      float floor, ipr::Image &green)
{
    const int H = cfa.height(), W = cfa.width();
    const ipr::PixelReader<float> T(cfa), lpf(lowPass);
    const ipr::PixelWriter<float> G(green);
    for (int y = 5; y < H - 5; ++y)
        for (int x = 5; x < W - 5; ++x) {
            if (p[size_t(y & 1)][size_t(x & 1)] == 1) continue;
            const float L = lpf(y, x);
            const auto ratio = [&](int yy, int xx) {
                const float l = lpf(yy, xx);
                // Absolute values: the same ratio on a positive mosaic, bounded to [0, 2]
                // where the low-pass is near or below 0 (signed shadows).
                return 1.f + (L - l) / (Eps + std::abs(L) + std::abs(l));
            };
            const auto estimate = [&](int dy, int dx) {
                const float neighbour = T(y + dy, x + dx);
                const float byRatio = neighbour * ratio(y + 2 * dy, x + 2 * dx);
                const float byLaplacian = neighbour + 0.5f * (T(y, x) - T(y + 2 * dy, x + 2 * dx));
                return LaplacianWeight * byLaplacian + (1.f - LaplacianWeight) * byRatio;
            };
            const float nEst = estimate(-1, 0), sEst = estimate(1, 0), wEst = estimate(0, -1), eEst = estimate(0, 1);
            const float nG = Eps + std::abs(T(y - 1, x) - T(y + 1, x)) + std::abs(T(y, x) - T(y - 2, x)) +
                             std::abs(T(y - 1, x) - T(y - 3, x)) + std::abs(T(y - 2, x) - T(y - 4, x));
            const float sG = Eps + std::abs(T(y + 1, x) - T(y - 1, x)) + std::abs(T(y, x) - T(y + 2, x)) +
                             std::abs(T(y + 1, x) - T(y + 3, x)) + std::abs(T(y + 2, x) - T(y + 4, x));
            const float wG = Eps + std::abs(T(y, x - 1) - T(y, x + 1)) + std::abs(T(y, x) - T(y, x - 2)) +
                             std::abs(T(y, x - 1) - T(y, x - 3)) + std::abs(T(y, x - 2) - T(y, x - 4));
            const float eG = Eps + std::abs(T(y, x + 1) - T(y, x - 1)) + std::abs(T(y, x) - T(y, x + 2)) +
                             std::abs(T(y, x + 1) - T(y, x + 3)) + std::abs(T(y, x + 2) - T(y, x + 4));
            const float vEst = (sG * nEst + nG * sEst) / (nG + sG);
            const float hEst = (eG * wEst + wG * eEst) / (wG + eG);
            const float d = direction(vh, y, x);
            G(y, x) = std::max(floor, d * hEst + (1.f - d) * vEst);
        }
}

// RCD on a padded tile, values ~[0, 1]: green from ratio-corrected directional
// estimates, the direction decided per pixel and pulled toward the most decided
// diagonal neighbour; red and blue from gradient-weighted colour differences.
// Interpolated values are bounded below by floor (0, or -inf to keep them signed).
ipr::Image rcd(const ipr::Image &cfa, const Pattern &p, float floor)
{
    const int H = cfa.height(), W = cfa.width();
    const auto colour = [&](int y, int x) { return p[size_t(y & 1)][size_t(x & 1)]; };
    const ipr::PixelReader<float> T(cfa);
    ipr::Image directions = ipr::filled(cfa.size(), ipr::Depth::F32, 1, 0.5);
    ipr::Image lowPass = ipr::filled(cfa.size(), ipr::Depth::F32, 1, 0.0);
    ipr::Image green = cfa.clone();
    const ipr::PixelWriter<float> vh(directions), lpf(lowPass);
    const ipr::PixelReader<float> decidedVh(directions), G(green);
    // 1. Vertical/horizontal discrimination: directional high-pass energy.
    for (int y = 4; y < H - 4; ++y)
        for (int x = 4; x < W - 4; ++x) {
            float v = Eps, h = Eps;
            for (int k = -1; k <= 1; ++k) {
                const float dv = T(y - 1, x + k) - T(y + 1, x + k);
                const float sv = T(y - 2, x + k) - 2 * T(y, x + k) + T(y + 2, x + k);
                const float dh = T(y + k, x - 1) - T(y + k, x + 1);
                const float sh = T(y + k, x - 2) - 2 * T(y + k, x) + T(y + k, x + 2);
                v += dv * dv + 0.5f * sv * sv;
                h += dh * dh + 0.5f * sh * sh;
            }
            vh(y, x) = v / (v + h);
        }
    // 2. Low-pass of the mosaic (all colours mixed 1:2:1).
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x)
            lpf(y, x) = T(y, x) + 0.5f * (T(y - 1, x) + T(y + 1, x) + T(y, x - 1) + T(y, x + 1)) +
                        0.25f * (T(y - 1, x - 1) + T(y - 1, x + 1) + T(y + 1, x - 1) + T(y + 1, x + 1));
    // 3. Green at red and blue pixels.
    interpolateGreen(cfa, decidedVh, lowPass, p, floor, green);
    const auto decided = [&](int y, int x) { return direction(decidedVh, y, x); };
    // 4. Red at blue pixels and blue at red pixels: diagonal colour differences.
    ipr::Image red = ipr::filled(cfa.size(), ipr::Depth::F32, 1, 0.0), blue = ipr::filled(cfa.size(), ipr::Depth::F32, 1, 0.0);
    const ipr::PixelWriter<float> R(red), B(blue);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int c = colour(y, x);
            if (c == 0) R(y, x) = T(y, x);
            if (c == 2) B(y, x) = T(y, x);
        }
    for (int y = 6; y < H - 6; ++y)
        for (int x = 6; x < W - 6; ++x) {
            const int c = colour(y, x);
            if (c == 1) continue;
            const ipr::PixelWriter<float> &target = c == 0 ? B : R;   // the colour on the diagonals
            const auto diff = [&](int yy, int xx) { return T(yy, xx) - G(yy, xx); };
            const float dP = T(y - 1, x - 1) - T(y + 1, x + 1), dQ = T(y - 1, x + 1) - T(y + 1, x - 1);
            const float gP = G(y - 1, x - 1) - 2 * G(y, x) + G(y + 1, x + 1);
            const float gQ = G(y - 1, x + 1) - 2 * G(y, x) + G(y + 1, x - 1);
            const float pS = Eps + dP * dP + gP * gP, qS = Eps + dQ * dQ + gQ * gQ;
            const float nw = Eps + std::abs(dP) + std::abs(diff(y - 1, x - 1) - diff(y - 3, x - 3));
            const float se = Eps + std::abs(dP) + std::abs(diff(y + 1, x + 1) - diff(y + 3, x + 3));
            const float ne = Eps + std::abs(dQ) + std::abs(diff(y - 1, x + 1) - diff(y - 3, x + 3));
            const float sw = Eps + std::abs(dQ) + std::abs(diff(y + 1, x - 1) - diff(y + 3, x - 3));
            const float estP = (se * diff(y - 1, x - 1) + nw * diff(y + 1, x + 1)) / (nw + se);
            const float estQ = (sw * diff(y - 1, x + 1) + ne * diff(y + 1, x - 1)) / (ne + sw);
            const float pq = pS / (pS + qS);
            target(y, x) = std::max(floor, G(y, x) + pq * estQ + (1.f - pq) * estP);
        }
    // 5. Red and blue at green pixels: cardinal colour differences, direction of step 1.
    for (int y = 8; y < H - 8; ++y)
        for (int x = 8; x < W - 8; ++x) {
            if (colour(y, x) != 1) continue;
            const auto g = [&](int yy, int xx) { return G(yy, xx); };
            const float d = decided(y, x);
            const float gn = Eps + std::abs(g(y - 1, x) - g(y + 1, x)) + std::abs(g(y, x) - g(y - 2, x));
            const float gs = Eps + std::abs(g(y + 1, x) - g(y - 1, x)) + std::abs(g(y, x) - g(y + 2, x));
            const float gw = Eps + std::abs(g(y, x - 1) - g(y, x + 1)) + std::abs(g(y, x) - g(y, x - 2));
            const float ge = Eps + std::abs(g(y, x + 1) - g(y, x - 1)) + std::abs(g(y, x) - g(y, x + 2));
            for (const ipr::PixelWriter<float> *plane : {&R, &B}) {
                const auto diff = [&](int yy, int xx) { return (*plane)(yy, xx) - g(yy, xx); };
                const float v = (gs * diff(y - 1, x) + gn * diff(y + 1, x)) / (gn + gs);
                const float h = (ge * diff(y, x - 1) + gw * diff(y, x + 1)) / (gw + ge);
                (*plane)(y, x) = std::max(floor, g(y, x) + d * h + (1.f - d) * v);
            }
        }
    return ipr::merged({&red, &green, &blue});
}

// Robust sigma of the diagonal Haar coefficient: the finest noise band, little
// affected by sparse edges; constant 2x2 cells (clipped) are skipped.
float haarSigma(const ipr::Image &plane)
{
    std::vector<float> values;
    for (int y = 0; y + 1 < plane.height(); y += 2) {
        const float *a = plane.row<float>(y), *b = plane.row<float>(y + 1);
        for (int x = 0; x + 1 < plane.width(); x += 2) {
            if (a[x] == a[x + 1] && a[x] == b[x] && a[x] == b[x + 1]) continue;
            values.push_back(0.5f * (a[x] - a[x + 1] - b[x] + b[x + 1]));
        }
    }
    if (values.empty()) return 0.f;
    const auto mid = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    const float centre = *mid;
    for (float &v : values) v = std::abs(v - centre);
    std::nth_element(values.begin(), mid, values.end());
    return *mid / 0.6745f;
}

// Weight of the median: 1 where the luminance gradient (after a 1-pixel blur) is
// below FlatGradient noise sigmas, 0 above DetailGradient, linear between.
ipr::Image flatWeight(const ipr::Image &luma)
{
    const float sigma = haarSigma(luma);
    if (!(sigma > 0.f)) return ipr::filled(luma.size(), ipr::Depth::F32, 1, 1.0);
    const ipr::Image smooth = ipr::gaussianBlurred(luma, 1.0, ipr::BlurBorder::MirrorEdge);
    const ipr::Image gradient = ipr::magnitude(ipr::centralDifference(smooth, true), ipr::centralDifference(smooth, false));
    // (top - gradient) / span, as OpenCV folded it: one scale and one shift, made in double, applied
    // with one rounding.
    const double inverse = 1.0 / double((DetailGradient - FlatGradient) * sigma);
    const float scale = float(-inverse), shift = float(double(DetailGradient * sigma) * inverse);
    return ipr::mapped(gradient, [&](float g) { return std::min(std::max(std::fma(g, scale, shift), 0.f), 1.f); });
}

// One 3x3 median on R - G and B - G where the luminance is flat, green unchanged;
// red and blue bounded below by floor.
void flatChromaMedian(ipr::Image &rgb, float floor)
{
    std::vector<ipr::Image> c = ipr::splitChannels(rgb);
    const ipr::Image weight = flatWeight(ipr::mapped(c[0], c[1], c[2], [](float r, float g, float b) {
        const float sum = r + g * 2.f;
        return sum * 0.25f + b * 0.25f;
    }));
    for (int k : {0, 2}) {
        const ipr::Image diff = ipr::mapped(c[size_t(k)], c[1], [](float a, float g) { return a - g; });
        const ipr::Image median = ipr::median(diff, 3);
        const ipr::Image mixed = ipr::mapped(median, weight, diff, [](float m, float w, float d) { return m * w + d * (1.f - w); });
        c[size_t(k)] = ipr::mapped(mixed, c[1], [floor](float mix, float g) { return std::max(mix + g, floor); });
    }
    rgb = ipr::merged(c);
}

} // namespace

int workerThreads()
{
    const int host = int(std::thread::hardware_concurrency());
    return std::clamp(host - 1, 1, MaximumThreads);
}

bool interpolate(const ipr::Image &mosaic, const Pattern &pattern, ChromaMedian median, Range range, ipr::Image &rgb,
                 ipr::Image *beforeMedian, const std::function<bool()> &cancelled)
{
    if (mosaic.depth() != ipr::Depth::F32 || mosaic.channels() != 1 || mosaic.width() < 2 || mosaic.height() < 2) return false;
    const float floor = range == Range::NonNegative ? 0.f : -std::numeric_limits<float>::infinity();
    const int width = mosaic.width(), height = mosaic.height();
    rgb = ipr::Image::create(mosaic.size(), ipr::Depth::F32, 3);
    if (beforeMedian) *beforeMedian = ipr::Image::create(mosaic.size(), ipr::Depth::F32, 3);
    std::vector<ipr::Recti> tiles;
    for (int y = 0; y < height; y += TileSize)
        for (int x = 0; x < width; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, width - x), std::min(TileSize, height - y));
    const auto processTile = [&](const ipr::Recti &core) {
        // real neighbours where they exist, mirrored with the same parity at the border
        const ipr::Image padded = ipr::paddedRegion(mosaic, core, TileMargin, TileMargin, TileMargin, TileMargin);
        ipr::Image tile = rcd(padded, pattern, floor);
        const ipr::Recti inner(TileMargin, TileMargin, core.width, core.height);
        if (beforeMedian) {
            ipr::Image place = beforeMedian->mutableView(core);
            ipr::copyInto(tile.view(inner), place);
        }
        if (median == ChromaMedian::FlatOnly) flatChromaMedian(tile, floor);
        ipr::Image place = rgb.mutableView(core);
        ipr::copyInto(tile.view(inner), place);
    };
    // Tile by tile from a shared counter on a bounded pool, the calling thread
    // included; every thread is joined on every path, exceptions included. Only
    // the calling thread asks for cancellation (the callback need not be
    // thread-safe); the others read the shared flag.
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stopped{false};
    const auto work = [&](bool caller) {
        try {
            for (std::size_t i; (i = next.fetch_add(1)) < tiles.size();) {
                if (stopped.load() || (caller && cancelled && cancelled())) {
                    stopped.store(true);
                    return;
                }
                processTile(tiles[i]);
            }
        } catch (...) {
            stopped.store(true);
        }
    };
    std::vector<std::thread> pool;
    struct JoinAll {
        std::vector<std::thread> &threads;
        ~JoinAll()
        {
            for (auto &t : threads)
                if (t.joinable()) t.join();
        }
    } joinAll{pool};
    const int threads = std::min(workerThreads(), int(tiles.size()));
    pool.reserve(std::size_t(threads));
    for (int k = 1; k < threads; ++k) pool.emplace_back(work, false);
    work(true);
    for (auto &t : pool) t.join();
    return !stopped.load();
}

} // namespace Demosaic
