// Manual measurement of the noise-reduction stack on a real series: noise against the
// reference, what the fusion keeps of the detail, registration, time and resources.
// Reads the sources only; everything it writes goes to --out and --cache.
#include "CameraDng.h"
#include "CameraProfileEngine.h"
#include "CameraSignal.h"
#include "ColorPipeline.h"
#include "ImageExporter.h"
#include "RawDecoder.h"
#include "StackJob.h"
#include "Analysis.h"
#include "Filters.h"
#include "Neighbourhood.h"
#include "Parallel.h"
#include "Pixels.h"
#include "Resize.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

constexpr int tileSide = 128;
constexpr double blurForDetail = 4.0;

double peakMemoryGb()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return double(counters.PeakWorkingSetSize) / 1e9;
#endif
    return -1.0;
}

QString sha256(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString gitOutput(const QStringList &arguments)
{
    QProcess git;
    git.start(QStringLiteral("git"), arguments);
    if (!git.waitForFinished(30000))
        return {};
    return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
}

RawDecoder::Options decodeOptions()
{
    RawDecoder::Options options;
    options.automaticWhiteBalance = false;
    // The tool runs from the source tree, where the profiles lie too.
    options.normalizationManifest = QFileInfo(QStringLiteral("config/raw-normalization.json")).absoluteFilePath();
    options.useNormalizationEnvironment = false;
    options.preferCameraNeutral = true;
    options.signedDevelopment = true;
    return options;
}

bool writeCache(const ipr::Image &image, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const int header[2]{image.width(), image.height()};
    bool fine = file.write(reinterpret_cast<const char *>(header), sizeof(header)) == qint64(sizeof(header));
    const qint64 rowBytes = qint64(image.width()) * qint64(image.pixelBytes());
    for (int y = 0; fine && y < image.height(); ++y)
        fine = file.write(reinterpret_cast<const char *>(image.row<std::uint8_t>(y)), rowBytes) == rowBytes;
    return fine;
}

ipr::Image readCache(const QString &path)
{
    QFile file(path);
    int header[2]{};
    if (!file.open(QIODevice::ReadOnly) || file.read(reinterpret_cast<char *>(header), sizeof(header)) != qint64(sizeof(header)) ||
        header[0] <= 0 || header[1] <= 0)
        return {};
    ipr::Image image = ipr::Image::create(ipr::Size(header[0], header[1]), ipr::Depth::F32, 3);
    const qint64 rowBytes = qint64(image.width()) * qint64(image.pixelBytes());
    for (int y = 0; y < image.height(); ++y)
        if (file.read(reinterpret_cast<char *>(image.mutableRow<std::uint8_t>(y)), rowBytes) != rowBytes)
            return {};
    return image;
}

ipr::Image luminanceOf(const ipr::Image &rgb)
{
    ipr::Image luminance = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
    ipr::parallelRows(rgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const ipr::Vec3f *in = rgb.row<ipr::Vec3f>(y);
            float *out = luminance.mutableRow<float>(y);
            for (int x = 0; x < rgb.width(); ++x)
                out[x] = 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2];
        }
    });
    return luminance;
}

ipr::Image differenceOf(const ipr::Image &a, const ipr::Image &b)
{
    return ipr::mapped(a, b, [](float x, float y) { return x - y; });
}

// The windowed spectrum of a tile of a plane, its mean taken out first.
std::vector<std::complex<double>> spectrumOf(const ipr::Image &plane, const ipr::Recti &tile, const ipr::Image &window)
{
    const ipr::Image part = plane.copy(tile);
    const float mean = float(ipr::meanOf(part)[0]);
    return ipr::fourierTransform(ipr::mapped(part, window, [mean](float v, float w) { return (v - mean) * w; }));
}

struct Tiles
{
    std::vector<ipr::Recti> flat;     // the least detail of the frame: noise is read there
    std::vector<ipr::Recti> textured; // the most: detail is read there
};

// Ranks the tiles by the detail of the noise-free view: the merged image's coarse gradients.
Tiles chooseTiles(const ipr::Image &merged)
{
    const ipr::Image luminance = luminanceOf(merged);
    const ipr::Image coarse = ipr::gaussianBlurred(luminance, blurForDetail);
    const ipr::Image energy = ipr::mapped(ipr::sobel(coarse, true), ipr::sobel(coarse, false), [](float x, float y) { return x * x + y * y; });
    struct Ranked { double score; ipr::Recti tile; double level; };
    std::vector<Ranked> ranked;
    for (int y = 0; y + tileSide <= merged.height(); y += tileSide)
        for (int x = 0; x + tileSide <= merged.width(); x += tileSide) {
            const ipr::Recti tile(x, y, tileSide, tileSide);
            const double level = ipr::meanOf(luminance.view(tile))[0];
            ranked.push_back({ipr::meanOf(energy.view(tile))[0] / std::max(1e-6, level * level), tile, level});
        }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.score < b.score; });
    Tiles tiles;
    const size_t share = std::max<size_t>(1, ranked.size() / 10);
    for (size_t i = 0; i < std::min(share, ranked.size()); ++i)
        tiles.flat.push_back(ranked[i].tile);
    for (size_t i = ranked.size() - std::min(share, ranked.size()); i < ranked.size(); ++i)
        tiles.textured.push_back(ranked[i].tile);
    return tiles;
}

