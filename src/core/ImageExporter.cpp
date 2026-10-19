#include "ImageExporter.h"

#include "WorkingColorSpace.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>

#include <opencv2/core.hpp>
#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ImageExporter {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("ImageExporter", text); }

constexpr uint16_t LinearRawPhotometricInterpretation = 34892;

TIFF *openTiffForWrite(const QString &fileName)
{
#ifdef Q_OS_WIN
    return TIFFOpenW(reinterpret_cast<const wchar_t *>(fileName.utf16()), "w");
#else
    return TIFFOpen(fileName.toUtf8().constData(), "w");
#endif
}

bool writeLinearDng(const ColorPipeline::WorkingImage &image,
                    const QString &fileName, QString *error)
{
    const ColorPipeline::WorkingImage gamutMapped =
        ColorPipeline::mapToSrgbGamut(image, error);
    if (gamutMapped.isNull())
        return false;

    TIFF *tiff = openTiffForWrite(fileName);
    if (!tiff) {
        if (error)
            *error = QStringLiteral("Impossible de créer le fichier DNG.");
        return false;
    }

    constexpr uint8_t dngVersion[] = {1, 4, 0, 0};
    constexpr uint8_t dngBackwardVersion[] = {1, 1, 0, 0};
    constexpr float blackLevel[] = {0.0f, 0.0f, 0.0f};
    constexpr uint32_t whiteLevel[] = {65535, 65535, 65535};
    constexpr float asShotNeutral[] = {1.0f, 1.0f, 1.0f};
    // DNG ColorMatrix maps XYZ at CalibrationIlluminant1 (D65) to the
    // stored linear RGB, not RGB to XYZ. ICC must describe linear samples.
    const cv::Matx33f xyzToStoredRgb(WorkingColor::srgbToXyz().inv());
    const QByteArray uniqueCameraModel =
        QByteArrayLiteral("IPR Rendered Linear DNG");
    const QByteArray software = QByteArrayLiteral("IPR");
    const QByteArray iccProfile =
        QColorSpace(QColorSpace::SRgbLinear).iccProfile();

    bool configured =
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH,
                     static_cast<uint32_t>(gamutMapped.linearRgba.cols)) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH,
                     static_cast<uint32_t>(gamutMapped.linearRgba.rows)) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC,
                     LinearRawPhotometricInterpretation) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MAKE, "IPR") == 1 &&
        TIFFSetField(tiff, TIFFTAG_MODEL, "Rendered Linear DNG") == 1 &&
        TIFFSetField(tiff, TIFFTAG_SOFTWARE, software.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGVERSION, dngVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGBACKWARDVERSION,
                     dngBackwardVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_UNIQUECAMERAMODEL,
                     uniqueCameraModel.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BLACKLEVEL, 3, blackLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_WHITELEVEL, 3, whiteLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COLORMATRIX1, 9, xyzToStoredRgb.val) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ASSHOTNEUTRAL, 3, asShotNeutral) == 1 &&
        TIFFSetField(tiff, TIFFTAG_CALIBRATIONILLUMINANT1, 21) == 1;
    if (!iccProfile.isEmpty()) {
        configured = configured &&
            TIFFSetField(tiff, TIFFTAG_ICCPROFILE,
                         static_cast<uint32_t>(iccProfile.size()),
                         const_cast<char *>(iccProfile.constData())) == 1;
    }
    if (!configured) {
        TIFFClose(tiff);
        QFile::remove(fileName);
        if (error)
            *error = QStringLiteral("Impossible d'écrire les métadonnées DNG.");
        return false;
    }

    std::vector<quint16> row(
        static_cast<size_t>(gamutMapped.linearRgba.cols) * 3);
    for (int y = 0; y < gamutMapped.linearRgba.rows; ++y) {
        const cv::Vec4f *source = gamutMapped.linearRgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < gamutMapped.linearRgba.cols; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                row[static_cast<size_t>(x) * 3 + channel] =
                    static_cast<quint16>(std::lround(
                        std::clamp(source[x][channel], 0.0f, 1.0f) * 65535.0f));
            }
        }
        if (TIFFWriteScanline(tiff, row.data(), static_cast<uint32_t>(y), 0) < 0) {
            TIFFClose(tiff);
            QFile::remove(fileName);
            if (error)
                *error = QStringLiteral("Impossible d'écrire les pixels DNG.");
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
}
}

QString normalizedFormat(QString format)
{
    format = format.trimmed().toUpper();
    return format == QStringLiteral("JPG") ? QStringLiteral("JPEG") : format;
}

QString suffix(const QString &normalizedFormat)
{
    if (normalizedFormat == QStringLiteral("JPEG"))
        return QStringLiteral("jpg");
    if (normalizedFormat == QStringLiteral("PNG"))
        return QStringLiteral("png");
    if (normalizedFormat == QStringLiteral("TIFF"))
        return QStringLiteral("tiff");
    if (normalizedFormat == QStringLiteral("DNG"))
        return QStringLiteral("dng");
    return {};
}

QString localPath(const QUrl &target)
{
    if (target.isLocalFile())
        return target.toLocalFile();

    const QString value = target.toString(QUrl::FullyDecoded);
    const bool nativeWindowsPath =
        value.size() >= 3 && value.at(0).isLetter() &&
        value.at(1) == QLatin1Char(':') &&
        (value.at(2) == QLatin1Char('/') || value.at(2) == QLatin1Char('\\'));
    if ((target.scheme().isEmpty() && QDir::isAbsolutePath(value)) ||
        (target.scheme().size() == 1 && nativeWindowsPath))
        return QDir::toNativeSeparators(value);
    return {};
}

