#include "HdrJob.h"

#include "Filters.h"
#include "Pixels.h"
#include "image_testing.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
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

const ipr::Size size(480, 320);
const ipr::Vec3d colour(0.85, 1.0, 0.7);
const HdrMerge::NoiseModel noise{1.0 / 4000.0, 2.5e-7};

// A scene of high local contrast: a log-normal radiance, 3 stops of deviation around 0.06 of
// the reference's white, textured at three scales so that any range of it holds detail.
ipr::Image sceneRadiance()
{
    ipr::Image z = ipr::Image::zeros(size, ipr::Depth::F32, 1);
    unsigned seed = 5;
    for (const double sigma : {1.5, 5.0, 18.0}) {
        const ipr::Image layer = ipr::gaussianBlurred(imagetest::normalNoise(size, 1, 0.0, 1.0, seed++), sigma);
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(layer, mean, deviation);
        const float weight = float(1.0 / std::max(1e-9, deviation[0]) / std::sqrt(3.0));
        z = ipr::mapped(z, layer, [weight](float a, float b) { return a + b * weight; });
    }
    ipr::Image scene = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x) {
            const double level = 0.06 * std::pow(2.0, 3.0 * z.row<float>(y)[x]);
            for (int c = 0; c < 3; ++c)
                scene.mutableRow<ipr::Vec3f>(y)[x][c] = float(level * colour[c]);
        }
    return scene;
}

struct Shot
{
    double exposure;     // relative to the reference
    double nominal;      // what the metadata say, 0 when unknown
    ipr::Point2i shift;  // where the scene's (0, 0) lands in the frame
};

// A frame as a camera records it: the scene moved by whole pixels, scaled, noisy, clipped.
ipr::Image capture(const ipr::Image &scene, const Shot &shot, unsigned seed)
{
    const ipr::Matrix33d move(1, 0, shot.shift.x, 0, 1, shot.shift.y, 0, 0, 1);
    const ipr::Image moved = imagetest::moved(scene, move, size);
    const ipr::Image unit = imagetest::normalNoise(size, 3, 0.0, 1.0, seed);
    ipr::Image frame = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            for (int c = 0; c < 3; ++c) {
                const double value = shot.exposure * moved.row<ipr::Vec3f>(y)[x][c];
                const double deviation = std::sqrt(noise.shot * std::max(0.0, value) + noise.read);
                frame.mutableRow<ipr::Vec3f>(y)[x][c] = float(std::min(1.0, value + deviation * unit.row<float>(y)[x * 3 + c]));
            }
    return frame;
}

// -6, 0, +2 and -3 stops, in that order: the darkest is decoded before the frame between it
// and the reference, which alone shares well-exposed pixels with both.
std::vector<Shot> bracket()
{
    return {{1.0 / 64.0, 1.0 / 64.0, {3, -2}}, {1.0, 1.0, {0, 0}}, {4.0, 4.0, {-4, 1}}, {1.0 / 8.0, 1.0 / 8.0, {2, 3}}};
}

HdrJob::Request request(const ipr::Image &scene, const std::vector<Shot> &shots, const QString &scratch,
                        std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0))
{
    HdrJob::Request request;
    request.frameCount = int(shots.size());
    request.referenceIndex = 1;
    request.scratchRoot = scratch;
    request.minimumFreeDiskBytes = 0;
    request.frame = [&scene, shots, calls](int index) {
        ++*calls;
        HdrJob::Frame frame;
        frame.cameraRgb = capture(scene, shots[size_t(index)], unsigned(31 + index));
        frame.nominalExposure = shots[size_t(index)].nominal;
        frame.noise = noise;
        return frame;
    };
    return request;
}

struct Error
{
    double rms = 0.0;
    int samples = 0;
};

// Of the green channel relative to the truth, where the truth lies in [low, high), the image
// at the reference's place over the common area, scaled by 1 / scale.
Error errorOf(const ipr::Image &image, double scale, const ipr::Image &truth, const ipr::Recti &area, double low, double high)
{
    double squares = 0.0;
    int n = 0;
    for (int y = 0; y < area.height; ++y)
        for (int x = 0; x < area.width; ++x) {
            const double expected = truth.row<ipr::Vec3f>(area.y + y)[area.x + x][1];
            if (expected < low || expected >= high)
                continue;
            const double relative = (image.row<ipr::Vec3f>(y)[x][1] / scale - expected) / expected;
            squares += relative * relative;
            ++n;
        }
    return {n ? std::sqrt(squares / n) : 0.0, n};
}

