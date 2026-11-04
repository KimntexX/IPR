// Manual measurement of the panorama on a real series: what joins, the geometry, the seams'
// inputs, time and resources. Reads the sources only; everything it writes goes to --out
// and --cache.
#include "CameraProfileEngine.h"
#include "CameraSignal.h"
#include "ColorPipeline.h"
#include "PanoramaJob.h"
#include "RawDecoder.h"

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

#include <iostream>
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

RawDecoder::Options decodeOptions(bool lensCorrection)
{
    RawDecoder::Options options;
    options.lensCorrectionEnabled = lensCorrection;
    options.automaticWhiteBalance = false;
    // The tool runs from the source tree, where the profiles lie too.
    options.normalizationManifest = QFileInfo(QStringLiteral("config/raw-normalization.json")).absoluteFilePath();
    options.useNormalizationEnvironment = false;
    options.preferCameraNeutral = true;
    options.signedDevelopment = true;
    return options;
}

bool writeCache(const cv::Mat &image, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const int header[2]{image.cols, image.rows};
    bool fine = file.write(reinterpret_cast<const char *>(header), sizeof(header)) == qint64(sizeof(header));
    const qint64 rowBytes = qint64(image.cols) * qint64(image.elemSize());
    for (int y = 0; fine && y < image.rows; ++y)
        fine = file.write(reinterpret_cast<const char *>(image.ptr(y)), rowBytes) == rowBytes;
    return fine;
}

cv::Mat readCache(const QString &path)
{
    QFile file(path);
    int header[2]{};
    if (!file.open(QIODevice::ReadOnly) || file.read(reinterpret_cast<char *>(header), sizeof(header)) != qint64(sizeof(header)))
        return {};
    cv::Mat image(header[1], header[0], CV_32FC3);
    const qint64 rowBytes = qint64(image.cols) * qint64(image.elemSize());
    for (int y = 0; y < image.rows; ++y)
        if (file.read(reinterpret_cast<char *>(image.ptr(y)), rowBytes) != rowBytes)
            return {};
    return image;
}

