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

constexpr int ChromaMedianIso = 800;
constexpr int MaximumThreads = 8;
constexpr int TileSize = 512;
constexpr int TileMargin = 12;   // even: keeps the pattern; covers the 9-pixel stencils and the medians
constexpr float Eps = 1e-5f;

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
    const auto decided = [&](int y, int x) {
        const float own = vh.at<float>(y, x);
        const float n = 0.25f * (vh.at<float>(y - 1, x - 1) + vh.at<float>(y - 1, x + 1) +
                                 vh.at<float>(y + 1, x - 1) + vh.at<float>(y + 1, x + 1));
        return std::abs(0.5f - own) < std::abs(0.5f - n) ? n : own;
    };
    // 3. Green at red and blue pixels.
    for (int y = 5; y < H - 5; ++y)
        for (int x = 5; x < W - 5; ++x) {
            if (colour(y, x) == 1) continue;
            const float L = lpf.at<float>(y, x);
            const auto ratio = [&](int yy, int xx) {
                const float l = lpf.at<float>(yy, xx);
                return 1.f + (L - l) / (Eps + L + l);
            };
            const float nEst = T(y - 1, x) * ratio(y - 2, x), sEst = T(y + 1, x) * ratio(y + 2, x);
            const float wEst = T(y, x - 1) * ratio(y, x - 2), eEst = T(y, x + 1) * ratio(y, x + 2);
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
            const float d = decided(y, x);
            G.at<float>(y, x) = std::max(0.f, d * hEst + (1.f - d) * vEst);
        }
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

// 3x3 median on R - G and B - G, green unchanged.
void chromaMedian(cv::Mat &rgb, int passes)
{
    std::vector<cv::Mat> c;
    cv::split(rgb, c);
    for (int k : {0, 2}) {
        cv::Mat diff = c[size_t(k)] - c[1];
        for (int n = 0; n < passes; ++n) {
            cv::Mat median;
            cv::medianBlur(diff, median, 3);   // not in place
            diff = median;
        }
        c[size_t(k)] = cv::max(diff + c[1], 0.f);
    }
    cv::merge(c, rgb);
}

} // namespace

int chromaMedianPasses(int iso)
{
    return iso >= ChromaMedianIso ? 1 : 0;
}

int workerThreads()
{
    const int host = int(std::thread::hardware_concurrency());
    return std::clamp(host - 1, 1, MaximumThreads);
}

bool interpolate(Pixel *image, int width, int height, const Pattern &pattern,
                 int medianPasses, const std::function<bool()> &cancelled)
{
    if (!image || width < 2 || height < 2) return false;
    const auto colour = [&](int y, int x) { return pattern[size_t(y & 1)][size_t(x & 1)]; };
    cv::Mat cfa(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        float *o = cfa.ptr<float>(y);
        for (int x = 0; x < width; ++x) o[x] = image[size_t(y) * size_t(width) + size_t(x)][colour(y, x)] / 65535.f;
    }
    std::vector<cv::Rect> tiles;
    for (int y = 0; y < height; y += TileSize)
        for (int x = 0; x < width; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, width - x), std::min(TileSize, height - y));
    const auto processTile = [&](const cv::Rect &core) {
        cv::Mat padded;
        // real neighbours where they exist, mirrored with the same parity at the border
        cv::copyMakeBorder(cfa(core), padded, TileMargin, TileMargin, TileMargin, TileMargin, cv::BORDER_REFLECT_101);
        cv::Mat rgb = rcd(padded, pattern);
        if (medianPasses > 0) chromaMedian(rgb, medianPasses);
        for (int y = 0; y < core.height; ++y) {
            const auto *s = rgb.ptr<cv::Vec3f>(y + TileMargin) + TileMargin;
            for (int x = 0; x < core.width; ++x) {
                auto &px = image[size_t(core.y + y) * size_t(width) + size_t(core.x + x)];
                for (int c = 0; c < 3; ++c)
                    px[c] = std::uint16_t(std::clamp(std::lround(s[x][c] * 65535.f), 0L, 65535L));
            }
        }
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
