#include "Demosaic.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
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
float direction(const cv::Mat &vh, int y, int x)
{
    const float own = vh.at<float>(y, x);
    const float n = 0.25f * (vh.at<float>(y - 1, x - 1) + vh.at<float>(y - 1, x + 1) +
                             vh.at<float>(y + 1, x - 1) + vh.at<float>(y + 1, x + 1));
    return std::abs(0.5f - own) < std::abs(0.5f - n) ? n : own;
}

// Green at red and blue pixels: gradient-weighted directional estimates, each the
// even blend of the neighbour green scaled by the low-pass ratio (RCD) and of the
// neighbour green corrected by half the same-colour difference toward the centre
// (Laplacian). The ratio alone was softer than Lightroom at ISO 800 to 3200
// (0.89 to 0.92 of its fine-band energy), the Laplacian alone less accurate.
void interpolateGreen(const cv::Mat &cfa, const cv::Mat &vh, const cv::Mat &lpf, const Pattern &p, cv::Mat &G)
{
    const int H = cfa.rows, W = cfa.cols;
    const auto T = [&](int y, int x) { return cfa.at<float>(y, x); };
    for (int y = 5; y < H - 5; ++y)
        for (int x = 5; x < W - 5; ++x) {
            if (p[size_t(y & 1)][size_t(x & 1)] == 1) continue;
            const float L = lpf.at<float>(y, x);
            const auto ratio = [&](int yy, int xx) {
                const float l = lpf.at<float>(yy, xx);
                return 1.f + (L - l) / (Eps + L + l);
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
            G.at<float>(y, x) = std::max(0.f, d * hEst + (1.f - d) * vEst);
        }
}

// RCD on a padded tile, values ~[0, 1]: green from ratio-corrected directional
// estimates, the direction decided per pixel and pulled toward the most decided
// diagonal neighbour; red and blue from gradient-weighted colour differences.
cv::Mat rcd(const cv::Mat &cfa, const Pattern &p)
{
    const int H = cfa.rows, W = cfa.cols;
    const auto colour = [&](int y, int x) { return p[size_t(y & 1)][size_t(x & 1)]; };
    const auto T = [&](int y, int x) { return cfa.at<float>(y, x); };
    cv::Mat vh(H, W, CV_32F, cv::Scalar(0.5f)), lpf(H, W, CV_32F, cv::Scalar(0.f));
    cv::Mat G = cfa.clone();
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
            vh.at<float>(y, x) = v / (v + h);
        }
    // 2. Low-pass of the mosaic (all colours mixed 1:2:1).
    for (int y = 1; y < H - 1; ++y)
        for (int x = 1; x < W - 1; ++x)
            lpf.at<float>(y, x) = T(y, x) + 0.5f * (T(y - 1, x) + T(y + 1, x) + T(y, x - 1) + T(y, x + 1)) +
                                  0.25f * (T(y - 1, x - 1) + T(y - 1, x + 1) + T(y + 1, x - 1) + T(y + 1, x + 1));
    // 3. Green at red and blue pixels.
    interpolateGreen(cfa, vh, lpf, p, G);
    const auto decided = [&](int y, int x) { return direction(vh, y, x); };
    // 4. Red at blue pixels and blue at red pixels: diagonal colour differences.
    cv::Mat R(H, W, CV_32F, cv::Scalar(0.f)), B(H, W, CV_32F, cv::Scalar(0.f));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int c = colour(y, x);
            if (c == 0) R.at<float>(y, x) = T(y, x);
            if (c == 2) B.at<float>(y, x) = T(y, x);
        }
    for (int y = 6; y < H - 6; ++y)
        for (int x = 6; x < W - 6; ++x) {
            const int c = colour(y, x);
            if (c == 1) continue;
            cv::Mat &target = c == 0 ? B : R;   // the colour on the diagonals
            const auto diff = [&](int yy, int xx) { return T(yy, xx) - G.at<float>(yy, xx); };
            const float dP = T(y - 1, x - 1) - T(y + 1, x + 1), dQ = T(y - 1, x + 1) - T(y + 1, x - 1);
            const float gP = G.at<float>(y - 1, x - 1) - 2 * G.at<float>(y, x) + G.at<float>(y + 1, x + 1);
            const float gQ = G.at<float>(y - 1, x + 1) - 2 * G.at<float>(y, x) + G.at<float>(y + 1, x - 1);
            const float pS = Eps + dP * dP + gP * gP, qS = Eps + dQ * dQ + gQ * gQ;
            const float nw = Eps + std::abs(dP) + std::abs(diff(y - 1, x - 1) - diff(y - 3, x - 3));
            const float se = Eps + std::abs(dP) + std::abs(diff(y + 1, x + 1) - diff(y + 3, x + 3));
            const float ne = Eps + std::abs(dQ) + std::abs(diff(y - 1, x + 1) - diff(y - 3, x + 3));
            const float sw = Eps + std::abs(dQ) + std::abs(diff(y + 1, x - 1) - diff(y + 3, x - 3));
            const float estP = (se * diff(y - 1, x - 1) + nw * diff(y + 1, x + 1)) / (nw + se);
            const float estQ = (sw * diff(y - 1, x + 1) + ne * diff(y + 1, x - 1)) / (ne + sw);
            const float pq = pS / (pS + qS);
            target.at<float>(y, x) = std::max(0.f, G.at<float>(y, x) + pq * estQ + (1.f - pq) * estP);
        }
    // 5. Red and blue at green pixels: cardinal colour differences, direction of step 1.
    for (int y = 8; y < H - 8; ++y)
        for (int x = 8; x < W - 8; ++x) {
            if (colour(y, x) != 1) continue;
            const auto g = [&](int yy, int xx) { return G.at<float>(yy, xx); };
            const float d = decided(y, x);
            const float gn = Eps + std::abs(g(y - 1, x) - g(y + 1, x)) + std::abs(g(y, x) - g(y - 2, x));
            const float gs = Eps + std::abs(g(y + 1, x) - g(y - 1, x)) + std::abs(g(y, x) - g(y + 2, x));
            const float gw = Eps + std::abs(g(y, x - 1) - g(y, x + 1)) + std::abs(g(y, x) - g(y, x - 2));
            const float ge = Eps + std::abs(g(y, x + 1) - g(y, x - 1)) + std::abs(g(y, x) - g(y, x + 2));
            for (cv::Mat *plane : {&R, &B}) {
                const auto diff = [&](int yy, int xx) { return plane->at<float>(yy, xx) - g(yy, xx); };
                const float v = (gs * diff(y - 1, x) + gn * diff(y + 1, x)) / (gn + gs);
                const float h = (ge * diff(y, x - 1) + gw * diff(y, x + 1)) / (gw + ge);
                plane->at<float>(y, x) = std::max(0.f, g(y, x) + d * h + (1.f - d) * v);
            }
        }
    cv::Mat rgb;
    cv::merge(std::vector<cv::Mat>{R, G, B}, rgb);
    return rgb;
}

