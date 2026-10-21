#include "TestRecipes.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <cmath>
#include <mutex>
#include <stdexcept>

namespace TestRecipes {
namespace {
const QStringList bands{"red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta"};
const QStringList components{"hue", "saturation", "luminance"};
const QStringList zones{"shadows", "midtones", "highlights"};
const QStringList channels{"luminance", "red", "green", "blue"};

void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}
}

const QStringList &groups()
{
    static const QStringList names{"raw", "lumiere", "courbes", "couleur", "melangeur",
                                   "grading", "effets", "bruit", "nettete", "optique"};
    return names;
}

QFileInfoList sources(const QString &base)
{
    QStringList extensions = RawDecoder::supportedExtensions();
    extensions.append(QStringList{"tif", "tiff", "png", "jpg", "jpeg", "bmp", "webp"});
    QFileInfoList result;
    for (const QFileInfo &file : QDir(base).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name))
        if (extensions.contains(file.suffix().toLower())) result.append(file);
    return result;
}

QList<Feature> features()
{
    QList<Feature> result{{"auto-wb", {0, 1}}, {"exposure", {-1, -0.5, 0, 0.5, 1}},
        {"temperature", {2000, 3500, 5200, 6500, 9000, 50000}},
        {"tint", {-150, -100, -50, -25, 0, 25, 50, 100, 150}},
        {"blur", {0, 5, 20}}, {"grayscale", {0, 1}},
        {"sharpening-amount", {0, 25, 50, 100, 200}},
        {"sharpening-radius", {0.3, 1, 2, 3}}, {"sharpening-masking", {0, 25, 50, 100}},
        {"lens-correction", {0, 1}}, {"chromatic-aberration", {0, 1}}};
    for (const QString &name : QStringList{"contrast", "shadows", "blacks", "highlights", "whites"})
        result.append({name, {-100, -75, -50, -25, 0, 25, 50, 75, 100}});
    for (const QString &name : QStringList{"vibrance", "saturation", "texture", "clarity", "dehaze"})
        result.append({name, {-100, -50, -25, 0, 25, 50, 100}});
    for (const QString &name : QStringList{"grain", "denoise-luminance", "denoise-chrominance"})
        result.append({name, {0, 25, 50, 100}});
    for (const QString &band : bands)
        for (const QString &component : components)
            result.append({"mixer-" + band + "-" + component, {-100, -50, -25, 0, 25, 50, 100}});
    for (const QString &zone : zones) {
        result.append({"grading-" + zone + "-hue", {0, 60, 120, 180, 240, 300}});
        result.append({"grading-" + zone + "-saturation", {0, 25, 50, 100}});
    }
    for (const QString &channel : channels)
        result.append({"curve-" + channel, {-100, -50, 0, 50, 100}});
    return result;
}

QString featureGroup(const QString &name)
{
    if (name == "auto-wb") return "raw";
    if (name == "lens-correction" || name == "chromatic-aberration") return "optique";
    if (QStringList{"exposure", "contrast", "shadows", "blacks", "highlights", "whites"}.contains(name)) return "lumiere";
    if (name.startsWith("curve-")) return "courbes";
    if (name.startsWith("mixer-")) return "melangeur";
    if (name.startsWith("grading-")) return "grading";
    if (name.startsWith("denoise-")) return "bruit";
    if (name.startsWith("sharpening-")) return "nettete";
    if (QStringList{"temperature", "tint", "vibrance", "saturation", "grayscale"}.contains(name)) return "couleur";
    return "effets";
}

QString featureFolder(const QString &name)
{
    if (name == "temperature") return "couleur/temperature";
    if (name == "tint") return "couleur/nuance";
    if (name == "grayscale") return "couleur/niveaux_gris";
    if (name == "vibrance" || name == "saturation") return "couleur/" + name;
    if (name == "texture") return name;
    if (name == "clarity") return "clarte";
    if (name == "dehaze") return "correction_de_voile";
    if (name.startsWith("mixer-")) return "color-mixer/" + name.split('-').at(1);
    if (name == "denoise-luminance") return "bruit/luminance";
    if (name == "denoise-chrominance") return "bruit/couleur";
    if (name.startsWith("grading-")) return "grading/" + name.split('-').at(1);
    return featureGroup(name) + '/' + name;
}

