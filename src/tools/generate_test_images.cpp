#include "imageeditor.h"
#include "CameraProfileEngine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

#include <cmath>
#include <stdexcept>

namespace {
struct Feature {
    QString name;
    double minimum;
    double maximum;
    bool integer;
    QList<double> defaults;
};

const QStringList bands{"red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta"};
const QStringList components{"hue", "saturation", "luminance"};
const QStringList zones{"shadows", "midtones", "highlights"};
const QStringList channels{"luminance", "red", "green", "blue"};

QList<Feature> features()
{
    QList<Feature> result{{"neutral", 0, 0, true, {0}},
        {"exposure", -5, 5, false, {-1, 0, 1}},
        {"temperature", 2000, 50000, true, {4000, 6500, 9000}},
        {"tint", -150, 150, true, {-100, -50, 0, 50, 100}},
        {"blur", 0, 20, true, {0, 5, 20}},
        {"grayscale", 0, 1, true, {0, 1}},
        {"auto-wb", 0, 1, true, {0, 1}},
        {"sharpening-amount", 0, 200, true, {0, 25, 50, 100, 200}},
        {"sharpening-radius", 0.3, 3, false, {0.3, 1, 2, 3}},
        {"sharpening-masking", 0, 100, true, {0, 25, 50, 100}}};
    for (const QString &name : QStringList{"contrast", "shadows", "blacks", "highlights",
            "whites", "vibrance", "saturation", "texture", "clarity", "dehaze"})
        result.append({name, -100, 100, true, {-100, -50, 0, 50, 100}});
    for (const QString &name : QStringList{"grain", "denoise-luminance", "denoise-chrominance"})
        result.append({name, 0, 100, true, {0, 25, 50, 100}});
    for (const QString &band : bands)
        for (const QString &component : components)
            result.append({"mixer-" + band + "-" + component, -100, 100, true,
                           {-100, -50, -25, 0, 25, 50, 100}});
    for (const QString &zone : zones) {
        result.append({"grading-" + zone + "-hue", 0, 359, true, {0, 60, 120, 180, 240, 300}});
        result.append({"grading-" + zone + "-saturation", 0, 100, true, {0, 25, 50, 100}});
    }
    for (const QString &channel : channels)
        result.append({"curve-" + channel, -100, 100, true, {-100, -50, 0, 50, 100}});
    return result;
}

void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toUtf8().constData());
}

QString hashFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash: " + path);
    return QString::fromLatin1(hash.result().toHex());
}

void saveJson(const QString &path, const QJsonObject &object)
{
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot write manifest: " + path);
    require(file.write(bytes) == bytes.size() && file.commit(), "Cannot publish manifest: " + path);
}

QString safeStem(const QString &path)
{
    QString stem = QFileInfo(path).completeBaseName();
    stem.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return stem.left(80).isEmpty() ? QString("image") : stem.left(80);
}

QJsonArray array(const std::array<int, ColorMixer::BandCount> &values)
{
    QJsonArray result;
    for (int value : values)
        result.append(value);
    return result;
}
} // namespace

// The only friend of ImageEditor for offline generation. No editor instance,
// setters, timers, preview proxy or persisted adjustments enter this workflow.
class IprTestImageGenerator
{
public:
    using Settings = ImageEditor::Settings;
    using LoadResult = ImageEditor::LoadResult;

