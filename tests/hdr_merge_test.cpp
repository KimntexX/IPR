#include "HdrMerge.h"

#include "Image.h"
#include "Pixels.h"
#include "image_testing.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

const ipr::Size size(160, 96);
const ipr::Vec3d colour(0.8, 1.0, 0.6);
const HdrMerge::NoiseModel noise{1.0 / 4000.0, 2.5e-7};
const std::vector<double> exposures{0.25, 1.0, 4.0};   // -2, 0 and +2 stops; the reference is the middle one

// The scene's radiance, in the reference's units: 14 stops from left to right, from 1e-3 to
// 16 times the reference's white, with a texture and a colour.
ipr::Image sceneRadiance()
{
    ipr::Image scene = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x) {
            const double level = 1e-3 * std::pow(2.0, 14.0 * x / size.width);
            const double texture = 1.0 + 0.2 * std::sin(0.7 * x) * std::cos(0.45 * y);
            for (int c = 0; c < 3; ++c)
                scene.mutableRow<ipr::Vec3f>(y)[x][c] = float(level * texture * colour[c]);
        }
    return scene;
}

// What a camera records at this exposure: the scene scaled, its shot and read noise, then the
// clip at white. noisy false: the exact values, for the linearity.
ipr::Image capture(const ipr::Image &scene, double exposure, bool noisy, unsigned seed, const ipr::Vec3d &gains = {1.0, 1.0, 1.0})
{
    const ipr::Image unit = imagetest::normalNoise(size, 3, 0.0, 1.0, seed);
    ipr::Image frame = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            for (int c = 0; c < 3; ++c) {
                const double value = exposure * gains[c] * scene.row<ipr::Vec3f>(y)[x][c];
                const double deviation = std::sqrt(noise.shot * std::max(0.0, value) + noise.read);
                const double recorded = value + (noisy ? deviation * unit.row<float>(y)[x * 3 + c] : 0.0);
                frame.mutableRow<ipr::Vec3f>(y)[x][c] = float(std::min(1.0, recorded));
            }
    return frame;
}

std::vector<HdrMerge::Frame> series(const ipr::Image &scene, bool noisy)
{
    std::vector<HdrMerge::Frame> frames;
    for (size_t i = 0; i < exposures.size(); ++i) {
        HdrMerge::Frame frame;
        frame.pixels = capture(scene, exposures[i], noisy, unsigned(11 + i));
        frame.exposure = ipr::Vec3d(exposures[i], exposures[i], exposures[i]);
        frame.noise = noise;
        frames.push_back(std::move(frame));
    }
    return frames;
}

// The same frames, their pixels shared: an image is not copied implicitly.
std::vector<HdrMerge::Frame> shared(const std::vector<HdrMerge::Frame> &frames)
{
    std::vector<HdrMerge::Frame> copies;
    for (const HdrMerge::Frame &frame : frames) {
        HdrMerge::Frame copy;
        copy.pixels = frame.pixels.share();
        copy.exposure = frame.exposure;
        copy.noise = frame.noise;
        copies.push_back(std::move(copy));
    }
    return copies;
}

HdrMerge::Options options()
{
    HdrMerge::Options options;
    options.referenceIndex = 1;
    return options;
}

// The columns whose scene level lies in [low, high) times the reference's white.
std::pair<int, int> columnsOf(double low, double high)
{
    const auto column = [](double level) { return int(std::ceil(std::log2(level / 1e-3) / 14.0 * size.width)); };
    return {std::clamp(column(low), 0, size.width), std::clamp(column(high), 0, size.width)};
}

struct Error
{
    double bias = 0.0;   // mean of (value - truth) / truth
    double rms = 0.0;    // root mean square of (value - truth) / truth
};

// Of the green channel, relative to the truth, over these columns, scaled by 1 / scale.
Error errorOf(const ipr::Image &image, double scale, const ipr::Image &truth, std::pair<int, int> columns)
{
    double sum = 0.0, squares = 0.0;
    int n = 0;
    for (int y = 2; y < size.height - 2; ++y)
        for (int x = std::max(2, columns.first); x < std::min(size.width - 2, columns.second); ++x) {
            const double expected = truth.row<ipr::Vec3f>(y)[x][1];
            const double relative = (image.row<ipr::Vec3f>(y)[x][1] / scale - expected) / expected;
            sum += relative;
            squares += relative * relative;
            ++n;
        }
    return n ? Error{sum / n, std::sqrt(squares / n)} : Error{};
}

