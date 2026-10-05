#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include "RawBlackLevelEstimator.h"

#include <QFileInfo>
#include <QBuffer>
#include <QDebug>
#include <QElapsedTimer>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <cmath>
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
                      QString *error)
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
        if (value.isEmpty() || value.contains(QLatin1Char('.')) ||
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

    store.handlers.append({normalized, std::move(decodeFunction)});
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

bool estimateMissingCanonCr3BlackLevel(LibRaw &raw, const QString &fileName)
{
    auto &data = raw.imgdata;
    RawBlackLevelEstimator::Input input;
    input.extension = QFileInfo(fileName).suffix();
    input.cameraMake = QString::fromLocal8Bit(data.idata.make);
    input.mosaic = data.rawdata.raw_image;
    input.rawWidth = data.sizes.raw_width;
    input.rawHeight = data.sizes.raw_height;
    input.leftMargin = data.sizes.left_margin;
    input.topMargin = data.sizes.top_margin;
    input.activeWidth = data.sizes.width;
    input.activeHeight = data.sizes.height;
    input.maximum = data.color.maximum;
    input.hasCfa = data.idata.filters != 0 && data.idata.colors >= 3;
    input.globalBlack = data.color.black;
    input.rawGlobalBlack = data.rawdata.color.black;
    input.patternBlack = data.color.cblack[4];
    input.rawPatternBlack = data.rawdata.color.cblack[4];
    for (int channel = 0; channel < 4; ++channel) {
        input.channelBlack[channel] = data.color.cblack[channel];
        input.rawChannelBlack[channel] = data.rawdata.color.cblack[channel];
    }
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column)
            input.cfaChannels[row * 2 + column] = raw.COLOR(row, column);
    }

    const RawBlackLevelEstimator::Result estimate =
        RawBlackLevelEstimator::estimate(input);
    if (!estimate.wasEstimated())
        return false;
    for (int channel = 0; channel < 4; ++channel) {
        data.color.cblack[channel] = estimate.levels[channel];
        data.rawdata.color.cblack[channel] = estimate.levels[channel];
    }
    data.color.black = 0;
    data.rawdata.color.black = 0;
    data.color.cblack[4] = 0;
    data.rawdata.color.cblack[4] = 0;
    qInfo().noquote()
        << QStringLiteral("CR3 Canon : niveau noir optique estimé depuis les marges CFA "
                          "(R=%1, G1=%2, B=%3, G2=%4).")
               .arg(estimate.levels[0])
               .arg(estimate.levels[1])
               .arg(estimate.levels[2])
               .arg(estimate.levels[3]);
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
                                             SourceMetadata *metadata,
                                             QString *error)
{
    RawDecodeTimer timer(fileName);
    LibRaw raw;
    int result = raw.open_file(fileName.toLocal8Bit().constData());
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }
    timer.mark("open_file");

    if (metadata) {
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

    const SourceMetadata sourceMetadata = metadata ? *metadata : SourceMetadata{
        QFileInfo(fileName).suffix().toLower(),
        QString::fromLocal8Bit(raw.imgdata.idata.make).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.idata.model).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.lens.LensMake).trimmed(),
        QString::fromLocal8Bit(raw.imgdata.lens.Lens).trimmed()
    };
    const CameraProfileEngine::Selection profiles =
        CameraProfileEngine::findProfiles(
            sourceMetadata.cameraMake, sourceMetadata.cameraModel,
            sourceMetadata.lensMake, sourceMetadata.lensModel,
            options.profileDirectories);
    timer.mark("find_profiles");
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
    if ((result = raw.unpack()) == LIBRAW_SUCCESS) {
        const bool estimatedOpticalBlackLevel =
            estimateMissingCanonCr3BlackLevel(raw, fileName);
        sourceWhiteBalanceKelvin = estimateWhiteBalanceKelvin(raw);
        if (metadata)
            metadata->opticalBlackLevelEstimated = estimatedOpticalBlackLevel;
        result = raw.dcraw_process();
    }
    timer.mark("unpack_and_process");
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
        image = ColorPipeline::fromLinearRgb16(
            reinterpret_cast<const quint16 *>(processed->data), processed->width,
            processed->height, error);
        if (!image.isNull() && !profiles.lcpPath.isEmpty()) {
            std::vector<cv::Mat> channels;
            cv::split(image.linearRgba, channels);
            cv::Mat rgb;
            const std::vector<cv::Mat> colorChannels{
                channels[0], channels[1], channels[2]};
            cv::merge(colorChannels, rgb);
            if (!CameraProfileEngine::applyLcp(profiles.lcpPath, rgb, capture,
                                               error)) {
                image = {};
            } else {
                std::vector<cv::Mat> correctedChannels;
                cv::split(rgb, correctedChannels);
                correctedChannels.push_back(channels[3]);
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
                                            error))
            cameraRgb.release();
        if (!cameraRgb.empty() &&
            !CameraProfileEngine::applyDcp(
                profiles.dcpPath, cameraRgb,
                sourceWhiteBalanceKelvin.value_or(5500.0),
                                            error))
            cameraRgb.release();
        if (!cameraRgb.empty()) {
            std::vector<cv::Mat> channels;
            cv::split(cameraRgb, channels);
            channels.push_back(cv::Mat(cameraRgb.rows, cameraRgb.cols,
                                       CV_32FC1, cv::Scalar(1.0f)));
            cv::merge(channels, image.linearRgba);
            image.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
        }
    }
    if (!image.isNull()) {
        image.sourceProfile = profiles.dcpPath.isEmpty()
                                  ? QStringLiteral("Matrice couleur LibRaw → sRGB linéaire")
                                  : QStringLiteral("DCP %1 → sRGB linéaire")
                                        .arg(QFileInfo(profiles.dcpPath).completeBaseName());
        image.referenceWhiteBalanceKelvin = sourceWhiteBalanceKelvin;
    }
    timer.mark("color_and_lens_profiles");
    LibRaw::dcraw_clear_mem(processed);
    return image;
}