bool folderIsEmpty(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
}

void bracketedSeries()
{
    const ipr::Image scene = sceneRadiance();
    QTemporaryDir scratch;
    const std::vector<Shot> shots = bracket();
    const auto calls = std::make_shared<std::atomic_int>(0);
    const HdrJob::Result result = HdrJob::run(request(scene, shots, scratch.path(), calls));
    if (!result.success)
        std::cerr << result.error.toStdString() << '\n';
    check(result.success && result.usedFrames == 4, "a bracketed series is merged");
    check(*calls == 4, "each frame decoded once");
    check(folderIsEmpty(scratch.path()), "and the scratch folder removed");
    if (!result.success)
        return;

    for (size_t i = 0; i < shots.size(); ++i) {
        const HdrJob::FrameReport &report = result.frames[i];
        const ipr::Matrix33d &h = report.alignment.referenceToFrame;
        std::cerr << "frame " << i << ": shift " << h(0, 2) << ' ' << h(1, 2) << ", exposure " << report.exposure[0] << ' '
                  << report.exposure[1] << ' ' << report.exposure[2] << ", through " << report.measuredThrough << '\n';
        check(report.used, "every frame is used");
        check(std::abs(h(0, 2) - shots[i].shift.x) < 0.15 && std::abs(h(1, 2) - shots[i].shift.y) < 0.15,
              "each frame registered on the reference across exposures");
        for (int c = 0; c < 3; ++c)
            check(std::abs(report.exposure[c] / shots[i].exposure - 1.0) < 0.015, "each exposure measured within 1.5 %");
        check(report.warning.isEmpty(), "and as the metadata said");
    }
    check(result.frames[0].measuredThrough == 3, "six stops away, through the frame at three");
    check(result.unitsFrame == 1 && std::abs(result.rangeEv - 8.0) < 0.05, "the levels of the middle exposure, over 8 stops");
    check(result.frames[3].measuredThrough == 1 && result.frames[2].measuredThrough == 1, "the others against the reference");

    // The common area: what every shifted frame covers, kept inside its kernel's support.
    const ipr::Recti area = result.commonArea;
    check(area.x >= 4 && area.y >= 2 && area.x + area.width <= size.width - 4 && area.y + area.height <= size.height - 3 &&
              area.width > size.width - 20 && area.height > size.height - 20,
          "the common area is what all frames cover");

    // Shadows: the long frame's cleanliness; highlights: the -3 frame's, where the reference is clipped.
    const Error mergedShadows = errorOf(result.cameraRgb, 1.0, scene, area, 0.003, 0.03);
    const Error referenceShadows = errorOf(capture(scene, {1.0, 1.0, {0, 0}}, 32).view(area).clone(), 1.0, scene, area, 0.003, 0.03);
    std::cerr << "shadows: merged " << mergedShadows.rms << ", reference " << referenceShadows.rms << " over " << mergedShadows.samples << '\n';
    check(mergedShadows.samples > 1000 && mergedShadows.rms < 0.6 * referenceShadows.rms, "the shadows much cleaner than the reference's");
    const Error mergedHighlights = errorOf(result.cameraRgb, 1.0, scene, area, 1.5, 6.0);
    std::cerr << "highlights: merged " << mergedHighlights.rms << " over " << mergedHighlights.samples << '\n';
    check(mergedHighlights.samples > 500 && mergedHighlights.rms < 0.05, "the highlights the reference clipped are recovered");
    check(imagetest::rangeOf(result.cameraRgb).high > 8.f, "far above the reference's white");
    check(std::abs(result.merge.share[0] + result.merge.share[1] + result.merge.share[2] + result.merge.share[3] - 1.0) < 1e-6,
          "the shares of the frames add up to the whole");
    check(result.merge.share[2] > result.merge.share[1], "the long frame holds more than the reference");

    // The bands do not change the result.
    HdrJob::Request small = request(scene, shots, scratch.path());
    small.memoryBudgetBytes = quint64(4) * quint64(size.width) * 3 * sizeof(float) * 20;
    const HdrJob::Result banded = HdrJob::run(small);
    check(banded.success && banded.bandRows < result.bandRows, "a small budget cuts more bands");
    check(banded.success && imagetest::same(banded.cameraRgb, result.cameraRgb), "with the same result");
}