void linearity()
{
    const ipr::Image scene = sceneRadiance();
    ipr::Image merged;
    HdrMerge::Statistics statistics;
    check(HdrMerge::merge(series(scene, false), options(), merged, &statistics), "a series is merged");
    check(merged.size() == size && merged.channels() == 3, "of the frames' size");

    // Wherever the shortest frame stays below its knee, texture included, the merge is the scene itself.
    const std::pair<int, int> seen = columnsOf(1e-3, 0.7 / exposures[0]);
    double worst = 0.0;
    for (int y = 0; y < size.height; ++y)
        for (int x = seen.first; x < seen.second; ++x)
            for (int c = 0; c < 3; ++c) {
                const double expected = scene.row<ipr::Vec3f>(y)[x][c];
                worst = std::max(worst, std::abs(merged.row<ipr::Vec3f>(y)[x][c] - expected) / expected);
            }
    if (worst > 1e-5)
        std::cerr << "linearity worst " << worst << '\n';
    check(worst <= 1e-5, "the merge of exact frames is the scene, over 12 stops");
    check(imagetest::rangeOf(merged).high > 3.5f, "the highlights go far above the reference's white");

    // Beyond what the shortest frame saw: clipped, counted, and never above its clip.
    check(statistics.clippedEverywhere > 0 && statistics.clippedEverywhere < statistics.pixels, "what no frame saw is counted");
    check(imagetest::rangeOf(merged).high <= float(1.0 / exposures[0]) + 1e-5f, "and not invented beyond the shortest frame's clip");
    check(statistics.share.size() == 3 && std::abs(statistics.share[0] + statistics.share[1] + statistics.share[2] - 1.0) < 1e-6,
          "the frames' shares add up to the whole");
    check(statistics.share[2] > statistics.share[1] && statistics.share[0] > 0.1, "the long frame holds the shadows, the short one the highlights");
}

void noiseFollowsTheBestFrame()
{
    const ipr::Image scene = sceneRadiance();
    const std::vector<HdrMerge::Frame> frames = series(scene, true);
    ipr::Image merged;
    check(HdrMerge::merge(frames, options(), merged), "a noisy series is merged");

    // Shadows: the long frame is the least noisy, and the merge no noisier than it.
    const std::pair<int, int> shadows = columnsOf(2e-3, 2e-2);
    const Error longFrame = errorOf(frames[2].pixels, exposures[2], scene, shadows);
    const Error reference = errorOf(frames[1].pixels, exposures[1], scene, shadows);
    const Error inShadows = errorOf(merged, 1.0, scene, shadows);
    std::cerr << "shadows rms: merged " << inShadows.rms << ", long " << longFrame.rms << ", reference " << reference.rms << '\n';
    check(inShadows.rms <= 1.02 * longFrame.rms, "the shadows are as clean as the long frame's");
    check(inShadows.rms < 0.6 * reference.rms, "and much cleaner than the reference's");
    check(std::abs(inShadows.bias) < 0.01, "without a bias");

    // Highlights only the short frame sees: its noise, and no bias from the clipped ones.
    const std::pair<int, int> highlights = columnsOf(1.5, 3.0);
    const Error shortFrame = errorOf(frames[0].pixels, exposures[0], scene, highlights);
    const Error inHighlights = errorOf(merged, 1.0, scene, highlights);
    std::cerr << "highlights rms: merged " << inHighlights.rms << ", short " << shortFrame.rms << '\n';
    check(inHighlights.rms <= 1.05 * shortFrame.rms, "the highlights keep the short frame's precision");
    check(std::abs(inHighlights.bias) < 0.005, "and its level");

    // Midtones three frames see: better than any one of them.
    const std::pair<int, int> midtones = columnsOf(0.05, 0.15);
    const Error bestOfOne = errorOf(frames[2].pixels, exposures[2], scene, midtones);
    const Error inMidtones = errorOf(merged, 1.0, scene, midtones);
    check(inMidtones.rms <= bestOfOne.rms, "the midtones at least as clean as the best frame");
}

