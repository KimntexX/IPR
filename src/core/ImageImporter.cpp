#include "ImageImporter.h"

#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <cmath>

namespace {
std::optional<double> readWhiteBalanceKelvin(const QImage &image)
{
    static const QRegularExpression kelvinPattern(
        QStringLiteral("^\\s*(\\d+(?:\\.\\d+)?)\\s*(?:K|kelvin)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString &key : image.textKeys()) {
        QString normalizedKey = key.toCaseFolded();
        normalizedKey.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
        if (normalizedKey != QStringLiteral("whitebalancetemperature") &&
            normalizedKey != QStringLiteral("colortemperature") &&
            normalizedKey != QStringLiteral("temperaturekelvin"))
            continue;

        const QRegularExpressionMatch match = kelvinPattern.match(image.text(key));
        if (!match.hasMatch())
            continue;
        bool ok = false;
        const double kelvin = match.captured(1).toDouble(&ok);
        if (ok && std::isfinite(kelvin) && kelvin >= 2000.0 && kelvin <= 50000.0)
            return kelvin;
    }
    return std::nullopt;
}

QString normalizedMetadataKey(QString key)
{
    key = key.toCaseFolded();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return key;
}

QString readMetadataText(const QImage &image, const QStringList &names)
{
    QStringList normalizedNames;
    for (const QString &name : names)
        normalizedNames.append(normalizedMetadataKey(name));

    for (const QString &key : image.textKeys()) {
        const QString normalizedKey = normalizedMetadataKey(key);
        for (const QString &name : normalizedNames) {
            if (normalizedKey == name || normalizedKey.endsWith(name)) {
                const QString value = image.text(key).trimmed();
                if (!value.isEmpty())
                    return value;
            }
        }
    }
    return {};
}

QDateTime readCaptureDate(const QImage &image)
{
    const QString value = readMetadataText(
        image, {QStringLiteral("DateTimeOriginal"),
                 QStringLiteral("CreateDate"), QStringLiteral("DateTime")});
    if (value.isEmpty())
        return {};

    const QStringList formats{
        QStringLiteral("yyyy:MM:dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss")
    };
    for (const QString &format : formats) {
        const QDateTime date = QDateTime::fromString(value, format);
        if (date.isValid())
            return date;
    }
    return QDateTime::fromString(value, Qt::ISODate);
}

std::optional<double> readMetadataNumber(const QImage &image,
                                         const QStringList &names)
{
    const QString value = readMetadataText(image, names);
    if (value.isEmpty())
        return std::nullopt;

    const QStringList fraction = value.split(QLatin1Char('/'));
    bool ok = false;
    double number = 0.0;
    if (fraction.size() == 2) {
        const double numerator = fraction.at(0).trimmed().toDouble(&ok);
        if (!ok)
            return std::nullopt;
        const double denominator = fraction.at(1).trimmed().toDouble(&ok);
        if (!ok || denominator == 0.0)
            return std::nullopt;
        number = numerator / denominator;
    } else {
        number = value.toDouble(&ok);
    }
    return ok && std::isfinite(number) && number > 0.0
               ? std::optional<double>(number) : std::nullopt;
}

}

namespace ImageImport {
Result load(const QString &fileName, const RawDecoder::Options &rawOptions)
{
    Result result;
    const auto extension = QFileInfo(fileName).suffix().toLower();
    if (RawDecoder::supportedExtensions().contains(extension)) {
        auto options = rawOptions;
        result.image = RawDecoder::decode(
            fileName, options,
            &result.metadata, &result.error);
        result.dcpSource = options.retainedDcpSource;
    } else {
        result.metadata.decoderId = QStringLiteral("qt-image-reader");
        result.metadata.decoderVersion = QStringLiteral(QT_VERSION_STR);
        QImageReader reader(fileName);
        reader.setAutoTransform(true);
        QImage loaded;
        if (reader.canRead())
            loaded = reader.read();
        if (loaded.isNull()) {
            result.error = reader.errorString();
        } else {
            result.metadata.format = extension;
            result.metadata.cameraMake = readMetadataText(
                loaded, {QStringLiteral("Make"), QStringLiteral("CameraMake")});
            result.metadata.cameraModel = readMetadataText(
                loaded, {QStringLiteral("Model"), QStringLiteral("CameraModel")});
            result.metadata.lensModel = readMetadataText(
                loaded, {QStringLiteral("LensModel"), QStringLiteral("Lens")});
            if (const auto iso = readMetadataNumber(loaded,
                    {QStringLiteral("ISOSpeedRatings"), QStringLiteral("ISO"),
                     QStringLiteral("PhotographicSensitivity")}))
                result.metadata.iso = static_cast<int>(std::lround(*iso));
            if (const auto focalLength = readMetadataNumber(loaded,
                    {QStringLiteral("FocalLength")}))
                result.metadata.focalLengthMm = *focalLength;
            if (const auto exposureTime = readMetadataNumber(loaded,
                    {QStringLiteral("ExposureTime"), QStringLiteral("ShutterSpeed")}))
                result.metadata.exposureSeconds = *exposureTime;
            result.metadata.captureDate = readCaptureDate(loaded);
            result.metadata.location = readMetadataText(
                loaded, {QStringLiteral("Location"), QStringLiteral("GPSPosition"),
                         QStringLiteral("LocationShown")});
            result.metadata.colorLabel = readMetadataText(
                loaded, {QStringLiteral("ColorLabel"), QStringLiteral("Label")});
            bool ratingOk = false;
            const int rating = readMetadataText(loaded, {QStringLiteral("Rating")})
                                   .toInt(&ratingOk);
            if (ratingOk)
                result.metadata.rating = qBound(0, rating, 5);
            result.metadata.hasEmbeddedColorProfile = loaded.colorSpace().isValid();
            if (result.metadata.hasEmbeddedColorProfile)
                result.metadata.embeddedColorProfile = loaded.colorSpace().description();
            result.image = ColorPipeline::fromSourceImage(loaded, &result.error);
            if (!result.image.isNull())
                result.image.referenceWhiteBalanceKelvin =
                    readWhiteBalanceKelvin(loaded);
        }
    }
    complete(result);
    return result;
}

bool complete(Result &result)
{
    result.capabilities = {};
    result.provenance = {};
    result.warnings.clear();
    const auto &image = result.image;
    bool valid = !image.isNull() && image.colorSpace == ColorPipeline::workingColorSpace() &&
        image.linearRgba.dims == 2 && image.linearRgba.type() == CV_32FC4 &&
        cv::checkRange(image.linearRgba, true);
    if (valid) {
        for (int y=0; y<image.linearRgba.rows && valid; ++y) {
            const auto *row = image.linearRgba.ptr<cv::Vec4f>(y);
            for (int x=0; x<image.linearRgba.cols; ++x)
                if (row[x][3] < 0.f || row[x][3] > 1.f) { valid=false; break; }
        }
    }
    if (!valid || !result.error.isEmpty()) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("L'import n'a pas fourni un master Rec.2020 linéaire RGBA fini.");
        result.image = {};
        result.dcpSource.reset();
        return false;
    }
    result.provenance.decoderId = result.metadata.decoderId;
    result.provenance.decoderVersion = result.metadata.decoderVersion;
    result.provenance.sourceProfile = image.sourceProfile;
    result.provenance.colorOrigin = result.metadata.cameraRaw
        ? (result.metadata.dcpProfile.isEmpty() ? ColorOrigin::DecoderMatrix : ColorOrigin::CameraDcp)
        : (result.metadata.hasEmbeddedColorProfile ? ColorOrigin::EmbeddedIcc : ColorOrigin::AssumedSrgb);
    result.capabilities.cameraDevelopment = result.metadata.cameraRaw;
    result.capabilities.cameraWhiteBalanceContext = result.metadata.cameraRaw && bool(image.dcpContext);
    result.capabilities.profileWithoutRedecode = result.metadata.cameraRaw &&
        !result.metadata.rawNormalizationApplied && result.dcpSource && !result.dcpSource->cameraRgb.empty();
    if (!result.metadata.developmentWarning.isEmpty())
        result.warnings.append(result.metadata.developmentWarning);
    if (!result.metadata.cameraRaw && !result.metadata.hasEmbeddedColorProfile)
        result.warnings.append(QStringLiteral("Profil ICC absent ou invalide : sRGB supposé."));
    return true;
}
}
