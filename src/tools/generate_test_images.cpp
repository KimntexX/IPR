#include "imageeditor.h"
#include "SystemMemory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTextStream>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <tiffio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
struct Feature {
    QString name;
    QList<double> values;
};

const QStringList bands{"red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta"};
const QStringList components{"hue", "saturation", "luminance"};
const QStringList zones{"shadows", "midtones", "highlights"};
const QStringList channels{"luminance", "red", "green", "blue"};
const QStringList groups{"raw", "lumiere", "courbes", "couleur", "melangeur",
                         "grading", "effets", "bruit", "nettete", "optique"};

void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
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

// Prefer the working tree from which the tool was invoked; fall back to its build.
QString findBase()
{
    for (const QString &start : QStringList{QDir::currentPath(), QCoreApplication::applicationDirPath()}) {
        QDir directory(start);
        do {
            const QFileInfo base(directory.filePath("tests/image/base"));
            if (base.isDir()) return base.canonicalFilePath();
        } while (directory.cdUp());
    }
    require(false, "Cannot find tests/image/base. Run from the project or its build directory.");
    return {};
}

bool rawOnly(const QString &name)
{
    return name == "auto-wb" || featureGroup(name) == "optique";
}

// Resolve the nearest existing parent before creating a missing subdirectory.
// Refuse redirected folders outside base, including Windows junctions.
void checkFolder(const QString &base, const QString &folder)
{
    QFileInfo existing(folder);
    while (!existing.exists() && !existing.isSymLink()) existing.setFile(existing.absolutePath());
    const QString resolved = existing.canonicalFilePath();
    require(existing.isDir() && (resolved == base || resolved.startsWith(base + '/')),
            "Output folder is not inside base: " + folder);
}
} // namespace