void motion()
{
    const ipr::Image scene = sceneRadiance();
    std::vector<HdrMerge::Frame> frames = series(scene, true);
    // Something dark crossed the long frame's view, in the midtones.
    const ipr::Recti area(60, 30, 20, 20);
    for (int y = area.y; y < area.y + area.height; ++y)
        for (int x = area.x; x < area.x + area.width; ++x)
            frames[2].pixels.mutableRow<ipr::Vec3f>(y)[x] *= 0.2f;
    const auto meanRatio = [&](const ipr::Image &merged) {
        double sum = 0.0;
        for (int y = area.y + 2; y < area.y + area.height - 2; ++y)
            for (int x = area.x + 2; x < area.x + area.width - 2; ++x)
                sum += merged.row<ipr::Vec3f>(y)[x][1] / scene.row<ipr::Vec3f>(y)[x][1];
        return sum / double((area.width - 4) * (area.height - 4));
    };

    ipr::Image merged;
    HdrMerge::Statistics statistics;
    check(HdrMerge::merge(frames, options(), merged, &statistics), "a series with a moving subject is merged");
    std::cerr << "moving area level " << meanRatio(merged) << ", rejected " << statistics.motionRejected << '\n';
    check(std::abs(meanRatio(merged) - 1.0) < 0.02, "the reference decides where the long frame differs: no ghost");
    check(statistics.motionRejected >= quint64((area.width - 2) * (area.height - 2)), "and the rejected samples are counted");

    HdrMerge::Options still = options();
    still.motionSigma = 0.0;
    check(HdrMerge::merge(frames, still, merged, &statistics), "the same without the motion test");
    check(meanRatio(merged) < 0.6 && statistics.motionRejected == 0, "keeps the ghost: the test is what removes it");

    // A still noisy series loses almost nothing to the test.
    check(HdrMerge::merge(series(scene, true), options(), merged, &statistics), "a still series");
    check(double(statistics.motionRejected) < 0.01 * double(statistics.pixels), "few samples taken for motion in a still scene");
}

void bands()
{
    // The merge of a band with its context is the same as the middle of the whole merge.
    const ipr::Image scene = sceneRadiance();
    const std::vector<HdrMerge::Frame> frames = series(scene, true);
    ipr::Image whole;
    check(HdrMerge::merge(frames, options(), whole), "the whole merge");
    const int context = (HdrMerge::decisionWindow - 1) / 2;
    const ipr::Recti band(0, 40 - context, size.width, 20 + 2 * context);
    std::vector<HdrMerge::Frame> parts = shared(frames);
    for (HdrMerge::Frame &part : parts)
        part.pixels = part.pixels.view(band);
    ipr::Image cut;
    check(HdrMerge::merge(parts, options(), cut), "a band's merge");
    const ipr::Image middle = cut.view(ipr::Recti(0, context, size.width, 20));
    const ipr::Image expected = whole.view(ipr::Recti(0, 40, size.width, 20));
    check(imagetest::same(middle.clone(), expected.clone()), "is the same in its middle, whatever the bands");
}

