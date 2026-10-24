#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include "Demosaic.h"
#include "RawBlackLevelEstimator.h"
#include "RawNormalization.h"
#include "CameraSignal.h"

#include <QFileInfo>
#include <QDir>
#include <QBuffer>
#include <QDebug>
#include <QElapsedTimer>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QFile>
#include <QCryptographicHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#ifdef IPR_WITH_LIBRAW
#include <libraw/libraw.h>
#endif

namespace RawDecoder {
namespace {

struct FormatHandler
{
    QStringList extensions;
    DecodeFunction decode;
    FormatOptions options;
};

struct Registry
{
    QMutex mutex;
    QList<FormatHandler> handlers;
};

Registry &registry()
{
    static Registry instance;
    return instance;
}

bool addFormatHandler(const QStringList &extensions, DecodeFunction decodeFunction,
                      QString *error, const FormatOptions &options)
{
    if (!decodeFunction || extensions.isEmpty()) {
        if (error)
            *error = QStringLiteral("Un décodeur RAW et au moins une extension sont requis.");
        return false;
    }

    QStringList normalized;
    QSet<QString> unique;
    for (const QString &extension : extensions) {
        const QString value = extension.trimmed().toLower();
        static const QRegularExpression suffix(QStringLiteral("^[a-z0-9]+$"));
        if (!suffix.match(value).hasMatch() ||
            unique.contains(value)) {
            if (error)
                *error = QStringLiteral("La liste d'extensions RAW est invalide.");
            return false;
        }
        unique.insert(value);
        normalized.append(value);
    }

    Registry &store = registry();
    QMutexLocker locker(&store.mutex);
    for (const FormatHandler &handler : store.handlers) {
        for (const QString &extension : normalized) {
            if (handler.extensions.contains(extension)) {
                if (error)
                    *error = QStringLiteral("Un décodeur existe déjà pour .%1.")
                                 .arg(extension);
                return false;
            }
        }
    }

    store.handlers.append({normalized, std::move(decodeFunction), options});
    if (error) error->clear();
    return true;
}

#ifdef IPR_WITH_LIBRAW
class RawDecodeTimer final
{
public:
    explicit RawDecodeTimer(const QString &fileName)
        : m_fileName(QFileInfo(fileName).fileName())
        , m_enabled(qEnvironmentVariableIsSet("IPR_LOG_RAW_TIMING"))
    {
        if (m_enabled)
            m_total.start();
    }

    void mark(const char *stage)
    {
        if (!m_enabled)
            return;
        const qint64 elapsed = m_total.elapsed();
        qInfo().noquote()
            << QStringLiteral("IPR RAW timing | file=%1 | stage=%2 | elapsed_ms=%3 | delta_ms=%4")
                   .arg(m_fileName, QString::fromLatin1(stage))
                   .arg(elapsed)
                   .arg(elapsed - m_lastElapsed);
        m_lastElapsed = elapsed;
    }

    ~RawDecodeTimer()
    {
        if (m_enabled)
            mark("total");
    }

private:
    QString m_fileName;
    bool m_enabled = false;
    QElapsedTimer m_total;
    qint64 m_lastElapsed = 0;
};

// LibRaw with IPR's demosaic in place of its interpolation step (LibRaw keeps
// its callbacks protected). Only Bayer mosaics reach the callback.
class RawProcessor : public LibRaw
{
public:
    void useIprDemosaic(int medianPasses, std::function<bool()> cancelled)
    {
        m_medianPasses = medianPasses;
        m_cancelled = std::move(cancelled);
        m_failed = false;
        imgdata.params.user_qual = 0;   // no LibRaw algorithm-specific step around the callback
        callbacks.interpolate_bayer_cb = &RawProcessor::interpolateBayer;
    }
    bool demosaicFailed() const { return m_failed; }

private:
    static void interpolateBayer(void *context)
    {
        auto *self = static_cast<RawProcessor *>(static_cast<LibRaw *>(context));
        auto &d = self->imgdata;
        Demosaic::Pattern pattern{};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const int colour = self->FC(y, x);
                pattern[size_t(y)][size_t(x)] = colour == 3 ? 1 : colour;   // second green folded
            }
        try {
            self->m_failed = !Demosaic::interpolate(d.image, d.sizes.iwidth, d.sizes.iheight, pattern,
                                                    self->m_medianPasses, self->m_cancelled);
        } catch (...) {
            // never let an exception cross LibRaw: the decode fails below instead
            self->m_failed = true;
        }
    }

