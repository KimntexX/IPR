#include "CameraDng.h"

#include "HalfFloat.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace CameraDng {
namespace {

constexpr uint16_t linearRawPhotometric = 34892;
constexpr const char *markerKey = "ipr_camera_dng";
// 2: binary16 samples, white 1, any range, values below the black kept as they are.
// 1, still read for the files already exported: 16-bit codes over a black of 1024 and a
// white of 65535, the signal scaled down by its headroom, at most 8.
constexpr int formatVersion = 2;
constexpr int integerFormatVersion = 1;
constexpr float integerBlackCode = 1024.0f;
constexpr float integerWhiteCode = 65535.0f;
constexpr double integerLargestHeadroom = 8.0;
// Two bytes a sample, deflated after the predictor of floating-point data (TIFF technical
// note 3), which DNG 1.4 accepts.
constexpr int halfBits = 16;
constexpr int rowsPerStrip = 16;

TIFF *openTiff(const QString &fileName, const char *mode)
{
#ifdef Q_OS_WIN
    return TIFFOpenW(reinterpret_cast<const wchar_t *>(fileName.utf16()), mode);
#else
    return TIFFOpen(fileName.toUtf8().constData(), mode);
#endif
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// The channels' scale between the signal and the file, which holds them before white
// balance, as a raw file does: the largest neutral.
double neutralScale(const ipr::Vec3d &neutral)
{
    return std::max({neutral[0], neutral[1], neutral[2]});
}

QJsonObject description(const Content &content)
{
    const CameraProfileEngine::DcpRenderOptions &options = content.renderOptions;
    return QJsonObject{
        {QLatin1String(markerKey), formatVersion},
        {QStringLiteral("neutral"), QJsonArray{content.cameraNeutral[0], content.cameraNeutral[1], content.cameraNeutral[2]}},
        {QStringLiteral("kelvin"), content.whiteBalanceKelvin},
        {QStringLiteral("baseline_ev"), options.cameraBaselineExposureEv},
        {QStringLiteral("profile_compensation_ev"), options.profileExposureCompensationEv},
        {QStringLiteral("preserve_tone_curve_hue"), options.preserveToneCurveHue},
        {QStringLiteral("calibrated_white"), int(content.calibratedWhite)},
        {QStringLiteral("provenance"), content.provenance},
        {QStringLiteral("lens_make"), content.lensMake},
        {QStringLiteral("lens_model"), content.lensModel},
        {QStringLiteral("iso"), content.iso},
        {QStringLiteral("exposure_s"), content.exposureSeconds},
        {QStringLiteral("focal_mm"), content.focalLengthMm},
        {QStringLiteral("capture_date"), content.captureDate.toString(Qt::ISODate)},
    };
}

QJsonObject readDescription(TIFF *tiff)
{
    const char *text = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEDESCRIPTION, &text) != 1 || !text)
        return {};
    const QJsonObject object = QJsonDocument::fromJson(QByteArray(text)).object();
    const int version = object.value(QLatin1String(markerKey)).toInt();
    return version == formatVersion || version == integerFormatVersion ? object : QJsonObject{};
}

// The file's samples of the signal: before white balance, white 1.
std::array<float, 3> toFile(const ipr::Vec3d &neutral)
{
    const double scale = neutralScale(neutral);
    return {float(neutral[0] / scale), float(neutral[1] / scale), float(neutral[2] / scale)};
}

bool readHalfPixels(TIFF *tiff, const ipr::Vec3d &neutral, ipr::Image &rgb)
{
    const std::array<float, 3> scale = toFile(neutral);
    const std::array<float, 3> fromFile{1.f / scale[0], 1.f / scale[1], 1.f / scale[2]};
    std::vector<uint16_t> row(size_t(rgb.width()) * 3);
    for (int y = 0; y < rgb.height(); ++y) {
        if (TIFFReadScanline(tiff, row.data(), uint32_t(y), 0) < 0)
            return false;
        ipr::Vec3f *out = rgb.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < rgb.width(); ++x)
            for (int c = 0; c < 3; ++c)
                out[x][c] = ipr::fromHalf(row[size_t(x) * 3 + size_t(c)]) * fromFile[size_t(c)];
    }
    return true;
}

