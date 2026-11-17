// Manual measurement of the HDR merge on a real bracketed series (plan B9, step H2,
// docs/architecture/fusion-hdr.md): the merge against each frame where that frame is well
// exposed (linearity), the measured exposures against the nominal ones, and the noise of a
// merge against one frame's, both read on twin frames, so that no true image is needed.
// Reads the sources only; everything it writes goes to --out, --cache and --evidence.
#include "CameraSignal.h"
#include "ColorPipeline.h"
#include "HdrJob.h"
#include "ImageExporter.h"
#include "NoiseLaw.h"
#include "RawDecoder.h"
#include "Filters.h"
#include "Neighbourhood.h"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
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

constexpr int blockSide = 16;            // linearity: means over blocks, the noise averaged out
constexpr int tileSide = 32;             // noise: deviations over tiles
constexpr double wellExposedLow = 0.01;  // of a frame's white: below, its block mean is noise-led
constexpr double wellExposedHigh = 0.8;  // above, near the clip and the merge's knee
constexpr double edgeLimit = 0.2;        // a block whose green varies more is an edge: registration, not tone
const std::array<double, 10> levelEdges{0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.5, 0.65, 0.8, 1.0};

struct Shot
{
    QString name;
    QString make;
    QString model;
    double exposureSeconds = 0.0;
    double aperture = 0.0;
    int iso = 0;
    double white = 0.0;
    double nominal() const { return exposureSeconds > 0.0 && iso > 0 ? exposureSeconds * iso / (aperture > 0.0 ? aperture * aperture : 1.0) : 0.0; }
};

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

// As the application decodes the frames of an assembly (ImageEditor's stack decoder).
RawDecoder::Options decodeOptions()
{
    RawDecoder::Options options;
    options.automaticWhiteBalance = false;
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

bool writeShot(const Shot &shot, const QString &path)
{
    QFile file(path);
    const QJsonObject object{{QStringLiteral("make"), shot.make}, {QStringLiteral("model"), shot.model},
                             {QStringLiteral("exposure_s"), shot.exposureSeconds}, {QStringLiteral("aperture"), shot.aperture},
                             {QStringLiteral("iso"), shot.iso}, {QStringLiteral("white"), shot.white}};
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) > 0;
}

std::optional<Shot> readShot(const QString &name, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.isEmpty())
        return std::nullopt;
    Shot shot;
    shot.name = name;
    shot.make = object.value(QStringLiteral("make")).toString();
    shot.model = object.value(QStringLiteral("model")).toString();
    shot.exposureSeconds = object.value(QStringLiteral("exposure_s")).toDouble();
    shot.aperture = object.value(QStringLiteral("aperture")).toDouble();
    shot.iso = object.value(QStringLiteral("iso")).toInt();
    shot.white = object.value(QStringLiteral("white")).toDouble();
    return shot;
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

double quantile(std::vector<double> values, double q)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[size_t(q * double(values.size() - 1))];
}

struct BlockStats
{
    bool valid = false;
    ipr::Vec3d mean{0.0, 0.0, 0.0};
    double peak = 0.0;        // the largest channel of any pixel
    double meanPeak = 0.0;    // the mean of each pixel's largest channel
    double deviation = 0.0;   // of the green
};

BlockStats blockStats(const ipr::Image &image, int x0, int y0, int side)
{
    BlockStats stats;
    if (x0 < 0 || y0 < 0 || x0 + side > image.width() || y0 + side > image.height())
        return stats;
    double sum[3]{0.0, 0.0, 0.0}, squares = 0.0, peaks = 0.0;
    for (int y = y0; y < y0 + side; ++y) {
        const ipr::Vec3f *row = image.row<ipr::Vec3f>(y);
        for (int x = x0; x < x0 + side; ++x) {
            const double largest = std::max({double(row[x][0]), double(row[x][1]), double(row[x][2])});
            for (int c = 0; c < 3; ++c)
                sum[c] += row[x][c];
            stats.peak = std::max(stats.peak, largest);
            peaks += largest;
            squares += double(row[x][1]) * double(row[x][1]);
        }
    }
    const double count = double(side) * double(side);
    stats.mean = ipr::Vec3d(sum[0] / count, sum[1] / count, sum[2] / count);
    stats.meanPeak = peaks / count;
    stats.deviation = std::sqrt(std::max(0.0, squares / count - stats.mean[1] * stats.mean[1]));
    stats.valid = true;
    return stats;
}