// The same full-resolution rendering/export path as the IHM, without restoring
// or writing image sidecars and preferences.
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

    static Settings adjusted(Settings settings, const QString &name, double value)
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

    // Same RAW recipe as the IHM default: Camera Neutral and the RAW calibration
    // (ADR-0007). Optics stay off outside the optique series, like the Lightroom
    // references, until the complete validation enables them by default.
    static LoadResult load(const QString &path, bool autoWb = false,
                           bool lensCorrection = false, bool chromaticAberration = false)
    {
        RawDecoder::Options options;
        options.preferCameraNeutral = true;
        options.useNormalizationEnvironment = false;
        options.normalizationManifest = ImageEditor::defaultCalibrationManifest();
        options.lensCorrectionEnabled = lensCorrection;
        options.chromaticAberrationEnabled = chromaticAberration;
        LoadResult result = ImageEditor::loadFromDisk(path, autoWb, options);
        require(!result.image.isNull(), "Decode failed for " + path + ": " + result.error);
        result.editProxy = {};
        return result;
    }

    static QJsonObject declaredSettings(const Settings &s)
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

    static QString digest(const QString &path)
    {
        static QHash<QString, QString> hashes;
        static std::mutex lock;
        const std::lock_guard<std::mutex> guard(lock);
        if (!hashes.contains(path)) {
            QFile file(path);
            require(file.open(QIODevice::ReadOnly), "Cannot hash source: " + path);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            require(hash.addData(&file), "Cannot hash source: " + path);
            hashes.insert(path, QString::fromLatin1(hash.result().toHex()));
        }
        return hashes.value(path);
    }

    static void write(const LoadResult &source, const Settings &settings, const QString &path,
                      const QString &input, const QString &group, const QString &feature, double value,
                      const QString &reference)
    {
        require(!QFileInfo::exists(path) && !QFileInfo(path).isSymLink(), "Output already exists: " + path);
        QStorageInfo disk(QFileInfo(path).absolutePath());
        const qint64 needed = qint64(source.image.linearRgba.cols) * source.image.linearRgba.rows * 6
            + 512LL * 1024 * 1024;
        require(!disk.isValid() || disk.bytesAvailable() < 0 || disk.bytesAvailable() >= needed,
                "Not enough disk space (512 MiB reserve); stopped without deleting files.");
        const auto result = ImageEditor::writeImage(source.image, settings, path, "TIFF", 100);
        require(result.success, result.message);
        require(QFileInfo::exists(path), "Unexpected export filename: " + result.message);
        // Provenance lives inside the image, never in a companion file.
        const QJsonObject metadata{{"ipr_test_schema", 1}, {"scene", QFileInfo(input).completeBaseName()},
            {"group", group}, {"feature", feature}, {"value", value},
            {"reference", QDir(findBase()).relativeFilePath(reference)},
            {"settings", declaredSettings(settings)},
            {"source_sha256", digest(input)}, {"executable_sha256", digest(QCoreApplication::applicationFilePath())},
            {"qt_version", qVersion()}, {"created_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"dcp_profile", source.metadata.dcpProfile}, {"lcp_profile", source.metadata.lcpProfile},
            {"lcp_sha256", source.metadata.lcpSha256}, {"raw_normalization", source.metadata.rawNormalizationApplied},
            {"lens_geometry_applied", source.metadata.lensGeometryApplied},
            {"lens_vignetting_applied", source.metadata.lensVignettingApplied},
            {"chromatic_aberration_applied", source.metadata.chromaticAberrationApplied}};
        const QByteArray payload = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
        TIFF *tiff = TIFFOpen(QFile::encodeName(path).constData(), "r+");
        require(tiff != nullptr, "Cannot embed TIFF provenance: " + path);
        const bool tagged = TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, payload.constData()) == 1
            && TIFFRewriteDirectory(tiff) == 1;
        TIFFClose(tiff);
        require(tagged, "Cannot embed TIFF provenance: " + path);
    }

    // Variants of one decoded source are independent renders of the same
    // read-only master: they run in parallel. One render of a 32 MP CR3 was
    // measured at about 1.0 GiB (31 B/px: float working copy, split channels,
    // 16-bit output) on top of the shared master; 36 B/px keeps ~15 % above.
    // The worker count follows the free memory, keeping 2 GiB for the system,
    // and stays at most 4 (writes of ~250 MB TIFF files).
    static int workerCount(quint64 pixels, quint64 *freeBytes = nullptr)
    {
        const quint64 perRender = pixels * 36 + 128ULL * 1024 * 1024;
        const quint64 reserve = 2ULL * 1024 * 1024 * 1024;
        const auto memory = SystemMemory::read();
        const quint64 free = memory.valid && memory.availableBytes > reserve ? memory.availableBytes - reserve : 0;
        if (freeBytes) *freeBytes = memory.valid ? memory.availableBytes : 0;
        const int byMemory = int(std::min<quint64>(free / std::max<quint64>(perRender, 1), 4));
        const int byCores = std::max(1, int(std::thread::hardware_concurrency()) / 2);
        return std::clamp(std::min(byMemory, byCores), 1, 4);
    }

    // Runs jobs on `workers` threads; the first failure stops new jobs and is
    // rethrown once running ones have finished (no file is deleted).
    static void runParallel(const std::vector<std::function<void()>> &jobs, int workers)
    {
        std::atomic<size_t> next{0};
        std::atomic<bool> failed{false};
        std::exception_ptr error;
        std::mutex errorLock;
        const auto work = [&] {
            for (size_t i = 0; !failed && (i = next++) < jobs.size();) {
                try { jobs[i](); }
                catch (...) {
                    const std::lock_guard<std::mutex> guard(errorLock);
                    if (!error) error = std::current_exception();
                    failed = true;
                }
            }
        };
        std::vector<std::thread> threads;
        for (int t = 1; t < workers; ++t) threads.emplace_back(work);
        work();
        for (auto &thread : threads) thread.join();
        if (error) std::rethrow_exception(error);
    }
};

