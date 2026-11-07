#include "CameraDng.h"

#include "Pixels.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace CameraDng {
namespace {

constexpr uint16_t linearRawPhotometric = 34892;
constexpr const char *markerKey = "ipr_camera_dng";
constexpr int formatVersion = 1;
// Room for the values below the black, about 1.6 % of white.
constexpr float blackCode = 1024.0f;
constexpr float whiteCode = 65535.0f;
// A signal may exceed white after a vignetting correction. Beyond this it is an outlier,
// which must not cost the whole image its precision.
constexpr double largestHeadroom = 8.0;

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
// balance, as a raw file does: largest neutral first, then the headroom of the data.
double neutralScale(const ipr::Vec3d &neutral)
{
    return std::max({neutral[0], neutral[1], neutral[2]});
}

double headroomOf(const ipr::Image &rgb, const ipr::Vec3d &neutral)
{
    const double scale = neutralScale(neutral);
    const std::vector<ipr::Image> channels = ipr::splitChannels(rgb);
    double headroom = 1.0;
    for (int c = 0; c < 3; ++c) {
        const double highest = ipr::extremesOf(channels[size_t(c)]).largest;
        headroom = std::max(headroom, highest * neutral[c] / scale);
    }
    return std::min(headroom, largestHeadroom);
}

QJsonObject description(const Content &content, double headroom)
{
    const CameraProfileEngine::DcpRenderOptions &options = content.renderOptions;
    return QJsonObject{
        {QLatin1String(markerKey), formatVersion},
        {QStringLiteral("neutral"), QJsonArray{content.cameraNeutral[0], content.cameraNeutral[1], content.cameraNeutral[2]}},
        {QStringLiteral("headroom"), headroom},
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
    return object.value(QLatin1String(markerKey)).toInt() == formatVersion ? object : QJsonObject{};
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

    const double scale = neutralScale(neutral);
    const double headroom = headroomOf(content.cameraRgb, neutral);
    TIFF *tiff = openTiff(fileName, "w");
    if (!tiff)
        return fail(error, QStringLiteral("Impossible de créer le fichier DNG."));
    auto closing = qScopeGuard([&] { TIFFClose(tiff); });

    constexpr uint8_t dngVersion[] = {1, 4, 0, 0};
    constexpr uint8_t dngBackwardVersion[] = {1, 1, 0, 0};
    const float blackLevel[] = {blackCode, blackCode, blackCode};
    const uint32_t whiteLevel[] = {uint32_t(whiteCode), uint32_t(whiteCode), uint32_t(whiteCode)};
    const float asShotNeutral[] = {float(neutral[0] / scale), float(neutral[1] / scale), float(neutral[2] / scale)};
    float colorMatrix[9];
    for (int i = 0; i < 9; ++i)
        colorMatrix[i] = float(content.colorMatrix[size_t(i)]);
    const auto &options = content.renderOptions;
    const float baselineExposure = float(options.cameraBaselineExposureEv + options.profileExposureCompensationEv + std::log2(headroom));
    const QByteArray make = content.cameraMake.toUtf8();
    const QByteArray model = content.cameraModel.toUtf8();
    const QByteArray unique = (content.cameraModel.startsWith(content.cameraMake) ? content.cameraModel
                               : content.cameraMake + QLatin1Char(' ') + content.cameraModel).toUtf8();
    const QByteArray text = QJsonDocument(description(content, headroom)).toJson(QJsonDocument::Compact);

    // NewSubfileType 0 marks the main image; Adobe readers reject an IFD without it.
    const bool configured =
        TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, 0) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(content.cameraRgb.width())) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(content.cameraRgb.height())) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, linearRawPhotometric) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 16) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MAKE, make.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MODEL, model.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SOFTWARE, "IPR") == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, text.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGVERSION, dngVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGBACKWARDVERSION, dngBackwardVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_UNIQUECAMERAMODEL, unique.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BLACKLEVEL, 3, blackLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_WHITELEVEL, 3, whiteLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COLORMATRIX1, 9, colorMatrix) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ASSHOTNEUTRAL, 3, asShotNeutral) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BASELINEEXPOSURE, baselineExposure) == 1 &&
        TIFFSetField(tiff, TIFFTAG_CALIBRATIONILLUMINANT1, content.illuminant) == 1;
    if (!configured)
        return fail(error, QStringLiteral("Impossible d'écrire les métadonnées DNG."));

    const std::array<float, 3> toCode{float(neutral[0] / scale / headroom * (whiteCode - blackCode)),
                                      float(neutral[1] / scale / headroom * (whiteCode - blackCode)),
                                      float(neutral[2] / scale / headroom * (whiteCode - blackCode))};
    std::vector<quint16> row(size_t(content.cameraRgb.width()) * 3);
    for (int y = 0; y < content.cameraRgb.height(); ++y) {
        const ipr::Vec3f *in = content.cameraRgb.row<ipr::Vec3f>(y);
        for (int x = 0; x < content.cameraRgb.width(); ++x)
            for (int c = 0; c < 3; ++c)
                row[size_t(x) * 3 + size_t(c)] = quint16(std::clamp(blackCode + in[x][c] * toCode[size_t(c)], 0.f, whiteCode) + 0.5f);
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

    uint32_t width = 0, height = 0;
    uint16_t bits = 0, samples = 0, planar = PLANARCONFIG_CONTIG;
    if (TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width) != 1 || TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height) != 1 ||
        TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits) != 1 || TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples) != 1 ||
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar) != 1 || bits != 16 || samples != 3 ||
        planar != PLANARCONFIG_CONTIG || width == 0 || height == 0 ||
        qint64(TIFFScanlineSize64(tiff)) != qint64(width) * 6)
        return fail(error, QStringLiteral("Structure inattendue du fichier DNG."));

    const QJsonArray neutralValues = object.value(QStringLiteral("neutral")).toArray();
    const double headroom = object.value(QStringLiteral("headroom")).toDouble();
    const ipr::Vec3d neutral(neutralValues.at(0).toDouble(), neutralValues.at(1).toDouble(), neutralValues.at(2).toDouble());
    if (neutralValues.size() != 3 || !(neutral[0] > 0.0 && neutral[1] > 0.0 && neutral[2] > 0.0) ||
        !(headroom >= 1.0 && headroom <= largestHeadroom))
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

    const double scale = neutralScale(neutral);
    const std::array<float, 3> fromCode{float(scale / neutral[0] * headroom / (whiteCode - blackCode)),
                                        float(scale / neutral[1] * headroom / (whiteCode - blackCode)),
                                        float(scale / neutral[2] * headroom / (whiteCode - blackCode))};
    content.cameraRgb = ipr::Image::create({int(width), int(height)}, ipr::Depth::F32, 3);
    std::vector<quint16> row(size_t(width) * 3);
    for (uint32_t y = 0; y < height; ++y) {
        if (TIFFReadScanline(tiff, row.data(), y, 0) < 0) {
            content.cameraRgb = {};
            return fail(error, QStringLiteral("Impossible de lire les pixels du DNG."));
        }
        ipr::Vec3f *out = content.cameraRgb.mutableRow<ipr::Vec3f>(int(y));
        for (uint32_t x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                out[x][c] = (float(row[size_t(x) * 3 + size_t(c)]) - blackCode) * fromCode[size_t(c)];
    }
    return true;
}

} // namespace CameraDng