    int m_medianPasses = 0;
    std::function<bool()> m_cancelled;
    bool m_failed = false;
};

// Opens a RAW by its wide path on Windows: the local 8-bit encoding cannot hold
// every file name (accented folders such as tests/image/colorimétrie).
int openRaw(LibRaw &raw, const QString &fileName)
{
#ifdef _WIN32
    const std::wstring path = QDir::toNativeSeparators(fileName).toStdWString();
    return raw.open_file(path.c_str());
#else
    return raw.open_file(QFile::encodeName(fileName).constData());
#endif
}

// Black level read by IPR on every RAW after unpacking: measured on the
// optical black pixels and compared with LibRaw's value, which is kept when it
// agrees (RawBlackLevelEstimator). Returns true when the measurement replaced it.
bool readBlackLevel(LibRaw &raw, const QString &fileName)
{
    auto &data = raw.imgdata;
    RawBlackLevelEstimator::Input input;
    input.mosaic = data.rawdata.raw_image;
    input.rawWidth = data.sizes.raw_width;
    input.rawHeight = data.sizes.raw_height;
    input.rowStrideSamples = int(data.sizes.raw_pitch / sizeof(std::uint16_t));
    const bool canon = QString::fromLocal8Bit(data.idata.make).contains(QStringLiteral("Canon"), Qt::CaseInsensitive);
    if (canon) {
        // Canon bounds are inclusive. Prefer the left masked area: the audited
        // high-ISO image has a slightly elevated upper border. Never infer these
        // rectangles from the smaller photographic crop.
        const auto addCanonArea = [&](const libraw_area_t &area) {
            if (area.l >= 0 && area.t >= 0 && area.r > area.l && area.b > area.t &&
                area.r < input.rawWidth && area.b < input.rawHeight) {
                input.maskedAreas.push_back({area.l, area.t, area.r - area.l + 1, area.b - area.t + 1});
                return true;
            }
            return false;
        };
        if (!addCanonArea(data.makernotes.canon.LeftOpticalBlack))
            addCanonArea(data.makernotes.canon.UpperOpticalBlack);
        input.marginsAreMasked = true;
    } else {
        // Masked areas declared by the format (top, left, bottom, right; exclusive).
        for (const auto &m : data.sizes.mask)
            if (m[2] > m[0] && m[3] > m[1])
                input.maskedAreas.push_back({m[1], m[0], m[3] - m[1], m[2] - m[0]});
    }
    input.leftMargin = data.sizes.left_margin;
    input.topMargin = data.sizes.top_margin;
    input.activeWidth = data.sizes.width;
    input.activeHeight = data.sizes.height;
    input.maximum = data.color.maximum;
    input.hasCfa = data.idata.filters != 0 && data.idata.colors >= 3;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            input.cfaChannels[row * 2 + column] = raw.COLOR(row, column);

    const auto measured = RawBlackLevelEstimator::measure(input);
    std::array<unsigned int, 4> decoder{};
    for (int channel = 0; channel < 4; ++channel)
        decoder[size_t(channel)] = data.color.black + data.color.cblack[channel];
    const bool pattern = data.color.cblack[4] != 0 || data.rawdata.color.cblack[4] != 0;
    const auto decision = RawBlackLevelEstimator::decide(decoder, pattern, measured, int(data.color.maximum));
    const QString name = QFileInfo(fileName).fileName();
    if (decision.source != RawBlackLevelEstimator::Source::Measured) {
        if (measured.wasMeasured())
            qInfo().noquote() << QStringLiteral("IPR black | %1 | LibRaw kept (%2 %3 %4 %5), measured %6 %7 %8 %9")
                .arg(name).arg(decoder[0]).arg(decoder[1]).arg(decoder[2]).arg(decoder[3])
                .arg(measured.levels[0]).arg(measured.levels[1]).arg(measured.levels[2]).arg(measured.levels[3]);
        return false;
    }
    for (int channel = 0; channel < 4; ++channel) {
        data.color.cblack[channel] = measured.levels[size_t(channel)];
        data.rawdata.color.cblack[channel] = measured.levels[size_t(channel)];
    }
    data.color.black = 0;
    data.rawdata.color.black = 0;
    qInfo().noquote() << QStringLiteral("IPR black | %1 | measured on %2 (%3 %4 %5 %6) replaces LibRaw (%7 %8 %9 %10)")
        .arg(name, input.maskedAreas.empty() ? QStringLiteral("the CFA margins") : QStringLiteral("the optical black"))
        .arg(measured.levels[0]).arg(measured.levels[1]).arg(measured.levels[2]).arg(measured.levels[3])
        .arg(decoder[0]).arg(decoder[1]).arg(decoder[2]).arg(decoder[3]);
    return true;
}

std::optional<double> estimateWhiteBalanceKelvin(const LibRaw &raw)
{
    const auto &color = raw.imgdata.color;

    // LibRaw's cam_xyz expresses how the camera's native channels respond
    // to a scene color given in XYZ (camRGB = cam_xyz * XYZ), not the
    // reverse. To recover the XYZ chromaticity of a camera-neutral RGB
    // triplet we must invert this 3x3 matrix (the 4th row/column, used
    // only by non-RGB sensors, is ignored like in the rest of this file).
    double matrix[3][3];
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            matrix[row][column] = color.cam_xyz[row][column];

    const double determinant =
        matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
        matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
        matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        return std::nullopt;

    double inverse[3][3];
    inverse[0][0] = (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) / determinant;
    inverse[0][1] = (matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2]) / determinant;
    inverse[0][2] = (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]) / determinant;
    inverse[1][0] = (matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]) / determinant;
    inverse[1][1] = (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]) / determinant;
    inverse[1][2] = (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]) / determinant;
    inverse[2][0] = (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]) / determinant;
    inverse[2][1] = (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]) / determinant;
    inverse[2][2] = (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]) / determinant;

    // cam_mul holds the as-shot white balance multipliers (what the camera
    // actually metered/dialed for this capture), while pre_mul is a fixed
    // per-model daylight (~6500K) calibration constant that never varies
    // from shot to shot. Using pre_mul here made every photo report
    // roughly the same wrong "source" temperature regardless of the real
    // shooting conditions; pre_mul is kept only as a defensive fallback
    // when cam_mul is unavailable.
    double neutralRgb[3] = {0.0, 0.0, 0.0};
    bool haveNeutral = true;
    for (int channel = 0; channel < 3; ++channel) {
        const double gain = color.cam_mul[channel];
        if (!std::isfinite(gain) || gain <= 0.0) {
            haveNeutral = false;
            break;
        }
        neutralRgb[channel] = 1.0 / gain;
    }
    if (!haveNeutral) {
        haveNeutral = true;
        for (int channel = 0; channel < 3; ++channel) {
            const double gain = color.pre_mul[channel];
            if (!std::isfinite(gain) || gain <= 0.0) {
                haveNeutral = false;
                break;
            }
            neutralRgb[channel] = 1.0 / gain;
        }
    }
    if (!haveNeutral)
        return std::nullopt;

    double xyz[3] = {0.0, 0.0, 0.0};
    for (int component = 0; component < 3; ++component)
        for (int channel = 0; channel < 3; ++channel)
            xyz[component] += inverse[component][channel] * neutralRgb[channel];

    const double sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || sum <= 0.0)
        return std::nullopt;

    const double x = xyz[0] / sum;
    const double y = xyz[1] / sum;
    const double denominator = 0.1858 - y;
    if (std::abs(denominator) < 1e-8)
        return std::nullopt;
    const double n = (x - 0.3320) / denominator;
    const double kelvin = 449.0 * n * n * n + 3525.0 * n * n +
                          6823.3 * n + 5520.33;
    if (!std::isfinite(kelvin) || kelvin < 2000.0 || kelvin > 50000.0)
        return std::nullopt;
    return kelvin;
}