bool readIntegerPixels(TIFF *tiff, const ipr::Vec3d &neutral, double headroom, ipr::Image &rgb)
{
    const std::array<float, 3> scale = toFile(neutral);
    const float perCode = float(headroom / (integerWhiteCode - integerBlackCode));
    const std::array<float, 3> fromCode{perCode / scale[0], perCode / scale[1], perCode / scale[2]};
    std::vector<uint16_t> row(size_t(rgb.width()) * 3);
    for (int y = 0; y < rgb.height(); ++y) {
        if (TIFFReadScanline(tiff, row.data(), uint32_t(y), 0) < 0)
            return false;
        ipr::Vec3f *out = rgb.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < rgb.width(); ++x)
            for (int c = 0; c < 3; ++c)
                out[x][c] = (float(row[size_t(x) * 3 + size_t(c)]) - integerBlackCode) * fromCode[size_t(c)];
    }
    return true;
}

} // namespace

bool write(const Content &content, const QString &fileName, QString *error)
{
    if (content.cameraRgb.empty() || content.cameraRgb.depth() != ipr::Depth::F32 || content.cameraRgb.channels() != 3)
        return fail(error, QStringLiteral("Le signal à écrire est vide ou d'un type inattendu."));
    const ipr::Vec3d neutral = content.cameraNeutral;
    if (!(neutral[0] > 0.0 && neutral[1] > 0.0 && neutral[2] > 0.0))
        return fail(error, QStringLiteral("La balance des blancs du signal est inconnue."));
    if (std::all_of(content.colorMatrix.begin(), content.colorMatrix.end(), [](double v) { return v == 0.0; }))
        return fail(error, QStringLiteral("La matrice couleur du boîtier est inconnue."));

    TIFF *tiff = openTiff(fileName, "w");
    if (!tiff)
        return fail(error, QStringLiteral("Impossible de créer le fichier DNG."));
    auto closing = qScopeGuard([&] { TIFFClose(tiff); });

    // Floating-point samples need DNG 1.4 to be read.
    constexpr uint8_t dngVersion[] = {1, 4, 0, 0};
    constexpr uint8_t dngBackwardVersion[] = {1, 4, 0, 0};
    const std::array<float, 3> toFileScale = toFile(neutral);
    float colorMatrix[9];
    for (int i = 0; i < 9; ++i)
        colorMatrix[i] = float(content.colorMatrix[size_t(i)]);
    const auto &options = content.renderOptions;
    const float baselineExposure = float(options.cameraBaselineExposureEv + options.profileExposureCompensationEv);
    const QByteArray make = content.cameraMake.toUtf8();
    const QByteArray model = content.cameraModel.toUtf8();
    const QByteArray unique = (content.cameraModel.startsWith(content.cameraMake) ? content.cameraModel
                               : content.cameraMake + QLatin1Char(' ') + content.cameraModel).toUtf8();
    const QByteArray text = QJsonDocument(description(content)).toJson(QJsonDocument::Compact);

    // NewSubfileType 0 marks the main image; Adobe readers reject an IFD without it. No black
    // and no white level: a floating-point DNG's defaults, 0 and 1.
    const bool configured =
        TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, 0) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(content.cameraRgb.width())) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(content.cameraRgb.height())) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, halfBits) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, linearRawPhotometric) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PREDICTOR, PREDICTOR_FLOATINGPOINT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, rowsPerStrip) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MAKE, make.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MODEL, model.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SOFTWARE, "IPR") == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, text.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGVERSION, dngVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGBACKWARDVERSION, dngBackwardVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_UNIQUECAMERAMODEL, unique.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COLORMATRIX1, 9, colorMatrix) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ASSHOTNEUTRAL, 3, toFileScale.data()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BASELINEEXPOSURE, baselineExposure) == 1 &&
        TIFFSetField(tiff, TIFFTAG_CALIBRATIONILLUMINANT1, content.illuminant) == 1;
    if (!configured)
        return fail(error, QStringLiteral("Impossible d'écrire les métadonnées DNG."));

    std::vector<uint16_t> row(size_t(content.cameraRgb.width()) * 3);
    for (int y = 0; y < content.cameraRgb.height(); ++y) {
        const ipr::Vec3f *in = content.cameraRgb.row<ipr::Vec3f>(y);
        for (int x = 0; x < content.cameraRgb.width(); ++x)
            for (int c = 0; c < 3; ++c)
                row[size_t(x) * 3 + size_t(c)] = ipr::toHalf(in[x][c] * toFileScale[size_t(c)]);
        if (TIFFWriteScanline(tiff, row.data(), uint32_t(y), 0) < 0)
            return fail(error, QStringLiteral("Impossible d'écrire les pixels DNG."));
    }
    // The directory is written at the flush: a full disk shows here, not at TIFFClose.
    if (TIFFFlush(tiff) != 1)
        return fail(error, QStringLiteral("Impossible de terminer l'écriture du DNG."));
    return true;
}