    static Settings neutral(const LoadResult &loaded)
    {
        Settings settings;
        settings.temperature = static_cast<int>(std::lround(
            loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
        return settings;
    }

    static Settings adjusted(Settings settings, const QString &name, double value,
                             int gradingHue, int gradingSaturation, int sharpeningAmount)
    {
        const int integer = static_cast<int>(value);
        if (name == "neutral") { /* identity */ }
        else if (name == "exposure") settings.brightness = static_cast<float>(value);
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
            settings.sharpeningAmount = sharpeningAmount;
            settings.sharpeningRadius = static_cast<float>(value);
        } else if (name == "sharpening-masking") {
            settings.sharpeningAmount = sharpeningAmount;
            settings.sharpeningMasking = integer;
        } else if (name == "auto-wb") settings.automaticWhiteBalance = integer != 0;
        else if (name.startsWith("mixer-")) {
            const QStringList parts = name.split('-');
            const int band = bands.indexOf(parts.at(1));
            const QString component = parts.at(2);
            if (component == "hue") settings.mixerHue.at(band) = integer;
            else if (component == "saturation") settings.mixerSaturation.at(band) = integer;
            else settings.mixerLuminance.at(band) = integer;
        } else if (name.startsWith("grading-")) {
            const QStringList parts = name.split('-');
            const int zone = zones.indexOf(parts.at(1));
            settings.gradingHue.at(zone) = parts.at(2) == "hue" ? integer : gradingHue;
            settings.gradingSaturation.at(zone) = parts.at(2) == "saturation" ? integer : gradingSaturation;
        } else if (name.startsWith("curve-")) {
            const int channel = channels.indexOf(name.mid(6));
            // Diagnostic monotone three-point curve: endpoints fixed, midpoint
            // shifted by up to 0.4. This is not a Lightroom slider equivalent.
            settings.toneCurves.at(channel) = {{0, 0}, {0.5f, float(0.5 + value * 0.004)}, {1, 1}};
        } else require(false, "Unsupported feature: " + name);
        return settings;
    }

    static QJsonObject settingsJson(const Settings &s)
    {
        QJsonObject result{{"exposure", double(s.brightness)}, {"contrast", s.contrast},
            {"blur", s.blur}, {"grayscale", s.grayscale}, {"shadows", s.shadows},
            {"blacks", s.blacks}, {"highlights", s.highlights}, {"whites", s.whites},
            {"temperature", s.temperature}, {"tint", s.tint}, {"vibrance", s.vibrance},
            {"saturation", s.saturation}, {"texture", s.texture}, {"clarity", s.clarity},
            {"dehaze", s.dehaze}, {"grain", s.grain}, {"denoiseLuminance", s.denoiseLuminance},
            {"denoiseChrominance", s.denoiseChrominance}, {"sharpeningAmount", s.sharpeningAmount},
            {"sharpeningRadius", double(s.sharpeningRadius)}, {"sharpeningMasking", s.sharpeningMasking},
            {"automaticWhiteBalance", s.automaticWhiteBalance},
            {"mixerHue", array(s.mixerHue)}, {"mixerSaturation", array(s.mixerSaturation)},
            {"mixerLuminance", array(s.mixerLuminance)}};
        QJsonArray grading, curves;
        for (int zone = 0; zone < 3; ++zone)
            grading.append(QJsonObject{{"hue", s.gradingHue.at(zone)},
                                       {"saturation", s.gradingSaturation.at(zone)}});
        for (const auto &curve : s.toneCurves) {
            QJsonArray points;
            for (const auto &point : curve)
                points.append(QJsonArray{double(point.x), double(point.y)});
            curves.append(points);
        }
        result.insert("grading", grading);
        result.insert("toneCurves", curves);
        return result;
    }

    static LoadResult load(const QString &path, bool autoWb)
    {
        LoadResult result = ImageEditor::loadFromDisk(path, QFileInfo(path).suffix().toLower(), autoWb);
        require(!result.image.isNull(), "Decode failed for " + path + ": " + result.error);
        // Export full resolution; free the unused editing proxy immediately.
        result.editProxy = {};
        return result;
    }

    static void write(const LoadResult &loaded, const Settings &settings,
                      const QString &path, const QString &format, int quality)
    {
        require(!QFileInfo::exists(path), "Output already exists: " + path);
        const auto result = ImageEditor::writeImage(loaded.image, settings, path, format, quality);
        require(result.success, result.message);
        require(QFileInfo::exists(path), "Unexpected export filename: " + result.message);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Match main.cpp: AppDataLocation is part of profile discovery. A separate
    // tool name would silently hide the user's AppData/IPR/IPR/profiles folder.
    QCoreApplication::setOrganizationName("IPR");
    QCoreApplication::setApplicationName("IPR");
    QCoreApplication::setApplicationVersion(IPR_PROJECT_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Generate isolated IPR test exports. Originals and sidecars are never written.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("images", "Input RAW/TIFF/PNG/JPEG files.", "[images...]");
    parser.addOptions({{{"f", "feature"}, "Feature or comma-separated features; repeatable; 'all' selects every feature.", "name", "neutral"},
        {{"i", "input"}, "Input image (repeatable, or use positional paths).", "path"},
        {{"o", "output"}, "New campaign folder (must not already exist).", "folder"},
        {"values", "Override feature defaults; e.g. --values=-50,-25,25,50. Order is preserved.", "numbers"},
        {"format", "TIFF (16-bit sRGB SDR), PNG (16-bit), JPEG (8-bit).", "name", "TIFF"},
        {"quality", "JPEG quality 1..100.", "number", "100"},
        {"grading-hue", "Fixed hue when testing grading saturation, 0..359.", "number", "30"},
        {"grading-saturation", "Fixed saturation when testing grading hue, 1..100.", "number", "25"},
        {"sharpening-base", "Fixed amount for radius/masking tests, 1..200.", "number", "50"},
        {"list", "List features, ranges and default values; no exports."},
        {"dry-run", "Validate inputs/options and print the export plan, without decoding or writing."}});
    parser.process(app);
    QTextStream out(stdout), err(stderr);
    QString manifestPath;
    QJsonObject manifest;
    try {
        const auto catalogue = features();
        if (parser.isSet("list")) {
            for (const auto &feature : catalogue) {
                QStringList values;
                for (double value : feature.defaults) values.append(QString::number(value));
                out << feature.name << " [" << feature.minimum << ", " << feature.maximum
                    << "] defaults=" << values.join(',') << Qt::endl;
            }
            return 0;
        }
        QList<Feature> selected;
        QStringList requested;
        for (const QString &argument : parser.values("feature"))
            requested.append(argument.split(',', Qt::KeepEmptyParts));
        if (requested.contains("all")) {
            require(requested.size() == 1, "Use 'all' alone.");
            for (const auto &feature : catalogue)
                if (feature.name != "neutral") requested.append(feature.name);
            requested.removeFirst();
        }
        requested.removeDuplicates();
        for (const QString &name : requested) {
            bool found = false;
            for (Feature feature : catalogue) {
                if (feature.name != name) continue;
                found = true;
                if (parser.isSet("values")) {
                    feature.defaults.clear();
                    for (const QString &token : parser.value("values").split(',', Qt::KeepEmptyParts)) {
                        bool ok = false;
                        const double value = token.toDouble(&ok);
                        require(ok && std::isfinite(value) && value >= feature.minimum && value <= feature.maximum
                                    && (!feature.integer || std::floor(value) == value),
                                "Invalid value '" + token + "' for " + name);
                        require(!feature.defaults.contains(value), "Duplicate value: " + token);
                        feature.defaults.append(value);
                    }
                }
                selected.append(feature);
            }
            require(found, "Unknown feature: " + name + ". Use --list.");
        }
        auto integerOption = [&](const QString &name, int minimum, int maximum) {
            bool ok = false;
            const int value = parser.value(name).toInt(&ok);
            require(ok && value >= minimum && value <= maximum, "Invalid --" + name);
            return value;
        };
        const int quality = integerOption("quality", 1, 100);
        const int gradingHue = integerOption("grading-hue", 0, 359);
        const int gradingSaturation = integerOption("grading-saturation", 1, 100);
        const int sharpeningAmount = integerOption("sharpening-base", 1, 200);
        const QString format = parser.value("format").toUpper();
        require(QStringList{"TIFF", "PNG", "JPEG"}.contains(format), "Unsupported --format; use TIFF, PNG or JPEG.");
        const QString suffix = format == "TIFF" ? "tiff" : format == "JPEG" ? "jpg" : "png";
        QStringList inputs = parser.values("input") + parser.positionalArguments();
        require(!inputs.isEmpty(), "Provide at least one input image.");
        QStringList extensions = RawDecoder::supportedExtensions();
        extensions.append(QStringList{"tif", "tiff", "png", "jpg", "jpeg", "bmp", "webp"});
        for (QString &input : inputs) {
            QFileInfo file(input);
            require(file.isFile() && file.isReadable(), "Invalid input file: " + input);
            require(extensions.contains(file.suffix().toLower()), "Unsupported input extension: " + input);
            input = file.canonicalFilePath();
            require(!input.isEmpty(), "Cannot resolve input: " + file.filePath());
            if (requested.contains("auto-wb"))
                require(RawDecoder::supportedExtensions().contains(file.suffix().toLower()),
                        "auto-wb requires a RAW input: " + input);
        }
        inputs.removeDuplicates();
        require(parser.isSet("output") && !parser.value("output").isEmpty(), "Provide --output with a new campaign folder.");
        const QString output = QDir::cleanPath(QFileInfo(parser.value("output")).absoluteFilePath());
        require(!QFileInfo::exists(output), "Campaign folder already exists; choose a new --output: " + output);
        qsizetype perImage = 1; // mandatory baseline, regardless of selected values
        QJsonArray plan;
        for (const auto &feature : selected) {
            if (feature.name == "neutral") continue;
            QJsonArray values;
            for (double value : feature.defaults) values.append(value);
            perImage += feature.defaults.size();
            plan.append(QJsonObject{{"feature", feature.name}, {"values", values}});
        }
        out << inputs.size() << " input(s), " << perImage << " export(s) per input, "
            << inputs.size() * perImage << " total. " << format << " -> " << output << Qt::endl;
        out << QJsonDocument(plan).toJson(QJsonDocument::Compact) << Qt::endl;
        if (parser.isSet("dry-run")) return 0;

        QFileInfo outputInfo(output);
        QDir parent(outputInfo.absolutePath());
        require(QDir().mkpath(parent.absolutePath()) && parent.mkdir(outputInfo.fileName()),
                "Cannot create new campaign folder: " + output);
        manifestPath = QDir(output).filePath("manifest.json");
        manifest = {{"schema", 1}, {"status", "running"},
            {"startedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"iprVersion", QCoreApplication::applicationVersion()}, {"qtVersion", qVersion()},
            {"executableSha256", hashFile(QCoreApplication::applicationFilePath())},
            {"arguments", QJsonArray::fromStringList(app.arguments().mid(1))},
            {"format", format}, {"quality", quality}, {"outputColorSpace", "sRGB"},
            {"bitDepth", format == "JPEG" ? 8 : 16}, {"hdr", false},
            {"profileSearchDirectories", QJsonArray::fromStringList(CameraProfileEngine::defaultProfileDirectories())},
            {"mixerBands", QJsonArray::fromStringList(bands)},
            {"gradingZones", QJsonArray::fromStringList(zones)},
            {"curveChannels", QJsonArray::fromStringList(channels)},
            {"sidecars", "ignored; never written"}, {"plan", plan},
            {"expectedExports", double(inputs.size() * perImage)}, {"exports", QJsonArray{}}};
        saveJson(manifestPath, manifest);
        QJsonArray exports;
        for (qsizetype inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
            const QString &input = inputs.at(inputIndex);
            out << "Decoding " << input << Qt::endl;
            const QString sourceHash = hashFile(input);
            auto loaded = IprTestImageGenerator::load(input, false);
            const QString stem = safeStem(input);
            const QString directory = QString("%1-%2").arg(inputIndex + 1, 3, 10, QChar('0')).arg(stem);
            int sequence = 0;
            auto exportVariant = [&](const IprTestImageGenerator::LoadResult &source,
                                     const QString &feature, double value) {
                const auto settings = IprTestImageGenerator::adjusted(
                    IprTestImageGenerator::neutral(source), feature, value,
                    gradingHue, gradingSaturation, sharpeningAmount);
                const QString folder = QDir(output).filePath(directory + '/' + feature);
                require(QDir().mkpath(folder), "Cannot create: " + folder);
                const QString relative = directory + '/' + feature + '/' + stem + "-IPR-"
                    + QString::number(++sequence) + '.' + suffix;
                const QString path = QDir(output).filePath(relative);
                out << relative << " value=" << value << Qt::endl;
                IprTestImageGenerator::write(source, settings, path, format, quality);
                exports.append(QJsonObject{{"source", input}, {"sourceSha256", sourceHash},
                    {"file", relative}, {"sha256", hashFile(path)}, {"feature", feature}, {"value", value},
                    {"width", source.image.linearRgba.cols}, {"height", source.image.linearRgba.rows},
                    {"referenceTemperatureKelvin", source.image.referenceWhiteBalanceKelvin.value_or(5200.0)},
                    {"sourceProfile", source.image.sourceProfile}, {"dcpProfile", source.metadata.dcpProfile},
                    {"lcpProfile", source.metadata.lcpProfile}, {"cameraMake", source.metadata.cameraMake},
                    {"cameraModel", source.metadata.cameraModel}, {"iso", source.metadata.iso},
                    {"settings", IprTestImageGenerator::settingsJson(settings)}});
                manifest.insert("exports", exports);
                saveJson(manifestPath, manifest);
            };
            exportVariant(loaded, "neutral", 0);
            for (const auto &feature : selected) {
                if (feature.name == "neutral") continue;
                for (double value : feature.defaults) {
                    if (feature.name == "auto-wb" && value == 1) {
                        // This switch changes decoding, not just a post-render flag.
                        auto automatic = IprTestImageGenerator::load(input, true);
                        exportVariant(automatic, feature.name, value);
                    } else exportVariant(loaded, feature.name, value);
                }
            }
            require(hashFile(input) == sourceHash, "Input changed during generation: " + input);
        }
        manifest.insert("status", "complete");
        manifest.insert("finishedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        saveJson(manifestPath, manifest);
        out << "Complete: " << exports.size() << " exports. " << manifestPath << Qt::endl;
        return 0;
    } catch (const std::exception &exception) {
        err << "Generation failed: " << exception.what() << Qt::endl;
        if (!manifestPath.isEmpty() && !manifest.isEmpty()) {
            manifest.insert("status", "failed");
            manifest.insert("error", QString::fromUtf8(exception.what()));
            try { saveJson(manifestPath, manifest); }
            catch (const std::exception &failure) { err << failure.what() << Qt::endl; }
        }
        return 1;
    }
}
