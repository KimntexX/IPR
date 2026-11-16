#include "HdrMerge.h"

#include "Filters.h"
#include "Neighbourhood.h"
#include "Parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>

namespace HdrMerge {
namespace {

// A sample's weight falls from 1 at the motion limit to 0 at this multiple of it.
constexpr float fadeEnd = 1.5f;
// A smoothed luminance is the mean of the decision window: its variance, of one pixel's.
constexpr float smoothedVariance = 1.f / float(decisionWindow * decisionWindow);
// Below this share of the clip a channel is mostly noise: no ratio is read from it.
constexpr float ratioFloor = 0.01f;
constexpr int minimumRatioSamples = 50;

float luminance(const ipr::Vec3f &v)
{
    return 0.25f * v[0] + 0.5f * v[1] + 0.25f * v[2];
}

double meanExposure(const ipr::Vec3d &e)
{
    return 0.25 * e[0] + 0.5 * e[1] + 0.25 * e[2];
}

// What the decisions read of a frame: its brightest channel around each pixel (a demosaicked
// neighbour of a clipped site is not trustworthy either), and its smoothed luminance.
struct Decisions
{
    ipr::Image peak;
    ipr::Image luminance;
};

Decisions decisionsOf(const ipr::Image &frame)
{
    ipr::Image brightest = ipr::Image::create(frame.size(), ipr::Depth::F32, 1);
    ipr::Image level = ipr::Image::create(frame.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < frame.height(); ++y) {
        const ipr::Vec3f *in = frame.row<ipr::Vec3f>(y);
        float *top = brightest.mutableRow<float>(y), *lum = level.mutableRow<float>(y);
        for (int x = 0; x < frame.width(); ++x) {
            top[x] = std::max({in[x][0], in[x][1], in[x][2]});
            lum[x] = luminance(in[x]);
        }
    }
    const ipr::Size window(decisionWindow, decisionWindow);
    return {ipr::maximum(brightest, window, ipr::Beyond::Nothing), ipr::boxMean(level, window, ipr::BlurBorder::LastPixel)};
}

bool validFrames(const std::vector<Frame> &frames, const Options &options)
{
    if (frames.empty() || options.referenceIndex < 0 || options.referenceIndex >= int(frames.size()) ||
        !(options.clipLevel > 0.0) || !(options.knee > 0.0 && options.knee < 1.0) || !(options.motionSigma >= 0.0) ||
        !(options.exposureTolerance >= 0.0))
        return false;
    const ipr::Size size = frames.front().pixels.size();
    for (const Frame &frame : frames) {
        if (frame.pixels.empty() || frame.pixels.depth() != ipr::Depth::F32 || frame.pixels.channels() != 3 ||
            frame.pixels.size() != size)
            return false;
        for (int c = 0; c < 3; ++c)
            if (!(frame.exposure[c] > 0.0 && std::isfinite(frame.exposure[c])))
                return false;
        if (!(frame.noise.shot >= 0.0 && frame.noise.read > 0.0))
            return false;
    }
    return true;
}

} // namespace

bool merge(const std::vector<Frame> &frames, const Options &options, ipr::Image &merged, Statistics *statistics)
{
    if (!validFrames(frames, options))
        return false;
    const int count = int(frames.size());
    const ipr::Size size = frames.front().pixels.size();
    const int reference = options.referenceIndex;

    std::vector<Decisions> decisions(frames.size());
    ipr::parallelRows(count, [&](int first, int last) {
        for (int i = first; i < last; ++i)
            decisions[size_t(i)] = decisionsOf(frames[size_t(i)].pixels);
    });

    std::vector<float> exposure(frames.size()), shot(frames.size()), read(frames.size());
    std::vector<ipr::Vec3f> inverse(frames.size());
    int shortest = 0;
    for (int i = 0; i < count; ++i) {
        const Frame &frame = frames[size_t(i)];
        exposure[size_t(i)] = float(meanExposure(frame.exposure));
        shot[size_t(i)] = float(frame.noise.shot);
        read[size_t(i)] = float(frame.noise.read);
        inverse[size_t(i)] = ipr::Vec3f(float(1.0 / frame.exposure[0]), float(1.0 / frame.exposure[1]), float(1.0 / frame.exposure[2]));
        if (exposure[size_t(i)] < exposure[size_t(shortest)])
            shortest = i;
    }
    const float clip = float(options.clipLevel);
    const float kneeWidth = float(options.clipLevel * (1.0 - options.knee));
    const float sigma = float(options.motionSigma);
    const float tolerance = float(options.exposureTolerance);
    const bool rejecting = sigma > 0.f && count > 1;

    merged = ipr::Image::create(size, ipr::Depth::F32, 3);
    std::mutex totals;
    quint64 clippedEverywhere = 0, motionRejected = 0;
    std::vector<double> share(frames.size(), 0.0);

    ipr::parallelRows(size.height, [&](int first, int last) {
        std::vector<float> trust(frames.size()), weight(frames.size()), level(frames.size());
        std::vector<double> shareHere(frames.size(), 0.0);
        quint64 clippedHere = 0, rejectedHere = 0;
        for (int y = first; y < last; ++y) {
            ipr::Vec3f *out = merged.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < size.width; ++x) {
                // How far each frame is from its clip, and the scene's level it shows.
                int longestTrusted = -1, pivot = -1;
                for (int i = 0; i < count; ++i) {
                    const float peak = decisions[size_t(i)].peak.row<float>(y)[x];
                    trust[size_t(i)] = std::clamp((clip - peak) / kneeWidth, 0.f, 1.f);
                    level[size_t(i)] = decisions[size_t(i)].luminance.row<float>(y)[x] / exposure[size_t(i)];
                    if (trust[size_t(i)] >= 1.f) {
                        if (longestTrusted < 0 || exposure[size_t(i)] > exposure[size_t(longestTrusted)])
                            longestTrusted = i;
                        // The pivot of the motion test: the reference, else the frame nearest
                        // below it in exposure that still sees the highlight.
                        if (exposure[size_t(i)] <= exposure[size_t(reference)] &&
                            (pivot < 0 || exposure[size_t(i)] > exposure[size_t(pivot)]))
                            pivot = i;
                    }
                }
                if (pivot < 0)
                    pivot = shortest;
                if (trust[size_t(shortest)] <= 0.f)
                    ++clippedHere;
                // The scene's level from the least noisy frame that saw it, for the weights:
                // a frame's own value would bias them with its noise.
                const float scene = std::max(0.f, level[size_t(longestTrusted >= 0 ? longestTrusted : shortest)]);

                float total = 0.f;
                for (int i = 0; i < count; ++i) {
                    const float e = exposure[size_t(i)];
                    const float variance = shot[size_t(i)] * e * scene + read[size_t(i)];
                    weight[size_t(i)] = trust[size_t(i)] * e * e / variance;
                }
                if (rejecting) {
                    const float pe = exposure[size_t(pivot)];
                    const float pivotVariance = (shot[size_t(pivot)] * pe * scene + read[size_t(pivot)]) * smoothedVariance / (pe * pe);
                    for (int i = 0; i < count; ++i) {
                        if (i == pivot || weight[size_t(i)] <= 0.f)
                            continue;
                        const float e = exposure[size_t(i)];
                        const float variance = (shot[size_t(i)] * e * scene + read[size_t(i)]) * smoothedVariance / (e * e);
                        const float allowed = sigma * std::sqrt(variance + pivotVariance) + tolerance * scene;
                        const float distance = std::abs(level[size_t(i)] - level[size_t(pivot)]);
                        if (distance > allowed) {
                            ++rejectedHere;
                            weight[size_t(i)] *= std::max(0.f, (fadeEnd * allowed - distance) / ((fadeEnd - 1.f) * allowed));
                        }
                    }
                }
                for (int i = 0; i < count; ++i)
                    total += weight[size_t(i)];
                if (!(total > 0.f)) {
                    // Clipped everywhere: the shortest frame, as it is.
                    std::fill(weight.begin(), weight.end(), 0.f);
                    weight[size_t(shortest)] = 1.f;
                    total = 1.f;
                }
                ipr::Vec3f sum(0.f, 0.f, 0.f);
                for (int i = 0; i < count; ++i) {
                    if (weight[size_t(i)] <= 0.f)
                        continue;
                    const ipr::Vec3f v = frames[size_t(i)].pixels.row<ipr::Vec3f>(y)[x];
                    const ipr::Vec3f &scale = inverse[size_t(i)];
                    sum += ipr::Vec3f(v[0] * scale[0], v[1] * scale[1], v[2] * scale[2]) * weight[size_t(i)];
                    shareHere[size_t(i)] += double(weight[size_t(i)] / total);
                }
                out[x] = sum * (1.f / total);
            }
        }
        const std::lock_guard<std::mutex> lock(totals);
        clippedEverywhere += clippedHere;
        motionRejected += rejectedHere;
        for (size_t i = 0; i < share.size(); ++i)
            share[i] += shareHere[i];
    });
    if (statistics) {
        statistics->pixels = quint64(size.area());
        statistics->clippedEverywhere = clippedEverywhere;
        statistics->motionRejected = motionRejected;
        for (double &s : share)
            s /= std::max(1.0, double(size.area()));
        statistics->share = std::move(share);
    }
    return true;
}

