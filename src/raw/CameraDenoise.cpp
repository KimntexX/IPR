#include "CameraDenoise.h"

#include "Pixels.h"

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
constexpr int Levels = 3;
constexpr int TileSize = 256;
constexpr float MadNormal = 0.67448975f;
constexpr float Kernel[5] = {.0625f, .25f, .375f, .25f, .0625f};

struct Noise
{
    ipr::Vec3f sigma{};
    ipr::Matrix33f inverseCorrelation = ipr::Matrix33f::identity();
    float expectedJointEnergy = 0;
};

constexpr int CurveSamples = 128;
struct NoiseCurve
{
    std::array<Noise, CurveSamples> values;
    Noise pooled;
    float low = 0, high = 0;
    float transition = 0;
    bool valid = false;
    bool pooledValid = false;
    bool constant = true;
};
using Energy = ipr::Vec4f; // Intensity and joint energies, then their expectations.

struct Pyramid
{
    std::array<ipr::Image, Levels> detail;
    ipr::Image signal; // Smoothed intensity guide; never the noisy detail itself.
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

ipr::Vec3f opponent(const ipr::Vec3f &x)
{
    return {.25f * x[0] + .5f * x[1] + .25f * x[2],
            .5f * x[0] - .5f * x[2], x[1] - .5f * x[0] - .5f * x[2]};
}

// All filtering loops are local to this translation unit; no nested OpenCV pool.
bool blur(const ipr::Image &source, ipr::Image &output, int step,
          const std::function<bool()> &stop)
{
    const int width = source.width(), height = source.height();
    ipr::Image horizontal = ipr::Image::create(source.size(), ipr::Depth::F32, 3);
    output = ipr::Image::create(source.size(), ipr::Depth::F32, 3);
    for (int y = 0; y < height; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        const auto *in = source.row<ipr::Vec3f>(y);
        auto *out = horizontal.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < width; ++x) {
            ipr::Vec3f value{};
            for (int k = -2; k <= 2; ++k) value += in[reflect(x + k * step, width)] * Kernel[k + 2];
            out[x] = value;
        }
    }
    const ipr::PixelReader<ipr::Vec3f> across(horizontal);
    for (int y = 0; y < height; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        auto *out = output.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < width; ++x) {
            ipr::Vec3f value{};
            for (int k = -2; k <= 2; ++k) value += across(reflect(y + k * step, height), x) * Kernel[k + 2];
            out[x] = value;
        }
    }
    return true;
}

bool pyramid(ipr::Image first, Pyramid &out, const std::array<int, Levels> &steps,
             const std::function<bool()> &stop)
{
    ipr::Image current = std::move(first);
    for (int level = 0; level < Levels; ++level) {
        ipr::Image base;
        if (!blur(current, base, steps[std::size_t(level)], stop)) return false;
        if (level == 0) out.signal = ipr::Image::create(base.size(), ipr::Depth::F32, 1);
        auto &detail = out.detail[std::size_t(level)];
        detail = ipr::Image::create(current.size(), ipr::Depth::F32, 3);
        for (int y = 0; y < current.height(); ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *a = current.row<ipr::Vec3f>(y), *b = base.row<ipr::Vec3f>(y);
            auto *d = detail.mutableRow<ipr::Vec3f>(y);
            float *signal = level == 0 ? out.signal.mutableRow<float>(y) : nullptr;
            for (int x = 0; x < current.width(); ++x) {
                d[x] = a[x] - b[x];
                if (signal) signal[x] = b[x][0];
            }
        }
        current = std::move(base);
    }
    return true;
}