void ratios()
{
    const ipr::Image scene = sceneRadiance();
    const ipr::Image reference = capture(scene, 1.0, true, 21);
    // Two stops below, with another balance: a frame of the camera's automatic white balance.
    const ipr::Image dark = capture(scene, 0.25, true, 22, ipr::Vec3d(1.08, 1.0, 0.93));
    const std::optional<HdrMerge::Ratio> ratio = HdrMerge::measureRatio(HdrMerge::proxyOf(reference, 4), HdrMerge::proxyOf(dark, 4));
    check(ratio.has_value(), "two stops apart, the exposure is measured");
    if (ratio) {
        std::cerr << "ratio " << ratio->exposure[0] << ' ' << ratio->exposure[1] << ' ' << ratio->exposure[2] << '\n';
        check(std::abs(ratio->exposure[1] / 0.25 - 1.0) < 0.01, "the green ratio within 1 %");
        check(std::abs(ratio->exposure[0] / (0.25 * 1.08) - 1.0) < 0.01 && std::abs(ratio->exposure[2] / (0.25 * 0.93) - 1.0) < 0.01,
              "and the other channels with their balance");
        check(ratio->samples > 200, "from many samples");
    }
    // Twenty stops apart nothing is seen well by both: no ratio, rather than a wrong one.
    const ipr::Image black = capture(scene, 1e-6, true, 23);
    const HdrMerge::Proxy proxy = HdrMerge::proxyOf(reference, 4);
    check(proxy.mean.size() == ipr::Size(40, 24) && proxy.peak.size() == proxy.mean.size(), "a sample every 4 pixels");
    check(!HdrMerge::measureRatio(proxy, HdrMerge::proxyOf(black, 4)).has_value(), "too far apart, no ratio");
    check(!HdrMerge::measureRatio(proxy, HdrMerge::proxyOf(reference, 8)).has_value(), "proxies of different sizes, no ratio");
    check(HdrMerge::proxyOf(reference, 3).mean.empty(), "a step below the sample's reach is refused");
    check(HdrMerge::proxyOf(ipr::Image(), 4).mean.empty() && !HdrMerge::measureRatio(proxy, HdrMerge::Proxy{}).has_value(),
          "an empty frame, no proxy and no ratio");
    check(HdrMerge::proxyOf(reference.view(ipr::Recti(0, 0, 3, 3)).clone(), 4).mean.empty(), "no sample in a smaller frame");

    // Every other column clipped, every average below white: the peak leaves those samples out,
    // which an average alone would not (it would read 0.6 / 0.275, about 2.2).
    ipr::Image fine = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x) {
            const float level = x < size.width / 2 ? (x % 2 ? 0.08f : 0.02f) : (x % 2 ? 0.5f : 0.05f);
            fine.mutableRow<ipr::Vec3f>(y)[x] = ipr::Vec3f(level, level, level);
        }
    const ipr::Image brighter = ipr::mapped(fine, [](float v) { return std::min(1.f, 4.f * v); });
    const std::optional<HdrMerge::Ratio> clipped = HdrMerge::measureRatio(HdrMerge::proxyOf(fine, 4), HdrMerge::proxyOf(brighter, 4));
    check(clipped && std::abs(clipped->exposure[1] - 4.0) < 1e-4, "blocks with a clipped pixel are left out of the ratio");
}

void refusals()
{
    const ipr::Image scene = sceneRadiance();
    ipr::Image merged;
    check(!HdrMerge::merge({}, options(), merged), "no frame, no merge");
    std::vector<HdrMerge::Frame> frames = series(scene, false);
    HdrMerge::Options outside = options();
    outside.referenceIndex = 3;
    check(!HdrMerge::merge(frames, outside, merged), "a reference outside the series is refused");
    HdrMerge::Options knee = options();
    knee.knee = 1.0;
    check(!HdrMerge::merge(frames, knee, merged), "a knee at the clip is refused");
    std::vector<HdrMerge::Frame> zero = shared(frames);
    zero[0].exposure = ipr::Vec3d(0.0, 0.25, 0.25);
    check(!HdrMerge::merge(zero, options(), merged), "a null exposure is refused");
    std::vector<HdrMerge::Frame> silent = shared(frames);
    silent[1].noise.read = 0.0;
    check(!HdrMerge::merge(silent, options(), merged), "a frame without read noise is refused: its weight would be infinite");
    std::vector<HdrMerge::Frame> smaller = shared(frames);
    smaller[2].pixels = smaller[2].pixels.view(ipr::Recti(0, 0, 80, 40)).clone();
    check(!HdrMerge::merge(smaller, options(), merged), "frames of different sizes are refused");

    // One frame: itself, in its own units.
    std::vector<HdrMerge::Frame> one = shared(frames);
    one.erase(one.begin() + 2);
    one.erase(one.begin());
    HdrMerge::Options single = options();
    single.referenceIndex = 0;
    check(HdrMerge::merge(one, single, merged) && ipr::largestDifference(merged, frames[1].pixels) < 1e-6, "one frame is itself");
}

} // namespace

int main()
{
    linearity();
    noiseFollowsTheBestFrame();
    motion();
    bands();
    ratios();
    refusals();
    if (passed)
        std::cout << "hdr merge: ok\n";
    return passed ? 0 : 1;
}
