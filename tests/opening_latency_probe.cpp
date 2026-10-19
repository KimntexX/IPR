// Manual measurement only: no changes to rendering, no corpus sidecar writes.
#include "imageeditor.h"
#include "imageprovider.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <cmath>
#include <functional>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // LibRaw includes winsock2.h afterwards.
#endif
#include <windows.h>
#include <psapi.h>
#endif

static QString digest(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex());
}
static double elapsed(const QElapsedTimer &t) { return t.nsecsElapsed() / 1.e6; }
static QJsonObject profiles()
{
    const auto s = CameraProfileEngine::preparedProfileCacheStats();
    return {{"hits", double(s.hits)}, {"misses", double(s.misses)},
            {"preparations", double(s.preparations)}, {"entries", s.entries},
            {"retained_bytes", double(s.retainedBytes)}};
}

// Process counters, not the controller's accounting: peaks include decoder,
// profile and allocator memory. Unavailable outside Windows in this probe.
static QJsonObject processMemory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c)))
        return {{"working_set_bytes", double(c.WorkingSetSize)}, {"peak_working_set_bytes", double(c.PeakWorkingSetSize)},
                {"private_bytes", double(c.PrivateUsage)}, {"peak_commit_bytes", double(c.PeakPagefileUsage)}};
#endif
    return {{"available", false}};
}