// The merge against one frame, on blocks that frame exposes well: with M the merge in the
// units frame's levels and F the frame, M should be F x e_units / e_frame (measured exposures).
// Their ratio by the frame's level tells the merge's linearity; its median against the
// nominal exposures tells how far the measure lies from the EXIF.
QJsonObject linearityAgainst(const ipr::Image &merged, const ipr::Recti &area, const ipr::Image &frame,
                             const ipr::Matrix33d &referenceToFrame, const ipr::Vec3d &unitsOverFrame, double nominalUnitsOverFrame)
{
    std::array<std::vector<double>, levelEdges.size() - 1> byLevel;
    std::array<std::vector<double>, 3> measured;
    std::vector<double> nominal;
    for (int by = 0; by + blockSide <= merged.height(); by += blockSide)
        for (int bx = 0; bx + blockSide <= merged.width(); bx += blockSide) {
            const BlockStats m = blockStats(merged, bx, by, blockSide);
            if (!m.valid || m.mean[1] <= 0.0 || m.deviation > edgeLimit * m.mean[1])
                continue;
            const double cx = area.x + bx + 0.5 * blockSide, cy = area.y + by + 0.5 * blockSide;
            const double w = referenceToFrame(2, 0) * cx + referenceToFrame(2, 1) * cy + referenceToFrame(2, 2);
            if (!(std::abs(w) > 1e-12))
                continue;
            const double u = (referenceToFrame(0, 0) * cx + referenceToFrame(0, 1) * cy + referenceToFrame(0, 2)) / w;
            const double v = (referenceToFrame(1, 0) * cx + referenceToFrame(1, 1) * cy + referenceToFrame(1, 2)) / w;
            const BlockStats f = blockStats(frame, int(std::lround(u - 0.5 * blockSide)), int(std::lround(v - 0.5 * blockSide)), blockSide);
            if (!f.valid || f.peak >= wellExposedHigh || f.mean[1] < wellExposedLow)
                continue;
            for (int c = 0; c < 3; ++c)
                if (f.mean[c] > 0.0)
                    measured[size_t(c)].push_back(m.mean[c] / (f.mean[c] * unitsOverFrame[c]));
            const double ratio = m.mean[1] / (f.mean[1] * unitsOverFrame[1]);
            nominal.push_back(m.mean[1] / (f.mean[1] * nominalUnitsOverFrame));
            for (size_t bin = 0; bin + 1 < levelEdges.size(); ++bin)
                if (f.mean[1] >= levelEdges[bin] && f.mean[1] < levelEdges[bin + 1])
                    byLevel[bin].push_back(ratio);
        }
    QJsonArray bins;
    for (size_t bin = 0; bin + 1 < levelEdges.size(); ++bin) {
        if (byLevel[bin].empty())
            continue;
        bins.append(QJsonObject{{QStringLiteral("frame_level"), QJsonArray{levelEdges[bin], levelEdges[bin + 1]}},
                                {QStringLiteral("blocks"), int(byLevel[bin].size())},
                                {QStringLiteral("median_ratio_green"), median(byLevel[bin])},
                                {QStringLiteral("p10_ratio_green"), quantile(byLevel[bin], 0.1)},
                                {QStringLiteral("p90_ratio_green"), quantile(byLevel[bin], 0.9)}});
    }
    const double nominalRatio = median(nominal);
    return QJsonObject{{QStringLiteral("blocks"), int(nominal.size())},
                       {QStringLiteral("median_ratio_rgb"), QJsonArray{median(measured[0]), median(measured[1]), median(measured[2])}},
                       {QStringLiteral("measured_over_exif_ev"), nominalRatio > 0.0 ? std::log2(nominalRatio) : 0.0},
                       {QStringLiteral("by_frame_level"), bins}};
}

// Pixel (i, j) of the result samples the source at (x + i, y + j), bilinearly; the caller
// keeps every sample and its right and lower neighbours inside the source.
ipr::Image shiftedCopy(const ipr::Image &source, double x, double y, const ipr::Size &size)
{
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = float(x - x0), fy = float(y - y0);
    ipr::Image out = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int j = 0; j < size.height; ++j) {
        const ipr::Vec3f *upper = source.row<ipr::Vec3f>(y0 + j) + x0;
        const ipr::Vec3f *lower = source.row<ipr::Vec3f>(y0 + j + 1) + x0;
        ipr::Vec3f *row = out.mutableRow<ipr::Vec3f>(j);
        for (int i = 0; i < size.width; ++i)
            for (int c = 0; c < 3; ++c)
                row[i][c] = (1.f - fy) * ((1.f - fx) * upper[i][c] + fx * upper[i + 1][c]) + fy * ((1.f - fx) * lower[i][c] + fx * lower[i + 1][c]);
    }
    return out;
}

struct Contribution
{
    double exposure = 1.0;   // relative to the frame the levels are read in
    HdrMerge::NoiseModel noise;
};

