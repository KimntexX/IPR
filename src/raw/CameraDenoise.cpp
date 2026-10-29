#include "CameraDenoise.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <set>
#include <thread>
#include <vector>

namespace CameraDenoise {
namespace {
constexpr int Levels = 3;
constexpr int TileSize = 256;
constexpr float MadNormal = 0.67448975f;
constexpr float Kernel[5] = {.0625f, .25f, .375f, .25f, .0625f};

struct Noise
{
    cv::Vec3f sigma{};
    cv::Matx33f inverseCorrelation = cv::Matx33f::eye();
    float expectedJointEnergy = 0;
};

struct Pyramid
{
    std::array<cv::Mat, Levels> detail;
};

int reflect(int x, int length)
{
    if (length == 1) return 0;
    while (x < 0 || x >= length) x = x < 0 ? -x : 2 * length - 2 - x;
    return x;
}

bool validContext(const Context &c)
{
    if (!std::isfinite(c.iso) || c.iso < 0 ||
        !std::isfinite(c.pixelScale) || c.pixelScale <= 0 || c.pixelScale > 1)
        return false;
    for (double gain : c.gains) if (!std::isfinite(gain) || gain <= 0) return false;
    return true;
}

cv::Vec3f opponent(const cv::Vec3f &rgb, const cv::Vec3f &inverseGains)
{
    const cv::Vec3f x = rgb.mul(inverseGains);
    return {.25f * x[0] + .5f * x[1] + .25f * x[2],
            .5f * x[0] - .5f * x[2], x[1] - .5f * x[0] - .5f * x[2]};
}

// All filtering loops are local to this translation unit; no nested OpenCV pool.
bool blur(const cv::Mat &source, cv::Mat &output, int step,
          const std::function<bool()> &stop)
{
    cv::Mat horizontal(source.size(), CV_32FC3);
    output.create(source.size(), CV_32FC3);
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        const auto *in = source.ptr<cv::Vec3f>(y);
        auto *out = horizontal.ptr<cv::Vec3f>(y);
        for (int x = 0; x < source.cols; ++x) {
            cv::Vec3f value{};
            for (int k = -2; k <= 2; ++k) value += in[reflect(x + k * step, source.cols)] * Kernel[k + 2];
            out[x] = value;
        }
    }
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        auto *out = output.ptr<cv::Vec3f>(y);
        for (int x = 0; x < source.cols; ++x) {
            cv::Vec3f value{};
            for (int k = -2; k <= 2; ++k) value += horizontal.ptr<cv::Vec3f>(reflect(y + k * step, source.rows))[x] * Kernel[k + 2];
            out[x] = value;
        }
    }
    return true;
}

bool pyramid(cv::Mat current, Pyramid &out, const std::array<int, Levels> &steps,
             const std::function<bool()> &stop)
{
    for (int level = 0; level < Levels; ++level) {
        cv::Mat base;
        if (!blur(current, base, steps[std::size_t(level)], stop)) return false;
        auto &detail = out.detail[std::size_t(level)]; detail.create(current.size(), CV_32FC3);
        for (int y = 0; y < current.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *a = current.ptr<cv::Vec3f>(y), *b = base.ptr<cv::Vec3f>(y);
            auto *d = detail.ptr<cv::Vec3f>(y);
            for (int x = 0; x < current.cols; ++x) d[x] = a[x] - b[x];
        }
        current = std::move(base);
    }
    return true;
}

cv::Mat patch(const cv::Mat &source, const cv::Rect &core, int margin,
              const cv::Vec3f &inverseGains)
{
    cv::Mat out(core.height + 2 * margin, core.width + 2 * margin, CV_32FC3);
    for (int y = 0; y < out.rows; ++y) {
        const auto *in = source.ptr<cv::Vec3f>(reflect(core.y + y - margin, source.rows));
        auto *row = out.ptr<cv::Vec3f>(y);
        for (int x = 0; x < out.cols; ++x)
            row[x] = opponent(in[reflect(core.x + x - margin, source.cols)], inverseGains);
    }
    return out;
}

float median(std::vector<float> values)
{
    if (values.empty()) return 0;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    const float upper = *middle;
    return values.size() % 2 ? upper : .5f * upper + .5f * *std::max_element(values.begin(), middle);
}

struct PatchNoise
{
    cv::Vec3f center{}, sigma{};
    std::vector<cv::Vec3f> samples;
    double score = 0;
};