ColorPipeline::WorkingImage decodeCr2(const QString &fileName,
                                      const Options &options,
                                      SourceMetadata *metadata, QString *error)
{
    return decodeWithLibRaw(fileName, options, metadata, error);
}

ColorPipeline::WorkingImage decodeCr3(const QString &fileName,
                                      const Options &options,
                                      SourceMetadata *metadata, QString *error)
{
    return decodeWithLibRaw(fileName, options, metadata, error);
}

QImage decodeEmbeddedThumbnailWithLibRaw(const QString &fileName,
                                         const QSize &maximumSize,
                                         QString *error)
{
    LibRaw raw;
    int result = raw.open_file(fileName.toLocal8Bit().constData());
    if (result != LIBRAW_SUCCESS) {
        if (error)
            *error = QString::fromLatin1(libraw_strerror(result));
        return {};
    }

    result = raw.unpack_thumb();
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
    if (!addFormatHandler({QStringLiteral("cr2")}, decodeCr2, &error) ||
        !addFormatHandler({QStringLiteral("cr3")}, decodeCr3, &error) ||
        !addFormatHandler({QStringLiteral("dng")}, decodeWithLibRaw, &error))
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

bool registerFormatHandler(const QStringList &extensions,
                           DecodeFunction decodeFunction, QString *error)
{
    ensureBuiltInHandlers();
    return addFormatHandler(extensions, std::move(decodeFunction), error);
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

ColorPipeline::WorkingImage decode(const QString &fileName,
                                   const Options &options,
                                   SourceMetadata *metadata, QString *error)
{
    ensureBuiltInHandlers();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (metadata)
        *metadata = SourceMetadata{extension};

    DecodeFunction decoder;
    {
        Registry &store = registry();
        QMutexLocker locker(&store.mutex);
        for (const FormatHandler &handler : store.handlers) {
            if (handler.extensions.contains(extension)) {
                decoder = handler.decode;
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
    return decoder(fileName, options, metadata, error);
}

QImage decodeEmbeddedThumbnail(const QString &fileName,
                               const QSize &maximumSize, QString *error)
{
#ifdef IPR_WITH_LIBRAW
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (!supportedExtensions().contains(extension)) {
        if (error)
            *error = QStringLiteral("Aucun décodeur RAW n'est enregistré pour .%1.")
                         .arg(extension);
        return {};
    }
    return decodeEmbeddedThumbnailWithLibRaw(fileName, maximumSize, error);
#else
    Q_UNUSED(fileName)
    Q_UNUSED(maximumSize)
    if (error)
        *error = QStringLiteral("LibRaw n'est pas disponible pour lire les vignettes RAW.");
    return {};
#endif
}

}