ColorPipeline::WorkingImage decodeWithLibRaw(const QString &fileName,
                                             const Options &options,
                                             ImageMetadata *metadata,
                                             QString *error)
{
    RawDecodeTimer timer(fileName);
    RawProcessor raw;
    raw.set_progress_handler([](void *context, enum LibRaw_progress, int, int) {
        const auto *options = static_cast<const Options *>(context);
        return options->cancellationRequested && options->cancellationRequested() ? 1 : 0;
    }, const_cast<Options *>(&options));
    int result = openRaw(raw, fileName);
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }
    timer.mark("open_file");

    if (metadata) {
        metadata->decoderId = QStringLiteral("libraw");
        metadata->decoderVersion = QString::fromLatin1(LibRaw::version());
        metadata->cameraRaw = true;
        metadata->cameraMake = QString::fromLocal8Bit(raw.imgdata.idata.make).trimmed();
        metadata->cameraModel = QString::fromLocal8Bit(raw.imgdata.idata.model).trimmed();
        metadata->lensMake = QString::fromLocal8Bit(raw.imgdata.lens.LensMake).trimmed();
        metadata->lensModel = QString::fromLocal8Bit(raw.imgdata.lens.Lens).trimmed();
        metadata->iso = static_cast<int>(std::lround(raw.imgdata.other.iso_speed));
        metadata->focalLengthMm = raw.imgdata.other.focal_len;
        metadata->exposureSeconds = raw.imgdata.other.shutter;
        if (raw.imgdata.other.timestamp > 0)
            metadata->captureDate = QDateTime::fromSecsSinceEpoch(
                static_cast<qint64>(raw.imgdata.other.timestamp));
    }

    const ImageMetadata sourceMetadata = metadata ? *metadata : ImageMetadata{
        QFileInfo(fileName).suffix().toLower(),
        QString::fromLocal8Bit(raw.imgdata.idata.make).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.idata.model).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.lens.LensMake).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.lens.Lens).trimmed()
    };
    CameraProfileEngine::Selection profiles =
        CameraProfileEngine::findProfiles(
            sourceMetadata.cameraMake, sourceMetadata.cameraModel,
            sourceMetadata.lensMake, sourceMetadata.lensModel,
            options.profileDirectories);
    if (options.preferCameraNeutral) {
        const auto available = CameraProfileEngine::availableDcpProfiles(
            sourceMetadata.cameraMake, sourceMetadata.cameraModel, options.profileDirectories);
        for (const auto &path : available) {
            if (QFileInfo(path).completeBaseName().endsWith(" Camera Neutral", Qt::CaseInsensitive)) {
                profiles.dcpPath = path;
                break;
            }
        }
    }
    const QString defaultProfile = profiles.dcpPath;
    if (!options.automaticLcpSelection)
        profiles.lcpPath.clear();
    if (!options.lcpProfileOverride.isEmpty()) {
        if (CameraProfileEngine::lcpMatchesLens(options.lcpProfileOverride,
                sourceMetadata.cameraMake, sourceMetadata.cameraModel,
                sourceMetadata.lensMake, sourceMetadata.lensModel)) {
            profiles.lcpPath = options.lcpProfileOverride;
        } else {
            profiles.lcpPath.clear();
            if (metadata)
                metadata->developmentWarning += QStringLiteral(" Profil LCP enregistré introuvable ou incompatible ; corrections optiques non appliquées. ");
        }
    }
    const CameraProfileEngine::LcpRenderOptions opticalOptions{
        options.lensCorrectionEnabled, options.chromaticAberrationEnabled};
    CameraProfileEngine::LcpApplied opticalApplied;
    if (!options.dcpProfileOverride.isEmpty()) {
        if (CameraProfileEngine::dcpMatchesCamera(options.dcpProfileOverride,
                sourceMetadata.cameraMake, sourceMetadata.cameraModel)) {
            profiles.dcpPath = options.dcpProfileOverride;
        } else {
            // A session selection may belong to the previous camera. Never
            // apply it to a different camera, but keep automatic import usable.
            qWarning() << "Profil DCP incompatible : sélection automatique conservée.";
            if (metadata)
                metadata->developmentWarning = QStringLiteral("Profil demandé introuvable ou incompatible ; profil automatique utilisé.");
        }
    }
    if (metadata && options.listDcpProfiles)
        metadata->availableDcpProfiles = CameraProfileEngine::availableDcpProfiles(
            sourceMetadata.cameraMake, sourceMetadata.cameraModel, options.profileDirectories);
    timer.mark("find_profiles");
    if (options.cancellationRequested && options.cancellationRequested()) {
        if (error) *error = QStringLiteral("Chargement annulé.");
        return {};
    }
    CameraProfileEngine::CaptureSettings capture;
    capture.focalLengthMm = raw.imgdata.other.focal_len;
    capture.aperture = raw.imgdata.other.aperture;
    if (metadata) {
        metadata->dcpProfile = profiles.dcpPath.isEmpty()
                                   ? QString()
                                   : QFileInfo(profiles.dcpPath).completeBaseName();
        metadata->lcpProfile = profiles.lcpPath.isEmpty()
                                   ? QString()
                                   : QFileInfo(profiles.lcpPath).completeBaseName();
        metadata->lcpPath = profiles.lcpPath;
        QFile lcpFile(profiles.lcpPath);
        if (lcpFile.open(QIODevice::ReadOnly))
            metadata->lcpSha256 = QString::fromLatin1(QCryptographicHash::hash(lcpFile.readAll(), QCryptographicHash::Sha256).toHex());
    }

    raw.imgdata.params.use_camera_wb = options.automaticWhiteBalance ? 0 : 1;
    raw.imgdata.params.use_auto_wb = options.automaticWhiteBalance ? 1 : 0;
    raw.imgdata.params.use_camera_matrix = profiles.dcpPath.isEmpty() ? 1 : 0;
    raw.imgdata.params.no_auto_bright = 1;
    raw.imgdata.params.output_bps = 16;
    raw.imgdata.params.output_color = profiles.dcpPath.isEmpty() ? 1 : 0;
    raw.imgdata.params.gamm[0] = 1.0;
    raw.imgdata.params.gamm[1] = 1.0;

    std::optional<double> sourceWhiteBalanceKelvin;
    CameraProfileEngine::DcpRenderOptions dcpOptions;
    if (metadata) {
        metadata->rawNormalizationApplied = false;
        metadata->normalizationWhiteLevel = 0;
        metadata->normalizationBaselineExposureEv = 0.0;
        metadata->profileExposureCompensationEv = 0.0;
        metadata->profileRenderingProvenance.clear();
        metadata->normalizationProvenance.clear();
    }
    if ((result = raw.unpack()) == LIBRAW_SUCCESS) {
        timer.mark("unpack");
        const bool estimatedOpticalBlackLevel = readBlackLevel(raw, fileName);
        sourceWhiteBalanceKelvin = estimateWhiteBalanceKelvin(raw);
        if (metadata)
            metadata->opticalBlackLevelEstimated = estimatedOpticalBlackLevel;
        const QString manifest = options.normalizationManifest.isEmpty() && options.useNormalizationEnvironment
            ? qEnvironmentVariable("IPR_RAW_NORMALIZATION_MANIFEST")
            : options.normalizationManifest;
        RawNormalization::Context context;
        context.fileName = fileName;
        context.profilePath = profiles.dcpPath;
        context.cameraMake = QString::fromLocal8Bit(raw.imgdata.idata.make).trimmed();
        context.cameraModel = QString::fromLocal8Bit(raw.imgdata.idata.model).trimmed();
        context.decoderId = QStringLiteral("libraw");
        context.decoderVersion = QString::fromLatin1(LibRaw::version());
        context.iso = int(std::lround(raw.imgdata.other.iso_speed));
        context.cameraWhiteBalance = !options.automaticWhiteBalance;
        context.nativeBaselineUnknown =
            raw.imgdata.color.dng_levels.baseline_exposure == -999;
        for (int channel = 0; channel < 4; ++channel)
            context.black[channel] = raw.imgdata.color.cblack[channel];
        context.globalBlack = raw.imgdata.color.black;
        context.nativeWhiteLevel = raw.imgdata.color.maximum;
        if (context.cameraMake.compare(QStringLiteral("Canon"), Qt::CaseInsensitive) == 0 &&
            raw.imgdata.makernotes.canon.SpecularWhiteLevel > 0)
            context.specularWhiteLevel = unsigned(raw.imgdata.makernotes.canon.SpecularWhiteLevel);
        // Common and per-channel black are audited independently; spatial patterns
        // remain unsupported. Never replace the measured black by a DNG average.
        if (raw.imgdata.color.cblack[4])
            context.nativeBaselineUnknown = false;
        QString normalizationReason;
        const auto calibration = RawNormalization::resolve(manifest, context, &normalizationReason);
        if (metadata)
            metadata->normalizationReason = normalizationReason.isEmpty()
                ? QStringLiteral("Aucune règle compatible avec ce boîtier, profil et mode capteur.")
                : normalizationReason;
        if (calibration) {
            raw.imgdata.color.maximum = raw.imgdata.rawdata.color.maximum = calibration->whiteLevel;
            raw.imgdata.params.adjust_maximum_thr = 0.0f;
            dcpOptions.cameraBaselineExposureEv = calibration->baselineExposureEv;
            dcpOptions.profileExposureCompensationEv = calibration->profileExposureCompensationEv;
            dcpOptions.preserveToneCurveHue = true;
            if (metadata) {
                metadata->rawNormalizationApplied = true;
                metadata->normalizationWhiteLevel = calibration->whiteLevel;
                metadata->normalizationBaselineExposureEv = calibration->baselineExposureEv;
                metadata->profileExposureCompensationEv = calibration->profileExposureCompensationEv;
                metadata->profileRenderingProvenance = calibration->profileRenderingProvenance;
                metadata->normalizationProvenance = calibration->provenance;
            }
            qInfo().noquote() << "IPR RAW normalization |" << QFileInfo(fileName).fileName()
                             << "| white=" << calibration->whiteLevel
                             << "| baseline_ev=" << calibration->baselineExposureEv
                             << "| profile_compensation_ev=" << calibration->profileExposureCompensationEv
                             << "| source=" << calibration->provenance;
        } else if (!normalizationReason.isEmpty()) {
            qWarning().noquote() << normalizationReason;
        }
        timer.mark("black_wb_calibration");
        raw.useIprDemosaic(Demosaic::chromaMedianPasses(int(std::lround(raw.imgdata.other.iso_speed))),
                           options.cancellationRequested);
        result = raw.dcraw_process();
    }
    timer.mark("unpack_and_process");
    if (result == LIBRAW_SUCCESS && raw.demosaicFailed())
        result = LIBRAW_CANCELLED_BY_CALLBACK;
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }

    libraw_processed_image_t *processed = raw.dcraw_make_mem_image(&result);
    timer.mark("make_memory_image");
    if (!processed || result != LIBRAW_SUCCESS ||
        processed->type != LIBRAW_IMAGE_BITMAP || processed->bits != 16 ||
        processed->colors != 3) {
        if (error) {
            *error = result == LIBRAW_SUCCESS
                         ? QStringLiteral("LibRaw n'a pas fourni une image RGB 16 bits.")
                         : QString::fromLatin1(libraw_strerror(result));
        }
        if (processed)
            LibRaw::dcraw_clear_mem(processed);
        return {};
    }

    ColorPipeline::WorkingImage image;
    if (profiles.dcpPath.isEmpty()) {
        if (profiles.lcpPath.isEmpty()) {
            image = ColorPipeline::fromLinearRgb16(
                reinterpret_cast<const quint16 *>(processed->data), processed->width,
                processed->height, error);
        } else {
            // Lateral CA refers to the decoder's red/blue planes. Apply it
            // before changing primaries, then enter the master exactly once.
            cv::Mat rgb16(int(processed->height), int(processed->width), CV_16UC3, processed->data);
            cv::Mat rgb;
            rgb16.convertTo(rgb, CV_32FC3, 1.0 / 65535.0);
            if (CameraProfileEngine::applyLcp(profiles.lcpPath, rgb, capture,
                                               error, opticalOptions, &opticalApplied)) {
                WorkingColor::transform(rgb, WorkingColor::fromSrgb());
                std::vector<cv::Mat> correctedChannels;
                cv::split(rgb, correctedChannels);
                correctedChannels.push_back(cv::Mat(rgb.size(), CV_32FC1, cv::Scalar(1.f)));
                cv::merge(correctedChannels, image.linearRgba);
            }
        }
    } else {
        cv::Mat rgb16(int(processed->height), int(processed->width), CV_16UC3,
                      processed->data);
        cv::Mat cameraRgb;
        rgb16.convertTo(cameraRgb, CV_32FC3, 1.0 / 65535.0);
        if (!profiles.lcpPath.isEmpty() &&
            !CameraProfileEngine::applyLcp(profiles.lcpPath, cameraRgb, capture,
                                            error, opticalOptions, &opticalApplied))
            cameraRgb.release();
        if (metadata) {
            metadata->lensGeometryApplied = opticalApplied.geometry;
            metadata->lensVignettingApplied = opticalApplied.vignetting;
            metadata->chromaticAberrationApplied = opticalApplied.chromaticAberration;
        }
        if (!cameraRgb.empty()) {
            CameraSignal::Source signal;
            signal.rgb = std::move(cameraRgb);
            signal.cameraMake = sourceMetadata.cameraMake;
            signal.cameraModel = sourceMetadata.cameraModel;
            signal.whiteBalanceKelvin = sourceWhiteBalanceKelvin;
            signal.whiteBalanceApplied = true;
            // scale_colors updates pre_mul to the multipliers actually used,
            // including auto WB. Ratios remove the common scaling factor.
            const auto &color = raw.imgdata.color;
            for (int c = 0; c < 3; ++c)
                if (color.pre_mul[c] > 0 && std::isfinite(color.pre_mul[c]))
                    signal.cameraNeutral[c] = color.pre_mul[1] / color.pre_mul[c];
            image = CameraSignal::render(std::move(signal), profiles.dcpPath, dcpOptions, error);
        }
        // Calibration changes the LibRaw white level upstream: never reuse this
        // buffer for a profile-independent fast path when a manifest is active.
        constexpr size_t retainedBufferLimit = 384u * 1024u * 1024u;
        if (!image.isNull() && image.dcpContext->cameraRgb.total() * image.dcpContext->cameraRgb.elemSize() <= retainedBufferLimit && options.retainedDcpSource &&
            options.normalizationManifest.isEmpty() && !options.useNormalizationEnvironment) {
            options.retainedDcpSource->cameraRgb = image.dcpContext->cameraRgb;
            options.retainedDcpSource->workingContext = image.dcpContext;
            options.retainedDcpSource->whiteBalanceKelvin = sourceWhiteBalanceKelvin;
            options.retainedDcpSource->defaultProfile = defaultProfile;
            options.retainedDcpSource->automaticWhiteBalance = options.automaticWhiteBalance;
            options.retainedDcpSource->lensCorrectionEnabled = options.lensCorrectionEnabled;
            options.retainedDcpSource->chromaticAberrationEnabled = options.chromaticAberrationEnabled;
            if (metadata)
                options.retainedDcpSource->metadata = *metadata;
        }
    }
    if (metadata) {
        metadata->lensGeometryApplied = opticalApplied.geometry;
        metadata->lensVignettingApplied = opticalApplied.vignetting;
        metadata->chromaticAberrationApplied = opticalApplied.chromaticAberration;
    }
    if (!image.isNull()) {
        image.sourceProfile = profiles.dcpPath.isEmpty()
                                  ? QStringLiteral("Matrice couleur LibRaw → Rec.2020 linéaire")
                                  : QStringLiteral("DCP %1 → Rec.2020 linéaire")
                                        .arg(QFileInfo(profiles.dcpPath).completeBaseName());
        image.referenceWhiteBalanceKelvin = sourceWhiteBalanceKelvin;
    }
    timer.mark("color_and_lens_profiles");
    LibRaw::dcraw_clear_mem(processed);
    return image;
}