// The deviation an ideal inverse-variance merge would leave, over one frame's, at green level
// s of that frame (white 1) whose largest channel lies at peak: as HdrMerge weighs them, each
// frame counts with its law while its largest channel stays below the knee.
double idealRatio(double s, double peak, const std::vector<Contribution> &frames, const HdrMerge::NoiseModel &single, double knee)
{
    double inverse = 0.0;
    for (const Contribution &frame : frames) {
        if (frame.exposure * peak >= knee)
            continue;
        const double level = frame.exposure * s;
        inverse += frame.exposure * frame.exposure / (frame.noise.shot * level + frame.noise.read);
    }
    const double singleVariance = single.shot * s + single.read;
    return inverse > 0.0 && singleVariance > 0.0 ? std::sqrt(1.0 / inverse / singleVariance) : 0.0;
}

// A frame's variance as measured on the twins, by level: read, shot and a part proportional
// to the signal (the photosites' unequal response), which a frame's own law leaves out.
struct MeasuredNoise
{
    double read = 0.0;
    double shot = 0.0;
    double proportional = 0.0;
    double at(double s) const { return read + shot * s + proportional * s * s; }
};

// Least squares of variance = read + shot s + proportional s^2; empty below three points.
std::optional<MeasuredNoise> fitNoise(const std::vector<double> &levels, const std::vector<double> &variances)
{
    if (levels.size() < 3 || levels.size() != variances.size())
        return std::nullopt;
    double m[3][4]{};
    for (size_t k = 0; k < levels.size(); ++k) {
        const double row[3]{1.0, levels[k], levels[k] * levels[k]};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                m[i][j] += row[i] * row[j];
            m[i][3] += row[i] * variances[k];
        }
    }
    for (int i = 0; i < 3; ++i) {   // Gauss-Jordan with partial pivoting
        int pivot = i;
        for (int r = i + 1; r < 3; ++r)
            if (std::abs(m[r][i]) > std::abs(m[pivot][i]))
                pivot = r;
        if (!(std::abs(m[pivot][i]) > 0.0))
            return std::nullopt;
        std::swap(m[i], m[pivot]);
        for (int r = 0; r < 3; ++r) {
            if (r == i)
                continue;
            const double f = m[r][i] / m[i][i];
            for (int c = i; c < 4; ++c)
                m[r][c] -= f * m[i][c];
        }
    }
    return MeasuredNoise{m[0][3] / m[0][0], m[1][3] / m[1][1], m[2][3] / m[2][2]};
}

// The same merge, its weights still from the laws as HdrMerge sets them, but each frame's
// variance as the twins measured it: what the merge can reach with this sensor.
double idealRatioMeasured(double s, double peak, const std::vector<Contribution> &frames, const MeasuredNoise &measured, double knee)
{
    double total = 0.0, variance = 0.0;
    std::vector<double> weights;
    for (const Contribution &frame : frames) {
        const double level = frame.exposure * s;
        weights.push_back(frame.exposure * peak < knee ? frame.exposure * frame.exposure / (frame.noise.shot * level + frame.noise.read) : 0.0);
        total += weights.back();
    }
    if (!(total > 0.0) || !(measured.at(s) > 0.0))
        return 0.0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const double w = weights[i] / total, e = frames[i].exposure;
        variance += w * w * measured.at(e * s) / (e * e);
    }
    return std::sqrt(variance / measured.at(s));
}