QString projectionName(PanoramaGeometry::Projection projection)
{
    switch (projection) {
    case PanoramaGeometry::Projection::Automatic: return QStringLiteral("automatic");
    case PanoramaGeometry::Projection::Rectilinear: return QStringLiteral("rectilinear");
    case PanoramaGeometry::Projection::Cylindrical: return QStringLiteral("cylindrical");
    case PanoramaGeometry::Projection::Spherical: return QStringLiteral("spherical");
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const auto value = [&](const QString &name, const QString &fallback = {}) {
        const qsizetype at = arguments.indexOf(name);
        return at >= 0 && at + 1 < arguments.size() ? arguments[at + 1] : fallback;
    };
    const QDir folder(value(QStringLiteral("--folder")));
    const QStringList names = value(QStringLiteral("--names")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QString outPath = value(QStringLiteral("--out")), cachePath = value(QStringLiteral("--cache"));
    const QString evidence = value(QStringLiteral("--evidence")), label = value(QStringLiteral("--label"), QStringLiteral("series"));
    const QString projection = value(QStringLiteral("--projection"), QStringLiteral("automatic"));
    const bool freeLens = arguments.contains(QStringLiteral("--free-lens"));
    const bool noProfile = arguments.contains(QStringLiteral("--no-lens-profile"));
    if (outPath.isEmpty() || cachePath.isEmpty() || !folder.exists() || names.size() < 2) {
        std::cerr << "usage: iprPanoramaAudit --folder <dir> --names a.CR2,b.CR2,... --out <dir> --cache <dir> [--evidence <dir>] [--label name] [--projection automatic|cylindrical|spherical|rectilinear] [--full]\n";
        return 2;
    }
    QDir().mkpath(outPath);
    QDir().mkpath(cachePath);
    const QDir out(outPath), cache(cachePath);

    // Each frame decoded once; a job reads it back from the audit's own cache.
    std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> context;
    ImageMetadata metadata;
    QJsonArray sources;
    const int middle = int(names.size()) / 2;
    QElapsedTimer watch;
    watch.start();
    for (int index = 0; index < names.size(); ++index) {
        const QString path = folder.filePath(names[index]);
        const QString cached = cache.filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".float"));
        const bool wanted = index == middle || !QFile::exists(cached);
        if (wanted) {
            ImageMetadata frameMetadata;
            QString error;
            const ColorPipeline::WorkingImage image = RawDecoder::decode(path, decodeOptions(!noProfile), &frameMetadata, &error);
            if (!image.dcpContext) {
                std::cerr << names[index].toStdString() << ": " << error.toStdString() << '\n';
                return 1;
            }
            if (!writeCache(image.dcpContext->cameraRgb, cached))
                return 1;
            if (index == middle) {
                context = image.dcpContext;
                metadata = frameMetadata;
            }
        }
        QJsonObject source;
        source[QStringLiteral("name")] = names[index];
        source[QStringLiteral("sha256")] = sha256(path);
        sources.append(source);
    }
    const double decodeSeconds = watch.elapsed() / 1000.0;

    PanoramaJob::Request request;
    request.frameCount = int(names.size());
    request.scratchRoot = out.absolutePath();
    request.projection = projection == QStringLiteral("cylindrical") ? PanoramaGeometry::Projection::Cylindrical
        : projection == QStringLiteral("spherical") ? PanoramaGeometry::Projection::Spherical
        : projection == QStringLiteral("rectilinear") ? PanoramaGeometry::Projection::Rectilinear
                                                      : PanoramaGeometry::Projection::Automatic;
    request.frame = [&](int index) {
        PanoramaJob::Frame frame;
        frame.cameraRgb = readCache(cache.filePath(QFileInfo(names[index]).completeBaseName() + QStringLiteral(".float")));
        frame.focalMm = metadata.focalLengthMm;
        frame.sensorWidthMm = freeLens ? 0.0 : metadata.sensorWidthMm;
        frame.lensGeometryCorrected = !freeLens && metadata.lensGeometryApplied;
        return frame;
    };
    watch.restart();
    PanoramaJob::Result job = PanoramaJob::run(request);
    const double jobSeconds = watch.elapsed() / 1000.0;

    QJsonObject report;
    report[QStringLiteral("schema")] = QStringLiteral("ipr-panorama-audit-v001");
    report[QStringLiteral("created_utc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report[QStringLiteral("commit")] = gitOutput({QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
    report[QStringLiteral("label")] = label;
    report[QStringLiteral("sources")] = sources;
    report[QStringLiteral("requested_projection")] = projection;
    report[QStringLiteral("success")] = job.success;
    report[QStringLiteral("error")] = job.error;
    report[QStringLiteral("decode_seconds")] = decodeSeconds;
    report[QStringLiteral("job_seconds")] = jobSeconds;
    report[QStringLiteral("scratch_gb")] = double(job.scratchBytes) / 1e9;
    report[QStringLiteral("frame_width")] = job.frameSize.width;
    report[QStringLiteral("frame_height")] = job.frameSize.height;
    report[QStringLiteral("used_frames")] = job.usedFrames;
    report[QStringLiteral("focal_pixels")] = job.focal;
    report[QStringLiteral("distortion")] = job.distortion;
    report[QStringLiteral("focal_mm")] = metadata.focalLengthMm;
    report[QStringLiteral("sensor_width_mm")] = metadata.sensorWidthMm;
    report[QStringLiteral("lens_profile")] = metadata.lcpProfile;
    report[QStringLiteral("lens_geometry_applied")] = metadata.lensGeometryApplied;
    report[QStringLiteral("residual_pixels")] = job.residual;
    report[QStringLiteral("worst_pair_pixels")] = job.worstPair;
    report[QStringLiteral("layout")] = job.shape.layout == PanoramaGeometry::Layout::Vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal");
    report[QStringLiteral("projection")] = projectionName(job.shape.projection);
    report[QStringLiteral("rows")] = job.shape.rows;
    report[QStringLiteral("span_along_degrees")] = job.shape.spanAlong * 180.0 / CV_PI;
    report[QStringLiteral("span_across_degrees")] = job.shape.spanAcross * 180.0 / CV_PI;
    report[QStringLiteral("canvas_width")] = job.canvas.width;
    report[QStringLiteral("canvas_height")] = job.canvas.height;
    report[QStringLiteral("free_lens")] = freeLens;
    report[QStringLiteral("no_lens_profile")] = noProfile;
    report[QStringLiteral("kept_x")] = job.kept.x;
    report[QStringLiteral("kept_y")] = job.kept.y;
    report[QStringLiteral("kept_width")] = job.kept.width;
    report[QStringLiteral("kept_height")] = job.kept.height;
    report[QStringLiteral("covered_share")] = job.coveredShare;
    report[QStringLiteral("seam_cell_pixels")] = job.seamCellPixels;
    report[QStringLiteral("band_rows")] = job.bandRows;
    QJsonArray frames;
    for (const PanoramaJob::FrameReport &frame : job.frames) {
        QJsonObject entry;
        entry[QStringLiteral("name")] = names[frame.index];
        entry[QStringLiteral("used")] = frame.used;
        entry[QStringLiteral("exclusion")] = frame.exclusion;
        entry[QStringLiteral("features")] = frame.features;
        entry[QStringLiteral("gain_r")] = frame.gains[0];
        entry[QStringLiteral("gain_g")] = frame.gains[1];
        entry[QStringLiteral("gain_b")] = frame.gains[2];
        entry[QStringLiteral("x")] = frame.area.x;
        entry[QStringLiteral("y")] = frame.area.y;
        entry[QStringLiteral("width")] = frame.area.width;
        entry[QStringLiteral("height")] = frame.area.height;
        frames.append(entry);
    }
    report[QStringLiteral("frames")] = frames;
    QJsonArray pairs;
    for (const PanoramaJob::PairReport &pair : job.pairs) {
        QJsonObject entry;
        entry[QStringLiteral("a")] = names[pair.a];
        entry[QStringLiteral("b")] = names[pair.b];
        entry[QStringLiteral("points")] = pair.points;
        entry[QStringLiteral("residual_pixels")] = pair.residual;
        pairs.append(entry);
    }
    report[QStringLiteral("pairs")] = pairs;

    if (job.success && context) {
        CameraSignal::Source signal;
        signal.rgb = job.cameraRgb;
        signal.cameraMake = metadata.cameraMake;
        signal.cameraModel = metadata.cameraModel;
        signal.whiteBalanceKelvin = context->sourceKelvin;
        signal.whiteBalanceApplied = true;
        signal.cameraNeutral = context->cameraNeutral;
        QString error;
        watch.restart();
        const ColorPipeline::WorkingImage image = CameraSignal::render(std::move(signal), context->profilePath, context->options, &error);
        const QImage shown = image.isNull() ? QImage() : ColorPipeline::toSrgb(image, ColorPipeline::OutputFormat::Jpeg8Bit, &error);
        report[QStringLiteral("render_seconds")] = watch.elapsed() / 1000.0;
        if (!shown.isNull())
            shown.save(out.filePath(label + QStringLiteral(".jpg")), "JPEG", 92);
        else
            std::cerr << error.toStdString() << '\n';
    }
    report[QStringLiteral("peak_memory_gb")] = peakMemoryGb();
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    std::cout << bytes.constData();
    if (!evidence.isEmpty()) {
        QDir().mkpath(evidence);
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmss'Z'"));
        QFile file(QDir(evidence).filePath(QStringLiteral("b8-panorama-audit-v001-%1-%2.json").arg(label, stamp)));
        if (file.open(QIODevice::WriteOnly))
            file.write(bytes);
    }
    return job.success ? 0 : 1;
}