ColorPipeline::WorkingImage decodeCr2(const QString &fileName,
                                      const Options &options,
                                      ImageMetadata *metadata, QString *error)
{
    return decodeWithLibRaw(fileName, options, metadata, error);
}

ColorPipeline::WorkingImage decodeCr3(const QString &fileName,
                                      const Options &options,
                                      ImageMetadata *metadata, QString *error)
{
    return decodeWithLibRaw(fileName, options, metadata, error);
}

QImage decodeEmbeddedThumbnailWithLibRaw(const QString &fileName,
                                         const QSize &maximumSize,
                                         QString *error)
{
    // Heap: a LibRaw instance is too large for 1 MiB worker-thread stacks.
    const auto owned = std::make_unique<LibRaw>();
    LibRaw &raw = *owned;
    int result = openRaw(raw, fileName);
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }

    // Decode the smallest embedded preview that is large enough, not the
    // full-size JPEG: 12.6 ms instead of 204 ms on the 19-CR3 corpus
    // (apercus-integres-v001). Never below 1024 px: Canon's 160x120 THMB
    // can be letterboxed. Half the requested size suffices for an interim view.
    const int requested = maximumSize.isValid() ? std::max(maximumSize.width(), maximumSize.height()) : 0;
    const int required = requested ? std::max(1024, requested / 2) : std::numeric_limits<int>::max();
    const auto &thumbs = raw.imgdata.thumbs_list;
    int chosen = -1, chosenArea = 0;
    for (int i = 0; i < thumbs.thumbcount && i < LIBRAW_THUMBNAIL_MAXCOUNT; ++i) {
        const auto &item = thumbs.thumblist[i];
        const int area = int(item.twidth) * int(item.theight);
        if (std::max<int>(item.twidth, item.theight) >= required && (chosen < 0 || area < chosenArea)) {
            chosen = i;
            chosenArea = area;
        }
    }
    result = chosen >= 0 ? raw.unpack_thumb_ex(chosen) : raw.unpack_thumb();
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }

    libraw_processed_image_t *processed = raw.dcraw_make_mem_thumb(&result);
    if (!processed || result != LIBRAW_SUCCESS) {
        if (error) {
            *error = result == LIBRAW_SUCCESS
                         ? QStringLiteral("Aucune vignette embarquée n'est disponible.")
                         : QString::fromLatin1(libraw_strerror(result));
        }
        if (processed)
            LibRaw::dcraw_clear_mem(processed);
        return {};
    }

    QImage thumbnail;
    if (processed->type == LIBRAW_IMAGE_JPEG) {
        const QByteArray jpegData(
            reinterpret_cast<const char *>(processed->data),
            static_cast<int>(processed->data_size));
        QBuffer buffer;
        buffer.setData(jpegData);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        thumbnail = reader.read();
    } else if (processed->type == LIBRAW_IMAGE_BITMAP && processed->bits == 8) {
        QImage::Format format = QImage::Format_Invalid;
        if (processed->colors == 3)
            format = QImage::Format_RGB888;
        else if (processed->colors == 4)
            format = QImage::Format_RGBA8888;
        if (format != QImage::Format_Invalid) {
            const int bytesPerLine = static_cast<int>(processed->width) *
                                     static_cast<int>(processed->colors);
            thumbnail = QImage(processed->data, static_cast<int>(processed->width),
                               static_cast<int>(processed->height), bytesPerLine,
                               format).copy();
        }
    }
    LibRaw::dcraw_clear_mem(processed);

    if (thumbnail.isNull()) {
        if (error)
            *error = QStringLiteral("LibRaw a fourni une vignette non prise en charge.");
        return {};
    }
    if (maximumSize.isValid())
        thumbnail = thumbnail.scaled(maximumSize, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    return thumbnail;
}
#endif