#ifndef IPR_GENERATOR_UNIT_TEST
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("IPR");
    QCoreApplication::setApplicationName("IPR");
    QTextStream out(stdout), err(stderr);
    try {
        require(app.arguments().size() == 2 && (app.arguments().at(1) == "all"
                    || groups.contains(app.arguments().at(1))),
                "Usage: iprGenerateTests all|" + groups.join('|'));
        const QString group = app.arguments().at(1);
        const QString base = findBase();
        QStringList extensions = RawDecoder::supportedExtensions();
        extensions.append(QStringList{"tif", "tiff", "png", "jpg", "jpeg", "bmp", "webp"});
        QFileInfoList inputs;
        for (const QFileInfo &file : QDir(base).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name))
            if (extensions.contains(file.suffix().toLower())) inputs.append(file);
        require(!inputs.isEmpty(), "No supported images at the root of " + base);
        QList<Feature> selected;
        for (const Feature &feature : features())
            if (group == "all" || featureGroup(feature.name) == group) selected.append(feature);

        // Preflight all destinations before writing. Existing IPR results must be
        // deleted manually, not overwritten or renamed with accumulating suffixes.
        QStringList stems;
        qsizetype total = 0;
        for (const QFileInfo &input : inputs) {
            const QString stem = input.completeBaseName();
            require(!stems.contains(stem.toCaseFolded()), "Ambiguous source name at base root: " + stem);
            stems.append(stem.toCaseFolded());
            const QString neutralPath = QDir(base).filePath("neutre/" + stem + "-IPR-neutre-" + group + ".tiff");
            checkFolder(base, QFileInfo(neutralPath).absolutePath());
            require(!QFileInfo::exists(neutralPath) && !QFileInfo(neutralPath).isSymLink(),
                    "Remove the previous IPR neutral manually before regenerating: " + neutralPath);
            for (const Feature &feature : selected) {
                if (rawOnly(feature.name) && !RawDecoder::supportedExtensions().contains(input.suffix().toLower())) continue;
                const QString folder = QDir(base).filePath(featureFolder(feature.name));
                checkFolder(base, folder);
                for (double value : feature.values) {
                    const QString path = QDir(folder).filePath(fileName(stem, feature.name, value));
                    require(!QFileInfo::exists(path) && !QFileInfo(path).isSymLink(),
                            "Remove the previous IPR export manually before regenerating: " + path);
                    ++total;
                }
            }
        }
        require(total > 0, "No eligible variants for this group; raw/optique require RAW sources.");
        // Offline tool: one extra decode per source buys a whole-run estimate,
        // without retaining a bank of decoded images or writing a manifest.
        qint64 estimate = 512LL * 1024 * 1024;
        for (const QFileInfo &input : inputs) {
            const auto probe = IprTestImageGenerator::load(input.absoluteFilePath());
            qint64 count = 1; // neutral
            for (const Feature &feature : selected)
                if (!rawOnly(feature.name) || RawDecoder::supportedExtensions().contains(input.suffix().toLower()))
                    count += feature.values.size();
            estimate += count * (qint64(probe.image.linearRgba.cols) * probe.image.linearRgba.rows * 7 + 65536);
        }
        out << "Estimated storage incl. safety margin: " << estimate / (1024 * 1024)
            << " MiB (optical geometry/metadata can vary; checked again per image)." << Qt::endl;
        QStorageInfo available(base);
        require(!available.isValid() || available.bytesAvailable() < 0 || available.bytesAvailable() >= estimate,
                "Not enough space for estimated complete run; no output written.");
        out << "Base: " << base << "\nGroup: " << group << "\n"
            << inputs.size() << " source(s), " << total << " variant(s), TIFF 16-bit sRGB SDR.\n";
        std::atomic<qsizetype> completed{0};
        std::mutex outputLock;
        for (const QFileInfo &input : inputs) {
            out << "Decoding " << input.fileName() << Qt::endl;
            auto loaded = IprTestImageGenerator::load(input.absoluteFilePath());
            out << "DCP: " << loaded.metadata.dcpProfile << "\nLCP: " << loaded.metadata.lcpProfile << Qt::endl;
            const auto neutral = IprTestImageGenerator::neutral(loaded);
            const QString neutralPath = QDir(base).filePath("neutre/" + input.completeBaseName()
                + "-IPR-neutre-" + group + ".tiff");
            require(QDir().mkpath(QFileInfo(neutralPath).absolutePath()), "Cannot create neutre folder.");
            IprTestImageGenerator::write(loaded, neutral, neutralPath, input.absoluteFilePath(),
                group, "neutral", 0, neutralPath);
            std::vector<std::function<void()>> parallel, sequential;
            for (const Feature &feature : selected) {
                if (rawOnly(feature.name) && !RawDecoder::supportedExtensions().contains(input.suffix().toLower())) {
                    out << "Skipped " << feature.name << " (RAW input required)." << Qt::endl;
                    continue;
                }
                const QString folder = QDir(base).filePath(featureFolder(feature.name));
                require(QDir().mkpath(folder), "Cannot create " + folder);
                for (double value : feature.values) {
                    const QString path = QDir(folder).filePath(fileName(input.completeBaseName(), feature.name, value));
                    const auto report = [&out, &outputLock, &completed, total, path] {
                        const std::lock_guard<std::mutex> guard(outputLock);
                        out << '[' << ++completed << '/' << total << "] " << path << Qt::endl;
                    };
                    const bool needsDecode = (feature.name == "auto-wb" && value == 1)
                        || featureGroup(feature.name) == "optique";
                    auto support = neutral;
                    // Both manual WB axes use the fixed 5200 K / tint 0 anchor.
                    if (feature.name == "temperature" || feature.name == "tint") support.temperature = 5200;
                    QString reference = neutralPath;
                    if (feature.name == "temperature" || feature.name == "tint")
                        reference = QDir(folder).filePath(fileName(input.completeBaseName(), feature.name,
                            feature.name == "temperature" ? 5200 : 0));
                    if (feature.name == "sharpening-radius" || feature.name == "sharpening-masking"
                        || feature.name.startsWith("grading-") || featureGroup(feature.name) == "optique")
                        reference = QDir(folder).filePath(fileName(input.completeBaseName(), feature.name,
                            feature.name == "sharpening-radius" ? 1 : 0));
                    const QString inputPath = input.absoluteFilePath();
                    const QString name = feature.name;
                    // A variant that needs its own decode holds a second master:
                    // those run one at a time, after the parallel batch.
                    if (needsDecode) sequential.push_back([=, &out, &outputLock] {
                        report();
                        auto source = IprTestImageGenerator::load(inputPath, name == "auto-wb",
                            name == "lens-correction" && value == 1, name == "chromatic-aberration" && value == 1);
                        IprTestImageGenerator::write(source,
                            IprTestImageGenerator::adjusted(IprTestImageGenerator::neutral(source), name, value), path,
                            inputPath, featureGroup(name), name, value, reference);
                        if (featureGroup(name) == "optique") {
                            const std::lock_guard<std::mutex> guard(outputLock);
                            out << "Optical status: geometry=" << source.metadata.lensGeometryApplied
                                << ", vignetting=" << source.metadata.lensVignettingApplied
                                << ", CA=" << source.metadata.chromaticAberrationApplied << Qt::endl;
                        }
                    });
                    else parallel.push_back([=, &loaded] {
                        report();
                        IprTestImageGenerator::write(loaded, IprTestImageGenerator::adjusted(support, name, value), path,
                            inputPath, featureGroup(name), name, value, reference);
                    });
                }
            }
            quint64 freeBytes = 0;
            const int workers = IprTestImageGenerator::workerCount(quint64(loaded.image.linearRgba.total()), &freeBytes);
            out << "Parallel renders: " << workers << " (free memory " << freeBytes / (1024 * 1024)
                << " MiB, 2 GiB reserve)" << Qt::endl;
            IprTestImageGenerator::runParallel(parallel, workers);
            IprTestImageGenerator::runParallel(sequential, 1);
        }
        out << "Complete. Only TIFF images written; remove them manually after analysis." << Qt::endl;
        return 0;
    } catch (const std::exception &error) {
        err << "Generation failed: " << error.what() << Qt::endl;
        return 1;
    }
}
#endif
