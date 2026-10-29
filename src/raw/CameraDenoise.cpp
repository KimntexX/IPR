#include "CameraDenoise.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <set>
#include <thread>
#include <vector>

namespace CameraDenoise {
namespace {
constexpr int Levels = 5;
// Camera colour fluctuations may span wider regions than fine intensity noise.
// These extra scales do not turn into extra luminance smoothing.
constexpr int LuminanceLevels = 3;
constexpr int TileSize = 256;
constexpr float MadNormal = 0.67448975f;
constexpr float Kernel[5] = {.0625f, .25f, .375f, .25f, .0625f};

struct Noise
{
    cv::Vec3f sigma{};
    cv::Matx33f inverseCorrelation = cv::Matx33f::eye();
    float expectedJointEnergy = 0;
};

constexpr int CurveSamples = 128;
struct NoiseCurve
{
    std::array<Noise, CurveSamples> values;
    float low = 0, high = 0;
    bool valid = false;
    bool constant = true;
};
using Energy = cv::Vec<float, 8>; // Three energies, joint, three expectations, joint expectation.

struct Pyramid
{
    std::array<cv::Mat, Levels> detail;
    cv::Mat signal; // Smoothed intensity guide; never the noisy detail itself.
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
    return true;
}

cv::Vec3f opponent(const cv::Vec3f &x)
{
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
        if (steps[std::size_t(level)] == 0) break;
        cv::Mat base;
        if (!blur(current, base, steps[std::size_t(level)], stop)) return false;
        if (level == 0) out.signal.create(base.size(), CV_32F);
        auto &detail = out.detail[std::size_t(level)]; detail.create(current.size(), CV_32FC3);
        for (int y = 0; y < current.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *a = current.ptr<cv::Vec3f>(y), *b = base.ptr<cv::Vec3f>(y);
            auto *d = detail.ptr<cv::Vec3f>(y);
            for (int x = 0; x < current.cols; ++x) {
                d[x] = a[x] - b[x];
                if (level == 0) out.signal.at<float>(y,x) = b[x][0];
            }
        }
        current = std::move(base);
    }
    return true;
}

cv::Mat patch(const cv::Mat &source, const cv::Rect &core, int margin)
{
    cv::Mat out(core.height + 2 * margin, core.width + 2 * margin, CV_32FC3);
    for (int y = 0; y < out.rows; ++y) {
        const auto *in = source.ptr<cv::Vec3f>(reflect(core.y + y - margin, source.rows));
        auto *row = out.ptr<cv::Vec3f>(y);
        for (int x = 0; x < out.cols; ++x)
            row[x] = opponent(in[reflect(core.x + x - margin, source.cols)]);
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
    float signal = 0;
    float signalUncertainty = 0;
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

bool fromCovariance(const cv::Matx33f &covariance, Noise &model)
{
    cv::Matx33f correlation = cv::Matx33f::zeros(), regularized = cv::Matx33f::eye();
    for (int c = 0; c < 3; ++c) {
        model.sigma[c] = std::sqrt(std::max(0.f, covariance(c,c)));
        if (!std::isfinite(model.sigma[c])) return false;
        correlation(c,c) = model.sigma[c] > 0 ? 1.f : 0.f;
        for (int k = 0; k < c; ++k) {
            const double denominator = double(model.sigma[c]) * model.sigma[k];
            const float rho = denominator > 0 ? float(std::clamp(double(covariance(c,k))/denominator,-.95,.95)) : 0.f;
            correlation(c,k) = correlation(k,c) = rho;
            regularized(c,k) = regularized(k,c) = .25f*rho;
        }
    }
    bool invertible = false;
    model.inverseCorrelation = regularized.inv(cv::DECOMP_CHOLESKY, &invertible);
    if (!invertible) return false;
    model.expectedJointEnergy = 0;
    for (int c=0;c<3;++c) for (int k=0;k<3;++k)
        model.expectedJointEnergy += model.inverseCorrelation(c,k)*correlation(k,c);
    return std::isfinite(model.expectedJointEnergy);
}

struct Knot { float signal = 0; cv::Matx33f covariance = cv::Matx33f::zeros(); };

Knot fit(std::vector<const PatchNoise *> items)
{
    std::sort(items.begin(),items.end(),[](const auto *a,const auto *b){return a->score<b->score;});
    const std::size_t retained=std::max(std::size_t(1),(items.size()+3)/4);
    cv::Vec3f sigma; std::vector<float> signals;
    for(std::size_t i=0;i<retained;++i)signals.push_back(items[i]->signal);
    Knot result; result.signal=median(signals);
    for(int c=0;c<3;++c) {
        std::vector<float> values;
        for(std::size_t i=0;i<retained;++i)values.push_back(items[i]->sigma[c]);
        sigma[c]=median(values);
    }
    cv::Matx33d sums=cv::Matx33d::zeros();
    for(std::size_t i=0;i<retained;++i)for(auto v:items[i]->samples) {
        cv::Vec3d z{};
        for(int c=0;c<3;++c)if(sigma[c]>0)z[c]=std::clamp((double(v[c])-items[i]->center[c])/sigma[c],-3.,3.);
        for(int c=0;c<3;++c)for(int k=0;k<3;++k)sums(c,k)+=z[c]*z[k];
    }
    for(int c=0;c<3;++c) {
        result.covariance(c,c)=sigma[c]*sigma[c];
        for(int k=0;k<c;++k) {
            const double denominator=std::sqrt(sums(c,c)*sums(k,k));
            const float rho=denominator>0?float(std::clamp(sums(c,k)/denominator,-.95,.95)):0.f;
            result.covariance(c,k)=result.covariance(k,c)=rho*sigma[c]*sigma[k];
        }
    }
    return result;
}

bool estimate(const cv::Mat &source,
              const std::array<int, Levels> &steps, int margin,
              std::array<NoiseCurve, Levels> &models, const std::function<bool()> &stop,
              const std::function<void(const NoiseModelSummary &)> &observer)
{
    std::array<std::vector<PatchNoise>, Levels> patches;
    std::array<std::vector<PatchNoise>, Levels> allPatches;
    const int width = std::min(64, source.cols), height = std::min(64, source.rows);
    std::set<std::pair<int, int>> origins;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x)
        origins.emplace((source.cols - width) * x / 4, (source.rows - height) * y / 4);
    for (auto origin : origins) {
        if (stop()) return false;
        const auto input=patch(source,{origin.first,origin.second,width,height},margin);
        std::vector<float> signals; std::array<std::vector<float>,4> quarters;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const float v=input.at<cv::Vec3f>(margin+y,margin+x)[0]; signals.push_back(v);
            const int quadrant=(x>=width/2)+2*(y>=height/2);
            quarters[std::size_t(quadrant)].push_back(v);
        }
        const float signal=median(signals);
        float low=signal,high=signal;
        for(const auto &q:quarters)if(!q.empty()){const float v=median(q);low=std::min(low,v);high=std::max(high,v);}
        Pyramid p;
        if (!pyramid(input, p, steps, stop)) return false;
        bool flat=true; float signalUncertainty=0;
        for (int level = 0; level < Levels; ++level) {
            if (steps[std::size_t(level)] == 0) break;
            const auto &d = p.detail[std::size_t(level)];
            if (!cv::checkRange(d)) return false;
            PatchNoise item; item.signal=signal;
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
            if(level==0) {
                signalUncertainty=3*item.sigma[0]+1e-7f*std::max(1.f,std::abs(signal));
                flat=high-low<=signalUncertainty;
            }
            item.signalUncertainty=signalUncertainty;
            bool structured = false;
            for (int c = 0; c < 3; ++c) structured = structured || coherent(item, c, width, height, 2 * steps[std::size_t(level)]);
            if (observer && !structured) allPatches[std::size_t(level)].push_back(item);
            if (flat && !structured) patches[std::size_t(level)].push_back(std::move(item));
        }
    }
    for (int level = 0; level < Levels; ++level) {
        if (steps[std::size_t(level)] == 0) break;
        auto &items = patches[std::size_t(level)];
        NoiseModelSummary summary; summary.level = level;
        summary.flatCandidates = int(items.size());
        if (observer) {
            const auto &all = allPatches[std::size_t(level)];
            summary.candidates = int(all.size());
            std::vector<const PatchNoise *> group;
            for (const auto &item : all) group.push_back(&item);
            if (!group.empty()) {
                const auto global = fit(std::move(group));
                summary.globalSignal=global.signal;
                for (int c=0;c<3;++c) summary.globalSigma[c]=std::sqrt(global.covariance(c,c));
            }
        }
        if(items.size()<3) {
            if (observer) observer(summary);
            continue; // No qualified support: do not borrow another signal level.
        }
        std::sort(items.begin(),items.end(),[](const auto &a,const auto &b){return a.signal<b.signal;});
        std::vector<float> uncertainties;
        for(const auto &item:items)uncertainties.push_back(item.signalUncertainty);
        const float uncertainty=median(uncertainties);
        const int bands=items.size()>=9 && items.back().signal-items.front().signal>uncertainty?3:1;
        std::vector<Knot> knots;
        for(int band=0;band<bands;++band) {
            std::vector<const PatchNoise *> group;
            for(std::size_t i=items.size()*std::size_t(band)/std::size_t(bands);
                i<items.size()*std::size_t(band+1)/std::size_t(bands);++i)group.push_back(&items[i]);
            const auto knot=fit(std::move(group));
            if(!knots.empty() && knot.signal-knots.back().signal<=1e-7f*std::max(1.f,std::abs(knot.signal)))
                knots.back().covariance=(knots.back().covariance+knot.covariance)*.5f;
            else knots.push_back(knot);
        }
        auto &curve=models[std::size_t(level)];
        curve.low=items.front().signal-uncertainty;curve.high=items.back().signal+uncertainty;
        if(!std::isfinite(curve.low)||!std::isfinite(curve.high)||!(curve.high>curve.low))return false;
        for(int i=0;i<CurveSamples;++i) {
            const float signal=curve.low+(curve.high-curve.low)*(float(i)/(CurveSamples-1));
            cv::Matx33f covariance=knots.front().covariance;
            if(signal>=knots.back().signal)covariance=knots.back().covariance;
            else for(std::size_t k=1;k<knots.size();++k)if(signal>=knots[k-1].signal&&signal<knots[k].signal) {
                const float t=(signal-knots[k-1].signal)/(knots[k].signal-knots[k-1].signal);
                covariance=knots[k-1].covariance*(1-t)+knots[k].covariance*t;break;
            }
            if(!fromCovariance(covariance,curve.values[std::size_t(i)]))return false;
        }
        curve.valid=true;
        curve.constant=knots.size()==1;
        if (observer) {
            summary.supported=true; summary.low=curve.low; summary.high=curve.high;
            for (const auto &knot : knots) {
                cv::Vec3f sigma;
                for(int c=0;c<3;++c)sigma[c]=std::sqrt(knot.covariance(c,c));
                summary.nodes.emplace_back(knot.signal,sigma);
            }
            observer(summary);
        }
    }
    return true;
}

Energy energyAt(const NoiseCurve &curve,float signal,const cv::Vec3f &detail)
{
    Energy result{};
    if(!curve.valid || !std::isfinite(signal) || signal<curve.low || signal>curve.high)return result;
    const float position=curve.constant?0.f:std::clamp((signal-curve.low)/(curve.high-curve.low)*(CurveSamples-1),0.f,float(CurveSamples-1));
    const int lower=int(position),upper=std::min(lower+1,CurveSamples-1);
    const float t=curve.constant?0.f:position-lower;
    for(int side=0;side<2;++side) {
        const float weight=side?t:1-t;
        if(weight<=0)continue;
        const auto &model=curve.values[std::size_t(side?upper:lower)];
        cv::Vec3f z{};
        for(int c=0;c<3;++c)if(model.sigma[c]>0) {
            z[c]=float(std::clamp(double(detail[c])/model.sigma[c],-1e6,1e6));
            result[c]+=weight*z[c]*z[c];result[c+4]+=weight;
        }
        result[3]+=weight*std::max(0.f,z.dot(model.inverseCorrelation*z));
        result[7]+=weight*model.expectedJointEnergy;
    }
    return result;
}

bool boxMean(const cv::Mat &source, cv::Mat &out, int radius, const std::function<bool()> &stop)
{
    cv::Mat horizontal(source.size(), source.type()); out.create(source.size(), source.type());
    const float scale = 1.f / float(2 * radius + 1);
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        for (int x = 0; x < source.cols; ++x) {
            Energy sum{};
            for (int k = -radius; k <= radius; ++k) sum += source.at<Energy>(y, reflect(x + k, source.cols));
            horizontal.at<Energy>(y, x) = sum * scale;
        }
    }
    for (int y = 0; y < source.rows; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        for (int x = 0; x < source.cols; ++x) {
            Energy sum{};
            for (int k = -radius; k <= radius; ++k) sum += horizontal.at<Energy>(reflect(y + k, source.rows), x);
            out.at<Energy>(y, x) = sum * scale;
        }
    }
    return true;
}