Proxy proxyOf(const ipr::Image &frame, int divisor)
{
    Proxy proxy;
    if (frame.empty() || frame.depth() != ipr::Depth::F32 || frame.channels() != 3 || divisor < 1 ||
        frame.width() < divisor || frame.height() < divisor)
        return proxy;
    const ipr::Size size(frame.width() / divisor, frame.height() / divisor);
    proxy.mean = ipr::Image::create(size, ipr::Depth::F32, 3);
    proxy.peak = ipr::Image::create(size, ipr::Depth::F32, 1);
    const float area = 1.f / float(divisor * divisor);
    ipr::parallelRows(size.height, [&](int first, int last) {
        for (int by = first; by < last; ++by) {
            ipr::Vec3f *mean = proxy.mean.mutableRow<ipr::Vec3f>(by);
            float *peak = proxy.peak.mutableRow<float>(by);
            for (int bx = 0; bx < size.width; ++bx) {
                ipr::Vec3f sum(0.f, 0.f, 0.f);
                float brightest = -std::numeric_limits<float>::infinity();
                for (int y = by * divisor; y < (by + 1) * divisor; ++y) {
                    const ipr::Vec3f *in = frame.row<ipr::Vec3f>(y);
                    for (int x = bx * divisor; x < (bx + 1) * divisor; ++x) {
                        sum += in[x];
                        brightest = std::max({brightest, in[x][0], in[x][1], in[x][2]});
                    }
                }
                mean[bx] = sum * area;
                peak[bx] = brightest;
            }
        }
    });
    return proxy;
}