void registerBuiltInHandlers()
{
#ifdef IPR_WITH_LIBRAW
    QString error;
    const FormatOptions format{decodeEmbeddedThumbnailWithLibRaw,
        QStringLiteral("libraw"), QString::fromLatin1(LibRaw::version()), [](const QString &path) {
            const auto raw = std::make_unique<LibRaw>();   // heap: too large for worker stacks
            if (openRaw(*raw, path) != LIBRAW_SUCCESS) return QSize{};
            return QSize(raw->imgdata.sizes.raw_width, raw->imgdata.sizes.raw_height);
        }};
    if (!addFormatHandler({QStringLiteral("cr2")}, decodeCr2, &error, format) ||
        !addFormatHandler({QStringLiteral("cr3")}, decodeCr3, &error, format) ||
        !addFormatHandler({QStringLiteral("dng")}, decodeWithLibRaw, &error, format))
        qFatal("Failed to register built-in LibRaw handlers: %s",
               qPrintable(error));
#endif
}

void ensureBuiltInHandlers()
{
    static const bool initialized = [] {
        registerBuiltInHandlers();
        return true;
    }();
    Q_UNUSED(initialized)
}

}

ColorPipeline::WorkingImage applyProfile(const DcpSource &source, const QString &profile,
                                        ImageMetadata *metadata, QString *error)
{
    // Calibrated camera buffers are bound to the selected profile/white level.
    // Profile changes must resolve calibration/render parameters again by decoding.
    if (source.metadata.rawNormalizationApplied) {
        if (error) *error = QStringLiteral("Un changement de profil calibré nécessite un nouveau décodage.");
        return {};
    }
    const QString path = profile.isEmpty() ? source.defaultProfile : profile;
    if (source.cameraRgb.empty() || !CameraProfileEngine::dcpMatchesCamera(
            path, source.metadata.cameraMake, source.metadata.cameraModel)) {
        if (error) *error = QStringLiteral("Profil DCP absent ou incompatible avec le boîtier.");
        return {};
    }
    CameraSignal::Source signal;
    signal.rgb = source.cameraRgb; // Already immutable, shared with the active source.
    signal.cameraMake = source.metadata.cameraMake;
    signal.cameraModel = source.metadata.cameraModel;
    signal.whiteBalanceKelvin = source.whiteBalanceKelvin;
    signal.whiteBalanceApplied = true;
    if (source.workingContext) signal.cameraNeutral = source.workingContext->cameraNeutral;
    auto image = CameraSignal::render(std::move(signal), path,
        source.workingContext ? source.workingContext->options : CameraProfileEngine::DcpRenderOptions{}, error);
    if (image.isNull()) return {};
    // A source without applied-WB context cannot promise pre-DCP WB editing.
    if (!source.workingContext) image.dcpContext.reset();
    if (metadata) {
        *metadata = source.metadata;
        metadata->dcpProfile = QFileInfo(path).completeBaseName();
        metadata->developmentWarning.clear();
    }
    return image;
}