// Robust sigma of the diagonal Haar coefficient: the finest noise band, little
// affected by sparse edges; constant 2x2 cells (clipped) are skipped.
float haarSigma(const cv::Mat &plane)
{
    std::vector<float> values;
    for (int y = 0; y + 1 < plane.rows; y += 2) {
        const float *a = plane.ptr<float>(y), *b = plane.ptr<float>(y + 1);
        for (int x = 0; x + 1 < plane.cols; x += 2) {
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
cv::Mat flatWeight(const cv::Mat &luma)
{
    const float sigma = haarSigma(luma);
    if (!(sigma > 0.f)) return cv::Mat(luma.size(), CV_32F, cv::Scalar(1.f));
    cv::Mat smooth, gx, gy, gradient;
    cv::GaussianBlur(luma, smooth, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    const cv::Mat dx = (cv::Mat_<float>(1, 3) << -0.5f, 0.f, 0.5f), dy = dx.t();
    cv::filter2D(smooth, gx, CV_32F, dx, cv::Point(-1, -1), 0, cv::BORDER_REFLECT);
    cv::filter2D(smooth, gy, CV_32F, dy, cv::Point(-1, -1), 0, cv::BORDER_REFLECT);
    cv::magnitude(gx, gy, gradient);
    cv::Mat weight = (DetailGradient * sigma - gradient) / ((DetailGradient - FlatGradient) * sigma);
    return cv::min(cv::max(weight, 0.f), 1.f);
}

// One 3x3 median on R - G and B - G where the luminance is flat, green unchanged.
void flatChromaMedian(cv::Mat &rgb)
{
    std::vector<cv::Mat> c;
    cv::split(rgb, c);
    const cv::Mat weight = flatWeight(0.25f * (c[0] + 2.f * c[1] + c[2]));
    for (int k : {0, 2}) {
        const cv::Mat diff = c[size_t(k)] - c[1];
        cv::Mat median;
        cv::medianBlur(diff, median, 3);   // not in place
        c[size_t(k)] = cv::max(median.mul(weight) + diff.mul(1.f - weight) + c[1], 0.f);
    }
    cv::merge(c, rgb);
}

} // namespace

int workerThreads()
{
    const int host = int(std::thread::hardware_concurrency());
    return std::clamp(host - 1, 1, MaximumThreads);
}

bool interpolate(const cv::Mat &mosaic, const Pattern &pattern, ChromaMedian median, cv::Mat &rgb,
                 cv::Mat *beforeMedian, const std::function<bool()> &cancelled)
{
    if (mosaic.type() != CV_32FC1 || mosaic.cols < 2 || mosaic.rows < 2) return false;
    const int width = mosaic.cols, height = mosaic.rows;
    rgb.create(height, width, CV_32FC3);
    if (beforeMedian) beforeMedian->create(height, width, CV_32FC3);
    std::vector<cv::Rect> tiles;
    for (int y = 0; y < height; y += TileSize)
        for (int x = 0; x < width; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, width - x), std::min(TileSize, height - y));
    const auto processTile = [&](const cv::Rect &core) {
        cv::Mat padded;
        // real neighbours where they exist, mirrored with the same parity at the border
        cv::copyMakeBorder(mosaic(core), padded, TileMargin, TileMargin, TileMargin, TileMargin, cv::BORDER_REFLECT_101);
        cv::Mat tile = rcd(padded, pattern);
        const cv::Rect inner(TileMargin, TileMargin, core.width, core.height);
        if (beforeMedian) tile(inner).copyTo((*beforeMedian)(core));
        if (median == ChromaMedian::FlatOnly) flatChromaMedian(tile);
        tile(inner).copyTo(rgb(core));
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