QString fileName(const QString &stem, const QString &feature, double value)
{
    return stem + "-IPR-" + feature + '-' + QString::number(value, 'g', 12) + ".tiff";
}

bool rawOnly(const QString &name)
{
    return name == "auto-wb" || featureGroup(name) == "optique";
}

bool eligible(const QString &feature, const QString &sourcePath)
{
    return !rawOnly(feature) || RawDecoder::supportedExtensions().contains(QFileInfo(sourcePath).suffix().toLower());
}

Decode decodeFor(const QString &feature, double value)
{
    Decode decode;
    decode.automaticWhiteBalance = feature == "auto-wb" && value == 1;
    decode.lensCorrection = feature == "lens-correction" && value == 1;
    decode.chromaticAberration = feature == "chromatic-aberration" && value == 1;
    return decode;
}

Reference referenceFor(const QString &feature)
{
    if (feature == "temperature") return {feature, 5200};
    if (feature == "tint" || feature == "sharpening-masking" || feature.startsWith("grading-")
        || featureGroup(feature) == "optique")
        return {feature, 0};
    if (feature == "sharpening-radius") return {feature, 1};
    return {"neutral", 0};
}

LoadResult load(const QString &path, const Decode &decode)
{
    RawDecoder::Options options;
    options.preferCameraNeutral = true;
    options.useNormalizationEnvironment = false;
    options.normalizationManifest = ImageEditor::defaultCalibrationManifest();
    options.lensCorrectionEnabled = decode.lensCorrection;
    options.chromaticAberrationEnabled = decode.chromaticAberration;
    LoadResult result = ImageEditor::loadFromDisk(path, decode.automaticWhiteBalance, options);
    require(!result.image.isNull(), "Decode failed for " + path + ": " + result.error);
    result.editProxy = {};
    return result;
}