// Noise of a merge against one frame's, without the true image: two merges A and B of
// distinct frames of the same exposures differ only by their noise, as two twin frames do.
// Read on the flatter half of the tiles, by level of the frame (one stop per bin).
QJsonObject noiseAgainstSingle(const ipr::Image &mergedA, const ipr::Image &mergedB, const ipr::Image &singleA, const ipr::Image &singleB,
                               const std::vector<Contribution> &frames, const HdrMerge::NoiseModel &single, double knee)
{
    struct Tile { double level; double peak; double score; double ideal; std::array<double, 3> merge; std::array<double, 3> single; };
    std::vector<Tile> tiles;
    ipr::Image green = ipr::Image::create(mergedA.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < mergedA.height(); ++y) {
        const ipr::Vec3f *in = mergedA.row<ipr::Vec3f>(y);
        float *out = green.mutableRow<float>(y);
        for (int x = 0; x < mergedA.width(); ++x)
            out[x] = in[x][1];
    }
    const ipr::Image coarse = ipr::gaussianBlurred(green, 4.0);
    const ipr::Image gx = ipr::sobel(coarse, true), gy = ipr::sobel(coarse, false);
    const auto deviationOfDifference = [](const ipr::Image &a, const ipr::Image &b, int x0, int y0, std::array<double, 3> &out) {
        for (int c = 0; c < 3; ++c) {
            double sum = 0.0, squares = 0.0;
            for (int y = y0; y < y0 + tileSide; ++y) {
                const ipr::Vec3f *ra = a.row<ipr::Vec3f>(y), *rb = b.row<ipr::Vec3f>(y);
                for (int x = x0; x < x0 + tileSide; ++x) {
                    const double d = (double(ra[x][c]) - double(rb[x][c])) * std::sqrt(0.5);
                    sum += d;
                    squares += d * d;
                }
            }
            const double count = double(tileSide) * double(tileSide);
            out[size_t(c)] = std::sqrt(std::max(0.0, squares / count - (sum / count) * (sum / count)));
        }
    };
    for (int y = 0; y + tileSide <= mergedA.height(); y += tileSide)
        for (int x = 0; x + tileSide <= mergedA.width(); x += tileSide) {
            const BlockStats level = blockStats(mergedA, x, y, tileSide);
            const BlockStats frame = blockStats(singleA, x, y, tileSide);
            if (level.mean[1] <= 0.0 || frame.peak >= knee)
                continue;
            double energy = 0.0;
            for (int yy = y; yy < y + tileSide; ++yy)
                for (int xx = x; xx < x + tileSide; ++xx) {
                    const double a = gx.row<float>(yy)[xx], b = gy.row<float>(yy)[xx];
                    energy += a * a + b * b;
                }
            Tile tile{level.mean[1], level.meanPeak, energy / (level.mean[1] * level.mean[1]),
                      idealRatio(level.mean[1], level.meanPeak, frames, single, knee), {}, {}};
            deviationOfDifference(mergedA, mergedB, x, y, tile.merge);
            deviationOfDifference(singleA, singleB, x, y, tile.single);
            tiles.push_back(tile);
        }
    std::vector<double> scores;
    for (const Tile &tile : tiles)
        scores.push_back(tile.score);
    const double flat = median(scores);
    std::map<int, std::vector<const Tile *>> bins;
    for (const Tile &tile : tiles)
        if (tile.score <= flat)
            bins[int(std::floor(std::log2(tile.level)))].push_back(&tile);
    // The single frame's variance by level, fitted on the bins, for the second ideal.
    std::vector<double> fitLevels, fitVariances;
    for (const auto &[stop, members] : bins) {
        if (members.size() < 8)
            continue;
        std::vector<double> levels, sigmas;
        for (const Tile *tile : members) {
            levels.push_back(tile->level);
            sigmas.push_back(tile->single[1]);
        }
        fitLevels.push_back(median(levels));
        fitVariances.push_back(median(sigmas) * median(sigmas));
    }
    const std::optional<MeasuredNoise> measured = fitNoise(fitLevels, fitVariances);
    QJsonArray out;
    for (const auto &[stop, members] : bins) {
        if (members.size() < 8)
            continue;
        std::vector<double> levels, sigmaMerge, sigmaSingle, ideals, idealsMeasured;
        std::array<std::vector<double>, 3> ratios;
        for (const Tile *tile : members) {
            levels.push_back(tile->level);
            ideals.push_back(tile->ideal);
            if (measured)
                idealsMeasured.push_back(idealRatioMeasured(tile->level, tile->peak, frames, *measured, knee));
            sigmaMerge.push_back(tile->merge[1]);
            sigmaSingle.push_back(tile->single[1]);
            for (int c = 0; c < 3; ++c)
                if (tile->single[size_t(c)] > 0.0)
                    ratios[size_t(c)].push_back(tile->merge[size_t(c)] / tile->single[size_t(c)]);
        }
        out.append(QJsonObject{{QStringLiteral("level_stop"), stop},
                               {QStringLiteral("median_level"), median(levels)},
                               {QStringLiteral("tiles"), int(members.size())},
                               {QStringLiteral("sigma_single_green"), median(sigmaSingle)},
                               {QStringLiteral("sigma_merge_green"), median(sigmaMerge)},
                               {QStringLiteral("ratio_rgb"), QJsonArray{median(ratios[0]), median(ratios[1]), median(ratios[2])}},
                               {QStringLiteral("ideal_ratio"), median(ideals)},
                               {QStringLiteral("ideal_ratio_measured_noise"), median(idealsMeasured)}});
    }
    QJsonObject result{{QStringLiteral("tiles"), int(tiles.size())}, {QStringLiteral("flat_score_limit"), flat}, {QStringLiteral("by_level"), out}};
    if (measured)
        result.insert(QStringLiteral("single_variance_fit"), QJsonObject{{QStringLiteral("read"), measured->read},
                                                                         {QStringLiteral("shot"), measured->shot},
                                                                         {QStringLiteral("proportional"), measured->proportional}});
    return result;
}