bool coherent(const PatchNoise &item, int channel, int width, int height, int lag)
{
    if (lag >= width || lag >= height || item.sigma[channel] <= 0) return false;
    const auto correlation = [&](bool horizontal) {
        double ab = 0, aa = 0, bb = 0;
        for (int y = 0; y < height - (horizontal ? 0 : lag); ++y)
            for (int x = 0; x < width - (horizontal ? lag : 0); ++x) {
                const double a = item.samples[std::size_t(y * width + x)][channel] - item.center[channel];
                const double b = item.samples[std::size_t((y + (horizontal ? 0 : lag)) * width + x + (horizontal ? lag : 0))][channel] - item.center[channel];
                ab += a * b; aa += a * a; bb += b * b;
            }
        return aa > 0 && bb > 0 ? std::abs(ab / std::sqrt(aa * bb)) : 0.;
    };
    // A highly repeating pattern is not a safe noise witness. Lag follows the
    // wavelet scale; no claim of separating every weak texture from sensor noise.
    return correlation(true) > .9 && correlation(false) > .9;
}

bool estimate(const cv::Mat &source, const cv::Vec3f &inverseGains,
              const std::array<int, Levels> &steps, int margin,
              std::array<Noise, Levels> &models, const std::function<bool()> &stop)
{
    std::array<std::vector<PatchNoise>, Levels> patches;
    const int width = std::min(64, source.cols), height = std::min(64, source.rows);
    std::set<std::pair<int, int>> origins;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x)
        origins.emplace((source.cols - width) * x / 4, (source.rows - height) * y / 4);
    for (auto origin : origins) {
        if (stop()) return false;
        Pyramid p;
        if (!pyramid(patch(source, {origin.first, origin.second, width, height}, margin, inverseGains), p, steps, stop)) return false;
        for (int level = 0; level < Levels; ++level) {
            const auto &d = p.detail[std::size_t(level)];
            if (!cv::checkRange(d)) return false;
            PatchNoise item;
            std::array<std::vector<float>, 3> samples;
            for (int y = margin; y < margin + height; ++y) for (int x = margin; x < margin + width; ++x) {
                const auto v = d.at<cv::Vec3f>(y, x); item.samples.push_back(v);
                for (int c = 0; c < 3; ++c) samples[std::size_t(c)].push_back(v[c]);
            }
            for (int c = 0; c < 3; ++c) {
                auto &v = samples[std::size_t(c)];
                item.center[c] = median(v);
                for (float &value : v) value = std::abs(value - item.center[c]);
                item.sigma[c] = median(v) / MadNormal;
                item.score += double(item.sigma[c]) * item.sigma[c];
            }
            if (!std::isfinite(item.score)) return false;
            bool structured = false;
            for (int c = 0; c < 3; ++c) structured = structured || coherent(item, c, width, height, 2 * steps[std::size_t(level)]);
            if (!structured) patches[std::size_t(level)].push_back(std::move(item));
        }
    }
    for (int level = 0; level < Levels; ++level) {
        auto &items = patches[std::size_t(level)];
        if (items.empty()) continue; // Abstain at this scale rather than fit texture.
        std::sort(items.begin(), items.end(), [](const PatchNoise &a, const PatchNoise &b) { return a.score < b.score; });
        const std::size_t retained = std::max(std::size_t(1), (items.size() + 3) / 4);
        auto &model = models[std::size_t(level)];
        for (int c = 0; c < 3; ++c) {
            std::vector<float> sigmas;
            for (std::size_t i = 0; i < retained; ++i) sigmas.push_back(items[i].sigma[c]);
            model.sigma[c] = median(sigmas);
        }
        cv::Matx33d sums = cv::Matx33d::zeros();
        for (std::size_t i = 0; i < retained; ++i) for (auto v : items[i].samples) {
            cv::Vec3d z{};
            for (int c = 0; c < 3; ++c) if (model.sigma[c] > 0)
                z[c] = std::clamp((double(v[c]) - items[i].center[c]) / model.sigma[c], -3., 3.);
            for (int c = 0; c < 3; ++c) for (int k = 0; k < 3; ++k) sums(c, k) += z[c] * z[k];
        }
        cv::Matx33f correlation = cv::Matx33f::zeros(), regularized = cv::Matx33f::eye();
        for (int c = 0; c < 3; ++c) {
            correlation(c, c) = model.sigma[c] > 0 ? 1.f : 0.f;
            for (int k = 0; k < c; ++k) {
                const double denominator = std::sqrt(sums(c, c) * sums(k, k));
                const float rho = denominator > 0 ? float(std::clamp(sums(c, k) / denominator, -.95, .95)) : 0.f;
                correlation(c, k) = correlation(k, c) = rho;
                // Shrink toward diagonal: stable even with strongly correlated channels.
                regularized(c, k) = regularized(k, c) = .25f * rho;
            }
        }
        bool invertible = false;
        model.inverseCorrelation = regularized.inv(cv::DECOMP_CHOLESKY, &invertible);
        if (!invertible) return false;
        for (int c = 0; c < 3; ++c) for (int k = 0; k < 3; ++k)
            model.expectedJointEnergy += model.inverseCorrelation(c, k) * correlation(k, c);
    }
    return true;
}