ipr::Image patch(const ipr::Image &source, const ipr::Recti &core, int margin)
{
    ipr::Image out = ipr::Image::create({core.width + 2 * margin, core.height + 2 * margin}, ipr::Depth::F32, 3);
    for (int y = 0; y < out.height(); ++y) {
        const auto *in = source.row<ipr::Vec3f>(reflect(core.y + y - margin, source.height()));
        auto *row = out.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < out.width(); ++x)
            row[x] = opponent(in[reflect(core.x + x - margin, source.width())]);
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
    ipr::Vec3f center{}, sigma{};
    std::vector<ipr::Vec3f> samples;
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

bool fromCovariance(const ipr::Matrix33f &covariance, Noise &model)
{
    ipr::Matrix33f correlation = ipr::Matrix33f::zeros(), regularized = ipr::Matrix33f::identity();
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
    model.inverseCorrelation = regularized.inv(&invertible);   // OpenCV inverted a 3 x 3 by cofactors whatever the method asked
    if (!invertible) return false;
    model.expectedJointEnergy = 0;
    for (int c=0;c<3;++c) for (int k=0;k<3;++k)
        model.expectedJointEnergy += model.inverseCorrelation(c,k)*correlation(k,c);
    return std::isfinite(model.expectedJointEnergy);
}

struct Knot { float signal = 0; ipr::Matrix33f covariance = ipr::Matrix33f::zeros(); };

Knot fit(std::vector<const PatchNoise *> items)
{
    std::sort(items.begin(),items.end(),[](const auto *a,const auto *b){return a->score<b->score;});
    const std::size_t retained=std::max(std::size_t(1),(items.size()+3)/4);
    ipr::Vec3f sigma; std::vector<float> signals;
    for(std::size_t i=0;i<retained;++i)signals.push_back(items[i]->signal);
    Knot result; result.signal=median(signals);
    for(int c=0;c<3;++c) {
        std::vector<float> values;
        for(std::size_t i=0;i<retained;++i)values.push_back(items[i]->sigma[c]);
        sigma[c]=median(values);
    }
    ipr::Matrix33d sums=ipr::Matrix33d::zeros();
    for(std::size_t i=0;i<retained;++i)for(auto v:items[i]->samples) {
        ipr::Vec3d z{};
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

bool floorDarkCovariance(Knot &dark, const Knot &pooled)
{
    ipr::Vec3d scale(1,1,1); bool changed=false;
    for(int c=0;c<3;++c) {
        const double original=dark.covariance(c,c);
        const double floor=std::max(original,double(pooled.covariance(c,c)));
        if(original>0)scale[c]=std::sqrt(floor/original);
        changed=changed||floor>original;
    }
    // Keep the dark witnesses' correlation, not a mixture of different signal
    // populations' correlations. Zero-variance axes have zero cross terms.
    for(int c=0;c<3;++c) {
        for(int k=0;k<c;++k)
            dark.covariance(c,k)=dark.covariance(k,c)=float(double(dark.covariance(c,k))*scale[c]*scale[k]);
        dark.covariance(c,c)=std::max(dark.covariance(c,c),pooled.covariance(c,c));
    }
    return changed;
}

bool estimate(const ipr::Image &source,
              const std::array<int, Levels> &steps, int margin,
              std::array<NoiseCurve, Levels> &models, const std::function<bool()> &stop,
              const std::function<void(const NoiseModelSummary &)> &observer)
{
    std::array<std::vector<PatchNoise>, Levels> patches;
    std::array<std::vector<PatchNoise>, Levels> allPatches;
    const int width = std::min(64, source.width()), height = std::min(64, source.height());
    std::set<std::pair<int, int>> origins;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x)
        origins.emplace((source.width() - width) * x / 4, (source.height() - height) * y / 4);
    for (auto origin : origins) {
        if (stop()) return false;
        ipr::Image input=patch(source,{origin.first,origin.second,width,height},margin);
        std::vector<float> signals; std::array<std::vector<float>,4> quarters;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const float v=input.row<ipr::Vec3f>(margin+y)[margin+x][0]; signals.push_back(v);
            const int quadrant=(x>=width/2)+2*(y>=height/2);
            quarters[std::size_t(quadrant)].push_back(v);
        }
        const float signal=median(signals);
        float low=signal,high=signal;
        for(const auto &q:quarters)if(!q.empty()){const float v=median(q);low=std::min(low,v);high=std::max(high,v);}
        Pyramid p;
        if (!pyramid(std::move(input), p, steps, stop)) return false;
        bool flat=true; float signalUncertainty=0;
        for (int level = 0; level < Levels; ++level) {
            const auto &d = p.detail[std::size_t(level)];
            if (!ipr::allFinite(d)) return false;
            PatchNoise item; item.signal=signal;
            std::array<std::vector<float>, 3> samples;
            for (int y = margin; y < margin + height; ++y) for (int x = margin; x < margin + width; ++x) {
                const auto v = d.row<ipr::Vec3f>(y)[x]; item.samples.push_back(v);
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
            if (!structured) allPatches[std::size_t(level)].push_back(item);
            if (flat && !structured) patches[std::size_t(level)].push_back(std::move(item));
        }
    }
    for (int level = 0; level < Levels; ++level) {
        auto &items = patches[std::size_t(level)];
        NoiseModelSummary summary; summary.level = level;
        summary.flatCandidates = int(items.size());
        const auto &all = allPatches[std::size_t(level)];
        summary.candidates = int(all.size());
        std::vector<const PatchNoise *> globalGroup;
        for (const auto &item : all) globalGroup.push_back(&item);
        Knot global;
        if (!globalGroup.empty()) {
            global = fit(std::move(globalGroup));
            summary.globalSignal=global.signal;
            for (int c=0;c<3;++c) summary.globalSigma[c]=std::sqrt(global.covariance(c,c));
        }
        auto &curve=models[std::size_t(level)];
        curve.pooledValid=!all.empty()&&fromCovariance(global.covariance,curve.pooled);
        summary.globalFallbackAvailable=curve.pooledValid;
        if(items.size()<3) {
            if (observer) observer(summary);
            continue; // No conditional support: use the pooled model if available.
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
        if(knots.size()>1 && !all.empty())
            summary.darkFloorApplied=floorDarkCovariance(knots.front(),global);
        curve.low=items.front().signal-uncertainty;curve.high=items.back().signal+uncertainty;
        curve.transition=uncertainty;
        if(!std::isfinite(curve.low)||!std::isfinite(curve.high)||!(curve.high>curve.low))return false;
        for(int i=0;i<CurveSamples;++i) {
            const float signal=curve.low+(curve.high-curve.low)*(float(i)/(CurveSamples-1));
            ipr::Matrix33f covariance=knots.front().covariance;
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
                ipr::Vec3f sigma;
                for(int c=0;c<3;++c)sigma[c]=std::sqrt(knot.covariance(c,c));
                summary.nodes.emplace_back(knot.signal,sigma);
            }
            observer(summary);
        }
    }
    return true;
}

void accumulateEnergy(Energy &result,const Noise &model,const ipr::Vec3f &detail,float weight)
{
    if(weight<=0)return;
    ipr::Vec3f z{};
    for(int c=0;c<3;++c)if(model.sigma[c]>0)
        z[c]=float(std::clamp(double(detail[c])/model.sigma[c],-1e6,1e6));
    if(model.sigma[0]>0){result[0]+=weight*z[0]*z[0];result[2]+=weight;}
    result[1]+=weight*std::max(0.f,z.dot(model.inverseCorrelation*z));
    result[3]+=weight*model.expectedJointEnergy;
}

Energy energyAt(const NoiseCurve &curve,float signal,const ipr::Vec3f &detail)
{
    Energy result{};
    if(!std::isfinite(signal))return result;
    if(!curve.valid) {
        if(curve.pooledValid)accumulateEnergy(result,curve.pooled,detail,1);
        return result;
    }
    if(signal<curve.low || signal>curve.high) {
        if(!curve.pooledValid)return result;
        // Transition only OUTSIDE the conditional range, in its already
        // measured intensity-uncertainty units; no hard jump at the boundary.
        const double distance=signal<curve.low?double(curve.low)-signal:double(signal)-curve.high;
        const float t=curve.transition>0?float(std::clamp(distance/double(curve.transition),0.,1.)):1.f;
        accumulateEnergy(result,curve.values[signal<curve.low?0:CurveSamples-1],detail,1-t);
        accumulateEnergy(result,curve.pooled,detail,t);
        return result;
    }
    const float position=curve.constant?0.f:std::clamp((signal-curve.low)/(curve.high-curve.low)*(CurveSamples-1),0.f,float(CurveSamples-1));
    const int lower=int(position),upper=std::min(lower+1,CurveSamples-1);
    const float t=curve.constant?0.f:position-lower;
    for(int side=0;side<2;++side) {
        const float weight=side?t:1-t;
        if(weight<=0)continue;
        accumulateEnergy(result,curve.values[std::size_t(side?upper:lower)],detail,weight);
    }
    return result;
}

bool boxMean(const ipr::Image &source, ipr::Image &out, int radius, const std::function<bool()> &stop)
{
    const int width = source.width(), height = source.height();
    ipr::Image horizontal = ipr::Image::create(source.size(), ipr::Depth::F32, 4);
    out = ipr::Image::create(source.size(), ipr::Depth::F32, 4);
    const float scale = 1.f / float(2 * radius + 1);
    for (int y = 0; y < height; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        const Energy *in = source.row<Energy>(y);
        Energy *row = horizontal.mutableRow<Energy>(y);
        for (int x = 0; x < width; ++x) {
            Energy sum{};
            for (int k = -radius; k <= radius; ++k) sum += in[reflect(x + k, width)];
            row[x] = sum * scale;
        }
    }
    const ipr::PixelReader<Energy> across(horizontal);
    for (int y = 0; y < height; ++y) {
        if ((y & 15) == 0 && stop()) return false;
        Energy *row = out.mutableRow<Energy>(y);
        for (int x = 0; x < width; ++x) {
            Energy sum{};
            for (int k = -radius; k <= radius; ++k) sum += across(reflect(y + k, height), x);
            row[x] = sum * scale;
        }
    }
    return true;
}

bool tile(const ipr::Image &source, const ipr::PixelWriter<float> &delta, const ipr::Recti &core, int margin,
          const std::array<int, Levels> &steps,
          const std::array<NoiseCurve, Levels> &models, float strength,
          int radius, const std::function<bool()> &stop)
{
    Pyramid p;
    if (!pyramid(patch(source, core, margin), p, steps, stop)) return false;
    for (int y = 0; y < core.height; ++y)
        std::fill_n(&delta(core.y + y, core.x), core.width, 0.f);
    for (int level = 0; level < Levels; ++level) {
        const auto &model = models[std::size_t(level)];
        if (!model.valid && !model.pooledValid) continue;
        const auto &detail = p.detail[std::size_t(level)];
        ipr::Image energy = ipr::Image::create(detail.size(), ipr::Depth::F32, 4), fallbackEnergy;
        for (int y = 0; y < detail.height(); ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *d = detail.row<ipr::Vec3f>(y); auto *e = energy.mutableRow<Energy>(y);
            const float *signals = p.signal.row<float>(y);
            for (int x = 0; x < detail.width(); ++x) {
                const float signal=signals[x];
                const bool fallback=model.valid&&model.pooledValid&&(signal<model.low||signal>model.high);
                const auto sample=energyAt(model,signal,d[x]);
                if(fallback) {
                    // Do not let normalized pooled energies alter the local
                    // decisions of already-supported conditional pixels.
                    if(fallbackEnergy.empty())fallbackEnergy=ipr::Image::zeros(detail.size(),ipr::Depth::F32,4);
                    fallbackEnergy.mutableRow<Energy>(y)[x]=sample;
                    e[x]=Energy{};
                } else e[x]=sample;
            }
        }
        ipr::Image local,fallbackLocal;
        if (!boxMean(energy, local, radius, stop)) return false;
        if(!fallbackEnergy.empty()&&!boxMean(fallbackEnergy,fallbackLocal,radius,stop))return false;
        for (int y = 0; y < core.height; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            auto *out = &delta(core.y + y, core.x);
            const auto *d = detail.row<ipr::Vec3f>(margin + y) + margin;
            const auto *e = local.row<Energy>(margin + y) + margin;
            const auto *current=energy.row<Energy>(margin+y)+margin;
            const auto *pooled=fallbackLocal.empty()?nullptr:fallbackLocal.row<Energy>(margin+y)+margin;
            const auto *pooledCurrent=fallbackEnergy.empty()?nullptr:fallbackEnergy.row<Energy>(margin+y)+margin;
            const float *signals = p.signal.row<float>(margin + y) + margin;
            for (int x = 0; x < core.width; ++x) {
                const float signal=signals[x];
                const bool usePooled=pooled&&pooledCurrent&&(signal<model.low||signal>model.high);
                const auto &stats=usePooled?pooled[x]:e[x];
                const auto &pixel=usePooled?pooledCurrent[x]:current[x];
                // A second-moment gate, not a chi-square probability.
                const float shared = stats[3]>0?std::clamp(1.f-stats[3]/std::max(stats[1],stats[3]),0.f,1.f):0.f;
                if (pixel[2] > 0 && stats[2] > 0) {
                    const float individual = std::clamp(1.f-stats[2]/std::max(stats[0],stats[2]),0.f,1.f);
                    out[x] -= strength * (1.f - std::max(individual, shared)) * d[x][0];
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

bool apply(ipr::Image &cameraRgb, int luminance, const Context &context,
           const std::function<bool()> &cancelled)
{
    if (cameraRgb.empty() || cameraRgb.depth() != ipr::Depth::F32 || cameraRgb.channels() != 3 ||
        luminance < 0 || luminance > 100) return false;
    if (luminance == 0) return true;
    try {
        const auto stop = [&] { return cancelled && cancelled(); };
        if (stop() || !validContext(context) || !ipr::allFinite(cameraRgb)) return false;
        std::array<int, Levels> steps{};
        for (int i = 0; i < Levels; ++i) steps[std::size_t(i)] = std::max(1, int(std::lround((1 << i) * context.pixelScale)));
        const int radius = std::max(1, int(std::lround(3 * context.pixelScale)));
        int margin = radius;
        for (int step : steps) margin += 2 * step;
        std::array<NoiseCurve, Levels> models;
        if (!estimate(cameraRgb, steps, margin, models, stop, context.noiseModelObserver)) return false;
        bool measured = false;
        for (const auto &curve : models) {
            if(curve.valid)for(const auto &m:curve.values)for(int c=0;c<3;++c)if(m.sigma[c]>0)measured=true;
            if(curve.pooledValid)for(int c=0;c<3;++c)if(curve.pooled.sigma[c]>0)measured=true;
        }
        if (!measured) {
            // Nonzero requests must detach even on abstention: the caller may
            // apply optical corrections in place to the returned camera buffer.
            if(stop())return false;
            auto unchanged=cameraRgb.clone();
            if(stop())return false;
            cameraRgb=std::move(unchanged);
            return true;
        }
        ipr::Image delta = ipr::Image::create(cameraRgb.size(), ipr::Depth::F32, 1);
        const ipr::PixelWriter<float> deltaPixels(delta);
        const int width = cameraRgb.width(), height = cameraRgb.height();
        std::vector<ipr::Recti> tiles;
        for (int y = 0; y < height; y += TileSize) for (int x = 0; x < width; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, width - x), std::min(TileSize, height - y));
        std::atomic<std::size_t> next{0}; std::atomic<bool> stopped{false};
        const float strength = luminance / 100.f;
        const auto work = [&](bool caller) {
            try {
                const auto halt = [&] {
                    if (stopped.load()) return true;
                    if (caller && stop()) { stopped.store(true); return true; }
                    return false;
                };
                for (std::size_t i; (i = next.fetch_add(1)) < tiles.size();) {
                    if (halt() || !tile(cameraRgb, deltaPixels, tiles[i], margin, steps, models, strength, radius, halt)) {
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
        if (stopped.load() || stop() || !ipr::allFinite(delta)) return false;
        // Balance the signed corrections, moving them toward zero, not adding an
        // offset to unchanged areas. Preserve DC without clamps.
        double positive = 0, negative = 0;
        for (int y = 0; y < height; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *d = delta.row<float>(y);
            for (int x = 0; x < width; ++x) {
                positive += std::max(0., double(d[x])); negative += std::max(0., -double(d[x]));
            }
        }
        const float plus = positive > 0 ? float(std::min(1., negative / positive)) : 1.f;
        const float minus = negative > 0 ? float(std::min(1., positive / negative)) : 1.f;
        ipr::Image output = ipr::Image::create(cameraRgb.size(), ipr::Depth::F32, 3);
        for (int y = 0; y < height; ++y) {
            if ((y & 15) == 0 && stop()) return false;
            const auto *in = cameraRgb.row<ipr::Vec3f>(y); const auto *d = delta.row<float>(y);
            auto *out = output.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < width; ++x)
                out[x] = in[x] + ipr::Vec3f::all(d[x] * (d[x] > 0 ? plus : minus));
        }
        if (stop() || !ipr::allFinite(output)) return false;
        cameraRgb = std::move(output);
        return true;
    } catch (...) { return false; }
}
} // namespace CameraDenoise
