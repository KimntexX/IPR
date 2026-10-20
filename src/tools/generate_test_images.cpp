#include "TestRecipes.h"
#include "SystemMemory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStorageInfo>
#include <QTextStream>
#include <tiffio.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

// TIFF exports of the test states, for visual review beside the Lightroom
// references. Measurements read the same states in memory (iprRenderServer).
namespace {
using namespace TestRecipes;

void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
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

// The same full-resolution rendering/export path as the IHM, without restoring
// or writing image sidecars and preferences.
void write(const LoadResult &source, const Settings &settings, const QString &path, const QString &input,
           const QString &group, const QString &feature, double value, const QString &reference)
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
    const QByteArray payload = QJsonDocument(declaration(source, settings, input, group, feature, value,
        QDir(findBase()).relativeFilePath(reference))).toJson(QJsonDocument::Compact);
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
int workerCount(quint64 pixels, quint64 *freeBytes)
{
    const quint64 perRender = pixels * 36 + 128ULL * 1024 * 1024;
    const quint64 reserve = 2ULL * 1024 * 1024 * 1024;
    const auto memory = SystemMemory::read();
    const quint64 free = memory.valid && memory.availableBytes > reserve ? memory.availableBytes - reserve : 0;
    *freeBytes = memory.valid ? memory.availableBytes : 0;
    const int byMemory = int(std::min<quint64>(free / std::max<quint64>(perRender, 1), 4));
    const int byCores = std::max(1, int(std::thread::hardware_concurrency()) / 2);
    return std::clamp(std::min(byMemory, byCores), 1, 4);
}

// Runs jobs on `workers` threads; the first failure stops new jobs and is
// rethrown once running ones have finished (no file is deleted).
void runParallel(const std::vector<std::function<void()>> &jobs, int workers)
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
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("IPR");
    QCoreApplication::setApplicationName("IPR");
    QTextStream out(stdout), err(stderr);
    try {
        require(app.arguments().size() == 2 && (app.arguments().at(1) == "all"
                    || groups().contains(app.arguments().at(1))),
                "Usage: iprGenerateTests all|" + groups().join('|'));
        const QString group = app.arguments().at(1);
        const QString base = findBase();
        const QFileInfoList inputs = sources(base);
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
                if (!eligible(feature.name, input.filePath())) continue;
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
            const auto probe = load(input.absoluteFilePath());
            qint64 count = 1; // neutral
            for (const Feature &feature : selected)
                if (eligible(feature.name, input.filePath())) count += feature.values.size();
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
            const QString inputPath = input.absoluteFilePath();
            const auto loaded = load(inputPath);
            out << "DCP: " << loaded.metadata.dcpProfile << "\nLCP: " << loaded.metadata.lcpProfile << Qt::endl;
            const QString neutralPath = QDir(base).filePath("neutre/" + input.completeBaseName()
                + "-IPR-neutre-" + group + ".tiff");
            require(QDir().mkpath(QFileInfo(neutralPath).absolutePath()), "Cannot create neutre folder.");
            write(loaded, settingsFor(loaded, "neutral", 0), neutralPath, inputPath, group, "neutral", 0, neutralPath);
            std::vector<std::function<void()>> parallel, sequential;
            for (const Feature &feature : selected) {
                if (!eligible(feature.name, inputPath)) {
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
                    const Reference ref = referenceFor(feature.name);
                    const QString reference = ref.feature == "neutral" ? neutralPath
                        : QDir(folder).filePath(fileName(input.completeBaseName(), ref.feature, ref.value));
                    const QString name = feature.name;
                    const Decode decode = decodeFor(name, value);
                    // A variant that needs its own decode holds a second master:
                    // those run one at a time, after the parallel batch.
                    if (!decode.isDefault()) sequential.push_back([=, &out, &outputLock] {
                        report();
                        const auto source = load(inputPath, decode);
                        write(source, settingsFor(source, name, value), path, inputPath, featureGroup(name), name,
                              value, reference);
                        if (featureGroup(name) == "optique") {
                            const std::lock_guard<std::mutex> guard(outputLock);
                            out << "Optical status: geometry=" << source.metadata.lensGeometryApplied
                                << ", vignetting=" << source.metadata.lensVignettingApplied
                                << ", CA=" << source.metadata.chromaticAberrationApplied << Qt::endl;
                        }
                    });
                    else parallel.push_back([=, &loaded] {
                        report();
                        write(loaded, settingsFor(loaded, name, value), path, inputPath, featureGroup(name), name,
                              value, reference);
                    });
                }
            }
            quint64 freeBytes = 0;
            const int workers = workerCount(quint64(loaded.image.linearRgba.total()), &freeBytes);
            out << "Parallel renders: " << workers << " (free memory " << freeBytes / (1024 * 1024)
                << " MiB, 2 GiB reserve)" << Qt::endl;
            runParallel(parallel, workers);
            runParallel(sequential, 1);
        }
        out << "Complete. Only TIFF images written; remove them manually after analysis." << Qt::endl;
        return 0;
    } catch (const std::exception &error) {
        err << "Generation failed: " << error.what() << Qt::endl;
        return 1;
    }
}