ipr::Image highPass(const ipr::Image &image)
{
    return differenceOf(image, ipr::gaussianBlurred(image, blurForDetail));
}

// Median over the flat tiles of the ratio of a stack's noise to the reference's, per channel.
QJsonObject noiseRatio(const ipr::Image &reference, const ipr::Image &stack, const std::vector<ipr::Recti> &tiles)
{
    const ipr::Image a = highPass(reference), b = highPass(stack);
    std::array<std::vector<double>, 3> ratios;
    std::array<std::vector<double>, 3> levels;
    for (const ipr::Recti &tile : tiles) {
        std::vector<double> mean, deviationA, deviationB;
        ipr::meanAndDeviationOf(a.view(tile), mean, deviationA);
        ipr::meanAndDeviationOf(b.view(tile), mean, deviationB);
        for (int c = 0; c < 3; ++c) {
            if (deviationA[c] > 0.0)
                ratios[size_t(c)].push_back(deviationB[c] / deviationA[c]);
            levels[size_t(c)].push_back(deviationA[c]);
        }
    }
    const auto median = [](std::vector<double> values) {
        if (values.empty())
            return 0.0;
        std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(values.size() / 2), values.end());
        return values[values.size() / 2];
    };
    return QJsonObject{
        {QStringLiteral("ratio_rgb"), QJsonArray{median(ratios[0]), median(ratios[1]), median(ratios[2])}},
        {QStringLiteral("reference_sigma_rgb"), QJsonArray{median(levels[0]), median(levels[1]), median(levels[2])}},
        {QStringLiteral("tiles"), int(tiles.size())},
    };
}

// The fusion's transfer of detail, by frequency: with A and B two stacks whose noise is
// independent of each other and of the reference R, T = <A conj B> / <A conj R>. The noise
// of each cancels in the averages, so a loss of T is a true loss of detail. 1 means none.
QJsonObject transfer(const ipr::Image &reference, const ipr::Image &a, const ipr::Image &b, const std::vector<ipr::Recti> &tiles)
{
    constexpr int bins = 9;
    std::array<double, bins> crossAB{}, crossAR{}, powerA{}, powerR{};
    const ipr::Image window = ipr::hannWindow(ipr::Size(tileSide, tileSide));
    const ipr::Image lr = luminanceOf(reference), la = luminanceOf(a), lb = luminanceOf(b);
    for (const ipr::Recti &tile : tiles) {
        const auto fr = spectrumOf(lr, tile, window), fa = spectrumOf(la, tile, window), fb = spectrumOf(lb, tile, window);
        for (int v = 0; v < tileSide; ++v)
            for (int u = 0; u < tileSide; ++u) {
                const double fu = (u <= tileSide / 2 ? u : u - tileSide) / double(tileSide);
                const double fv = (v <= tileSide / 2 ? v : v - tileSide) / double(tileSide);
                const int bin = int(std::hypot(fu, fv) / 0.05) - 1;
                if (bin < 0 || bin >= bins)
                    continue;
                const size_t at = size_t(v * tileSide + u);
                const std::complex<double> pa = fa[at], pb = fb[at], pr = fr[at];
                crossAB[size_t(bin)] += (pa * std::conj(pb)).real();
                crossAR[size_t(bin)] += (pa * std::conj(pr)).real();
                powerA[size_t(bin)] += std::norm(pa);
                powerR[size_t(bin)] += std::norm(pr);
            }
    }
    QJsonArray frequencies, transfers, coherences;
    for (int bin = 0; bin < bins; ++bin) {
        frequencies.append((bin + 1.5) * 0.05);
        transfers.append(std::abs(crossAR[size_t(bin)]) > 0.0 ? crossAB[size_t(bin)] / crossAR[size_t(bin)] : 0.0);
        coherences.append(powerA[size_t(bin)] > 0.0 && powerR[size_t(bin)] > 0.0
                              ? crossAR[size_t(bin)] / std::sqrt(powerA[size_t(bin)] * powerR[size_t(bin)]) : 0.0);
    }
    return QJsonObject{{QStringLiteral("cycles_per_pixel"), frequencies},
                       {QStringLiteral("transfer"), transfers},
                       {QStringLiteral("coherence_stack_reference"), coherences},
                       {QStringLiteral("tiles"), int(tiles.size())}};
}