bool tile(const cv::Mat &source, cv::Mat &delta, const cv::Rect &core, int margin,
          const std::array<int, Levels> &steps,
          const std::array<NoiseCurve, Levels> &models, const cv::Vec3f &strength,
          int radius, const std::function<bool()> &stop)
{
    Pyramid p;
    if (!pyramid(patch(source, core, margin), p, steps, stop)) return false;
    for (int y = 0; y < core.height; ++y)
        std::fill_n(delta.ptr<cv::Vec3f>(core.y + y) + core.x, core.width, cv::Vec3f{});
    for (int level = 0; level < Levels; ++level) {
        const auto &model = models[std::size_t(level)];
        if (!model.valid) continue;
        const auto &detail = p.detail[std::size_t(level)];
        cv::Mat energy(detail.size(), CV_32FC(8));
        for (int y = 0; y < detail.rows; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *d = detail.ptr<cv::Vec3f>(y); auto *e = energy.ptr<Energy>(y);
            for (int x = 0; x < detail.cols; ++x) {
                e[x] = energyAt(model,p.signal.at<float>(y,x),d[x]);
            }
        }
        cv::Mat local;
        if (!boxMean(energy, local, radius, stop)) return false;
        for (int y = 0; y < core.height; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            auto *out = delta.ptr<cv::Vec3f>(core.y + y) + core.x;
            const auto *d = detail.ptr<cv::Vec3f>(margin + y) + margin;
            const auto *e = local.ptr<Energy>(margin + y) + margin;
            const auto *current=energy.ptr<Energy>(margin+y)+margin;
            for (int x = 0; x < core.width; ++x) {
                const float shared = e[x][7]>0?std::clamp(1.f-e[x][7]/std::max(e[x][3],e[x][7]),0.f,1.f):0.f;
                for (int c = 0; c < 3; ++c) if ((c != 0 || level < LuminanceLevels) && strength[c] > 0 && current[x][c+4] > 0 && e[x][c+4]>0) {
                    const float individual = std::clamp(1.f-e[x][c+4]/std::max(e[x][c],e[x][c+4]),0.f,1.f);
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
        std::array<int, Levels> steps{};
        const int activeLevels = settings.chrominance > 0 ? Levels : LuminanceLevels;
        for (int i = 0; i < activeLevels; ++i) steps[std::size_t(i)] = std::max(1, int(std::lround((1 << i) * context.pixelScale)));
        const int radius = std::max(1, int(std::lround(3 * context.pixelScale)));
        int margin = radius;
        for (int step : steps) margin += 2 * step;
        std::array<NoiseCurve, Levels> models;
        if (!estimate(cameraRgb, steps, margin, models, stop, context.noiseModelObserver)) return false;
        bool measured = false;
        for (const auto &curve : models) if(curve.valid)
            for(const auto &m:curve.values)for(int c=0;c<3;++c)if(m.sigma[c]>0)measured=true;
        if (!measured) {
            // Nonzero requests must detach even on abstention: the caller may
            // apply optical corrections in place to the returned camera buffer.
            if(stop())return false;
            auto unchanged=cameraRgb.clone();
            if(stop())return false;
            cameraRgb=std::move(unchanged);
            return true;
        }
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
                    if (halt() || !tile(cameraRgb, delta, tiles[i], margin, steps, models, strength, radius, halt)) {
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
                out[x] = in[x] + cv::Vec3f(d[0] + d[1] - .5f * d[2], d[0] + .5f * d[2], d[0] - d[1] - .5f * d[2]);
            }
        }
        if (stop() || !cv::checkRange(delta)) return false;
        cameraRgb = std::move(delta);
        return true;
    } catch (...) { return false; }
}
} // namespace CameraDenoise