bool registerFormatHandler(const QStringList &extensions,
                           DecodeFunction decodeFunction, QString *error,
                           const FormatOptions &format)
{
    ensureBuiltInHandlers();
    return addFormatHandler(extensions, std::move(decodeFunction), error, format);
}

QStringList supportedExtensions()
{
    ensureBuiltInHandlers();
    Registry &store = registry();
    QMutexLocker locker(&store.mutex);
    QStringList extensions;
    for (const FormatHandler &handler : store.handlers)
        extensions.append(handler.extensions);
    extensions.removeDuplicates();
    extensions.sort();
    return extensions;
}

QString backendVersion()
{
#ifdef IPR_WITH_LIBRAW
    return QString::fromLatin1(LibRaw::version());
#else
    return QStringLiteral("no-libraw");
#endif
}

QSize sourceDimensions(const QString &fileName)
{
    ensureBuiltInHandlers();
    std::function<QSize(const QString &)> dimensions;
    {
        auto &store = registry();
        QMutexLocker lock(&store.mutex);
        for (const auto &handler : store.handlers) {
            if (handler.extensions.contains(QFileInfo(fileName).suffix().toLower())) {
                dimensions = handler.options.dimensions; break;
            }
        }
    }
    return dimensions ? dimensions(fileName) : QSize{};
}