bool boxMean(const cv::Mat &source, cv::Mat &out, int radius, const std::function<bool()> &stop)
{
    cv::Mat horizontal(source.size(), source.type()); out.create(source.size(), source.type());
    const float scale = 1.f / float(2 * radius + 1);
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        for (int x = 0; x < source.cols; ++x) {
            cv::Vec4f sum{};
            for (int k = -radius; k <= radius; ++k) sum += source.at<cv::Vec4f>(y, reflect(x + k, source.cols));
            horizontal.at<cv::Vec4f>(y, x) = sum * scale;
        }
    }
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        for (int x = 0; x < source.cols; ++x) {
            cv::Vec4f sum{};
            for (int k = -radius; k <= radius; ++k) sum += horizontal.at<cv::Vec4f>(reflect(y + k, source.rows), x);
            out.at<cv::Vec4f>(y, x) = sum * scale;
        }
    }
    return true;
}

bool tile(const cv::Mat &source, cv::Mat &delta, const cv::Rect &core, int margin,
          const cv::Vec3f &inverseGains, const std::array<int, Levels> &steps,
          const std::array<Noise, Levels> &models, const cv::Vec3f &strength,
          int radius, const std::function<bool()> &stop)
{
    Pyramid p;
    if (!pyramid(patch(source, core, margin, inverseGains), p, steps, stop)) return false;
    for (int y = 0; y < core.height; ++y)
        std::fill_n(delta.ptr<cv::Vec3f>(core.y + y) + core.x, core.width, cv::Vec3f{});
    for (int level = 0; level < Levels; ++level) {
        const auto &model = models[std::size_t(level)];
        if (model.expectedJointEnergy <= 0) continue;
        const auto &detail = p.detail[std::size_t(level)];
        cv::Mat energy(detail.size(), CV_32FC4);
        for (int y = 0; y < detail.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *d = detail.ptr<cv::Vec3f>(y); auto *e = energy.ptr<cv::Vec4f>(y);
            for (int x = 0; x < detail.cols; ++x) {
                cv::Vec3f z{};
                for (int c = 0; c < 3; ++c) if (model.sigma[c] > 0)
                    z[c] = float(std::clamp(double(d[x][c]) / model.sigma[c], -1e6, 1e6));
                const auto joint = model.inverseCorrelation * z;
                e[x] = {z[0] * z[0], z[1] * z[1], z[2] * z[2], std::max(0.f, z.dot(joint))};
            }
        }
        cv::Mat local;
        if (!boxMean(energy, local, radius, stop)) return false;
        for (int y = 0; y < core.height; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            auto *out = delta.ptr<cv::Vec3f>(core.y + y) + core.x;
            const auto *d = detail.ptr<cv::Vec3f>(margin + y) + margin;
            const auto *e = local.ptr<cv::Vec4f>(margin + y) + margin;
            for (int x = 0; x < core.width; ++x) {
                const float shared = std::clamp(1.f - model.expectedJointEnergy / std::max(e[x][3], model.expectedJointEnergy), 0.f, 1.f);
                for (int c = 0; c < 3; ++c) if (strength[c] > 0 && model.sigma[c] > 0) {
                    const float individual = std::clamp(1.f - 1.f / std::max(e[x][c], 1.f), 0.f, 1.f);
                    out[x][c] -= strength[c] * (1.f - std::max(individual, shared)) * d[x][c];
                }
            }
        }
    }
    return true;
}

struct JoinAll
{
    std::vector<std::thread> &threads;
    ~JoinAll() { for (auto &thread : threads) if (thread.joinable()) thread.join(); }
};
} // namespace

int workerThreads()
{
    const unsigned host = std::thread::hardware_concurrency();
    return int(std::max(1u, std::min(8u, host > 1 ? host - 1 : 1)));
}

bool identity(const Settings &settings)
{
    return settings.luminance == 0 && settings.chrominance == 0;
}