Settings neutral(const LoadResult &loaded)
{
    Settings settings;
    settings.temperature = static_cast<int>(std::lround(loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
    return settings;
}

Settings adjusted(Settings settings, const QString &name, double value)
{
    const int integer = static_cast<int>(value);
    if (name == "exposure") settings.brightness = static_cast<float>(value);
    else if (name == "contrast") settings.contrast = integer;
    else if (name == "blur") settings.blur = integer;
    else if (name == "grayscale") settings.grayscale = integer != 0;
    else if (name == "shadows") settings.shadows = integer;
    else if (name == "blacks") settings.blacks = integer;
    else if (name == "highlights") settings.highlights = integer;
    else if (name == "whites") settings.whites = integer;
    else if (name == "temperature") settings.temperature = integer;
    else if (name == "tint") settings.tint = integer;
    else if (name == "vibrance") settings.vibrance = integer;
    else if (name == "saturation") settings.saturation = integer;
    else if (name == "texture") settings.texture = integer;
    else if (name == "clarity") settings.clarity = integer;
    else if (name == "dehaze") settings.dehaze = integer;
    else if (name == "grain") settings.grain = integer;
    else if (name == "denoise-luminance") settings.denoiseLuminance = integer;
    else if (name == "denoise-chrominance") settings.denoiseChrominance = integer;
    else if (name == "sharpening-amount") settings.sharpeningAmount = integer;
    else if (name == "sharpening-radius") {
        settings.sharpeningAmount = 50;
        settings.sharpeningRadius = static_cast<float>(value);
    } else if (name == "sharpening-masking") {
        settings.sharpeningAmount = 50;
        settings.sharpeningMasking = integer;
    } else if (name == "auto-wb") settings.automaticWhiteBalance = integer != 0;
    else if (name.startsWith("mixer-")) {
        const QStringList parts = name.split('-');
        const int band = bands.indexOf(parts.at(1));
        if (parts.at(2) == "hue") settings.mixerHue.at(band) = integer;
        else if (parts.at(2) == "saturation") settings.mixerSaturation.at(band) = integer;
        else settings.mixerLuminance.at(band) = integer;
    } else if (name.startsWith("grading-")) {
        const QStringList parts = name.split('-');
        const int zone = zones.indexOf(parts.at(1));
        settings.gradingHue.at(zone) = parts.at(2) == "hue" ? integer : 30;
        settings.gradingSaturation.at(zone) = parts.at(2) == "saturation" ? integer : 25;
    } else if (name.startsWith("curve-")) {
        const int channel = channels.indexOf(name.mid(6));
        settings.toneCurves.at(channel) = {{0, 0}, {0.5f, float(0.5 + value * 0.004)}, {1, 1}};
    } else require(name == "lens-correction" || name == "chromatic-aberration",
                   "Unsupported feature: " + name);
    return settings;
}

Settings settingsFor(const LoadResult &loaded, const QString &feature, double value)
{
    Settings settings = neutral(loaded);
    if (feature == "neutral") return settings;
    // Both manual WB axes use the fixed 5200 K / tint 0 anchor.
    if (feature == "temperature" || feature == "tint") settings.temperature = 5200;
    return adjusted(settings, feature, value);
}

QJsonObject declaredSettings(const Settings &s)
{
    QJsonObject result{{"exposure", s.brightness}, {"contrast", s.contrast}, {"blur", s.blur},
        {"grayscale", s.grayscale}, {"shadows", s.shadows}, {"blacks", s.blacks},
        {"highlights", s.highlights}, {"whites", s.whites}, {"temperature", s.temperature},
        {"tint", s.tint}, {"vibrance", s.vibrance}, {"saturation", s.saturation},
        {"texture", s.texture}, {"clarity", s.clarity}, {"dehaze", s.dehaze}, {"grain", s.grain},
        {"denoiseLuminance", s.denoiseLuminance}, {"denoiseChrominance", s.denoiseChrominance},
        {"sharpeningAmount", s.sharpeningAmount}, {"sharpeningRadius", s.sharpeningRadius},
        {"sharpeningMasking", s.sharpeningMasking}, {"automaticWhiteBalance", s.automaticWhiteBalance}};
    QJsonArray hue, sat, lum, grading, curves;
    for (int i = 0; i < ColorMixer::BandCount; ++i) {
        hue.append(s.mixerHue.at(i)); sat.append(s.mixerSaturation.at(i)); lum.append(s.mixerLuminance.at(i));
    }
    for (int i = 0; i < 3; ++i)
        grading.append(QJsonObject{{"hue", s.gradingHue.at(i)}, {"saturation", s.gradingSaturation.at(i)}});
    for (const auto &curve : s.toneCurves) {
        QJsonArray points;
        for (const auto &point : curve) points.append(QJsonArray{point.x, point.y});
        curves.append(points);
    }
    result.insert("mixerHue", hue); result.insert("mixerSaturation", sat); result.insert("mixerLuminance", lum);
    result.insert("grading", grading); result.insert("toneCurves", curves);
    return result;
}

QString sha256(const QString &path)
{
    static QHash<QString, QString> hashes;
    static std::mutex lock;
    const std::lock_guard<std::mutex> guard(lock);
    if (!hashes.contains(path)) {
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "Cannot hash: " + path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        require(hash.addData(&file), "Cannot hash: " + path);
        hashes.insert(path, QString::fromLatin1(hash.result().toHex()));
    }
    return hashes.value(path);
}

QJsonObject declaration(const LoadResult &source, const Settings &settings, const QString &input,
                        const QString &group, const QString &feature, double value,
                        const QString &reference)
{
    return {{"ipr_test_schema", 1}, {"scene", QFileInfo(input).completeBaseName()},
        {"group", group}, {"feature", feature}, {"value", value}, {"reference", reference},
        {"settings", declaredSettings(settings)},
        {"source_sha256", sha256(input)}, {"executable_sha256", sha256(QCoreApplication::applicationFilePath())},
        {"qt_version", qVersion()}, {"created_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"dcp_profile", source.metadata.dcpProfile}, {"lcp_profile", source.metadata.lcpProfile},
        {"lcp_sha256", source.metadata.lcpSha256}, {"raw_normalization", source.metadata.rawNormalizationApplied},
        {"lens_geometry_applied", source.metadata.lensGeometryApplied},
        {"lens_vignetting_applied", source.metadata.lensVignettingApplied},
        {"chromatic_aberration_applied", source.metadata.chromaticAberrationApplied}};
}
}