bool isCameraDng(const QString &fileName)
{
    TIFF *tiff = openTiff(fileName, "r");
    if (!tiff)
        return false;
    const auto closing = qScopeGuard([&] { TIFFClose(tiff); });
    return !readDescription(tiff).isEmpty();
}

bool read(const QString &fileName, Content &content, QString *error)
{
    TIFF *tiff = openTiff(fileName, "r");
    if (!tiff)
        return fail(error, QStringLiteral("Impossible d'ouvrir le fichier DNG."));
    const auto closing = qScopeGuard([&] { TIFFClose(tiff); });
    const QJsonObject object = readDescription(tiff);
    if (object.isEmpty())
        return fail(error, QStringLiteral("Ce DNG n'est pas un signal caméra écrit par IPR."));
    const bool integer = object.value(QLatin1String(markerKey)).toInt() == integerFormatVersion;

    uint32_t width = 0, height = 0;
    uint16_t bits = 0, samples = 0, planar = PLANARCONFIG_CONTIG, format = SAMPLEFORMAT_UINT;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) != 1 || TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) != 1 ||
        TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits) != 1 || TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLEFORMAT, &format) != 1 || bits != 16 || samples != 3 ||
        format != (integer ? SAMPLEFORMAT_UINT : SAMPLEFORMAT_IEEEFP) ||
        planar != PLANARCONFIG_CONTIG || width == 0 || height == 0 ||
        qint64(TIFFScanlineSize64(tiff)) != qint64(width) * 6)
        return fail(error, QStringLiteral("Structure inattendue du fichier DNG."));

    const QJsonArray neutralValues = object.value(QStringLiteral("neutral")).toArray();
    const double headroom = integer ? object.value(QStringLiteral("headroom")).toDouble() : 1.0;
    const ipr::Vec3d neutral(neutralValues.at(0).toDouble(), neutralValues.at(1).toDouble(), neutralValues.at(2).toDouble());
    if (neutralValues.size() != 3 || !(neutral[0] > 0.0 && neutral[1] > 0.0 && neutral[2] > 0.0) ||
        !(headroom >= 1.0 && headroom <= integerLargestHeadroom))
        return fail(error, QStringLiteral("Description invalide dans le fichier DNG."));

    const char *make = nullptr, *model = nullptr;
    TIFFGetField(tiff, TIFFTAG_MAKE, &make);
    TIFFGetField(tiff, TIFFTAG_MODEL, &model);
    content.cameraMake = make ? QString::fromUtf8(make) : QString();
    content.cameraModel = model ? QString::fromUtf8(model) : QString();
    content.cameraNeutral = neutral;
    content.whiteBalanceKelvin = object.value(QStringLiteral("kelvin")).toDouble(5500.0);
    content.renderOptions.cameraBaselineExposureEv = object.value(QStringLiteral("baseline_ev")).toDouble();
    content.renderOptions.profileExposureCompensationEv = object.value(QStringLiteral("profile_compensation_ev")).toDouble();
    content.renderOptions.preserveToneCurveHue = object.value(QStringLiteral("preserve_tone_curve_hue")).toBool();
    content.calibratedWhite = unsigned(std::max(0, object.value(QStringLiteral("calibrated_white")).toInt()));
    content.provenance = object.value(QStringLiteral("provenance")).toString();
    content.lensMake = object.value(QStringLiteral("lens_make")).toString();
    content.lensModel = object.value(QStringLiteral("lens_model")).toString();
    content.iso = object.value(QStringLiteral("iso")).toInt();
    content.exposureSeconds = object.value(QStringLiteral("exposure_s")).toDouble();
    content.focalLengthMm = object.value(QStringLiteral("focal_mm")).toDouble();
    content.captureDate = QDateTime::fromString(object.value(QStringLiteral("capture_date")).toString(), Qt::ISODate);

    content.cameraRgb = ipr::Image::create({int(width), int(height)}, ipr::Depth::F32, 3);
    const bool pixels = integer ? readIntegerPixels(tiff, neutral, headroom, content.cameraRgb)
                                : readHalfPixels(tiff, neutral, content.cameraRgb);
    if (!pixels) {
        content.cameraRgb = {};
        return fail(error, QStringLiteral("Impossible de lire les pixels du DNG."));
    }
    return true;
}

} // namespace CameraDng
