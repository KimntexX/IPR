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
        std::cerr << "previews RAW... | stages RAW DCP CONFIG | navigation CONFIG auto|BUDGET IDLE_MS PRIVATE_A DCP_A PRIVATE_B DCP_B PRIVATE_C DCP_C\n";
        return 2;
    }
    result["qt"] = QT_VERSION_STR;
    result["libraw"] = RawDecoder::backendVersion();
    result["opencv_threads"] = cv::getNumThreads();
    result["logical_processors"] = QThread::idealThreadCount();
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.contains("error") ? 1 : 0;
}