ColorPipeline::WorkingImage decode(const QString &fileName,
                                   const Options &options,
                                   ImageMetadata *metadata, QString *error)
{
    ensureBuiltInHandlers();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (metadata)
        *metadata = ImageMetadata{extension};

    DecodeFunction decoder;
    FormatOptions format;
    {
        Registry &store = registry();
        QMutexLocker locker(&store.mutex);
        for (const FormatHandler &handler : store.handlers) {
            if (handler.extensions.contains(extension)) {
                decoder = handler.decode;
                format = handler.options;
                break;
            }
        }
    }

    if (!decoder) {
        if (error)
            *error = QStringLiteral("Aucun décodeur n'est enregistré pour .%1.")
                         .arg(extension);
        return {};
    }
    auto result = decoder(fileName, options, metadata, error);
    if (metadata) {
        metadata->decoderId = format.decoderId;
        metadata->decoderVersion = format.decoderVersion;
        metadata->cameraRaw = true;
    }
    if (!result.isNull() && (result.colorSpace != ColorPipeline::workingColorSpace() ||
        result.linearRgba.type() != CV_32FC4 || !cv::checkRange(result.linearRgba, true))) {
        if (error) *error = QStringLiteral("Le décodeur n'a pas fourni un master Rec.2020 linéaire RGBA fini.");
        return {};
    }
    if (!result.isNull() && metadata && options.retainedDcpSource &&
        !options.retainedDcpSource->cameraRgb.empty())
        options.retainedDcpSource->metadata = *metadata;
    return result;
}

QImage decodeEmbeddedThumbnail(const QString &fileName,
                               const QSize &maximumSize, QString *error)
{
    ensureBuiltInHandlers();
    if (error) error->clear();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    ThumbnailFunction thumbnail;
    {
        Registry &store = registry();
        QMutexLocker locker(&store.mutex);
        for (const FormatHandler &handler : store.handlers) {
            if (handler.extensions.contains(extension)) {
                thumbnail = handler.options.thumbnail;
                break;
            }
        }
    }
    if (!thumbnail) {
        if (error) *error = QStringLiteral("Aucun lecteur de vignette enregistré pour .%1.").arg(extension);
        return {};
    }
    // Execute outside the registry lock: a callback may query supported formats.
    auto result = thumbnail(fileName, maximumSize, error);
    if (!result.isNull() && maximumSize.isValid() &&
        (result.width() > maximumSize.width() || result.height() > maximumSize.height()))
        result = result.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

}