// The signal two stacks with independent noise have in common, by frequency: <A conj B>.
// Noise cancels in it, so a rise at high frequencies is detail recovered, not noise kept.
QJsonObject sharedSignal(const ipr::Image &a, const ipr::Image &b, const ipr::Recti &area)
{
    constexpr int bins = 9;
    constexpr int step = tileSide / 2;
    std::array<double, bins> power{};
    const ipr::Image window = ipr::hannWindow(ipr::Size(tileSide, tileSide));
    const ipr::Image la = luminanceOf(a), lb = luminanceOf(b);
    int tiles = 0;
    for (int y = area.y; y + tileSide <= area.y + area.height; y += step)
        for (int x = area.x; x + tileSide <= area.x + area.width; x += step) {
            const ipr::Recti tile(x, y, tileSide, tileSide);
            const auto fa = spectrumOf(la, tile, window), fb = spectrumOf(lb, tile, window);
            ++tiles;
            for (int v = 0; v < tileSide; ++v)
                for (int u = 0; u < tileSide; ++u) {
                    const double fu = (u <= tileSide / 2 ? u : u - tileSide) / double(tileSide);
                    const double fv = (v <= tileSide / 2 ? v : v - tileSide) / double(tileSide);
                    const int bin = int(std::hypot(fu, fv) / 0.05) - 1;
                    if (bin < 0 || bin >= bins)
                        continue;
                    const size_t at = size_t(v * tileSide + u);
                    power[size_t(bin)] += (fa[at] * std::conj(fb[at])).real();
                }
        }
    QJsonArray frequencies, values;
    for (int bin = 0; bin < bins; ++bin) {
        frequencies.append((bin + 1.5) * 0.05);
        values.append(power[size_t(bin)]);
    }
    return QJsonObject{{QStringLiteral("cycles_per_pixel"), frequencies}, {QStringLiteral("power"), values}, {QStringLiteral("tiles"), tiles}};
}

// How far blocks of the stack lie from the reference, what one transform leaves over a
// scene with depth (parallax).
QJsonObject localShifts(const ipr::Image &reference, const ipr::Image &stack, const std::vector<ipr::Recti> &tiles)
{
    constexpr int radius = 6;
    constexpr int half = 32;
    const auto root = [](float v) { return std::sqrt(std::max(v, 0.f)); };
    const ipr::Image ra = ipr::mapped(luminanceOf(reference), root), rb = ipr::mapped(luminanceOf(stack), root);
    std::vector<double> shifts;
    for (const ipr::Recti &tile : tiles) {
        const ipr::Point2i centre(tile.x + tile.width / 2, tile.y + tile.height / 2);
        const ipr::Recti block(centre.x - half, centre.y - half, 2 * half + 1, 2 * half + 1);
        const ipr::Recti search(block.x - radius, block.y - radius, block.width + 2 * radius, block.height + 2 * radius);
        if ((search & ipr::Recti(0, 0, ra.width(), ra.height())) != search)
            continue;
        const ipr::Image scores = ipr::matchedTemplate(rb.view(search), ra.view(block));
        // The first place of the best score, rows first.
        double top = -std::numeric_limits<double>::infinity();
        ipr::Point2i at;
        for (int y = 0; y < scores.height(); ++y)
            for (int x = 0; x < scores.width(); ++x)
                if (scores.row<float>(y)[x] > top) {
                    top = scores.row<float>(y)[x];
                    at = ipr::Point2i(x, y);
                }
        if (top < 0.8 || at.x <= 0 || at.y <= 0 || at.x >= scores.width() - 1 || at.y >= scores.height() - 1)
            continue;
        const auto offset = [](double before, double middle, double after) {
            const double curve = before - 2.0 * middle + after;
            return std::abs(curve) > 1e-12 ? 0.5 * (before - after) / curve : 0.0;
        };
        const float *row = scores.row<float>(at.y);
        const double dx = at.x + offset(row[at.x - 1], top, row[at.x + 1]) - radius;
        const double dy = at.y + offset(scores.row<float>(at.y - 1)[at.x], top, scores.row<float>(at.y + 1)[at.x]) - radius;
        shifts.push_back(std::hypot(dx, dy));
    }
    std::sort(shifts.begin(), shifts.end());
    const auto quantile = [&](double q) { return shifts.empty() ? 0.0 : shifts[size_t(q * double(shifts.size() - 1))]; };
    return QJsonObject{{QStringLiteral("blocks"), int(shifts.size())},
                       {QStringLiteral("median_px"), quantile(0.5)},
                       {QStringLiteral("p95_px"), quantile(0.95)},
                       {QStringLiteral("worst_px"), quantile(1.0)}};
}