void unknownExposures()
{
    // No exposure in the metadata: a first measure on the frames as decoded guides the registration.
    const ipr::Image scene = sceneRadiance();
    QTemporaryDir scratch;
    std::vector<Shot> shots = bracket();
    for (Shot &shot : shots)
        shot.nominal = 0.0;
    shots.erase(shots.begin());   // the -6 frame: too far for a first measure against the reference
    const HdrJob::Result result = HdrJob::run([&] {
        HdrJob::Request r = request(scene, shots, scratch.path());
        r.referenceIndex = 0;
        return r;
    }());
    if (!result.success)
        std::cerr << result.error.toStdString() << '\n';
    check(result.success && result.usedFrames == 3, "a series without exposures is merged");
    if (result.success)
        check(std::abs(result.frames[1].exposure[1] / 4.0 - 1.0) < 0.015 && std::abs(result.frames[2].exposure[1] / 0.125 - 1.0) < 0.015,
              "its exposures measured");
}

void middleExposureLevels()
{
    // The reference is the +2 frame: it decides the registration, the result keeps the levels of
    // the middle exposure, and the reports stay relative to the reference.
    const ipr::Image scene = sceneRadiance();
    QTemporaryDir scratch;
    const std::vector<Shot> shots{{1.0, 1.0, {0, 0}}, {4.0, 4.0, {-4, 1}}, {1.0 / 8.0, 1.0 / 8.0, {2, 3}}};
    HdrJob::Request r = request(scene, shots, scratch.path());
    r.referenceIndex = 1;
    const HdrJob::Result result = HdrJob::run(r);
    check(result.success && result.unitsFrame == 0, "the middle exposure gives the levels");
    if (!result.success)
        return;
    check(std::abs(result.frames[0].exposure[1] / 0.25 - 1.0) < 0.015, "an exposure is reported relative to the reference");
    std::vector<double> ratios;
    const ipr::Recti area = result.commonArea;
    for (int y = 0; y < area.height; y += 3)
        for (int x = 0; x < area.width; x += 3) {
            // The reference's frame: the scene moved as the +2 frame saw it.
            const int sx = area.x + x + 4, sy = area.y + y - 1;
            const double truth = scene.row<ipr::Vec3f>(sy)[sx][1];
            if (sx < size.width && sy >= 0 && truth > 0.01 && truth < 3.0)
                ratios.push_back(result.cameraRgb.row<ipr::Vec3f>(y)[x][1] / truth);
        }
    std::nth_element(ratios.begin(), ratios.begin() + std::ptrdiff_t(ratios.size() / 2), ratios.end());
    check(!ratios.empty() && std::abs(ratios[ratios.size() / 2] - 1.0) < 0.01, "at the scene's level as the middle frame saw it");
}

void noiseModels()
{
    QString reason;
    const auto catalog = NoiseLaw::loadCatalog(QStringLiteral(":/config/noise-laws.json"), &reason);
    check(catalog.has_value(), "the bundled noise catalogue");
    if (!catalog)
        return;
    const HdrMerge::NoiseModel r6 = HdrJob::noiseModel(&*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 100, 16000.0);
    const auto law = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 100, nullptr);
    check(law.has_value(), "the R6 III at ISO 100 is measured");
    if (law) {
        double shot = 0.0, read = 0.0;
        for (const auto &site : law->positions) {
            shot += site.shotSlope / 4.0;
            read += site.readVariance / 4.0;
        }
        check(std::abs(r6.shot - shot / 16000.0) < 1e-12 && std::abs(r6.read - read / (16000.0 * 16000.0)) < 1e-15,
              "a measured camera: its law over its white");
    }
    const HdrMerge::NoiseModel high = HdrJob::noiseModel(&*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 1600, 16000.0);
    check(high.shot > 10.0 * r6.shot && high.read > 10.0 * r6.read, "more noise at a higher ISO");
    const HdrMerge::NoiseModel unknown = HdrJob::noiseModel(&*catalog, QStringLiteral("Nobody"), QStringLiteral("Camera"), 400, 0.0);
    const HdrMerge::NoiseModel none = HdrJob::noiseModel(nullptr, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 400, 16000.0);
    check(unknown.shot > 0.0 && unknown.read > 0.0 && unknown.shot == none.shot && unknown.read == none.read,
          "an unknown camera or no catalogue: the generic sensor");
    const HdrMerge::NoiseModel base = HdrJob::noiseModel(nullptr, QString(), QString(), 100, 0.0);
    check(std::abs(unknown.shot / base.shot - 4.0) < 1e-9 && std::abs(unknown.read / base.read - 16.0) < 1e-9,
          "scaled by the ISO, its read variance by its square");
    check(HdrJob::noiseModel(nullptr, QString(), QString(), 0, 0.0).shot == base.shot, "an unknown ISO counts as 100");
}