bool renderCrop(const RawDecoder::DcpSource &source, const ipr::Image &cameraRgb, const ipr::Recti &area, double ev, const QString &path)
{
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

QJsonObject frameJson(const HdrJob::FrameReport &frame, const Shot &shot, double share)
{
    const ipr::Matrix33d &m = frame.alignment.referenceToFrame;
    return QJsonObject{
        {QStringLiteral("source"), shot.name},
        {QStringLiteral("exposure_s"), shot.exposureSeconds},
        {QStringLiteral("iso"), shot.iso},
        {QStringLiteral("aperture"), shot.aperture},
        {QStringLiteral("reference"), frame.reference},
        {QStringLiteral("used"), frame.used},
        {QStringLiteral("exclusion"), frame.exclusion},
        {QStringLiteral("warning"), frame.warning},
        {QStringLiteral("measured_exposure_rgb"), QJsonArray{frame.exposure[0], frame.exposure[1], frame.exposure[2]}},
        {QStringLiteral("measured_through"), frame.measuredThrough},
        {QStringLiteral("nominal_gap_ev"), frame.nominalGapEv},
        {QStringLiteral("shift_px"), QJsonArray{m(0, 2), m(1, 2)}},
        {QStringLiteral("residual_px"), frame.alignment.residual},
        {QStringLiteral("inliers"), frame.alignment.inliers},
        {QStringLiteral("local_largest_px"), frame.localLargest},
        {QStringLiteral("merge_share"), share},
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const auto value = [&](const QString &name, const QString &fallback = {}) {
        const qsizetype at = args.indexOf(name);
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : fallback;
    };
    const QDir folder(value(QStringLiteral("--folder")));
    const QString outPath = value(QStringLiteral("--out"));
    const QString cachePath = value(QStringLiteral("--cache"));
    const QString evidence = value(QStringLiteral("--evidence"));
    const QString label = value(QStringLiteral("--label"), QStringLiteral("series"));
    const QStringList names = value(QStringLiteral("--files")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (outPath.isEmpty() || cachePath.isEmpty() || !folder.exists() || names.size() < 2) {
        std::cerr << "usage: iprHdrAudit --folder <dir> --files a.CR2,b.CR2,... --label <name> --out <dir> --cache <dir> [--evidence <dir>] [--scratch <dir>] [--keep-all]\n";
        return 2;
    }
    // As the application's « Garder toutes les vues »: no frame faded as moving.
    const bool keepAll = args.contains(QStringLiteral("--keep-all"));
    QDir().mkpath(outPath);
    QDir().mkpath(cachePath);
    const QDir out(outPath), cache(cachePath);
    const int count = int(names.size());
    const auto listing = [&] { return folder.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name); };
    const QStringList listingBefore = listing();
    std::vector<QString> digests;
    for (const QString &name : names)
        digests.push_back(sha256(folder.filePath(name)));

    // Each frame decoded once, as the application decodes it; the jobs read the audit's cache.
    std::vector<Shot> shots(static_cast<size_t>(count));
    QJsonArray decodeSeconds;
    QElapsedTimer watch;
    for (int i = 0; i < count; ++i) {
        const QString cached = cache.filePath(names[i] + QStringLiteral(".rgb"));
        const QString described = cache.filePath(names[i] + QStringLiteral(".json"));
        if (QFileInfo::exists(cached))
            if (const std::optional<Shot> shot = readShot(names[i], described)) {
                shots[size_t(i)] = *shot;
                continue;
            }
        watch.restart();
        QString error;
        ImageMetadata metadata;
        const ColorPipeline::WorkingImage image = RawDecoder::decode(folder.filePath(names[i]), decodeOptions(), &metadata, &error);
        if (image.isNull() || !image.dcpContext || image.dcpContext->cameraRgb.empty()) {
            std::cerr << names[i].toStdString() << ": no camera signal. " << error.toStdString() << '\n';
            return 1;
        }
        decodeSeconds.append(double(watch.elapsed()) / 1000.0);
        Shot &shot = shots[size_t(i)];
        shot = {names[i], metadata.cameraMake, metadata.cameraModel, metadata.exposureSeconds, metadata.aperture, metadata.iso,
                double(metadata.normalizationWhiteLevel)};
        if (!writeCache(image.dcpContext->cameraRgb, cached) || !writeShot(shot, described)) {
            std::cerr << "cache write failed\n";
            return 1;
        }
        std::cout << "decoded " << names[i].toStdString() << std::endl;
    }

    // The exposure levels of the series, one stop apart or so; twins share a level. Set A takes
    // the first frame of each level, set B the second: two merges of distinct frames.
    std::vector<int> byExposure(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        byExposure[size_t(i)] = i;
    std::stable_sort(byExposure.begin(), byExposure.end(), [&](int a, int b) { return shots[size_t(a)].nominal() < shots[size_t(b)].nominal(); });
    std::vector<std::vector<int>> levels;
    for (const int i : byExposure) {
        if (!(shots[size_t(i)].nominal() > 0.0)) {
            std::cerr << names[i].toStdString() << ": no nominal exposure\n";
            return 1;
        }
        if (levels.empty() || std::abs(std::log2(shots[size_t(i)].nominal() / shots[size_t(levels.back().front())].nominal())) > 0.2)
            levels.push_back({});
        levels.back().push_back(i);
    }
    std::vector<int> setA, setB;
    for (const std::vector<int> &level : levels)
        if (level.size() >= 2) {
            setA.push_back(level[0]);
            setB.push_back(level[1]);
        }
    // The reference: the frame of the middle level (ascending), as the merge keeps its levels.
    const int referenceLevel = int(levels.size()) / 2;
    const int reference = levels[size_t(referenceLevel)].front();

    // The measured noise laws weigh the frames, as in the application.
    QString catalogError;
    const std::optional<NoiseLaw::Catalog> catalog = NoiseLaw::loadCatalog(QStringLiteral("config/noise-laws.json"), &catalogError);
    if (!catalog)
        std::cerr << "no noise laws, generic sensor: " << catalogError.toStdString() << '\n';
    const auto modelOf = [&](int global) {
        const Shot &shot = shots[size_t(global)];
        return HdrJob::noiseModel(catalog ? &*catalog : nullptr, shot.make, shot.model, shot.iso, shot.white);
    };
    const QString scratchRoot = value(QStringLiteral("--scratch"), out.path());
    const auto runJob = [&](const std::vector<int> &indexes, int referenceGlobal, HdrJob::Result &result) {
        HdrJob::Request request;
        request.frameCount = int(indexes.size());
        request.referenceIndex = int(std::find(indexes.begin(), indexes.end(), referenceGlobal) - indexes.begin());
        request.scratchRoot = scratchRoot;
        request.merge.motionSigma = keepAll ? 0.0 : HdrMerge::Options{}.motionSigma;
        request.frame = [&](int index) {
            const int global = indexes[size_t(index)];
            HdrJob::Frame frame;
            frame.cameraRgb = readCache(cache.filePath(names[global] + QStringLiteral(".rgb")));
            frame.nominalExposure = shots[size_t(global)].nominal();
            frame.noise = modelOf(global);
            if (frame.cameraRgb.empty())
                frame.error = QStringLiteral("not in the cache");
            return frame;
        };
        watch.restart();
        result = HdrJob::run(request);
        return double(watch.elapsed()) / 1000.0;
    };

    // The whole series.
    std::vector<int> all(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        all[size_t(i)] = i;
    HdrJob::Result whole;
    const double wholeSeconds = runJob(all, reference, whole);
    if (!whole.success) {
        std::cerr << "merge failed: " << whole.error.toStdString() << '\n';
        return 1;
    }
    const double wholePeak = peakMemoryGb();
    std::cout << "merged " << whole.usedFrames << " frames in " << wholeSeconds << " s, range " << whole.rangeEv << " EV" << std::endl;

    // Linearity: the merge against each frame, where that frame is well exposed.
    const ipr::Vec3d unitsExposure = whole.frames[size_t(whole.unitsFrame)].exposure;
    const double unitsNominal = shots[size_t(whole.unitsFrame)].nominal();
    QJsonArray frames;
    for (int i = 0; i < count; ++i) {
        const HdrJob::FrameReport &report = whole.frames[size_t(i)];
        QJsonObject entry = frameJson(report, shots[size_t(i)], whole.merge.share.empty() ? 0.0 : whole.merge.share[size_t(i)]);
        entry.insert(QStringLiteral("exif_ev_vs_units"), std::log2(shots[size_t(i)].nominal() / unitsNominal));
        if (report.used) {
            const ipr::Image frame = readCache(cache.filePath(names[i] + QStringLiteral(".rgb")));
            const ipr::Vec3d unitsOverFrame(unitsExposure[0] / report.exposure[0], unitsExposure[1] / report.exposure[1],
                                            unitsExposure[2] / report.exposure[2]);
            if (!frame.empty())
                entry.insert(QStringLiteral("linearity"), linearityAgainst(whole.cameraRgb, whole.commonArea, frame, report.alignment.referenceToFrame,
                                                                          unitsOverFrame, unitsNominal / shots[size_t(i)].nominal()));
        }
        frames.append(entry);
        std::cout << "linearity " << names[i].toStdString() << std::endl;
    }

    // Noise: two merges of distinct frames against two twin frames, in the reference's levels.
    QJsonObject noise;
    const int referenceB = levels[size_t(referenceLevel)].size() >= 2 ? levels[size_t(referenceLevel)][1] : -1;
    if (setA.size() >= 2 && referenceB >= 0) {
        HdrJob::Result mergeA, mergeB;
        const double secondsA = runJob(setA, reference, mergeA);
        runJob(setB, referenceB, mergeB);
        if (mergeA.success && mergeB.success) {
            // B's reference sits where the whole merge registered it on A's, a translation on a
            // tripod. B's merge and B's twin are both resampled there, bilinearly: a shift left
            // rounded would carry texture into the differences, more into the merge's, the
            // smaller noise; the resampling's smoothing applies alike to both.
            const ipr::Matrix33d &twin = whole.frames[size_t(referenceB)].alignment.referenceToFrame;
            const double tx = twin(0, 2), ty = twin(1, 2);
            const ipr::Point2i floor(int(std::floor(tx)), int(std::floor(ty)));
            const ipr::Recti inB(mergeB.commonArea.x - floor.x, mergeB.commonArea.y - floor.y, mergeB.commonArea.width - 1, mergeB.commonArea.height - 1);
            const ipr::Recti twinFrame(-floor.x, -floor.y, whole.frameSize.width - 1, whole.frameSize.height - 1);
            const ipr::Recti area = mergeA.commonArea & inB & twinFrame;
            const ipr::Image singleA = readCache(cache.filePath(names[reference] + QStringLiteral(".rgb")));
            const ipr::Image singleB = readCache(cache.filePath(names[referenceB] + QStringLiteral(".rgb")));
            // Each merge back to its reference's levels, from its units frame's.
            const auto scaled = [](const HdrJob::Result &job) {
                const ipr::Vec3d e = job.frames[size_t(job.unitsFrame)].exposure;
                ipr::Image image = job.cameraRgb.clone();
                for (int y = 0; y < image.height(); ++y) {
                    ipr::Vec3f *row = image.mutableRow<ipr::Vec3f>(y);
                    for (int x = 0; x < image.width(); ++x)
                        for (int c = 0; c < 3; ++c)
                            row[x][c] = float(row[x][c] / e[c]);
                }
                return image;
            };
            if (!area.empty() && !singleA.empty() && !singleB.empty()) {
                const ipr::Image a = scaled(mergeA).copy(area - mergeA.commonArea.tl());
                const ipr::Image b = shiftedCopy(scaled(mergeB), area.x + tx - mergeB.commonArea.x, area.y + ty - mergeB.commonArea.y, area.size());
                const ipr::Image ra = singleA.copy(area);
                const ipr::Image rb = shiftedCopy(singleB, area.x + tx, area.y + ty, area.size());
                std::vector<Contribution> contributions;
                for (size_t k = 0; k < setA.size(); ++k)
                    if (mergeA.frames[k].used)
                        contributions.push_back({(mergeA.frames[k].exposure[0] + mergeA.frames[k].exposure[1] + mergeA.frames[k].exposure[2]) / 3.0,
                                                 modelOf(setA[k])});
                noise = noiseAgainstSingle(a, b, ra, rb, contributions, modelOf(reference), HdrMerge::Options{}.knee);
                QJsonArray namesA, namesB;
                for (const int i : setA)
                    namesA.append(names[i]);
                for (const int i : setB)
                    namesB.append(names[i]);
                noise.insert(QStringLiteral("set_a"), namesA);
                noise.insert(QStringLiteral("set_b"), namesB);
                noise.insert(QStringLiteral("twin_shift_px"), QJsonArray{twin(0, 2), twin(1, 2)});
                noise.insert(QStringLiteral("seconds_merge_a"), secondsA);
            }
        }
        std::cout << "noise measured" << std::endl;
    }

    // Views to look at: the whole merge, and its darkest flat part against the reference's.
    RawDecoder::DcpSource source;
    {
        QString error;
        const ColorPipeline::WorkingImage image = RawDecoder::decode(folder.filePath(names[reference]), decodeOptions(), &source.metadata, &error);
        if (!image.isNull() && image.dcpContext) {
            source.workingContext = image.dcpContext;
            source.whiteBalanceKelvin = image.dcpContext->sourceKelvin;
        }
    }
    QJsonArray renders;
    if (source.workingContext) {
        const ipr::Image overview = ipr::resizedBy(whole.cameraRgb, 0.25, 0.25, ipr::Interpolation::Area);
        for (const double ev : {0.0, -2.0}) {
            const QString name = QStringLiteral("%1-merge-overview-ev%2.png").arg(label).arg(int(ev));
            if (renderCrop(source, overview, ipr::Recti(0, 0, overview.width(), overview.height()), ev, out.filePath(name)))
                renders.append(name);
        }
        const ipr::Image singleFull = readCache(cache.filePath(names[reference] + QStringLiteral(".rgb")));
        // The darkest 800 x 600 window of the merge, on a coarse grid.
        double darkest = 1e30;
        ipr::Recti window;
        for (int y = 0; y + 600 <= whole.cameraRgb.height(); y += 150)
            for (int x = 0; x + 800 <= whole.cameraRgb.width(); x += 200) {
                const double level = ipr::meanOf(whole.cameraRgb.view(ipr::Recti(x, y, 800, 600)))[1];
                if (level < darkest) {
                    darkest = level;
                    window = ipr::Recti(x, y, 800, 600);
                }
            }
        if (!singleFull.empty() && !window.empty()) {
            const double ev = std::clamp(std::round(std::log2(0.18 / std::max(1e-6, darkest))), 0.0, 6.0);
            const QString suffix = QStringLiteral("shadows-ev%1.png").arg(int(ev));
            renderCrop(source, whole.cameraRgb, window, ev, out.filePath(label + QStringLiteral("-merge-") + suffix));
            // The reference at the merge's levels: the units frame's exposure over its own.
            const ipr::Vec3d e = unitsExposure;
            const ipr::Image single = ipr::mapped(singleFull.copy(window + whole.commonArea.tl()), [k = float((e[0] + e[1] + e[2]) / 3.0)](float v) { return v * k; });
            renderCrop(source, single, ipr::Recti(0, 0, single.width(), single.height()), ev, out.filePath(label + QStringLiteral("-reference-") + suffix));
            renders.append(QJsonObject{{QStringLiteral("shadows_window"), QJsonArray{window.x, window.y, window.width, window.height}},
                                       {QStringLiteral("ev"), ev}});
        }
    }

    bool sourcesIntact = listing() == listingBefore;
    for (int i = 0; i < count; ++i)
        sourcesIntact = sourcesIntact && sha256(folder.filePath(names[i])) == digests[size_t(i)];
    QJsonArray digestsJson;
    for (const QString &digest : digests)
        digestsJson.append(digest);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    const QJsonObject root{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("data_version"), 1},
        {QStringLiteral("campaign_id"), QStringLiteral("b9-hdr-audit-v001-%1%2-%3").arg(label, keepAll ? QStringLiteral("-keep-all") : QString(), stamp)},
        {QStringLiteral("measured_at_utc"), now.toString(Qt::ISODate)},
        {QStringLiteral("group"), QStringLiteral("b9-hdr")},
        {QStringLiteral("candidate_revision"), gitOutput({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})},
        {QStringLiteral("candidate_dirty"), !gitOutput({QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty()},
        {QStringLiteral("method"), QStringLiteral(
             "Frames decoded as the application decodes them (signed camera RGB after lens corrections), merged by HdrJob with the "
             "middle exposure as reference. Linearity: 16 px block means of the merge against each frame where that frame lies in "
             "[0.01, 0.8) of its white and the block is no edge, the frame scaled by the measured exposures; the median against the "
             "EXIF exposures gives the measure's gap. Noise: two merges of distinct frames (first and second of each exposure) "
             "differ by their noise only, as the reference and its twin do; their deviations over the flatter half of 32 px tiles, "
             "per stop of the reference's level, against two ideals: the inverse-variance ratio of the noise laws, and the same weights applied to the variance the twins measure (read, shot and a part proportional to the signal, fitted by least squares)")},
        {QStringLiteral("limitations"), QStringLiteral(
             "A still life on a tripod, indoor light; twins differ by the shutter's repeatability and a registration rounded to "
             "the pixel; the ideal ratio ignores the demosaicing's correlation, alike for both terms")},
        {QStringLiteral("series"), label},
        {QStringLiteral("folder"), folder.dirName()},
        {QStringLiteral("sources_sha256"), digestsJson},
        {QStringLiteral("sources_intact"), sourcesIntact},
        {QStringLiteral("reference"), names[reference]},
        {QStringLiteral("units_frame"), names[whole.unitsFrame]},
        {QStringLiteral("exposure_levels"), int(levels.size())},
        {QStringLiteral("used_frames"), whole.usedFrames},
        {QStringLiteral("range_ev"), whole.rangeEv},
        {QStringLiteral("frame_size"), QJsonArray{whole.frameSize.width, whole.frameSize.height}},
        {QStringLiteral("common_area"), QJsonArray{whole.commonArea.x, whole.commonArea.y, whole.commonArea.width, whole.commonArea.height}},
        {QStringLiteral("clipped_everywhere_share"), whole.merge.pixels ? double(whole.merge.clippedEverywhere) / double(whole.merge.pixels) : 0.0},
        // Of the samples the motion test weighs: each pixel of each frame but its pivot.
        {QStringLiteral("motion_rejected_sample_share"), whole.merge.pixels && whole.usedFrames > 1
             ? double(whole.merge.motionRejected) / (double(whole.merge.pixels) * double(whole.usedFrames - 1)) : 0.0},
        {QStringLiteral("keep_all"), keepAll},
        {QStringLiteral("seconds_decode_each"), decodeSeconds},
        {QStringLiteral("seconds_merge_from_decoded"), wholeSeconds},
        {QStringLiteral("scratch_gb"), double(whole.scratchBytes) / 1e9},
        {QStringLiteral("band_rows"), whole.bandRows},
        {QStringLiteral("peak_memory_gb_after_merge"), wholePeak},
        {QStringLiteral("frames"), frames},
        {QStringLiteral("noise"), noise},
        {QStringLiteral("renders"), renders},
    };
    const QString target = QDir(evidence.isEmpty() ? out.path() : evidence).filePath(root.value(QStringLiteral("campaign_id")).toString() + QStringLiteral(".json"));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) <= 0) {
        std::cerr << "evidence not written\n";
        return 1;
    }
    file.close();
    std::cout << target.toStdString() << std::endl;
    return sourcesIntact ? 0 : 1;
}