std::optional<Ratio> measureRatio(const Proxy &base, const Proxy &frame, const Options &options)
{
    const auto valid = [](const Proxy &p) {
        return !p.mean.empty() && p.mean.depth() == ipr::Depth::F32 && p.mean.channels() == 3 && !p.peak.empty() &&
               p.peak.depth() == ipr::Depth::F32 && p.peak.channels() == 1 && p.peak.size() == p.mean.size();
    };
    if (!valid(base) || !valid(frame) || base.mean.size() != frame.mean.size() || !(options.clipLevel > 0.0) ||
        !(options.knee > 0.0 && options.knee < 1.0))
        return std::nullopt;
    const float floor = ratioFloor * float(options.clipLevel), ceiling = float(options.knee * options.clipLevel);
    std::array<std::vector<float>, 3> ratios;
    for (int y = 0; y < base.mean.height(); ++y) {
        const ipr::Vec3f *a = base.mean.row<ipr::Vec3f>(y), *b = frame.mean.row<ipr::Vec3f>(y);
        const float *peakA = base.peak.row<float>(y), *peakB = frame.peak.row<float>(y);
        for (int x = 0; x < base.mean.width(); ++x) {
            if (peakA[x] >= ceiling || peakB[x] >= ceiling)
                continue;
            for (int c = 0; c < 3; ++c)
                if (a[x][c] > floor && b[x][c] > floor)
                    ratios[size_t(c)].push_back(b[x][c] / a[x][c]);
        }
    }
    Ratio ratio;
    ratio.samples = int(std::min({ratios[0].size(), ratios[1].size(), ratios[2].size()}));
    if (ratio.samples < minimumRatioSamples)
        return std::nullopt;
    for (int c = 0; c < 3; ++c) {
        std::vector<float> &values = ratios[size_t(c)];
        const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
        std::nth_element(values.begin(), middle, values.end());
        ratio.exposure[c] = *middle;
    }
    return ratio;
}

} // namespace HdrMerge