#ifdef IPR_WITH_LIBRAW
#include <libraw/libraw.h>
#include <QBuffer>
#include <QImageReader>
#include <algorithm>
#include <memory>
// Embedded previews, measured outside production code. Each operation opens
// a fresh LibRaw instance, as the thumbnail provider does.
static QImage decodeProcessed(libraw_processed_image_t *processed)
{
    QImage image;
    if (processed && processed->type == LIBRAW_IMAGE_JPEG) {
        QBuffer buffer;
        buffer.setData(QByteArray(reinterpret_cast<const char *>(processed->data), int(processed->data_size)));
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        image = reader.read();
    } else if (processed && processed->type == LIBRAW_IMAGE_BITMAP && processed->bits == 8 && processed->colors == 3) {
        image = QImage(processed->data, processed->width, processed->height,
                       int(processed->width) * 3, QImage::Format_RGB888).copy();
    }
    if (processed) LibRaw::dcraw_clear_mem(processed);
    return image;
}
static QImage embedded(const QString &file, int index, const QSize &target, double *ms)
{
    QElapsedTimer t; t.start();
    auto owned = std::make_unique<LibRaw>(); // Too large for the main thread stack.
    auto &raw = *owned;
    QImage image;
    if (raw.open_file(file.toLocal8Bit().constData()) == LIBRAW_SUCCESS &&
        (index < 0 ? raw.unpack_thumb() : raw.unpack_thumb_ex(index)) == LIBRAW_SUCCESS) {
        int result = 0;
        image = decodeProcessed(raw.dcraw_make_mem_thumb(&result));
        if (!image.isNull() && target.isValid())
            image = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    *ms = elapsed(t);
    return image;
}
static double median(QList<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static QJsonObject previews(const QStringList &files)
{
    const QSize ribbon(116, 49);
    QJsonArray scenes;
    for (const auto &file : files) {
        QList<double> openMs, currentMs, ribbonMs, displayMs;
        QJsonArray list;
        int ribbonIndex = -1, displayIndex = -1;
        QSize fullSize;
        {
            QElapsedTimer t; t.start();
            auto owned = std::make_unique<LibRaw>();
            auto &raw = *owned;
            if (raw.open_file(file.toLocal8Bit().constData()) != LIBRAW_SUCCESS)
                return {{"error", "Cannot open " + file}};
            openMs.append(elapsed(t));
            fullSize = QSize(raw.imgdata.sizes.width, raw.imgdata.sizes.height);
            const auto &thumbs = raw.imgdata.thumbs_list;
            int ribbonArea = 0, displayArea = 0;
            for (int i = 0; i < thumbs.thumbcount; ++i) {
                const auto &item = thumbs.thumblist[i];
                const int area = int(item.twidth) * item.theight;
                list.append(QJsonObject{{"index", i}, {"width", item.twidth}, {"height", item.theight},
                    {"format", int(item.tformat)}, {"bytes", double(item.tlength)}});
                const int longSide = std::max<int>(item.twidth, item.theight);
                if (item.twidth >= ribbon.width() && item.theight >= ribbon.height() &&
                    (ribbonIndex < 0 || area < ribbonArea)) { ribbonIndex = i; ribbonArea = area; }
                if (longSide >= 1024 && (displayIndex < 0 || area < displayArea)) { displayIndex = i; displayArea = area; }
            }
        }
        QSize displaySize;
        for (int run = 0; run < 3; ++run) {
            double ms = 0;
            QElapsedTimer t; t.start();
            auto owned = std::make_unique<LibRaw>();
            owned->open_file(file.toLocal8Bit().constData());
            if (run) openMs.append(elapsed(t));
            RawDecoder::decodeEmbeddedThumbnail(file, ribbon);
            currentMs.append(0);
            QElapsedTimer c; c.start();
            const auto current = RawDecoder::decodeEmbeddedThumbnail(file, ribbon);
            currentMs.last() = elapsed(c);
            if (current.isNull()) return {{"error", "Production thumbnail failed: " + file}};
            if (ribbonIndex >= 0) { embedded(file, ribbonIndex, ribbon, &ms); ribbonMs.append(ms); }
            if (displayIndex >= 0) {
                displaySize = embedded(file, displayIndex, {}, &ms).size();
                displayMs.append(ms);
            }
        }
        QJsonObject scene{{"file", QFileInfo(file).fileName()}, {"sha256", digest(file)},
            {"raw_width", fullSize.width()}, {"raw_height", fullSize.height()},
            {"open_metadata_ms_median", median(openMs)},
            {"current_ribbon_thumbnail_ms_median", median(currentMs)},
            {"embedded_previews", list}, {"ribbon_index", ribbonIndex}, {"display_index", displayIndex}};
        if (!ribbonMs.isEmpty()) scene["smallest_ribbon_preview_ms_median"] = median(ribbonMs);
        if (!displayMs.isEmpty()) {
            scene["display_preview_ms_median"] = median(displayMs);
            scene["display_preview_width"] = displaySize.width();
            scene["display_preview_height"] = displaySize.height();
        }
        scenes.append(scene);
    }
    return {{"runs_per_scene", 3}, {"ribbon_target", "116x49"}, {"scenes", scenes}};
}
#endif

class IprTestImageGenerator
{
public:
    static QJsonObject stages(const QString &path, const QString &dcp, const QString &manifest)
    {
        RawDecoder::Options options;
        options.automaticWhiteBalance = false;
        options.dcpProfileOverride = dcp;
        options.normalizationManifest = manifest;
        options.useNormalizationEnvironment = false;
        options.preferCameraNeutral = true;
        options.listDcpProfiles = true;
        options.automaticLcpSelection = false;
        options.lensCorrectionEnabled = false;
        options.chromaticAberrationEnabled = false;
        options.retainedDcpSource = std::make_shared<RawDecoder::DcpSource>();
        QJsonArray runs;
        for (int i = 0; i < 3; ++i) {
            QElapsedTimer t;
            t.start();
            auto loaded = ImageImport::load(path, options);
            const double importMs = elapsed(t);
            if (loaded.image.isNull()) return {{"error", loaded.error}};
            t.restart();
            auto proxy = ImageEditor::buildEditProxy(loaded.image);
            const double proxyMs = elapsed(t);
            ImageEditor::Settings settings;
            settings.temperature = int(std::lround(loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
            t.restart();
            const auto rendered = ImageEditor::renderPreview(proxy, settings, true);
            const double previewMs = elapsed(t);
            t.restart();
            auto check = loaded;
            ImageImport::complete(check);
            const double validationMs = elapsed(t);
            const auto bytes = DecodedImageCache::entrySizeBytes({loaded.image, proxy, loaded.metadata});
            runs.append(QJsonObject{{"run", i + 1}, {"import_ms", importMs},
                {"proxy_ms", proxyMs}, {"preview_pair_histogram_ms", previewMs},
                {"cache_hit_validation_ms", validationMs}, {"entry_bytes", double(bytes)},
                {"width", loaded.image.linearRgba.cols}, {"height", loaded.image.linearRgba.rows},
                {"preview_valid", !rendered.preview.isNull()},
                {"camera", loaded.metadata.cameraModel}, {"normalization_applied", loaded.metadata.rawNormalizationApplied},
                {"dcp", loaded.metadata.dcpProfile}, {"prepared_cache", profiles()},
                {"estimated_decode_peak_bytes", double(SystemMemory::estimateDecodePeak(path))},
                {"process", processMemory()}});
        }
        return {{"runs", runs}};
    }

    // Diagnostic render with the generator's decode (Camera Neutral, optics on)
    // at one exposure value; the output path must stay outside the corpus.
    // An optional manifest applies the RAW calibration the IHM enables by default.
    static QJsonObject render(const QString &raw, double exposureEv, const QString &out,
                              const QString &manifest = {}, bool optics = true)
    {
        RawDecoder::Options options;
        options.preferCameraNeutral = true;
        options.useNormalizationEnvironment = false;
        options.normalizationManifest = manifest;
        options.lensCorrectionEnabled = optics;
        options.chromaticAberrationEnabled = optics;
        auto loaded = ImageEditor::loadFromDisk(raw, false, options);
        if (loaded.image.isNull()) return {{"error", loaded.error}};
        ImageEditor::Settings settings;
        settings.temperature = int(std::lround(loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
        settings.brightness = float(exposureEv);
        const auto written = ImageEditor::writeImage(loaded.image, settings, out, "TIFF", 100);
        return {{"output", out}, {"exposure", exposureEv}, {"success", written.success},
                {"message", written.message}, {"dcp", loaded.metadata.dcpProfile},
                {"raw_normalization", loaded.metadata.rawNormalizationApplied}};
    }

    // Equivalence check for refactors: renders every settings set of a JSON
    // file (keys of ImageEditor::Settings; arrays for mixer/grading; tone
    // curves as [[x, y], ...] per channel) on the full master and the proxy
    // preview, and reports SHA-256 digests of the pixels and the histogram.
    // Same digests before and after a refactor prove identical output.
    static QJsonObject equivalence(const QString &raw, const QString &settingsPath, const QString &manifest)
    {
        QFile file(settingsPath);
        if (!file.open(QIODevice::ReadOnly)) return {{"error", "cannot read " + settingsPath}};
        const QJsonObject sets = QJsonDocument::fromJson(file.readAll()).object();
        RawDecoder::Options options;
        options.preferCameraNeutral = true;
        options.useNormalizationEnvironment = false;
        options.normalizationManifest = manifest;
        options.lensCorrectionEnabled = false;
        options.chromaticAberrationEnabled = false;
        const auto loaded = ImageEditor::loadFromDisk(raw, false, options);
        if (loaded.image.isNull()) return {{"error", loaded.error}};
        const auto digest = [](const cv::Mat &m) {
            QCryptographicHash h(QCryptographicHash::Sha256);
            for (int y = 0; y < m.rows; ++y)
                h.addData(QByteArrayView(reinterpret_cast<const char *>(m.ptr(y)), qsizetype(m.cols * m.elemSize())));
            return QString::fromLatin1(h.result().toHex());
        };
        QJsonObject out;
        for (auto it = sets.begin(); it != sets.end(); ++it) {
            const QJsonObject j = it.value().toObject();
            ImageEditor::Settings s;
            s.temperature = int(std::lround(loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
            const auto integer = [&](const char *key, int &target) { if (j.contains(key)) target = j[key].toInt(); };
            s.brightness = float(j.value("brightness").toDouble(0.0));
            integer("contrast", s.contrast); integer("shadows", s.shadows); integer("blacks", s.blacks);
            integer("highlights", s.highlights); integer("whites", s.whites); integer("temperature", s.temperature);
            integer("tint", s.tint); integer("vibrance", s.vibrance); integer("saturation", s.saturation);
            integer("texture", s.texture); integer("clarity", s.clarity); integer("dehaze", s.dehaze);
            integer("grain", s.grain); integer("denoiseLuminance", s.denoiseLuminance);
            integer("denoiseChrominance", s.denoiseChrominance); integer("sharpeningAmount", s.sharpeningAmount);
            integer("sharpeningMasking", s.sharpeningMasking); integer("blur", s.blur);
            s.sharpeningRadius = float(j.value("sharpeningRadius").toDouble(1.0));
            s.grayscale = j.value("grayscale").toBool(false);
            const auto array = [&](const char *key, auto &target) {
                const QJsonArray a = j.value(key).toArray();
                for (int i = 0; i < a.size() && i < int(target.size()); ++i) target[size_t(i)] = a[i].toInt();
            };
            array("mixerHue", s.mixerHue); array("mixerSaturation", s.mixerSaturation);
            array("mixerLuminance", s.mixerLuminance); array("gradingHue", s.gradingHue);
            array("gradingSaturation", s.gradingSaturation);
            const QJsonArray curves = j.value("toneCurves").toArray();
            for (int c = 0; c < curves.size() && c < 4; ++c)
                for (const auto &point : curves[c].toArray())
                    s.toneCurves[size_t(c)].append({float(point.toArray()[0].toDouble()), float(point.toArray()[1].toDouble())});
            const auto master = ImageEditor::renderWorkingImage(loaded.image, s);
            const auto preview = ImageEditor::renderPreview(loaded.editProxy, s, true);
            QCryptographicHash histogram(QCryptographicHash::Sha256);
            histogram.addData(QJsonDocument(QJsonArray::fromVariantList(preview.histogram)).toJson(QJsonDocument::Compact));
            const QImage view = preview.preview.convertToFormat(QImage::Format_RGBA8888);
            out[it.key()] = QJsonObject{
                {"master", master.isNull() ? QString("null") : digest(master.linearRgba)},
                {"preview", digest(cv::Mat(view.height(), view.width(), CV_8UC4, const_cast<uchar *>(view.constBits()), size_t(view.bytesPerLine())))},
                {"histogram", QString::fromLatin1(histogram.result().toHex())}};
        }
        return {{"raw", raw}, {"sets", out}};
    }

    // Inputs MUST be private copies: this measures the real editor lifecycle,
    // including its normal .ipr writes, without modifying source recipes.
    // budget < 0 keeps the controller's automatic retention cap. With idleMs
    // > 0 the user pauses after each step, at most idleMs, until prefetch ends.
    static QJsonObject navigation(const QStringList &paths, const QStringList &dcps,
                                  const QString &manifest, qint64 budget, int idleMs)
    {
        for (int i = 0; i < paths.size(); ++i) {
            ImageEditor::RawDevelopment recipe;
            recipe.profile = dcps[i];
            recipe.profileSha256 = digest(dcps[i]);
            recipe.manifest = manifest;
            recipe.manifestSha256 = digest(manifest);
            recipe.normalizationEnabled = true;
            recipe.lensProfileResolved = true;
            recipe.lensCorrectionEnabled = false;
            recipe.chromaticAberrationEnabled = false;
            if (!ImageEditor::writeSidecar(paths[i], {}, {}, recipe))
                return {{"error", "Cannot seed private sidecar"}};
        }
        ImageProvider provider;
        ImageEditor editor(&provider);
        if (budget >= 0) editor.setDecodedImageCacheBudgetBytes(quint64(budget));
        // The private folder enables the editor's real neighbour prefetch.
        editor.openFolder(QUrl::fromLocalFile(QFileInfo(paths[0]).absolutePath()));
        QJsonArray runs;
        // Global usage sampled during each step; not a guarantee between samples.
        double maxUsed = 0;
        QTimer sampler;
        QObject::connect(&sampler, &QTimer::timeout, [&] {
            const auto m = SystemMemory::read();
            if (m.valid && m.totalBytes)
                maxUsed = std::max(maxUsed, 1.0 - double(m.availableBytes) / double(m.totalBytes));
        });
        sampler.start(50);
        const auto waitFor = [](const std::function<bool()> &done, int ms) {
            QElapsedTimer t;
            t.start();
            while (!done() && t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return done();
        };
        // A B A C A B A: repeated, same-camera and mixed-camera navigation.
        for (int index : {0, 1, 0, 2, 0, 1, 0}) {
            const auto before = editor.m_decodedImageCache.statistics();
            maxUsed = 0;
            QElapsedTimer timer;
            timer.start();
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool timedOut = false;
            double previewMs = -1;
            auto previewConnection = QObject::connect(&editor, &ImageEditor::previewChanged, &loop, [&] {
                if (!provider.image().isNull() && previewMs < 0) previewMs = elapsed(timer);
            });
            auto busyConnection = QObject::connect(&editor, &ImageEditor::busyChanged, &loop, [&] {
                if (!editor.busy()) loop.quit();
            });
            QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
            timeout.start(180000);
            const auto profilesBefore = profiles();
            const bool prefetchInFlight = editor.m_prefetchTaskActive &&
                QFileInfo(editor.m_prefetchFile) == QFileInfo(paths[index]);
            editor.loadImage(QUrl::fromLocalFile(paths[index]));
            if (editor.busy()) loop.exec();
            const double readyMs = elapsed(timer);
            QObject::disconnect(previewConnection);
            QObject::disconnect(busyConnection);
            bool prefetchIdle = true;
            if (idleMs > 0) prefetchIdle = waitFor([&] {
                return !editor.busy() && !editor.m_prefetchTaskActive &&
                    !editor.m_prefetchTimer.isActive() && editor.m_prefetchQueue.isEmpty();
            }, idleMs);
            // Statistics are published asynchronously by the controller thread.
            waitFor([] { return false; }, 100);
            const auto after = editor.m_decodedImageCache.statistics();
            runs.append(QJsonObject{{"scene", QFileInfo(paths[index]).completeBaseName()},
                {"ready_ms", readyMs}, {"provider_published_ms", previewMs},
                {"timed_out", timedOut}, {"has_image", !editor.m_original.isNull()},
                {"prefetch_in_flight_at_click", prefetchInFlight},
                {"prefetch_idle_after_wait", prefetchIdle},
                {"cache_hits", double(after.hits - before.hits)},
                {"cache_misses", double(after.misses - before.misses)},
                {"cache_evictions", double(after.evictions - before.evictions)},
                {"admissions_denied", double(after.denied - before.denied)},
                {"pressure_admissions", double(after.pressureAdmissions - before.pressureAdmissions)},
                {"waiting_requests", double(after.waitingRequests)},
                {"cache_entries", double(after.entries)}, {"cache_bytes", double(after.retainedBytes)},
                {"retention_budget_bytes", double(after.budgetBytes)},
                {"reserved_bytes", double(after.reservedBytes)}, {"pressure", after.pressure},
                {"max_global_used_ratio_sampled", maxUsed},
                {"entry_bytes", double(DecodedImageCache::entrySizeBytes({editor.m_original, editor.m_editProxy, editor.m_sourceMetadata}))},
                {"process", processMemory()},
                {"prepared_before", profilesBefore}, {"prepared_after", profiles()}});
            if (timedOut || editor.m_original.isNull() || editor.m_displayedFile != QUrl::fromLocalFile(paths[index]))
                return {{"error", "Navigation failed or memory admission denied"}, {"runs", runs},
                        {"displayed", editor.m_displayedFile.toLocalFile()}, {"current", editor.m_currentFile.toLocalFile()},
                        {"status", editor.statusMessage()}, {"busy", editor.busy()}};
        }
        const auto m = SystemMemory::read();
        return {{"budget_bytes", budget < 0 ? QJsonValue(QStringLiteral("auto")) : QJsonValue(double(budget))},
            {"idle_ms", idleMs}, {"total_ram_bytes", double(m.totalBytes)}, {"runs", runs}};
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("appIPR");
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::cerr << message.toStdString() << '\n';
    });
    const auto args = app.arguments();
    QJsonObject result;
    if (args.size() == 5 && args[1] == "stages")
        result = IprTestImageGenerator::stages(args[2], args[3], args[4]);
    else if (args.size() >= 5 && args.size() <= 7 && args[1] == "render" &&
             (args.size() < 7 || args[6] == "no-optics")) {
        bool ok = false;
        const double ev = args[3].toDouble(&ok);
        if (!ok) return 2;
        result = IprTestImageGenerator::render(args[2], ev, args[4], args.value(5), args.size() < 7);
    }
    else if (args.size() == 5 && args[1] == "equivalence")
        result = IprTestImageGenerator::equivalence(args[2], args[3], args[4]);
    else if (args.size() == 11 && args[1] == "navigation") {
        bool ok = args[3] == "auto", idleOk = false;
        const qint64 budget = ok ? -1 : qint64(args[3].toULongLong(&ok));
        const int idleMs = args[4].toInt(&idleOk);
        if (!ok || !idleOk || idleMs < 0) return 2;
        result = IprTestImageGenerator::navigation({args[5], args[7], args[9]},
            {args[6], args[8], args[10]}, args[2], budget, idleMs);
#ifdef IPR_WITH_LIBRAW
    } else if (args.size() >= 3 && args[1] == "previews") {
        result = previews(args.mid(2));
#endif
    } else {
        std::cerr << "render RAW EV OUT.tiff [CONFIG [no-optics]] | equivalence RAW SETTINGS.json CONFIG | previews RAW... | stages RAW DCP CONFIG | navigation CONFIG auto|BUDGET IDLE_MS PRIVATE_A DCP_A PRIVATE_B DCP_B PRIVATE_C DCP_C\n";
        return 2;
    }
    result["qt"] = QT_VERSION_STR;
    result["libraw"] = RawDecoder::backendVersion();
    result["opencv_threads"] = cv::getNumThreads();
    result["logical_processors"] = QThread::idealThreadCount();
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.contains("error") ? 1 : 0;
}
