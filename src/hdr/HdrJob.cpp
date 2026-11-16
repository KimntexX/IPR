#include "HdrJob.h"

#include "FrameSpool.h"
#include "StackJob.h"

#include "Pixels.h"

#include <QDir>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace HdrJob {
namespace {

constexpr int proxyDivisor = 8;           // pixels of a block, to measure the exposures: small enough to avoid clipped highlights
constexpr int minimumBandRows = 16;
// The range two exposures are compared on for the registration, in the reference's units:
// below the darker frame's noise and above the knee of the brighter one's clip, the two
// differ by what each cannot see, which would mislead the matches.
constexpr double alignmentFloor = 0.002;  // of the darker frame's clip
constexpr double alignmentCeiling = 0.9;  // of the brighter frame's clip

double meanOf(const ipr::Vec3d &exposure)
{
    return 0.25 * exposure[0] + 0.5 * exposure[1] + 0.25 * exposure[2];
}

// The range a frame of this exposure relative to the reference and the reference both see.
StackAlignment::Bounds sharedRange(double ratio, double clip)
{
    StackAlignment::Bounds bounds;
    bounds.low = alignmentFloor * clip / std::min(1.0, ratio);
    bounds.high = alignmentCeiling * clip * std::min(1.0, 1.0 / ratio);
    return bounds;
}

// The registered frames' exposures relative to the reference, measured on their proxies:
// against the reference first, else through a frame already measured, the nearest in
// exposure first. A frame no chain reaches keeps no exposure.
void measureExposures(const std::vector<const HdrMerge::Proxy *> &proxies, const std::vector<double> &guesses, int reference,
                      const HdrMerge::Options &options, std::vector<std::optional<ipr::Vec3d>> &exposures, std::vector<int> &through)
{
    const int count = int(proxies.size());
    exposures.assign(size_t(count), std::nullopt);
    through.assign(size_t(count), -1);
    exposures[size_t(reference)] = ipr::Vec3d(1.0, 1.0, 1.0);
    for (bool progressed = true; progressed;) {
        progressed = false;
        for (int i = 0; i < count; ++i) {
            if (exposures[size_t(i)])
                continue;
            std::vector<int> known;
            for (int j = 0; j < count; ++j)
                if (exposures[size_t(j)])
                    known.push_back(j);
            std::sort(known.begin(), known.end(), [&](int a, int b) {
                if (a == reference || b == reference)
                    return a == reference && b != reference;
                return std::abs(std::log(guesses[size_t(a)] / guesses[size_t(i)])) < std::abs(std::log(guesses[size_t(b)] / guesses[size_t(i)]));
            });
            for (const int j : known) {
                const std::optional<HdrMerge::Ratio> ratio = HdrMerge::measureRatio(*proxies[size_t(j)], *proxies[size_t(i)], options);
                if (!ratio)
                    continue;
                const ipr::Vec3d &base = *exposures[size_t(j)];
                exposures[size_t(i)] = ipr::Vec3d(base[0] * ratio->exposure[0], base[1] * ratio->exposure[1], base[2] * ratio->exposure[2]);
                through[size_t(i)] = j;
                progressed = true;
                break;
            }
        }
    }
}

} // namespace