QJsonObject frameJson(const StackJob::FrameReport &frame, const QString &name)
{
    QJsonArray matrix;
    for (int i = 0; i < 9; ++i)
        matrix.append(frame.alignment.referenceToFrame(i / 3, i % 3));
    return QJsonObject{
        {QStringLiteral("source"), name},
        {QStringLiteral("reference"), frame.reference},
        {QStringLiteral("used"), frame.used},
        {QStringLiteral("exclusion"), frame.exclusion},
        {QStringLiteral("model"), frame.alignment.model == StackAlignment::Model::Homography ? QStringLiteral("homography") : QStringLiteral("similarity")},
        {QStringLiteral("blocks"), frame.alignment.blocks},
        {QStringLiteral("inliers"), frame.alignment.inliers},
        {QStringLiteral("residual_px"), frame.alignment.residual},
        {QStringLiteral("support"), frame.alignment.support},
        {QStringLiteral("gains_rgb"), QJsonArray{frame.gains[0], frame.gains[1], frame.gains[2]}},
        {QStringLiteral("reference_to_frame"), matrix},
        {QStringLiteral("local_blocks"), frame.localBlocks},
        {QStringLiteral("local_largest_px"), frame.localLargest},
    };
}

bool renderCrop(const RawDecoder::DcpSource &source, const ipr::Image &cameraRgb, const ipr::Recti &area, double ev,
                const QString &path)
{
    // As the decoder renders a calibrated signal: applyProfile asks a new decode for it.
    CameraSignal::Source signal;
    const float gain = float(std::pow(2.0, ev));
    signal.rgb = ipr::mapped(cameraRgb.copy(area), [gain](float v) { return v * gain; });
    signal.cameraMake = source.metadata.cameraMake;
    signal.cameraModel = source.metadata.cameraModel;
    signal.whiteBalanceKelvin = source.whiteBalanceKelvin;
    signal.whiteBalanceApplied = true;
    signal.cameraNeutral = source.workingContext->cameraNeutral;
    QString error;
    const ColorPipeline::WorkingImage image = CameraSignal::render(std::move(signal), source.workingContext->profilePath,
                                                                   source.workingContext->options, &error);
    if (image.isNull()) {
        std::cerr << "render: " << error.toStdString() << '\n';
        return false;
    }
    QFile::remove(path);
    return ImageExporter::write(image, path, QStringLiteral("PNG"), 100).success;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    const auto value = [&](const QString &name, const QString &fallback = {}) {
        const qsizetype at = args.indexOf(name);
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : fallback;
    };
    const QDir folder(value(QStringLiteral("--folder")));
    const QString outPath = value(QStringLiteral("--out"));
    const QString cachePath = value(QStringLiteral("--cache"));
    const QString evidence = value(QStringLiteral("--evidence"));
    if (outPath.isEmpty() || cachePath.isEmpty() || !folder.exists()) {
        std::cerr << "usage: iprStackAudit --folder <series> --out <dir> --cache <dir> [--evidence <dir>] [--first name] [--limit N] [--scratch <dir>] [--quick] [--no-local] [--areas x,y,w,h;...]\n";
        return 2;
    }
    QDir().mkpath(outPath);
    QDir().mkpath(cachePath);
    const QDir out(outPath), cache(cachePath);

    QStringList names = folder.entryList({QStringLiteral("*.CR3"), QStringLiteral("*.CR2"), QStringLiteral("*.DNG")}, QDir::Files, QDir::Name);
    const int limit = value(QStringLiteral("--limit"), QString::number(names.size())).toInt();
    const qsizetype firstAt = names.indexOf(value(QStringLiteral("--first")));
    names = names.mid(std::max<qsizetype>(0, firstAt), limit);
    const int count = int(names.size());
    if (count < 4) {
        std::cerr << "at least 4 frames\n";
        return 2;
    }
    const int referenceIndex = count / 2;
    const auto listing = [&] { return folder.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name); };
    const QStringList listingBefore = listing();
    std::vector<QString> digests;
    for (const QString &name : names)
        digests.push_back(sha256(folder.filePath(name)));

    // Each frame decoded once; the jobs below read them back from the audit's own cache.
    auto referenceSource = std::make_shared<RawDecoder::DcpSource>();
    QJsonArray decodeSeconds;
    QElapsedTimer watch;
    for (int i = 0; i < count; ++i) {
        const QString cached = cache.filePath(names[i] + QStringLiteral(".rgb"));
        if (QFileInfo::exists(cached) && i != referenceIndex)
            continue;
        watch.restart();
        QString error;
        ImageMetadata metadata;
        const ColorPipeline::WorkingImage image = RawDecoder::decode(folder.filePath(names[i]), decodeOptions(), &metadata, &error);
        // The signal of the decode itself: the decoder retains no buffer under a calibration.
        if (image.isNull() || !image.dcpContext || image.dcpContext->cameraRgb.empty()) {
            std::cerr << names[i].toStdString() << ": no camera signal. " << error.toStdString() << '\n';
            continue;
        }
        decodeSeconds.append(double(watch.elapsed()) / 1000.0);
        const ipr::Image &signal = image.dcpContext->cameraRgb;
        if (i == referenceIndex) {
            referenceSource->cameraRgb = image.dcpContext->cameraRgb.share();
            referenceSource->workingContext = image.dcpContext;
            referenceSource->metadata = metadata;
            referenceSource->whiteBalanceKelvin = image.dcpContext->sourceKelvin;
        }
        if (!QFileInfo::exists(cached) && !writeCache(signal, cached))
            std::cerr << "cache write failed\n";
        std::cout << "decoded " << names[i].toStdString() << ' ' << signal.width() << 'x' << signal.height() << std::endl;
    }

    const QString scratchRoot = value(QStringLiteral("--scratch"), out.path());
    const auto runJob = [&](const std::vector<int> &indexes, int reference, bool mergeReference, StackJob::Result &result) {
        StackJob::Request request;
        request.frameCount = int(indexes.size());
        request.referenceIndex = reference;
        request.scratchRoot = scratchRoot;
        request.mergeReference = mergeReference;
        request.localRefinement = !args.contains(QStringLiteral("--no-local"));
        request.correctTurbulence = !args.contains(QStringLiteral("--no-turbulence"));
        request.turbulence.keepShare = value(QStringLiteral("--keep"), QStringLiteral("0.5")).toDouble();
        request.frame = [&](int index) {
            StackJob::Frame frame;
            frame.cameraRgb = readCache(cache.filePath(names[indexes[size_t(index)]] + QStringLiteral(".rgb")));
            if (frame.cameraRgb.empty())
                frame.error = QStringLiteral("not decoded");
            return frame;
        };
        watch.restart();
        result = StackJob::run(request);
        return double(watch.elapsed()) / 1000.0;
    };

    // The whole series.
    std::vector<int> all;
    for (int i = 0; i < count; ++i)
        all.push_back(i);
    StackJob::Result whole;
    const double wholeSeconds = runJob(all, referenceIndex, true, whole);
    if (!whole.success) {
        std::cerr << "stack failed: " << whole.error.toStdString() << '\n';
        return 1;
    }
    std::cout << "stack of " << whole.usedFrames << " in " << wholeSeconds << " s, area " << whole.commonArea.width << 'x' << whole.commonArea.height << std::endl;
    const double wholePeak = peakMemoryGb();
    const ipr::Image referenceFull = readCache(cache.filePath(names[referenceIndex] + QStringLiteral(".rgb")));
    if (referenceFull.empty()) {
        std::cerr << "the reference is not in the cache\n";
        return 1;
    }
    const ipr::Image reference = referenceFull.view(whole.commonArea);
    const Tiles tiles = chooseTiles(whole.cameraRgb);
    const QJsonObject noiseWhole = noiseRatio(reference, whole.cameraRgb, tiles.flat);

    // The gain as frames add up, around the reference.
    const bool quick = args.contains(QStringLiteral("--quick"));
    QJsonArray growth;
    for (const int frames : {2, 4, 8, 16}) {
        if (quick || args.contains(QStringLiteral("--no-growth")) || frames >= count)
            break;
        std::vector<int> subset;
        for (int i = 0; i < frames; ++i)
            subset.push_back((referenceIndex - frames / 2 + i + count) % count);
        const int at = int(std::find(subset.begin(), subset.end(), referenceIndex) - subset.begin());
        StackJob::Result partial;
        const double seconds = runJob(subset, at, true, partial);
        if (!partial.success)
            continue;
        const ipr::Recti area = partial.commonArea & whole.commonArea;
        const ipr::Recti inPartial(area.x - partial.commonArea.x, area.y - partial.commonArea.y, area.width, area.height);
        const ipr::Recti inWhole(area.x - whole.commonArea.x, area.y - whole.commonArea.y, area.width, area.height);
        std::vector<ipr::Recti> inside;
        for (const ipr::Recti &tile : tiles.flat)
            if ((tile & inWhole) == tile)
                inside.push_back(tile - inWhole.tl());
        growth.append(QJsonObject{{QStringLiteral("frames"), partial.usedFrames},
                                  {QStringLiteral("ideal_ratio"), 1.0 / std::sqrt(double(partial.usedFrames))},
                                  {QStringLiteral("seconds"), seconds},
                                  {QStringLiteral("noise"), noiseRatio(reference.view(inWhole), partial.cameraRgb.view(inPartial), inside)}});
        std::cout << "growth " << frames << std::endl;
    }

    // Two halves independent of each other and of the reference.
    std::vector<int> odd{referenceIndex}, even{referenceIndex};
    for (int i = 0; i < count; ++i)
        if (i != referenceIndex)
            ((i % 2) ? odd : even).push_back(i);
    StackJob::Result halfA, halfB;
    if (!quick) {
        runJob(odd, 0, false, halfA);
        runJob(even, 0, false, halfB);
    }
    QJsonObject detail;
    if (halfA.success && halfB.success) {
        const ipr::Recti area = (halfA.commonArea & halfB.commonArea) & whole.commonArea;
        const ipr::Recti inA(area.x - halfA.commonArea.x, area.y - halfA.commonArea.y, area.width, area.height);
        const ipr::Recti inB(area.x - halfB.commonArea.x, area.y - halfB.commonArea.y, area.width, area.height);
        const ipr::Image a = halfA.cameraRgb.view(inA), b = halfB.cameraRgb.view(inB), r = referenceFull.view(area);
        const Tiles halfTiles = chooseTiles(whole.cameraRgb.view(ipr::Recti(area.x - whole.commonArea.x, area.y - whole.commonArea.y, area.width, area.height)));
        const float half = std::sqrt(0.5f);
        const ipr::Image difference = ipr::mapped(a, b, [half](float x, float y) { return (x - y) * half; });
        std::array<std::vector<double>, 3> ratios;
        const ipr::Image hr = highPass(r);
        for (const ipr::Recti &tile : halfTiles.flat) {
            std::vector<double> mean, deviationD, deviationR;
            ipr::meanAndDeviationOf(difference.view(tile), mean, deviationD);
            ipr::meanAndDeviationOf(hr.view(tile), mean, deviationR);
            for (int c = 0; c < 3; ++c)
                if (deviationR[c] > 0.0)
                    ratios[size_t(c)].push_back(deviationD[c] / deviationR[c]);
        }
        QJsonArray measured;
        for (auto &values : ratios) {
            std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(values.size() / 2), values.end());
            measured.append(values.empty() ? 0.0 : values[values.size() / 2]);
        }
        detail = QJsonObject{{QStringLiteral("half_frames"), QJsonArray{halfA.usedFrames, halfB.usedFrames}},
                             {QStringLiteral("half_noise_ratio_rgb"), measured},
                             {QStringLiteral("ideal_half_ratio"), 1.0 / std::sqrt(0.5 * (halfA.usedFrames + halfB.usedFrames))},
                             {QStringLiteral("transfer_textured"), transfer(r, a, b, halfTiles.textured)},
                             {QStringLiteral("local_shift_textured"), localShifts(r, a, halfTiles.textured)}};
        const QStringList subjectParts = value(QStringLiteral("--subject")).split(QLatin1Char(','));
        if (subjectParts.size() == 4) {
            const ipr::Recti wanted(subjectParts[0].toInt(), subjectParts[1].toInt(), subjectParts[2].toInt(), subjectParts[3].toInt());
            const ipr::Recti inHalves = (wanted & area) - area.tl();
            detail.insert(QStringLiteral("shared_signal_subject"), sharedSignal(a, b, inHalves));
            detail.insert(QStringLiteral("subject"), QJsonArray{wanted.x, wanted.y, wanted.width, wanted.height});
        }
    }

    // The reference and the stack side by side, on a tile of each kind, at 100 %.
    QJsonArray crops;
    if (!referenceSource->cameraRgb.empty() && !tiles.flat.empty() && !tiles.textured.empty()) {
        const auto grown = [&](const ipr::Recti &tile) {
            const ipr::Point2i centre(tile.x + tile.width / 2, tile.y + tile.height / 2);
            return ipr::Recti(centre.x - 400, centre.y - 300, 800, 600) & ipr::Recti(0, 0, reference.width(), reference.height());
        };
        const std::vector<std::pair<QString, ipr::Recti>> areas{{QStringLiteral("flat"), grown(tiles.flat[tiles.flat.size() / 2])},
                                                              {QStringLiteral("textured"), grown(tiles.textured.back())},
                                                              {QStringLiteral("textured2"), grown(tiles.textured[tiles.textured.size() * 3 / 4])},
                                                              {QStringLiteral("textured3"), grown(tiles.textured[tiles.textured.size() / 2])}};
        for (const auto &[kind, area] : areas)
            for (const double ev : {0.0, 2.0}) {
                const QString suffix = QStringLiteral("%1-ev%2.png").arg(kind).arg(int(ev));
                renderCrop(*referenceSource, reference, area, ev, out.filePath(QStringLiteral("reference-") + suffix));
                renderCrop(*referenceSource, whole.cameraRgb, area, ev, out.filePath(QStringLiteral("stack-") + suffix));
                crops.append(suffix);
            }
    }

    // Views chosen by hand, in the reference's frame, the same from one run to the next; and
    // the whole picture at a quarter of its size, to choose them.
    if (!referenceSource->cameraRgb.empty()) {
        const ipr::Image overview = ipr::resizedBy(whole.cameraRgb, 0.25, 0.25, ipr::Interpolation::Area);
        renderCrop(*referenceSource, overview, ipr::Recti(0, 0, overview.width(), overview.height()), 0.0, out.filePath(QStringLiteral("overview.png")));
        int number = 0;
        for (const QString &text : value(QStringLiteral("--areas")).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QStringList parts = text.split(QLatin1Char(','));
            if (parts.size() != 4)
                continue;
            const ipr::Recti area = (ipr::Recti(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt()) & whole.commonArea) - whole.commonArea.tl();
            if (area.empty())
                continue;
            const QString name = QStringLiteral("area%1-ev%2.png").arg(number++);
            const double ev = value(QStringLiteral("--area-ev"), QStringLiteral("0")).toDouble();
            renderCrop(*referenceSource, reference, area, ev, out.filePath(QStringLiteral("reference-") + name.arg(int(ev))));
            renderCrop(*referenceSource, whole.cameraRgb, area, ev, out.filePath(QStringLiteral("stack-") + name.arg(int(ev))));
        }
    }

    // The result as a file: reopened through the decoder, it must render as the signal does.
    QJsonObject dngRoundTrip;
    if (!referenceSource->cameraRgb.empty()) {
        CameraDng::Content content;
        const CameraProfileEngine::DcpCameraData camera = CameraProfileEngine::dcpCameraData(referenceSource->workingContext->profilePath);
        content.cameraRgb = whole.cameraRgb.share();
        content.cameraMake = referenceSource->metadata.cameraMake;
        content.cameraModel = referenceSource->metadata.cameraModel;
        content.lensMake = referenceSource->metadata.lensMake;
        content.lensModel = referenceSource->metadata.lensModel;
        content.iso = referenceSource->metadata.iso;
        content.exposureSeconds = referenceSource->metadata.exposureSeconds;
        content.focalLengthMm = referenceSource->metadata.focalLengthMm;
        content.captureDate = referenceSource->metadata.captureDate;
        content.cameraNeutral = referenceSource->workingContext->cameraNeutral;
        content.whiteBalanceKelvin = referenceSource->workingContext->sourceKelvin;
        content.renderOptions = referenceSource->workingContext->options;
        content.colorMatrix = camera.colorMatrix;
        content.illuminant = camera.illuminant;
        content.provenance = QStringLiteral("IPR stack of %1 frames, reference %2").arg(whole.usedFrames).arg(names[referenceIndex]);
        const QString dngPath = out.filePath(QStringLiteral("stack.dng"));
        QString dngError;
        watch.restart();
        const bool written = CameraDng::write(content, dngPath, &dngError);
        const double writeSeconds = double(watch.elapsed()) / 1000.0;
        watch.restart();
        ImageMetadata reopenedMetadata;
        RawDecoder::Options reopenOptions;
        reopenOptions.useNormalizationEnvironment = false;
        reopenOptions.preferCameraNeutral = true;
        const ColorPipeline::WorkingImage reopened = written ? RawDecoder::decode(dngPath, reopenOptions, &reopenedMetadata, &dngError) : ColorPipeline::WorkingImage{};
        const double readSeconds = double(watch.elapsed()) / 1000.0;
        CameraSignal::Source signal;
        signal.rgb = whole.cameraRgb.clone();
        signal.cameraMake = content.cameraMake;
        signal.cameraModel = content.cameraModel;
        signal.whiteBalanceKelvin = content.whiteBalanceKelvin;
        signal.whiteBalanceApplied = true;
        signal.cameraNeutral = content.cameraNeutral;
        const ColorPipeline::WorkingImage direct = CameraSignal::render(std::move(signal), referenceSource->workingContext->profilePath, content.renderOptions, &dngError);
        double worst = -1.0, mean = -1.0;
        if (!reopened.isNull() && !direct.isNull() && reopened.linearRgba.size() == direct.linearRgba.size()) {
            worst = ipr::largestDifference(reopened.linearRgba, direct.linearRgba);
            mean = ipr::meanOf(ipr::mapped(reopened.linearRgba, direct.linearRgba,
                                           [](float a, float b) { return std::abs(a - b); }))[0];
        }
        dngRoundTrip = QJsonObject{{QStringLiteral("written"), written},
                                   {QStringLiteral("reopened"), !reopened.isNull()},
                                   {QStringLiteral("decoder"), reopenedMetadata.decoderId},
                                   {QStringLiteral("error"), dngError},
                                   {QStringLiteral("bytes"), double(QFileInfo(dngPath).size())},
                                   {QStringLiteral("seconds_write"), writeSeconds},
                                   {QStringLiteral("seconds_read"), readSeconds},
                                   {QStringLiteral("render_worst_difference"), worst},
                                   {QStringLiteral("render_mean_difference"), mean}};
        std::cout << "dng " << QJsonDocument(dngRoundTrip).toJson(QJsonDocument::Compact).toStdString() << std::endl;
    }

    QJsonArray frames;
    for (int i = 0; i < count; ++i)
        frames.append(frameJson(whole.frames[size_t(i)], names[i]));

    bool sourcesIntact = listing() == listingBefore;
    for (int i = 0; i < count; ++i)
        sourcesIntact = sourcesIntact && sha256(folder.filePath(names[i])) == digests[size_t(i)];
    QJsonArray digestsJson;
    for (const QString &digest : digests)
        digestsJson.append(digest);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    QJsonObject root{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("data_version"), 1},
        {QStringLiteral("campaign_id"), QStringLiteral("b7-stack-audit-v001-") + stamp},
        {QStringLiteral("measured_at_utc"), now.toString(Qt::ISODate)},
        {QStringLiteral("group"), QStringLiteral("b7-stacking")},
        {QStringLiteral("local_refinement"), !args.contains(QStringLiteral("--no-local"))},
        {QStringLiteral("candidate_revision"), gitOutput({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})},
        {QStringLiteral("candidate_dirty"), !gitOutput({QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty()},
        {QStringLiteral("method"), QStringLiteral("Signed camera RGB after lens corrections; noise as the high-pass deviation on the 10 % flattest tiles of the merged image, against the reference; detail transfer T=<A conj B>/<A conj R> between two independent half-stacks A, B and the reference R on the 10 % most textured tiles")},
        {QStringLiteral("limitations"), QStringLiteral("One hand-held indoor scene; no moving subject planted; a noise ratio below the ideal partly comes from the resampling, read with the transfer")},
        {QStringLiteral("units"), QJsonObject{{QStringLiteral("noise"), QStringLiteral("ratio of standard deviations, stack over reference, white 1")},
                                              {QStringLiteral("frequency"), QStringLiteral("cycles per pixel")}}},
        {QStringLiteral("series"), folder.dirName()},
        {QStringLiteral("sources_sha256"), digestsJson},
        {QStringLiteral("sources_intact"), sourcesIntact},
        {QStringLiteral("frames"), frames},
        {QStringLiteral("used_frames"), whole.usedFrames},
        {QStringLiteral("frame_size"), QJsonArray{referenceFull.width(), referenceFull.height()}},
        {QStringLiteral("common_area"), QJsonArray{whole.commonArea.x, whole.commonArea.y, whole.commonArea.width, whole.commonArea.height}},
        {QStringLiteral("area_kept"), double(whole.commonArea.area()) / double(referenceFull.size().area())},
        {QStringLiteral("rejected_share"), double(whole.merge.rejected) / std::max(1.0, double(whole.merge.samples))},
        {QStringLiteral("seconds_decode_each"), decodeSeconds},
        {QStringLiteral("seconds_stack_from_decoded"), wholeSeconds},
        {QStringLiteral("scratch_gb"), double(whole.scratchBytes) / 1e9},
        {QStringLiteral("band_rows"), whole.bandRows},
        {QStringLiteral("peak_memory_gb_after_stack"), wholePeak},
        {QStringLiteral("noise_whole"), noiseWhole},
        {QStringLiteral("ideal_ratio_whole"), 1.0 / std::sqrt(double(whole.usedFrames))},
        {QStringLiteral("growth"), growth},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("dng_round_trip"), dngRoundTrip},
        {QStringLiteral("turbulence"), QJsonObject{{QStringLiteral("applied"), whole.turbulence.applied},
                                                   {QStringLiteral("views"), whole.turbulence.views},
                                                   {QStringLiteral("kept"), whole.turbulence.kept},
                                                   {QStringLiteral("keep_share"), value(QStringLiteral("--keep"), QStringLiteral("0.5")).toDouble()},
                                                   {QStringLiteral("measured_blocks"), whole.turbulence.measuredBlocks},
                                                   {QStringLiteral("largest_px"), whole.turbulence.largestDisplacement},
                                                   {QStringLiteral("reason"), whole.turbulence.reason},
                                                   {QStringLiteral("subject"), QJsonArray{whole.subject.x, whole.subject.y, whole.subject.width, whole.subject.height}},
                                                   {QStringLiteral("sharpness"), [&] {
                                                        QJsonArray values;
                                                        for (const double v : whole.turbulence.sharpness)
                                                            values.append(v);
                                                        return values;
                                                    }()}}},
    };
    const QString target = QDir(evidence.isEmpty() ? out.path() : evidence).filePath(root.value(QStringLiteral("campaign_id")).toString() + QStringLiteral(".json"));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) <= 0) {
        std::cerr << "evidence not written\n";
        return 1;
    }
    file.close();
    std::cout << target.toStdString() << std::endl;
    std::cout << QJsonDocument(QJsonObject{{QStringLiteral("noise_whole"), noiseWhole}, {QStringLiteral("detail"), detail}, {QStringLiteral("growth"), growth}}).toJson().toStdString();
    return sourcesIntact ? 0 : 1;
}