void changedLight()
{
    // The metadata say -3 stops, the light made it -2.3: used, and said.
    const ipr::Image scene = sceneRadiance();
    QTemporaryDir scratch;
    std::vector<Shot> shots = bracket();
    shots[3].exposure = 0.2;
    const HdrJob::Result result = HdrJob::run(request(scene, shots, scratch.path()));
    check(result.success && result.frames[3].used, "a frame whose light changed is used");
    check(result.success && !result.frames[3].warning.isEmpty() && result.frames[3].nominalGapEv > 0.5, "and its gap reported");
    check(result.success && std::abs(result.frames[3].exposure[1] / 0.2 - 1.0) < 0.015, "with its measured exposure");
}

void refusals()
{
    const ipr::Image scene = sceneRadiance();
    QTemporaryDir scratch;
    const std::vector<Shot> shots = bracket();

    HdrJob::Request one = request(scene, shots, scratch.path());
    one.frameCount = 1;
    check(!HdrJob::run(one).success, "one frame is not a series");
    HdrJob::Request outside = request(scene, shots, scratch.path());
    outside.referenceIndex = 4;
    check(!HdrJob::run(outside).success, "a reference outside the series is refused");
    HdrJob::Request unreadable = request(scene, shots, scratch.path());
    unreadable.frame = [](int) {
        HdrJob::Frame frame;
        frame.error = QStringLiteral("broken");
        return frame;
    };
    const HdrJob::Result broken = HdrJob::run(unreadable);
    check(!broken.success && broken.error == QStringLiteral("broken"), "an unreadable reference fails with its reason");

    // A frame of another size is left out with its reason, the others merged.
    HdrJob::Request odd = request(scene, shots, scratch.path());
    const auto provider = odd.frame;
    odd.frame = [provider](int index) {
        HdrJob::Frame frame = provider(index);
        if (index == 0)
            frame.cameraRgb = frame.cameraRgb.view(ipr::Recti(0, 0, 200, 100)).clone();
        return frame;
    };
    const HdrJob::Result partial = HdrJob::run(odd);
    check(partial.success && partial.usedFrames == 3 && !partial.frames[0].used && !partial.frames[0].exclusion.isEmpty(),
          "a frame of another size is left out");

    // Not enough disk: said before anything is written.
    HdrJob::Request full = request(scene, shots, scratch.path());
    full.minimumFreeDiskBytes = quint64(1) << 62;
    const HdrJob::Result noDisk = HdrJob::run(full);
    check(!noDisk.success && noDisk.error.contains(QStringLiteral("disque")), "a full disk is said");

    // Cancelled after the second frame: stopped, and the scratch folder removed.
    std::atomic_int decoded{0};
    HdrJob::Request cancelled = request(scene, shots, scratch.path());
    const auto decode = cancelled.frame;
    cancelled.frame = [&decoded, decode](int index) {
        ++decoded;
        return decode(index);
    };
    cancelled.cancelled = [&decoded] { return decoded >= 2; };
    const HdrJob::Result stopped = HdrJob::run(cancelled);
    check(!stopped.success && stopped.cancelled, "a cancelled merge stops");
    check(folderIsEmpty(scratch.path()), "and leaves nothing in the scratch folder");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    bracketedSeries();
    unknownExposures();
    middleExposureLevels();
    noiseModels();
    changedLight();
    refusals();
    if (passed)
        std::cout << "hdr job: ok\n";
    return passed ? 0 : 1;
}