Result run(const Request &request)
{
    Result result;
    const auto stopped = [&] {
        if (request.cancelled && request.cancelled()) {
            result.cancelled = true;
            result.error = QStringLiteral("Annulé.");
            return true;
        }
        return false;
    };
    if (request.frameCount < 2 || !request.frame) {
        result.error = QStringLiteral("Il faut au moins deux expositions pour une fusion HDR.");
        return result;
    }
    if (request.referenceIndex < 0 || request.referenceIndex >= request.frameCount) {
        result.error = QStringLiteral("L'image de référence n'appartient pas à la série.");
        return result;
    }
    if (stopped())
        return result;

    int done = 0;
    int total = request.frameCount + 1;
    const auto advance = [&] {
        if (request.progress)
            request.progress(++done, total);
    };
    const double clip = request.merge.clipLevel;

    Frame reference = request.frame(request.referenceIndex);
    if (reference.cameraRgb.empty() || !(reference.cameraRgb.depth() == ipr::Depth::F32 && reference.cameraRgb.channels() == 3)) {
        result.error = reference.error.isEmpty() ? QStringLiteral("L'image de référence n'a pas pu être décodée.") : reference.error;
        return result;
    }
    const ipr::Size size = reference.cameraRgb.size();
    result.frameSize = size;

    const QString root = request.scratchRoot.isEmpty() ? QDir::tempPath() : request.scratchRoot;
    const QStorageInfo storage(root);
    const quint64 needed = StackJob::scratchBytesNeeded(request.frameCount, size);
    if (!storage.isValid() || quint64(std::max<qint64>(0, storage.bytesAvailable())) < needed + request.minimumFreeDiskBytes) {
        result.error = QStringLiteral("Espace disque insuffisant dans %1 : %2 Go nécessaires pour le tampon.")
            .arg(root).arg(double(needed + request.minimumFreeDiskBytes) / 1e9, 0, 'f', 1);
        return result;
    }
    // Declared before the spool: the files close before their folder goes.
    QTemporaryDir scratch(QDir(root).filePath(QStringLiteral("ipr-hdr-XXXXXX")));
    if (!scratch.isValid()) {
        result.error = QStringLiteral("Impossible de créer le dossier tampon dans %1.").arg(root);
        return result;
    }

    // What every used frame keeps until the merge: its file, its proxy, its noise.
    struct Kept
    {
        int index = 0;
        std::unique_ptr<FrameSpool> spool;
        HdrMerge::Proxy proxy;
        double guess = 1.0;            // exposure relative to the reference, before the measure
        HdrMerge::NoiseModel noise;
        std::vector<ipr::Vec2i> spans;
    };
    std::vector<Kept> kept;
    const auto spoolFrame = [&](Kept &entry, const ipr::Image &image) {
        entry.spool = std::make_unique<FrameSpool>(scratch.filePath(QStringLiteral("%1.frame").arg(entry.index)));
        return entry.spool->write(image, result.scratchBytes);
    };

    result.frames.resize(size_t(request.frameCount));
    FrameReport &referenceReport = result.frames[size_t(request.referenceIndex)];
    referenceReport.index = request.referenceIndex;
    referenceReport.reference = true;
    referenceReport.alignment.valid = true;
    referenceReport.nominalExposure = reference.nominalExposure;
    const ipr::Image referenceLuminance = StackAlignment::luminanceOf(reference.cameraRgb);
    const HdrMerge::Proxy referenceProxy = HdrMerge::proxyOf(reference.cameraRgb, proxyDivisor);
    {
        Kept entry;
        entry.index = request.referenceIndex;
        entry.proxy = {referenceProxy.mean.share(), referenceProxy.peak.share()};
        entry.noise = reference.noise;
        entry.spans.assign(size_t(size.height), ipr::Vec2i(0, size.width));
        if (!spoolFrame(entry, reference.cameraRgb)) {
            result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
            return result;
        }
        kept.push_back(std::move(entry));
    }
    reference.cameraRgb = {};
    advance();

    StackAlignment::Bounds referenceBounds{0.0, 0.0, 0.0};   // of the pyramid below; none built yet
    StackAlignment::Pyramid referencePyramid;
    for (int index = 0; index < request.frameCount; ++index) {
        if (index == request.referenceIndex)
            continue;
        if (stopped())
            return result;
        FrameReport &report = result.frames[size_t(index)];
        report.index = index;
        Frame frame = request.frame(index);
        report.nominalExposure = frame.nominalExposure;
        if (frame.cameraRgb.empty() || !(frame.cameraRgb.depth() == ipr::Depth::F32 && frame.cameraRgb.channels() == 3) ||
            frame.cameraRgb.size() != size) {
            report.exclusion = !frame.error.isEmpty() ? frame.error
                : frame.cameraRgb.empty() ? QStringLiteral("Image illisible.")
                                          : QStringLiteral("Dimensions différentes de l'image de référence.");
            advance();
            continue;
        }
        // The exposure to bring the frame to, for the registration: the nominal ratio, else a
        // first measure on the frames as they are, which a small shift hardly moves.
        double guess = 0.0;
        if (frame.nominalExposure > 0.0 && reference.nominalExposure > 0.0)
            guess = frame.nominalExposure / reference.nominalExposure;
        else if (const std::optional<HdrMerge::Ratio> rough = HdrMerge::measureRatio(referenceProxy, HdrMerge::proxyOf(frame.cameraRgb, proxyDivisor), request.merge))
            guess = meanOf(rough->exposure);
        if (!(guess > 0.0 && std::isfinite(guess))) {
            report.exclusion = QStringLiteral("Exposition inconnue et impossible à mesurer face à la référence.");
            advance();
            continue;
        }

        const StackAlignment::Bounds bounds = sharedRange(guess, clip);
        if (referencePyramid.empty() || bounds.low != referenceBounds.low || bounds.high != referenceBounds.high) {
            referencePyramid = StackAlignment::luminancePyramid(referenceLuminance, request.alignment, bounds);
            referenceBounds = bounds;
        }
        StackAlignment::Bounds frameBounds = bounds;
        frameBounds.scale = 1.0 / guess;
        const StackAlignment::Pyramid framePyramid =
            StackAlignment::luminancePyramid(StackAlignment::luminanceOf(frame.cameraRgb), request.alignment, frameBounds);
        report.alignment = StackAlignment::estimate(referencePyramid, framePyramid, request.alignment, request.cancelled);
        if (stopped())
            return result;
        if (!report.alignment.valid) {
            report.exclusion = report.alignment.error;
            advance();
            continue;
        }
        StackAlignment::Resampled resampled = StackAlignment::resample(
            frame.cameraRgb, framePyramid[0], referencePyramid[0], report.alignment.referenceToFrame, request.localRefinement, request.local);
        frame.cameraRgb = {};
        report.localBlocks = resampled.localBlocks;
        report.localLargest = resampled.localLargest;

        Kept entry;
        entry.index = index;
        entry.proxy = HdrMerge::proxyOf(resampled.pixels, proxyDivisor);
        entry.guess = guess;
        entry.noise = frame.noise;
        entry.spans.assign(size_t(size.height), ipr::Vec2i(0, size.width));
        StackAlignment::narrowSpans(report.alignment.referenceToFrame, size, report.localLargest, entry.spans);
        if (!spoolFrame(entry, resampled.pixels)) {
            result.error = QStringLiteral("Écriture impossible dans le dossier tampon (disque plein ?).");
            return result;
        }
        kept.push_back(std::move(entry));
        advance();
    }

    // The exposures, measured once every frame is registered: a frame two steps from the
    // reference is measured through the one between them, whatever the decoding order.
    std::vector<const HdrMerge::Proxy *> proxies;
    std::vector<double> guesses;
    for (const Kept &entry : kept) {
        proxies.push_back(&entry.proxy);
        guesses.push_back(entry.guess);
    }
    std::vector<std::optional<ipr::Vec3d>> exposures;
    std::vector<int> through;
    measureExposures(proxies, guesses, 0, request.merge, exposures, through);

    std::vector<HdrMerge::Frame> merging;   // pixels filled band by band
    std::vector<FrameSpool *> files;
    std::vector<int> indices;
    std::vector<ipr::Vec2i> spans(size_t(size.height), ipr::Vec2i(0, size.width));
    for (size_t k = 0; k < kept.size(); ++k) {
        Kept &entry = kept[k];
        FrameReport &report = result.frames[size_t(entry.index)];
        if (!exposures[k]) {
            report.exclusion = QStringLiteral("Aucune zone bien exposée en commun avec une autre image : exposition impossible à mesurer.");
            entry.spool->remove();
            continue;
        }
        report.exposure = *exposures[k];
        report.measuredThrough = through[k] >= 0 ? kept[size_t(through[k])].index : -1;
        if (!report.reference && entry.guess > 0.0 && report.nominalExposure > 0.0) {
            report.nominalGapEv = std::log2(meanOf(report.exposure) / entry.guess);
            if (std::abs(report.nominalGapEv) > largestNominalGapEv)
                report.warning = QStringLiteral("Exposition mesurée à %1 IL de la valeur prévue : la lumière a peut-être changé pendant la série.")
                                     .arg(report.nominalGapEv, 0, 'f', 1);
        }
        for (int y = 0; y < size.height; ++y) {
            spans[size_t(y)][0] = std::max(spans[size_t(y)][0], entry.spans[size_t(y)][0]);
            spans[size_t(y)][1] = std::min(spans[size_t(y)][1], entry.spans[size_t(y)][1]);
        }
        report.used = true;
        HdrMerge::Frame part;
        part.exposure = report.exposure;
        part.noise = entry.noise;
        merging.push_back(std::move(part));
        files.push_back(entry.spool.get());
        indices.push_back(entry.index);
    }
    result.usedFrames = int(merging.size());
    if (result.usedFrames < 2) {
        result.error = QStringLiteral("Aucune autre exposition n'a pu être recalée et mesurée face à la référence.");
        return result;
    }
    result.commonArea = StackJob::largestRectangle(spans);
    if (result.commonArea.empty()) {
        result.error = QStringLiteral("Les images recalées n'ont aucune zone commune.");
        return result;
    }

    HdrMerge::Options options = request.merge;
    options.referenceIndex = 0;   // the reference is the first kept
    const quint64 rowBytes = quint64(size.width) * quint64(3 * sizeof(float));
    const int bandRows = int(std::clamp<quint64>(request.memoryBudgetBytes / std::max<quint64>(1, rowBytes * quint64(result.usedFrames)),
                                                 quint64(minimumBandRows), quint64(std::max(minimumBandRows, result.commonArea.height))));
    result.bandRows = bandRows;
    const int bands = (result.commonArea.height + bandRows - 1) / bandRows;
    total = request.frameCount + bands;
    result.cameraRgb = ipr::Image::create(result.commonArea.size(), ipr::Depth::F32, 3);
    std::vector<ipr::Image> rows(merging.size());
    std::vector<double> share(merging.size(), 0.0);
    result.merge.share.assign(size_t(request.frameCount), 0.0);
    for (int band = 0; band < bands; ++band) {
        if (stopped()) {
            result.cameraRgb = {};
            return result;
        }
        const int top = band * bandRows;
        const int height = std::min(bandRows, result.commonArea.height - top);
        // Each band reads the decisions' context around it: the same result whatever the bands.
        const int context = (HdrMerge::decisionWindow - 1) / 2;
        const ipr::Recti wanted(result.commonArea.x - context, result.commonArea.y + top - context,
                                result.commonArea.width + 2 * context, height + 2 * context);
        const ipr::Recti readable = wanted & ipr::Recti(0, 0, size.width, size.height);
        for (size_t i = 0; i < merging.size(); ++i) {
            if (!files[i]->read(size, ipr::Depth::F32, 3, readable.y, readable.height, rows[i])) {
                result.cameraRgb = {};
                result.error = QStringLiteral("Lecture impossible dans le dossier tampon.");
                return result;
            }
            merging[i].pixels = rows[i].view(ipr::Recti(readable.x, 0, readable.width, readable.height));
        }
        ipr::Image merged;
        HdrMerge::Statistics statistics;
        if (!HdrMerge::merge(merging, options, merged, &statistics)) {
            result.cameraRgb = {};
            result.error = QStringLiteral("Fusion impossible.");
            return result;
        }
        const ipr::Recti middle(result.commonArea.x - readable.x, result.commonArea.y + top - readable.y, result.commonArea.width, height);
        ipr::Image place = result.cameraRgb.mutableView(ipr::Recti(0, top, result.commonArea.width, height));
        ipr::copyInto(merged.view(middle), place);
        // The context rows are counted in their own band: the statistics weigh the band's pixels.
        const double weight = double(middle.area()) / double(std::max<qint64>(1, statistics.pixels));
        result.merge.pixels += quint64(middle.area());
        result.merge.clippedEverywhere += quint64(std::llround(double(statistics.clippedEverywhere) * weight));
        result.merge.motionRejected += quint64(std::llround(double(statistics.motionRejected) * weight));
        for (size_t i = 0; i < merging.size(); ++i)
            share[i] += statistics.share[i] * double(middle.area());
        advance();
    }
    for (size_t i = 0; i < merging.size(); ++i)
        result.merge.share[size_t(indices[i])] = share[i] / double(std::max<quint64>(1, result.merge.pixels));
    result.success = true;
    return result;
}

} // namespace HdrJob