QString normalizedPath(const QString &target, const QString &normalizedFormat)
{
    const QString wanted = suffix(normalizedFormat);
    if (wanted.isEmpty())
        return {};
    const QFileInfo info(target);
    const QString existingSuffix = info.suffix().toLower();
    const bool matches = existingSuffix == wanted ||
        (normalizedFormat == QStringLiteral("JPEG") && existingSuffix == QStringLiteral("jpeg")) ||
        (normalizedFormat == QStringLiteral("TIFF") && existingSuffix == QStringLiteral("tif"));
    if (matches)
        return target;
    if (existingSuffix.isEmpty())
        return target + QLatin1Char('.') + wanted;
    return info.dir().filePath(info.completeBaseName() + QLatin1Char('.') + wanted);
}

QString availablePath(const QString &target)
{
    if (target.isEmpty())
        return {};
    const auto occupied = [](const QString &path) {
        const QFileInfo info(path);
        return info.exists() || info.isSymLink();
    };
    if (!occupied(target))
        return target;

    const QFileInfo info(target);
    QString baseName = info.completeBaseName();
    qulonglong number = 2;
    int padding = 0;
    static const QRegularExpression numberedName(QStringLiteral("^(.*)-([0-9]+)$"));
    const auto match = numberedName.match(baseName);
    if (match.hasMatch()) {
        bool valid = false;
        const QString digits = match.captured(2);
        const qulonglong previous = digits.toULongLong(&valid);
        if (valid && previous < std::numeric_limits<qulonglong>::max()) {
            baseName = match.captured(1);
            number = previous + 1;
            padding = digits.startsWith(QLatin1Char('0')) ? digits.size() : 0;
        }
    }
    for (;;) {
        const QString counter = QString::number(number).rightJustified(padding, QLatin1Char('0'));
        const QString candidate = info.dir().filePath(
            baseName + QLatin1Char('-') + counter + QLatin1Char('.') + info.suffix());
        if (!occupied(candidate))
            return candidate;
        if (number == std::numeric_limits<qulonglong>::max())
            return {};
        ++number;
    }
}

bool supports(const QString &format) { return !suffix(normalizedFormat(format)).isEmpty(); }

Result write(const ColorPipeline::WorkingImage &rendered, const QString &target,
             const QString &format, int quality)
{
    const QString normalized = normalizedFormat(format);
    const QString normalizedTarget = normalizedPath(target, normalized);
    // Encode beside the destination first. Publishing via QFile::rename does
    // not replace an occupied name, unlike a QSaveFile commit to that name.
    // This also covers a file appearing while the dialog/encoder was running.
    QString stagedPath;
    {
        QTemporaryFile reservation(QFileInfo(normalizedTarget).dir().filePath(
            QStringLiteral(".ipr-export-XXXXXX")));
        if (!reservation.open())
            return {false, tr("Échec de l'export (création du temporaire) : %1")
                .arg(reservation.errorString())};
        stagedPath = reservation.fileName();
        reservation.setAutoRemove(false);
        // QTemporaryFile::close() keeps its native handle open internally.
        // Destroy the object to release the Windows sharing lock before a
        // QSaveFile commit or TIFFOpenW touches this reserved temporary path.
    }
    auto cleanup = qScopeGuard([&] { QFile::remove(stagedPath); });
    const auto publish = [&]() -> Result {
        QFile completed(stagedPath);
        // Bound retries if another process continually takes proposed names.
        for (int attempt = 0; attempt < 32; ++attempt) {
            const QString finalPath = availablePath(normalizedTarget);
            if (finalPath.isEmpty())
                return {false, tr("Échec de l'export : aucun nom de fichier disponible.")};
            if (completed.rename(finalPath)) {
                cleanup.dismiss();
                return {true, normalized == QStringLiteral("DNG")
                    ? tr("Image exportée (DNG linéaire) : %1").arg(finalPath)
                    : tr("Image exportée : %1").arg(finalPath)};
            }
            const QFileInfo collision(finalPath);
            if (!collision.exists() && !collision.isSymLink())
                return {false, tr("Échec de l'export (publication de %1) : %2")
                    .arg(finalPath, completed.errorString())};
        }
        return {false, tr("Échec de l'export : noms occupés par un autre processus, réessayez.")};
    };

    if (normalized == QStringLiteral("DNG")) {
        QString dngError;
        if (!writeLinearDng(rendered, stagedPath, &dngError))
            return {false, tr("Échec de l'export DNG : %1").arg(dngError)};
        return publish();
    }

    QSaveFile destination(stagedPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        return {false, tr("Échec de l'export (écriture du temporaire) : %1")
                           .arg(destination.errorString())};
    }
    QImageWriter writer(&destination, normalized.toLatin1());
    const bool jpeg = normalized == QStringLiteral("JPEG");
    if (jpeg)
        writer.setQuality(qBound(0, quality, 100));
    QString colorError;
    const QImage output = ColorPipeline::toSrgb(
        rendered,
        jpeg ? ColorPipeline::OutputFormat::Jpeg8Bit
             : ColorPipeline::OutputFormat::HighBitDepth,
        &colorError);
    if (output.isNull()) {
        destination.cancelWriting();
        return {false, tr("Échec de la conversion couleur : %1").arg(colorError)};
    }
    if (!writer.write(output)) {
        destination.cancelWriting();
        return {false, tr("Échec de l'export : %1").arg(writer.errorString())};
    }
    if (!destination.commit())
        return {false, tr("Échec de l'export (validation du temporaire) : %1").arg(destination.errorString())};

    return publish();
}
}