bool apply(cv::Mat &cameraRgb, const Settings &settings, const Context &context,
           const std::function<bool()> &cancelled)
{
    if (cameraRgb.empty() || cameraRgb.dims != 2 || cameraRgb.type() != CV_32FC3 ||
        settings.luminance < 0 || settings.luminance > 100 || settings.chrominance < 0 || settings.chrominance > 100) return false;
    if (identity(settings)) return true;
    try {
        const auto stop = [&] { return cancelled && cancelled(); };
        if (stop() || !validContext(context) || !cv::checkRange(cameraRgb)) return false;
        const cv::Vec3f gains(float(context.gains[0]), float(.5 * context.gains[1] + .5 * context.gains[3]), float(context.gains[2]));
        cv::Vec3f inverseGains;
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(gains[c]) || gains[c] <= 0) return false;
            inverseGains[c] = 1.f / gains[c];
            if (!std::isfinite(inverseGains[c])) return false;
        }
        std::array<int, Levels> steps{};
        for (int i = 0; i < Levels; ++i) steps[std::size_t(i)] = std::max(1, int(std::lround((1 << i) * context.pixelScale)));
        const int radius = std::max(1, int(std::lround(3 * context.pixelScale)));
        int margin = radius;
        for (int step : steps) margin += 2 * step;
        std::array<Noise, Levels> models;
        if (!estimate(cameraRgb, inverseGains, steps, margin, models, stop)) return false;
        bool measured = false;
        for (const auto &m : models) for (int c = 0; c < 3; ++c) if (m.sigma[c] > 0) measured = true;
        if (!measured) return true;
        cv::Mat delta(cameraRgb.size(), CV_32FC3);
        std::vector<cv::Rect> tiles;
        for (int y = 0; y < cameraRgb.rows; y += TileSize) for (int x = 0; x < cameraRgb.cols; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, cameraRgb.cols - x), std::min(TileSize, cameraRgb.rows - y));
        std::atomic<std::size_t> next{0}; std::atomic<bool> stopped{false};
        const cv::Vec3f strength(settings.luminance / 100.f, settings.chrominance / 100.f, settings.chrominance / 100.f);
        const auto work = [&](bool caller) {
            try {
                const auto halt = [&] {
                    if (stopped.load()) return true;
                    if (caller && stop()) { stopped.store(true); return true; }
                    return false;
                };
                for (std::size_t i; (i = next.fetch_add(1)) < tiles.size();) {
                    if (halt() || !tile(cameraRgb, delta, tiles[i], margin, inverseGains, steps, models, strength, radius, halt)) {
                        stopped.store(true); return;
                    }
                }
            } catch (...) { stopped.store(true); }
        };
        std::vector<std::thread> pool; JoinAll join{pool};
        const int workers = std::min(workerThreads(), int(tiles.size()));
        pool.reserve(std::size_t(workers));
        try { for (int i = 1; i < workers; ++i) pool.emplace_back(work, false); }
        catch (...) { stopped.store(true); throw; }
        work(true);
        for (auto &thread : pool) if (thread.joinable()) thread.join();
        if (stopped.load() || stop() || !cv::checkRange(delta)) return false;
        // Balance signed corrections per opponent axis, moving corrections toward
        // zero, not adding an offset to unchanged areas. Preserve DC without clamps.
        cv::Vec3d positive{}, negative{};
        for (int y = 0; y < delta.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *d = delta.ptr<cv::Vec3f>(y);
            for (int x = 0; x < delta.cols; ++x) for (int c = 0; c < 3; ++c) {
                positive[c] += std::max(0., double(d[x][c])); negative[c] += std::max(0., -double(d[x][c]));
            }
        }
        cv::Vec3f plus, minus;
        for (int c = 0; c < 3; ++c) {
            plus[c] = positive[c] > 0 ? float(std::min(1., negative[c] / positive[c])) : 1.f;
            minus[c] = negative[c] > 0 ? float(std::min(1., positive[c] / negative[c])) : 1.f;
        }
        for (int y = 0; y < delta.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *in = cameraRgb.ptr<cv::Vec3f>(y); auto *out = delta.ptr<cv::Vec3f>(y);
            for (int x = 0; x < delta.cols; ++x) {
                cv::Vec3f d = out[x];
                for (int c = 0; c < 3; ++c) d[c] *= d[c] > 0 ? plus[c] : minus[c];
                out[x] = in[x] + cv::Vec3f(d[0] + d[1] - .5f * d[2], d[0] + .5f * d[2], d[0] - d[1] - .5f * d[2]).mul(gains);
            }
        }
        if (stop() || !cv::checkRange(delta)) return false;
        cameraRgb = std::move(delta);
        return true;
    } catch (...) { return false; }
}
} // namespace CameraDenoise
