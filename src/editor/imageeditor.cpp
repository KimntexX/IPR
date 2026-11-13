#include "imageeditor.h"
#include "AdjustmentTable.h"
#include "NeuralDevelopment.h"
#include "DetailProcessing.h"
#include "ColorGrading.h"
#include "ImageExporter.h"
#include "RecipeCodec.h"
#include "Sidecar.h"
#include "ToneCurve.h"
#include "ToneRecovery.h"
#include "RawDecoder.h"
#include "RawNormalization.h"
#include "CameraProfileEngine.h"
#include "imageprovider.h"
#include "ThumbnailProvider.h"
#include "Straightening.h"

#include "Resize.h"
#include "Pixels.h"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QDebug>

#include "Image.h"
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <QCborValue>
#include <vector>

namespace {

bool hasEmptyZones(const ipr::Image &rgba)
{
    ipr::Image alpha;
    alpha = ipr::channel(rgba, 3);
    return ipr::extremesOf(alpha).smallest < 1;
}

QString selectionKey(const QUrl &url)
{
    if (!url.isLocalFile()) return {};
    const QFileInfo info(url.toLocalFile());
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString defaultNormalizationManifest()
{
    const QStringList candidates{qEnvironmentVariable("IPR_RAW_NORMALIZATION_MANIFEST"),
        QDir(QCoreApplication::applicationDirPath()).filePath("config/raw-normalization.json"),
        QStringLiteral(":/config/raw-normalization.json")};
    for (const auto &path : candidates)
        if (!path.isEmpty() && QFileInfo(path).isFile()) return QFileInfo(path).absoluteFilePath();
    return {};
}

QString resourceDigest(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool cacheAssetsUnchanged(const DecodedImageCache::Entry &entry)
{
    return (!entry.image.dcpContext || resourceDigest(entry.image.dcpContext->profilePath) == entry.profileDigest) &&
        (entry.metadata.lcpPath.isEmpty() || resourceDigest(entry.metadata.lcpPath) == entry.metadata.lcpSha256);
}

// Relocation is allowed only for identical bytes, never by filename alone.
QString relocateResource(const QString &path, const QString &digest, const QString &folder)
{
    if (path.isEmpty() || QFileInfo(path).isFile() || digest.isEmpty()) return path;
    const QString root = QDir(QCoreApplication::applicationDirPath()).filePath(folder);
    QDirIterator files(root, {QFileInfo(path).fileName()}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto candidate = files.next();
        if (resourceDigest(candidate) == digest) return candidate;
    }
    return path;
}

QString formatFocalLength(double millimeters)
{
    if (!std::isfinite(millimeters) || millimeters <= 0.0)
        return {};
    const int decimals = std::abs(millimeters - std::round(millimeters)) < 0.05 ? 0 : 1;
    return QStringLiteral("%1 mm").arg(QLocale().toString(millimeters, 'f', decimals));
}

QString formatExposureTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0)
        return {};
    if (seconds >= 1.0) {
        const int decimals = seconds < 10.0 &&
                             std::abs(seconds - std::round(seconds)) >= 0.01 ? 1 : 0;
        return QStringLiteral("%1 s").arg(QLocale().toString(seconds, 'f', decimals));
    }

    const int denominator = static_cast<int>(std::lround(1.0 / seconds));
    return denominator > 0 ? QStringLiteral("1/%1 s").arg(denominator) : QString{};
}

}

ImageListModel::ImageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Images added, removed or renamed outside IPR reach the filmstrip. One copy
    // fires several notifications: they are coalesced into one listing.
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refreshTimer, qOverload<>(&QTimer::start));
    connect(&m_refreshTimer, &QTimer::timeout, this, &ImageListModel::refresh);
}

int ImageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ImageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case FileNameRole:
        return entry.fileName;
    case FileUrlRole:
        return entry.fileUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> ImageListModel::roleNames() const
{
    return {{FileNameRole, "fileName"}, {FileUrlRole, "fileUrl"}};
}

namespace {
QStringList supportedImagePatterns()
{
    QStringList patterns = {"*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff"};
    for (const QString &extension : RawDecoder::supportedExtensions()) {
        patterns.append(QStringLiteral("*.%1").arg(extension));
        patterns.append(QStringLiteral("*.%1").arg(extension.toUpper()));
    }
    return patterns;
}
}

QStringList ImageEditor::imageFileFilters() const
{
    return {tr("Images (%1)").arg(supportedImagePatterns().join(QLatin1Char(' ')))};
}

QList<ImageListModel::Entry> ImageListModel::listFolder(const QString &path)
{
    QList<Entry> entries;
    if (path.isEmpty())
        return entries;
    const QFileInfoList files = QDir(path).entryInfoList(
        supportedImagePatterns(), QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files)
        entries.append({file.fileName(), QUrl::fromLocalFile(file.absoluteFilePath())});
    return entries;
}

void ImageListModel::setFolder(const QUrl &folder)
{
    beginResetModel();
    m_refreshTimer.stop();
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_folderPath = folder.toLocalFile();
    m_entries = listFolder(m_folderPath);
    if (!m_folderPath.isEmpty())
        m_watcher.addPath(m_folderPath);
    endResetModel();
}

// Rows change in place, so the filmstrip keeps its position and thumbnails.
void ImageListModel::refresh()
{
    const QList<Entry> current = listFolder(m_folderPath);
    QSet<QString> present;
    for (const Entry &entry : current)
        present.insert(entry.fileName);
    QStringList removed;
    for (int row = int(m_entries.size()) - 1; row >= 0; --row) {
        if (present.contains(m_entries.at(row).fileName))
            continue;
        removed.prepend(m_entries.at(row).fileUrl.toLocalFile());
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    }
    // The remaining entries keep the listing's order: each new one goes in its place.
    for (int row = 0; row < int(current.size()); ++row) {
        if (row < int(m_entries.size()) && m_entries.at(row).fileName == current.at(row).fileName)
            continue;
        beginInsertRows({}, row, row);
        m_entries.insert(row, current.at(row));
        endInsertRows();
    }
    if (!removed.isEmpty())
        emit entriesRemoved(removed);
}

int ImageListModel::count() const
{
    return m_entries.size();
}

QStringList ImageListModel::localFiles(int first, int last) const
{
    QStringList files;
    const int entryCount = static_cast<int>(m_entries.size());
    first = std::max(0, first);
    last = std::min(last, entryCount - 1);
    for (int index = first; index <= last; ++index)
        files.append(m_entries.at(index).fileUrl.toLocalFile());
    return files;
}

ImageEditor::ImageEditor(ImageProvider *provider,
                         ThumbnailProvider *thumbnailProvider, QObject *parent,
                         ImageCacheController::Sampler memorySampler)
    : QObject(parent)
    , m_provider(provider)
    , m_thumbnailProvider(thumbnailProvider)
    , m_imageModel(this)
    , m_original()
    , m_taskResources(std::make_shared<TaskResources>(std::move(memorySampler)))
    , m_decodedImageCache(m_taskResources->cache)
    , m_renderStageCache(m_taskResources->stages)
{
    m_stacking = std::make_unique<StackSession>(
        [manifest = defaultNormalizationManifest()](const QString &file, const std::function<bool()> &cancelled) {
            RawDecoder::Options options;
            options.automaticWhiteBalance = false;
            options.normalizationManifest = manifest;
            options.useNormalizationEnvironment = false;
            options.preferCameraNeutral = true;
            options.signedDevelopment = true;
            options.cancellationRequested = cancelled;
            StackSession::Decoded decoded;
            const ColorPipeline::WorkingImage image = RawDecoder::decode(file, options, &decoded.metadata, &decoded.error);
            if (image.dcpContext) {
                decoded.context = image.dcpContext;
                decoded.cameraRgb = image.dcpContext->cameraRgb.share();
            }
            return decoded;
        },
        std::shared_ptr<ImageCacheController>(m_taskResources, &m_taskResources->cache));
    connect(&m_embeddedWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        QImage preview;
        try { preview = Tasks::takeResult(m_embeddedWatcher.future()); } catch (...) { /* optional interim view */ }
        // Only while that very development still runs: a fast cache hit wins.
        if (preview.isNull() || m_embeddedRevision != m_requestedLoadRevision || !m_loadWatcher.isRunning())
            return;
        if (!m_embeddedPreview) {
            m_restorePreview = m_provider->image();
            m_restoreOriginal = m_provider->originalImage();
            m_restoreImageSource = m_imageSource;
            m_restoreOriginalSource = m_originalImageSource;
        }
        m_embeddedPreview = true;
        m_provider->setImages(preview, preview);
        ++m_embeddedSerial;
        m_imageSource = QStringLiteral("image://editor/preview?embedded=%1").arg(m_embeddedSerial);
        m_originalImageSource = QStringLiteral("image://editor/original?embedded=%1").arg(m_embeddedSerial);
        m_histogram.clear();
        setStatusMessage(tr("Aperçu du boîtier affiché — développement IPR en cours…"));
        emit histogramChanged();
        emit previewChanged();
    });
    connect(&m_beforeWatcher, &QFutureWatcher<LoadResult>::finished, this, [this] {
        LoadResult before;
        try { before = Tasks::takeResult(m_beforeWatcher.future()); } catch (...) { /* the comparison only */ }
        if (m_beforePending) {
            m_beforePending = false;
            if (m_neuralApplied) startBeforeDevelopment();
            return;
        }
        const bool cancelled = m_beforeCancel && m_beforeCancel->load();
        if (cancelled || !m_neuralApplied || before.cacheKey != plainCacheKey())
            return;
        if (before.editProxy.isNull()) {
            reportNewError(tr("Vue Avant sans débruitage IA indisponible : %1").arg(before.error));
            return;
        }
        m_beforeProxy = std::move(before.editProxy);
        m_beforeKey = before.cacheKey;
        m_originalImageSource.clear();   // the original is rendered again from it
        scheduleRender();
    });
    m_prefetchTimer.setSingleShot(true);
    m_prefetchTimer.setInterval(250);
    connect(&m_prefetchTimer, &QTimer::timeout, this, &ImageEditor::startPrefetch);
    connect(&m_imageModel, &ImageListModel::entriesRemoved, this, [this](const QStringList &files) {
        bool deselected = false;
        for (const QString &file : files) {
            // Selection keys are canonical (selectionKey); a removed file no longer
            // resolves, its folder still does (a symbolic link, macOS's /var).
            const QFileInfo info(file);
            const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
            const QString key = folder.isEmpty() ? info.absoluteFilePath() : QDir(folder).filePath(info.fileName());
            deselected = m_selectedImageKeys.remove(key) || deselected;
            // The pixels stay on screen; saving its settings will fail until it comes back.
            if (m_displayedFile.isLocalFile() &&
                QFileInfo(m_displayedFile.toLocalFile()).absoluteFilePath() == QFileInfo(file).absoluteFilePath())
                setStatusMessage(tr("« %1 » a été supprimée, déplacée ou renommée hors d'IPR.")
                                     .arg(QFileInfo(file).fileName()));
        }
        if (deselected)
            emit selectionChanged();
    });
    connect(&m_decodedImageCache, &ImageCacheController::changed, this, [this] {
        emit decodedImageCacheChanged();
        const auto stats = m_decodedImageCache.statistics();
        constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
        if (stats.waitingRequests) {
            m_memoryWaitMessage = tr("En attente de libération de mémoire : %1 Gio requis, %2 Gio disponibles. "
                "Images anciennes déchargées ; fermer d'autres applications si l'attente persiste.")
                .arg(stats.waitingBytes / GiB, 0, 'f', 1).arg(stats.admissibleBytes / GiB, 0, 'f', 1);
            setStatusMessage(m_memoryWaitMessage);
        } else if (!m_memoryWaitMessage.isEmpty()) {
            if (m_statusMessage == m_memoryWaitMessage) setStatusMessage(tr("Mémoire libérée : traitement repris."));
            m_memoryWaitMessage.clear();
        }
        if (stats.pressure) {
            cancelPrefetch(); m_prefetchSuspended = true;
        } else if (m_prefetchSuspended && !busy()) {
            m_prefetchSuspended = false; schedulePrefetch();
        }
    });
    connect(&m_prefetchWatcher, &QFutureWatcher<LoadResult>::finished, this, [this] {
        LoadResult loaded;
        try { loaded = Tasks::takeResult(m_prefetchWatcher.future()); } catch (...) { /* speculative result discarded */ }
        m_prefetchTaskActive = false;
        m_prefetchTransfer.reset();
        if (m_activePrefetchGeneration == m_prefetchGeneration && m_prefetchCancel &&
            !m_prefetchCancel->load() && !loaded.image.isNull()) {
            const QSize fullSize = loaded.proxyOnly ? loaded.fullSize
                : QSize(loaded.image.linearRgba.width(), loaded.image.linearRgba.height());
            m_decodedImageCache.insert(loaded.cacheKey, retainedEntry(loaded.image, loaded.editProxy,
                loaded.metadata, loaded.dcpSource, fullSize, loaded.proxyOnly));
        }
        if (!m_prefetchQueue.isEmpty() && !busy()) m_prefetchTimer.start();
    });
    m_renderDebounce.setSingleShot(true);
    m_renderDebounce.setInterval(80);
    connect(&m_renderDebounce, &QTimer::timeout, this, &ImageEditor::startRender);
    m_sidecarDebounce.setSingleShot(true);
    m_sidecarDebounce.setInterval(450);
    connect(&m_sidecarDebounce, &QTimer::timeout,
            this, &ImageEditor::saveCurrentSidecar);
    m_detailDelay.setSingleShot(true);
    m_detailDelay.setInterval(350);
    connect(&m_detailDelay, &QTimer::timeout, this, &ImageEditor::startDetailRender);
    connect(&m_detailWatcher, &QFutureWatcher<RenderChain::Preview>::finished, this, [this] {
        RenderChain::Preview rendered;
        try { rendered = Tasks::takeResult(m_detailWatcher.future()); }
        catch (...) { rendered.error = tr("ressources insuffisantes ou traitement interrompu"); }
        const QByteArray key = std::exchange(m_detailRenderKey, QByteArray());
        const bool current = m_detailWanted && detailShowable() && key == detailKey();
        if (current && !rendered.preview.isNull()) {
            m_provider->setDetailImage(rendered.preview);
            m_detailImageKey = key;
            ++m_detailRevision;
        } else if (current) {
            m_detailFailedKey = key;
            setStatusMessage(tr("Vue pleine résolution indisponible : %1").arg(rendered.error));
        }
        emit detailChanged();
        refreshDetail();
    });

    connect(&m_renderWatcher, &QFutureWatcher<RenderChain::Preview>::finished,
            this, [this] {
        RenderChain::Preview rendered;
        try { rendered = Tasks::takeResult(m_renderWatcher.future()); }
        catch (...) { rendered.error = tr("Échec du rendu : ressources insuffisantes ou traitement interrompu."); }
        if (!rendered.error.isEmpty() && m_activeRevision == m_requestedRevision)
            reportNewError(rendered.error);
        if (m_activeRevision == m_requestedRevision &&
            !rendered.preview.isNull()) {
            // Publish the pair together. The neutral baseline is encoded only
            // once per decoded source, never on each slider change.
            const QImage original = rendered.originalPreview.isNull()
                ? m_provider->originalImage() : rendered.originalPreview;
            m_provider->setImages(rendered.preview, original);
            if (!rendered.originalPreview.isNull()) {
                m_originalImageSource = QStringLiteral("image://editor/original?revision=%1")
                                            .arg(m_activeRevision);
                m_originalGeometry = rendered.originalGeometry;
                m_originalWholeCanvas = rendered.originalWholeCanvas;
            }
            m_imageSource = QStringLiteral("image://editor/preview?revision=%1")
                                .arg(m_activeRevision);
            m_histogram = rendered.histogram;
            emit previewChanged();
            emit histogramChanged();
            refreshDetail();
            if (rendered.retouchesNotDone > 0)
                reportNewError(tr("%n retouche(s) laissée(s) sans effet : la zone est trop grande pour être reconstruite, ou sa source sort de l'image.", nullptr, rendered.retouchesNotDone));
        }

        if (m_renderPending) {
            m_renderPending = false;
            startRender();
        } else {
            refreshBusyState();
            if (!busy()) schedulePrefetch();
        }
    });

    connect(&m_batchApplyWatcher, &QFutureWatcher<BatchResult>::finished, this, [this] {
        BatchResult result;
        try { result = Tasks::takeResult(m_batchApplyWatcher.future()); }
        catch (...) { result.failed.append(tr("traitement interrompu")); }
        finishBatch(result, tr("Réglages appliqués à %n image(s).", nullptr, result.written));
        m_batchApplying = false;
        emit batchApplyChanged();
    });
    connect(&m_batchExportWatcher, &QFutureWatcher<BatchResult>::progressValueChanged, this, [this](int done) {
        m_batchExportDone = done;
        setStatusMessage(tr("Export par lot : %1 / %2…").arg(done).arg(m_batchExportTotal));
        emit batchExportChanged();
    });
    connect(&m_batchExportWatcher, &QFutureWatcher<BatchResult>::finished, this, [this] {
        BatchResult result;
        try { result = Tasks::takeResult(m_batchExportWatcher.future()); }
        catch (...) { result.failed.append(tr("traitement interrompu")); }
        finishBatch(result, result.cancelled
            ? tr("Export par lot annulé : %n image(s) exportée(s).", nullptr, result.written)
            : tr("Export par lot terminé : %n image(s) exportée(s).", nullptr, result.written));
        m_batchExportCancel.reset();
        m_batchExporting = false;
        emit batchExportChanged();
        if (!busy()) schedulePrefetch();
    });

    connect(&m_exportWatcher, &QFutureWatcher<ExportResult>::finished, this, [this] {
        ExportResult result;
        try { result = Tasks::takeResult(m_exportWatcher.future()); }
        catch (...) { result = {false, tr("Échec de l'export : ressources insuffisantes ou traitement interrompu.")}; }
        if (result.success)
            setStatusMessage(result.message);
        else
            reportError(result.message);
        refreshBusyState();
        if (!busy()) schedulePrefetch();
    });

    // Decoding (especially RAW demosaicing) can take seconds on large files;
    // running it as a task keeps the UI thread responsive, as rendering and export.
    connect(&m_loadWatcher, &QFutureWatcher<LoadResult>::finished, this, [this] {
        LoadResult loaded;
        try { loaded = Tasks::takeResult(m_loadWatcher.future()); }
        catch (...) { loaded.error = tr("Ressources insuffisantes ou échec du décodeur."); }
        if (!m_queuedFile.isEmpty()) {
            const QUrl queued = std::exchange(m_queuedFile, QUrl());
            loadImage(queued);
            return;
        }
        // Consume the result so a completed future cannot retain a second
        // full-resolution pre-DCP buffer after navigation through the LRU.
        if (m_activeLoadRevision != m_requestedLoadRevision) {
            // A newer loadImage()/setAutomaticWhiteBalance() call superseded
            // this one while it was decoding; discard the stale result.
            refreshBusyState();
            return;
        }
        applyLoadedImage(std::move(loaded));
    });
}

ImageEditor::~ImageEditor()
{
    cancelPrefetch();
    if (m_loadCancel) m_loadCancel->store(true);
    if (m_beforeCancel) m_beforeCancel->store(true);
    if (m_batchExportCancel) m_batchExportCancel->store(true);
    // Workers waiting for memory must not delay or deadlock closing.
    m_decodedImageCache.abortWaiting();
    // Final flush, even when the 450 ms debounce has not elapsed yet.
    m_sidecarDebounce.stop();
    // A running export or batch application ends; a stuck task cannot hold the close.
    m_tasks.shutdown();
    if (!m_pendingLoadIsFreshOpen && !m_original.isNull()) {
        // If closed during a RAW edit, persist the last committed base, not a
        // requested profile whose worker result has not yet been published.
        m_cameraProfile = m_appliedRawDevelopment.profile;
        m_profileSha256 = m_appliedRawDevelopment.profileSha256;
        m_normalizationManifest = m_appliedRawDevelopment.manifest;
        m_manifestSha256 = m_appliedRawDevelopment.manifestSha256;
        m_rawNormalizationEnabled = m_appliedRawDevelopment.normalizationEnabled;
        m_lensProfile = m_appliedRawDevelopment.lensProfile;
        m_lensProfileSha256 = m_appliedRawDevelopment.lensProfileSha256;
        m_lensProfileResolved = m_appliedRawDevelopment.lensProfileResolved;
        m_lensCorrectionEnabled = m_appliedRawDevelopment.lensCorrectionEnabled;
        m_chromaticAberrationEnabled = m_appliedRawDevelopment.chromaticAberrationEnabled;
        m_settings.automaticWhiteBalance = m_appliedAutomaticWhiteBalance;
    }
    saveCurrentSidecar();
}

QAbstractItemModel *ImageEditor::imageModel()
{
    return &m_imageModel;
}

QUrl ImageEditor::folder() const
{
    return m_folder;
}

QString ImageEditor::imageSource() const
{
    return m_imageSource;
}

QString ImageEditor::originalImageSource() const
{
    return m_originalImageSource;
}

QString ImageEditor::statusMessage() const
{
    return m_statusMessage;
}

bool ImageEditor::hasImage() const
{
    return !m_original.isNull();
}

bool ImageEditor::busy() const
{
    return m_busy;
}

QVariantList ImageEditor::histogram() const
{
    return m_histogram;
}

QString ImageEditor::imageInfo() const
{
    if (m_original.isNull())
        return {};
    QString info = tr("%1 × %2 px — %3")
                       .arg(m_fullSize.width())
                       .arg(m_fullSize.height())
                       .arg(m_displayedFile.fileName()); // Not a target still waiting for memory.
    if (!m_sourceMetadata.cameraModel.isEmpty())
        info += QStringLiteral(" — ") + m_sourceMetadata.cameraModel;
    if (!m_sourceMetadata.lensModel.isEmpty())
        info += QStringLiteral(" / ") + m_sourceMetadata.lensModel;
    if (!m_sourceMetadata.dcpProfile.isEmpty())
        info += QStringLiteral(" — DCP: ") + m_sourceMetadata.dcpProfile;
    if (!m_sourceMetadata.lcpProfile.isEmpty())
        info += QStringLiteral(" — LCP: ") + m_sourceMetadata.lcpProfile;
    if (!m_original.sourceProfile.isEmpty())
        info += QStringLiteral(" — Couleur: ") + m_original.sourceProfile;
    if (m_sourceMetadata.opticalBlackLevelEstimated)
        info += tr(" — niveau noir estimé");
    return info;
}

QVariantMap ImageEditor::imageDetails() const
{
    if (m_currentFile.isEmpty())
        return {};

    const QFileInfo fileInfo(m_currentFile.toLocalFile());
    QStringList cameraParts;
    if (!m_sourceMetadata.cameraMake.isEmpty())
        cameraParts.append(m_sourceMetadata.cameraMake);
    if (!m_sourceMetadata.cameraModel.isEmpty())
        cameraParts.append(m_sourceMetadata.cameraModel);
    const QString camera = cameraParts.join(QLatin1Char(' '));
    const QDateTime date = m_sourceMetadata.captureDate.isValid()
                               ? m_sourceMetadata.captureDate
                               : fileInfo.lastModified();
    const QString dimensions = m_original.isNull()
                                   ? QString()
                                   : tr("%1 × %2 px")
                                         .arg(m_fullSize.width())
                                         .arg(m_fullSize.height());

    return {
        {QStringLiteral("name"), fileInfo.fileName()},
        {QStringLiteral("importDecoder"), m_importProvenance.decoderId},
        {QStringLiteral("importDecoderVersion"), m_importProvenance.decoderVersion},
        {QStringLiteral("importColorOrigin"), int(m_importProvenance.colorOrigin)},
        {QStringLiteral("importSourceProfile"), m_importProvenance.sourceProfile},
        {QStringLiteral("importWarnings"), m_importWarnings},
        {QStringLiteral("date"), date.isValid()
                                     ? QLocale().toString(date, QLocale::ShortFormat)
                                     : tr("Non renseignée")},
        {QStringLiteral("location"), m_sourceMetadata.location.isEmpty()
                                         ? tr("Non renseigné")
                                         : m_sourceMetadata.location},
        {QStringLiteral("storage"), fileInfo.absolutePath()},
        {QStringLiteral("rating"), imageRating() > 0
                                       ? QString::number(imageRating()) +
                                             QStringLiteral(" / 5")
                                       : tr("Non renseignée")},
        {QStringLiteral("colorLabel"), imageColorLabel().isEmpty()
                                             ? tr("Aucun")
                                             : imageColorLabel()},
        {QStringLiteral("flagged"), imageFlagged()},
        {QStringLiteral("brand"), camera.isEmpty() ? tr("Non renseignée") : camera},
        {QStringLiteral("lens"), m_sourceMetadata.lensModel.isEmpty()
                                     ? tr("Non renseigné")
                                     : m_sourceMetadata.lensModel},
        {QStringLiteral("iso"), m_sourceMetadata.iso > 0
                                    ? QStringLiteral("ISO %1").arg(m_sourceMetadata.iso)
                                    : QString{}},
        {QStringLiteral("focalLength"), formatFocalLength(m_sourceMetadata.focalLengthMm)},
        {QStringLiteral("exposureTime"), formatExposureTime(m_sourceMetadata.exposureSeconds)},
        {QStringLiteral("format"), m_sourceMetadata.format.toUpper()},
        {QStringLiteral("dimensions"), dimensions},
        {QStringLiteral("fileSize"), QLocale().formattedDataSize(fileInfo.size())}
    };
}

int ImageEditor::imageRating() const
{
    const auto metadata = m_userImageMetadata.constFind(currentFileKey());
    return metadata != m_userImageMetadata.cend() && metadata->ratingSet
               ? metadata->rating
               : m_sourceMetadata.rating;
}

QString ImageEditor::imageColorLabel() const
{
    const auto metadata = m_userImageMetadata.constFind(currentFileKey());
    return metadata != m_userImageMetadata.cend() && metadata->colorLabelSet
               ? metadata->colorLabel
               : m_sourceMetadata.colorLabel;
}

bool ImageEditor::imageFlagged() const
{
    const auto metadata = m_userImageMetadata.constFind(currentFileKey());
    return metadata != m_userImageMetadata.cend() && metadata->flagged;
}

qulonglong ImageEditor::decodedImageCacheBudgetBytes() const
{
    return m_decodedImageCache.budgetBytes();
}

qulonglong ImageEditor::decodedImageCacheSizeBytes() const
{
    return m_decodedImageCache.sizeBytes();
}

int ImageEditor::decodedImageCacheEntryCount() const
{
    return static_cast<int>(m_decodedImageCache.count());
}

QVariantMap ImageEditor::memoryCacheStatus() const
{
    const auto s = m_decodedImageCache.statistics();
    return {{"availableBytes", QVariant::fromValue(s.memory.availableBytes)},
        {"totalBytes", QVariant::fromValue(s.memory.totalBytes)},
        {"reservedBytes", QVariant::fromValue(s.reservedBytes)},
        {"pressure", s.pressure}, {"available", s.memory.valid},
        {"hits", QVariant::fromValue(s.hits)}, {"misses", QVariant::fromValue(s.misses)}};
}

int ImageEditor::selectedImageCount() const
{
    return static_cast<int>(m_selectedImageKeys.size());
}

QStringList ImageEditor::selectedImageUrls() const
{
    QStringList urls;
    urls.reserve(m_selectedImageKeys.size());
    for (const QString &fileName : m_selectedImageKeys)
        urls.append(QUrl::fromLocalFile(fileName).toString());
    return urls;
}

QVariantList ImageEditor::cameraProfiles() const
{
    QVariantList result{QVariantMap{{"name", tr("Automatique (catalogue)")}, {"path", QString()}}};
    for (const auto &path : m_sourceMetadata.availableDcpProfiles)
        result.append(QVariantMap{{"name", QFileInfo(path).completeBaseName()}, {"path", path}});
    if (!m_cameraProfile.isEmpty() && !m_sourceMetadata.availableDcpProfiles.contains(m_cameraProfile))
        result.append(QVariantMap{{"name", tr("Profil enregistré indisponible : %1").arg(QFileInfo(m_cameraProfile).completeBaseName())}, {"path", m_cameraProfile}});
    return result;
}

bool ImageEditor::rawImage() const
{
    return hasImage() && m_importCapabilities.cameraDevelopment;
}

bool ImageEditor::rawProfileFastPathAvailable() const
{
    return rawImage() && m_importCapabilities.profileWithoutRedecode && !m_rawNormalizationEnabled && m_dcpSource && !m_dcpSource->cameraRgb.empty()
        && m_dcpSource->automaticWhiteBalance == m_settings.automaticWhiteBalance
        && m_dcpSource->lensCorrectionEnabled == m_lensCorrectionEnabled
        && m_dcpSource->chromaticAberrationEnabled == m_chromaticAberrationEnabled
        && m_dcpSource->metadata.lcpPath == m_lensProfile
        && m_dcpSource->metadata.lcpSha256 == resourceDigest(m_lensProfile);
}

void ImageEditor::resetRawDevelopment()
{
    if (busy() || !rawImage()) return;
    m_cameraProfile.clear();
    m_profileSha256.clear();
    m_normalizationManifest.clear();
    m_manifestSha256.clear();
    m_rawNormalizationEnabled = false;
    m_rawDevelopmentWarning.clear();
    reloadRawDevelopment();
}

QString ImageEditor::defaultCalibrationManifest()
{
    return defaultNormalizationManifest();
}

QString ImageEditor::rawNormalizationManifest() const
{
    if (!m_normalizationManifest.isEmpty())
        return m_normalizationManifest;
    return defaultNormalizationManifest();
}

void ImageEditor::setLensCorrectionEnabled(bool enabled)
{
    if (busy() || !rawImage() || enabled == m_lensCorrectionEnabled) return;
    m_lensCorrectionEnabled = enabled;
    reloadRawDevelopment();
}

void ImageEditor::setChromaticAberrationEnabled(bool enabled)
{
    if (busy() || !rawImage() || enabled == m_chromaticAberrationEnabled) return;
    m_chromaticAberrationEnabled = enabled;
    reloadRawDevelopment();
}

QString ImageEditor::opticalDevelopmentStatus() const
{
    if (!rawImage()) return tr("Corrections optiques disponibles pour les RAW.");
    QString text = m_sourceMetadata.lcpProfile.isEmpty()
        ? tr("Aucun profil LCP utilisé.") : tr("LCP utilisé : %1").arg(m_sourceMetadata.lcpProfile);
    text += QStringLiteral("\n") + (!m_lensCorrectionEnabled ? tr("Correction objectif désactivée.")
        : tr("Distorsion : %1 ; vignettage : %2.")
            .arg(m_sourceMetadata.lensGeometryApplied ? tr("appliquée") : tr("non appliquée"))
            .arg(m_sourceMetadata.lensVignettingApplied ? tr("appliqué") : tr("non appliqué")));
    text += QStringLiteral("\n") + (!m_chromaticAberrationEnabled ? tr("Aberration chromatique désactivée.")
        : m_sourceMetadata.chromaticAberrationApplied ? tr("Correction chromatique latérale appliquée.")
        : tr("Correction chromatique latérale non appliquée (données LCP absentes)."));
    return text;
}

QString ImageEditor::rawDevelopmentStatus() const
{
    if (!hasImage())
        return tr("Ouvrez un RAW pour choisir son profil.");
    const QString profile = m_sourceMetadata.dcpProfile.isEmpty()
        ? tr("aucun DCP (image non RAW ou matrice LibRaw)") : m_sourceMetadata.dcpProfile;
    const QString warning = m_rawDevelopmentWarning + m_sourceMetadata.developmentWarning;
    const QString prefix = (warning.isEmpty() ? QString() : tr("Attention : %1\n").arg(warning))
        + tr("Profil utilisé : %1\n").arg(profile);
    if (!m_rawNormalizationEnabled)
        return prefix + tr("Normalisation désactivée.");
    if (rawNormalizationManifest().isEmpty())
        return prefix + tr("Non appliquée : choisissez un manifeste de calibration.");
    if (m_sourceMetadata.rawNormalizationApplied) {
        QString status = prefix + tr("Normalisation appliquée — blanc %1, base %2 EV.\n%3")
            .arg(m_sourceMetadata.normalizationWhiteLevel)
            .arg(m_sourceMetadata.normalizationBaselineExposureEv, 0, 'f', 2)
            .arg(m_sourceMetadata.normalizationProvenance);
        if (m_sourceMetadata.profileExposureCompensationEv != 0.0)
            status += tr("\nCompensation de rendu du profil : %1 EV ; base effective %2 EV.\n%3")
                .arg(m_sourceMetadata.profileExposureCompensationEv, 0, 'f', 2)
                .arg(m_sourceMetadata.normalizationBaselineExposureEv +
                     m_sourceMetadata.profileExposureCompensationEv, 0, 'f', 2)
                .arg(m_sourceMetadata.profileRenderingProvenance);
        return status;
    }
    if (m_settings.automaticWhiteBalance)
        return prefix + tr("Non appliquée : désactivez la balance des blancs automatique.");
    return prefix + tr("Normalisation non appliquée. %1").arg(m_sourceMetadata.normalizationReason);
}

void ImageEditor::reloadRawDevelopment()
{
    emit rawDevelopmentChanged();
    if (!m_currentFile.isLocalFile() || m_original.isNull())
        return;
    const auto path = m_currentFile.toLocalFile();
    if (RawDecoder::supportedExtensions().contains(QFileInfo(path).suffix().toLower()))
        beginLoad(path, QFileInfo(path).suffix().toLower(), m_settings.automaticWhiteBalance, false);
}

void ImageEditor::setCameraProfile(const QString &path)
{
    if (busy() || path == m_cameraProfile ||
        (!path.isEmpty() && !m_sourceMetadata.availableDcpProfiles.contains(path)))
        return;
    m_cameraProfile = path;
    m_profileSha256 = resourceDigest(path);
    m_rawDevelopmentWarning.clear();
    reloadRawDevelopment();
}

void ImageEditor::setRawNormalizationEnabled(bool enabled)
{
    if (busy() || enabled == m_rawNormalizationEnabled)
        return;
    m_rawNormalizationEnabled = enabled;
    reloadRawDevelopment();
}

void ImageEditor::setRawNormalizationManifest(const QUrl &file)
{
    if (busy() || !file.isLocalFile())
        return;
    const QFileInfo info(file.toLocalFile());
    if (!info.isFile() || info.size() > 1024 * 1024) {
        reportError(tr("Manifeste invalide ou supérieur à 1 Mio."));
        return;
    }
    m_normalizationManifest = info.absoluteFilePath();
    m_manifestSha256 = resourceDigest(m_normalizationManifest);
    m_rawDevelopmentWarning.clear();
    if (m_rawNormalizationEnabled)
        reloadRawDevelopment();
    else {
        m_appliedRawDevelopment = rawDevelopmentRecipe();
        scheduleSidecarSave();
        emit rawDevelopmentChanged();
    }
}

QUrl ImageEditor::defaultExportFile(const QString &format, const QUrl &previousTarget) const
{
    if (!m_currentFile.isLocalFile())
        return {};

    const QString suffix = ImageExporter::suffix(ImageExporter::normalizedFormat(format));
    if (suffix.isEmpty())
        return {};

    const QFileInfo source(m_currentFile.toLocalFile());
    const QString baseName = source.completeBaseName().isEmpty()
                                 ? QStringLiteral("image")
                                 : source.completeBaseName();
    QDir destination = source.dir();
    if (!previousTarget.isEmpty()) {
        const QString previousPath = ImageExporter::localPath(previousTarget);
        if (previousPath.isEmpty())
            return {};
        destination = QFileInfo(previousPath).dir();
    }
    return QUrl::fromLocalFile(ImageExporter::availablePath(destination.filePath(
        baseName + QStringLiteral("-IPR.") + suffix)));
}

QVariantList ImageEditor::toneCurve(int channel) const
{
    if (channel < LuminanceCurve || channel > BlueCurve)
        return {};

    const QVector<ToneCurvePoint> &curve =
        m_settings.toneCurves[static_cast<size_t>(channel)];
    QVariantList points;
    points.reserve(curve.size() + 2);
    if (curve.isEmpty()) {
        points.append(QVariantMap{{QStringLiteral("x"), 0.0},
                                  {QStringLiteral("y"), 0.0}});
        points.append(QVariantMap{{QStringLiteral("x"), 1.0},
                                  {QStringLiteral("y"), 1.0}});
        return points;
    }
    for (const ToneCurvePoint &point : curve) {
        points.append(QVariantMap{{QStringLiteral("x"), point.x},
                                  {QStringLiteral("y"), point.y}});
    }
    return points;
}

double ImageEditor::toneCurveValue(const QVariantList &points, double x) const
{
    QVector<ToneCurvePoint> curve;
    curve.reserve(points.size());
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        curve.append({point.value(QStringLiteral("x")).toFloat(), point.value(QStringLiteral("y")).toFloat()});
    }
    std::sort(curve.begin(), curve.end(), [](const ToneCurvePoint &a, const ToneCurvePoint &b) { return a.x < b.x; });
    return curve.isEmpty() ? x : ToneCurve::evaluate(curve, float(x));
}

void ImageEditor::setToneCurve(int channel, const QVariantList &points)
{
    if (channel < LuminanceCurve || channel > BlueCurve)
        return;

    QVector<ToneCurvePoint> received;
    received.reserve(points.size());
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        bool xOk = false;
        bool yOk = false;
        const float x = point.value(QStringLiteral("x")).toFloat(&xOk);
        const float y = point.value(QStringLiteral("y")).toFloat(&yOk);
        if (xOk && yOk)
            received.append({x, y});
    }
    // The end points are the curve's own (raised black, clipped whites).
    m_settings.toneCurves[static_cast<size_t>(channel)] = ToneCurve::normalized(std::move(received));
    emit toneCurveChanged(channel);
    scheduleRender();
}

void ImageEditor::resetToneCurve(int channel)
{
    if (channel < LuminanceCurve || channel > BlueCurve)
        return;
    m_settings.toneCurves[static_cast<size_t>(channel)].clear();
    emit toneCurveChanged(channel);
    scheduleRender();
}

void ImageEditor::resetToneCurves()
{
    for (int channel = LuminanceCurve; channel <= BlueCurve; ++channel)
        m_settings.toneCurves[static_cast<size_t>(channel)].clear();
    emit toneCurvesReset();
    scheduleRender();
}

void ImageEditor::openFolder(const QUrl &folder)
{
    if (!folder.isLocalFile()) {
        reportError(tr("Le dossier sélectionné n'est pas un dossier local."));
        return;
    }

    if (m_thumbnailProvider)
        m_thumbnailProvider->setActiveFiles({});
    m_folder = folder;
    m_imageModel.setFolder(folder);
    emit folderChanged();
    setStatusMessage(tr("%n image(s) trouvée(s) dans %1.", nullptr, m_imageModel.count())
                         .arg(folder.toLocalFile()));
}

void ImageEditor::openImage(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        reportError(tr("Le fichier sélectionné n'est pas local."));
        return;
    }

    const QFileInfo fileInfo(fileUrl.toLocalFile());
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        reportError(tr("Le fichier sélectionné est introuvable."));
        return;
    }

    // Opening a single image always rebuilds the filmstrip from its parent
    // folder, so the selected file becomes part of the same navigation flow.
    openFolder(QUrl::fromLocalFile(fileInfo.absolutePath()));
    loadImage(fileUrl);
}

QString ImageEditor::thumbnailSource(const QUrl &fileUrl) const
{
    if (!fileUrl.isLocalFile())
        return {};
    return QStringLiteral("image://thumbnail/") +
           QString::fromLatin1(QUrl::toPercentEncoding(fileUrl.toLocalFile()));
}

void ImageEditor::setThumbnailWindow(int firstVisible, int lastVisible,
                                     int prefetchCount)
{
    if (!m_thumbnailProvider)
        return;

    const int count = m_imageModel.count();
    if (count == 0 || lastVisible < firstVisible) {
        m_thumbnailProvider->setActiveFiles({});
        return;
    }

    const int margin = std::max(0, prefetchCount);
    const int first = std::max(0, firstVisible - margin);
    const int last = std::min(count - 1, lastVisible + margin);
    m_thumbnailProvider->setActiveFiles(m_imageModel.localFiles(first, last));
}

void ImageEditor::setImageRating(int rating)
{
    if (m_currentFile.isEmpty())
        return;
    UserImageMetadata &metadata = m_userImageMetadata[currentFileKey()];
    metadata.rating = qBound(0, rating, 5);
    metadata.ratingSet = true;
    emit imageInfoChanged();
    scheduleSidecarSave();
}

void ImageEditor::setImageColorLabel(const QString &colorLabel)
{
    if (m_currentFile.isEmpty())
        return;
    UserImageMetadata &metadata = m_userImageMetadata[currentFileKey()];
    metadata.colorLabel = colorLabel;
    metadata.colorLabelSet = true;
    emit imageInfoChanged();
    scheduleSidecarSave();
}

void ImageEditor::setImageFlagged(bool flagged)
{
    if (m_currentFile.isEmpty())
        return;
    UserImageMetadata &metadata = m_userImageMetadata[currentFileKey()];
    metadata.flagged = flagged;
    emit imageInfoChanged();
    scheduleSidecarSave();
}

void ImageEditor::setImageSelected(const QUrl &fileUrl, bool selected)
{
    const QString key = selectionKey(fileUrl);
    if (key.isEmpty())
        return;
    const bool changed = selected ? !m_selectedImageKeys.contains(key)
                                  : m_selectedImageKeys.contains(key);
    if (selected)
        m_selectedImageKeys.insert(key);
    else
        m_selectedImageKeys.remove(key);
    if (changed)
        emit selectionChanged();
}

bool ImageEditor::isImageSelected(const QUrl &fileUrl) const
{
    const QString key = selectionKey(fileUrl);
    return !key.isEmpty() && m_selectedImageKeys.contains(key);
}

void ImageEditor::clearImageSelection()
{
    if (m_selectedImageKeys.isEmpty())
        return;
    m_selectedImageKeys.clear();
    emit selectionChanged();
}

bool ImageEditor::asShotWhiteBalance() const
{
    return m_settings.tint == 0 && m_settings.temperature ==
        static_cast<int>(std::lround(m_original.referenceWhiteBalanceKelvin.value_or(5200.0)));
}

void ImageEditor::applyAdjustmentsToSelection()
{
    if (m_selectedImageKeys.isEmpty() || m_currentFile.isEmpty() ||
        !hasImage() || busy() || embeddedPreview() || batchApplying() || batchExporting())
        return;
    // The open image already holds these settings: save them now, the others in the background.
    QStringList targets;
    bool current = false;
    for (const QString &fileName : std::as_const(m_selectedImageKeys)) {
        if (fileName == currentFileKey()) current = true;
        else targets.append(fileName);
    }
    targets.sort();
    if (current) {
        m_sidecarDebounce.stop();
        if (!saveCurrentSidecar()) current = false;
    }
    QHash<QString, UserImageMetadata> annotations;
    for (const QString &fileName : std::as_const(targets))
        annotations.insert(fileName, m_userImageMetadata.value(fileName));
    setStatusMessage(tr("Application des réglages à %n image(s)…", nullptr, int(targets.size()) + int(current)));
    m_batchApplyWatcher.setFuture(m_tasks.submit<BatchResult>(
        QStringLiteral("batchApply"), Tasks::Priority::Foreground,
        [targets, settings = m_settings, asShot = asShotWhiteBalance(), annotations, current](Tasks::Context &) {
            BatchResult result = applyToFiles(targets, settings, asShot, annotations);
            result.written += int(current);
            return result;
        }).future);
    m_batchApplying = true;
    emit batchApplyChanged();
}

ImageEditor::BatchResult ImageEditor::applyToFiles(const QStringList &files, const Settings &settings, bool asShot,
                                                   const QHash<QString, UserImageMetadata> &annotations)
{
    BatchResult result;
    for (const QString &fileName : files) {
        Sidecar::Document target;
        if (std::optional<Sidecar::Document> saved = Sidecar::read(fileName)) {
            target = std::move(*saved);
            if (target.history.isEmpty())
                target.history.reset(RecipeCodec::encodeRecipe(target.settings, target.raw));
        } else {
            target.metadata = annotations.value(fileName);
            // The development the target gets when opened without a sidecar.
            target.raw.normalizationEnabled = true;
        }
        // Retouches can be copied across cameras; RAW profiles cannot.
        Settings copied = settings;
        copied.geometry = target.settings.geometry;   // the framing belongs to each photo
        copied.localMasks = target.settings.localMasks;   // and so do zones drawn on it
        copied.retouches = target.settings.retouches;
        copied.automaticWhiteBalance = target.settings.automaticWhiteBalance;
        if (target.settings.automaticWhiteBalance) {
            copied.temperature = target.settings.temperature;
            copied.tint = target.settings.tint;
        } else if (asShot) {
            copied.temperature = static_cast<int>(std::lround(
                ImageImport::referenceWhiteBalanceKelvin(fileName).value_or(5200.0)));
            copied.tint = 0;
        }
        target.settings = std::move(copied);
        target.history.record(RecipeCodec::encodeRecipe(target.settings, target.raw));
        if (Sidecar::write(fileName, target))
            ++result.written;
        else
            result.failed.append(QFileInfo(fileName).fileName());
    }
    return result;
}

void ImageEditor::finishBatch(const BatchResult &result, const QString &done)
{
    if (result.failed.isEmpty()) {
        setStatusMessage(done);
        return;
    }
    QStringList shown = result.failed.mid(0, 3);
    if (result.failed.size() > 3) shown.append(QStringLiteral("…"));
    reportError(done + QLatin1Char(' ') +
                     tr("Échec pour %n image(s) : %1", nullptr, int(result.failed.size())).arg(shown.join(QStringLiteral(", "))));
}

void ImageEditor::exportSelection(const QUrl &folder, const QString &format, int quality)
{
    const QString folderPath = ImageExporter::localPath(folder);
    if (m_selectedImageKeys.isEmpty() || folderPath.isEmpty() || !QFileInfo(folderPath).isDir()) {
        reportError(tr("Choisissez un dossier local pour l'export par lot."));
        return;
    }
    if (!ImageExporter::supports(format)) {
        reportError(tr("Format d'export non pris en charge : %1").arg(format));
        return;
    }
    if (batchExporting() || batchApplying() || m_exportWatcher.isRunning()) {
        reportError(tr("Un export est déjà en cours."));
        return;
    }
    // Each image is exported with its saved recipe: the open one's must be on disk.
    if (hasImage() && m_selectedImageKeys.contains(currentFileKey())) {
        m_sidecarDebounce.stop();
        saveCurrentSidecar();
    }
    QStringList files(m_selectedImageKeys.cbegin(), m_selectedImageKeys.cend());
    files.sort();
    m_batchExportDone = 0;
    m_batchExportTotal = int(files.size());
    m_batchExportCancel = std::make_shared<std::atomic_bool>(false);
    cancelPrefetch();
    setStatusMessage(tr("Export par lot : 0 / %1…").arg(m_batchExportTotal));
    m_batchExportWatcher.setFuture(m_tasks.submit<BatchResult>(
        QStringLiteral("batchExport"), Tasks::Priority::Foreground,
        [files, folderPath, format = ImageExporter::normalizedFormat(format), quality,
         resources = m_taskResources, cancel = m_batchExportCancel,
         neural = m_neuralAllowed ? neuralService() : nullptr,
         neuralError = m_neuralError, neuralAllowed = m_neuralAllowed](Tasks::Context &context) {
            return exportFiles(context, files, folderPath, format, quality, &resources->cache, cancel,
                               neural, neuralError, neuralAllowed);
        }).future);
    m_batchExporting = true;
    emit batchExportChanged();
}

void ImageEditor::cancelBatchExport()
{
    if (m_batchExportCancel) m_batchExportCancel->store(true);
}

ImageEditor::BatchResult ImageEditor::exportFiles(Tasks::Context &context, const QStringList &files, const QString &folder,
                              const QString &format, int quality, ImageCacheController *controller,
                              ImageCacheController::CancelToken cancel,
                              std::shared_ptr<NeuralCfa::Service> neural,
                              const QString &neuralError, bool neuralAllowed)
{
    BatchResult result;
    const int total = int(files.size());
    context.progress(0, total);
    const QString suffix = ImageExporter::suffix(format);
    for (int index = 0; index < files.size(); ++index) {
        if (cancel->load()) { result.cancelled = true; break; }
        const QString &fileName = files.at(index);
        const QString name = QFileInfo(fileName).fileName();
        bool automaticWhiteBalance = false;
        QString key;
        RawDecoder::Options options = optionsForFile(fileName, &automaticWhiteBalance, &key);
        if (key.isEmpty()) {
            // A pinned profile changed since it was saved: never export with a substitute.
            result.failed.append(tr("%1 (profil enregistré modifié)").arg(name));
            context.progress(index + 1, total);
            continue;
        }
        options.cancellationRequested = [cancel] { return cancel->load(); };
        if (neuralAllowed) {
            if (const auto document = Sidecar::read(fileName); document && document->raw.neuralStrength > 0) {
                // A recipe saved with an earlier model takes the installed one, as the editor does.
                options.cfaStageFactory = NeuralDevelopment::Factory{
                    neural, document->raw.neuralStrength/100.0f, neuralError, {}};
            }
        }
        const auto admission = controller->acquire({}, SystemMemory::estimateDecodePeak(fileName), false, cancel).takeResult();
        if (!admission.reservation) {
            if (cancel->load()) { result.cancelled = true; break; }
            result.failed.append(tr("%1 (%2)").arg(name, admission.error));
            context.progress(index + 1, total);
            continue;
        }
        const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
        LoadResult loaded = loadFromDisk(fileName, automaticWhiteBalance, options);
        if (cancel->load()) { result.cancelled = true; break; }
        Settings settings;
        if (std::optional<Sidecar::Document> saved = Sidecar::read(fileName)) {
            settings = std::move(saved->settings);
            settings.neuralDenoiseApplied = loaded.neuralApplied;
            settings.denoiseBeforeProfile = saved->raw.developmentMode == EditRecipe::SignedDevelopment;
        } else
            settings.temperature = static_cast<int>(std::lround(
                loaded.image.referenceWhiteBalanceKelvin.value_or(5200.0)));
        const QString base = QFileInfo(fileName).completeBaseName().isEmpty()
            ? QStringLiteral("image") : QFileInfo(fileName).completeBaseName();
        const QString target = ImageExporter::availablePath(
            QDir(folder).filePath(base + QStringLiteral("-IPR.") + suffix));
        const ExportResult written = loaded.image.isNull()
            ? ExportResult{false, loaded.error}
            : writeImage(loaded.image, settings, target, format, quality);
        if (written.success) ++result.written;
        else result.failed.append(tr("%1 (%2)").arg(name, written.message));
        context.progress(index + 1, total);
    }
    return result;
}

QColor ImageEditor::colorMixerTrackColor(int band, int component, int value) const
{
    const ipr::Vec3f colour = ColorMixer::trackColour(band, component, value);
    return QColor::fromRgbF(colour[0], colour[1], colour[2]);
}

QColor ImageEditor::colorGradingDisplayColor(int zone, int hue, int saturation) const
{
    const ipr::Vec3f colour = ColorGrading::displayColour(zone, hue, saturation);
    return QColor::fromRgbF(colour[0], colour[1], colour[2]);
}

QVariantList ImageEditor::pointColors() const
{
    QVariantList points;
    const size_t count = std::min(m_settings.pointColor.count, PointColor::MaxPoints);
    for (size_t i = 0; i < count; ++i) {
        const PointColor::Point &p = m_settings.pointColor.points[i];
        const ipr::Vec3f swatch = WorkingColor::encodedInGamut(WorkingColor::workingToOklab(PointColor::fromHsv(p.source)));
        points.append(QVariantMap{{"colour", QColor::fromRgbF(swatch[0], swatch[1], swatch[2])},
                                  {"hue", p.hueShift}, {"saturation", p.saturationShift},
                                  {"luminance", p.luminanceShift}, {"variance", p.variance}, {"range", p.range}});
    }
    return points;
}

bool ImageEditor::addPointColorAt(qreal x, qreal y)
{
    const ColorPipeline::WorkingImage &source = previewSource();
    if (source.isNull() || m_settings.pointColor.count >= PointColor::MaxPoints)
        return false;
    // The colour entering the point colour: the tones' output the preview keeps,
    // or rendered here when it was not kept, then saturation, vibrance and mixer.
    const double pixelScale = previewPixelScale();
    const ipr::Image tones = RenderChain::tonesOutput(source, m_settings, pixelScale, &m_renderStageCache);
    if (tones.empty())
        return false;
    // The tones cover the whole canvas; the view may show its crop.
    const ipr::Point2d onCanvas = m_cropEditing ? ipr::Point2d(x, y)
                                               : Geometry::viewToCanvas(m_settings.geometry, {x, y});
    const auto colour = PerceptualColour::colourBeforePointColour(tones, onCanvas.x, onCanvas.y, 2,
                                                                  RenderChain::perceptualParameters(m_settings));
    if (!colour)
        return false;
    PointColor::Point point;
    point.source = PointColor::toHsv(*colour);
    m_settings.pointColor.points[m_settings.pointColor.count++] = point;
    emit pointColorsChanged();
    scheduleSidecarSave();
    return true;
}

void ImageEditor::setPointColorValue(int index, int component, int value)
{
    if (index < 0 || index >= int(std::min(m_settings.pointColor.count, PointColor::MaxPoints)) ||
        component < 0 || component > 4)
        return;
    PointColor::Point &point = m_settings.pointColor.points[size_t(index)];
    int &target = component == 0 ? point.hueShift : component == 1 ? point.saturationShift
                  : component == 2 ? point.luminanceShift : component == 3 ? point.variance : point.range;
    value = qBound(component == 4 ? 0 : -100, value, 100);
    if (target == value)
        return;
    target = value;
    emit pointColorsChanged();
    scheduleRender();
}

void ImageEditor::setPointColorOverlay(int index)
{
    index = index >= 0 && index < int(std::min(m_settings.pointColor.count, PointColor::MaxPoints)) ? index : -1;
    if (m_pointColorOverlay == index)
        return;
    m_pointColorOverlay = index;
    emit pointColorOverlayChanged();
    scheduleRender();
}

void ImageEditor::removePointColor(int index)
{
    PointColor::Parameters &points = m_settings.pointColor;
    if (index < 0 || index >= int(std::min(points.count, PointColor::MaxPoints)))
        return;
    std::move(points.points.begin() + index + 1, points.points.begin() + points.count, points.points.begin() + index);
    points.points[--points.count] = {};
    emit pointColorsChanged();
    scheduleRender();
}

int ImageEditor::colorMixerValue(int band, int component) const
{
    if (band < 0 || band >= static_cast<int>(m_settings.mixerHue.size()))
        return 0;
    if (component == 0)
        return m_settings.mixerHue[static_cast<size_t>(band)];
    if (component == 1)
        return m_settings.mixerSaturation[static_cast<size_t>(band)];
    if (component == 2)
        return m_settings.mixerLuminance[static_cast<size_t>(band)];
    return 0;
}

int ImageEditor::colorGradingValue(int zone, int component) const
{
    if (zone < 0 || zone >= static_cast<int>(m_settings.gradingHue.size()))
        return 0;
    const size_t index = static_cast<size_t>(zone);
    return component == 0 ? m_settings.gradingHue[index]
         : component == 1 ? m_settings.gradingSaturation[index] : m_settings.gradingLuminance[index];
}

void ImageEditor::setAdjustment(const QString &name, double value)
{
    if (AdjustmentTable::write(m_settings, name, value) == AdjustmentTable::Written::Changed)
        scheduleRender();
}

double ImageEditor::adjustmentValue(const QString &name) const
{
    double value = 0.0;
    if (AdjustmentTable::read(m_settings, name, value))
        return value;
    if (name == QStringLiteral("denoiseAmount")) {
        // -1: a custom pair from the advanced controls, not reachable by one amount.
        DetailProcessing::DenoiseSettings law;
        return DetailProcessing::denoiseFromAmount(m_settings.denoiseLuminance, law) &&
                       law.chrominance == m_settings.denoiseChrominance
                   ? m_settings.denoiseLuminance : -1.0;
    }
    if (name == QStringLiteral("denoiseBeforeProfile")) return m_settings.denoiseBeforeProfile ? 1.0 : 0.0;
    if (name == QStringLiteral("automaticWhiteBalance"))
        return m_settings.automaticWhiteBalance ? 1.0 : 0.0;
    return 0.0;
}

void ImageEditor::loadImage(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        reportError(tr("Le fichier sélectionné n'est pas local."));
        return;
    }

    if (m_loadWatcher.isRunning()) {
        m_queuedFile = fileUrl; // Coalesce navigation; never replace a running watcher's future.
        if (m_loadCancel) m_loadCancel->store(true);
        cancelPrefetch();
        return;
    }

    const QString fileName = fileUrl.toLocalFile();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    beginLoad(fileName, extension, false, true);
}

void ImageEditor::setDecodedImageCacheBudgetBytes(qulonglong budgetBytes)
{
    // Always forward: budgetBytes() is the effective budget published
    // asynchronously (0 until the first sample), not the requested cap.
    m_decodedImageCache.setBudgetBytes(budgetBytes);
    emit decodedImageCacheChanged();
}


void ImageEditor::setDenoiseAmount(int value)
{
    DetailProcessing::DenoiseSettings strengths;
    if (!DetailProcessing::denoiseFromAmount(qBound(0, value, 100), strengths))
        return;
    if (m_settings.denoiseLuminance == strengths.luminance &&
        m_settings.denoiseChrominance == strengths.chrominance)
        return;
    m_settings.denoiseLuminance = strengths.luminance;
    m_settings.denoiseChrominance = strengths.chrominance;
    scheduleRender();
}

void ImageEditor::setDenoiseBeforeProfile(bool enabled)
{
    if (m_denoiseBeforeProfile == enabled)
        return;
    m_denoiseBeforeProfile = enabled;
    m_settings.denoiseBeforeProfile = enabled;
    // A RAW is decoded again: signed development and the signal before the lens
    // corrections with the experiment, the application's development without.
    if (rawImage() && !busy())
        reloadRawDevelopment();
    else
        scheduleRender();
}

std::shared_ptr<NeuralCfa::Service> ImageEditor::neuralService() const
{
    if (!m_neuralService)
        m_neuralService = NeuralDevelopment::create(m_neuralModelPath, &m_neuralServiceId, &m_neuralError);
    return m_neuralService;
}

void ImageEditor::adoptInstalledNeuralModel()
{
    if (!m_neuralAllowed || m_neuralStrength <= 0 || m_neuralModelId.isEmpty() || !neuralService()) return;
    if (m_neuralModelId == m_neuralServiceId) return;
    m_neuralModelId = m_neuralServiceId;
    m_neuralModelUpdated = true;
}

QString ImageEditor::neuralDenoiseEngine() const
{
    return m_neuralService ? m_neuralService->engine() : QStringLiteral("Unavailable");
}

QString ImageEditor::neuralDenoiseUnavailableReason() const
{
    if (!rawImage()) return tr("Débruitage IA réservé aux images RAW.");
    if (!m_neuralAllowed) return tr("Autorisez le débruitage IA dans les préférences.");
    const auto service = neuralService();
    if (!service) return m_neuralError;
    QString reason;
    if (!service->available(m_sourceMetadata.cameraMake, m_sourceMetadata.cameraModel, m_sourceMetadata.iso, &reason))
        return reason;
    return m_neuralReason;
}

void ImageEditor::setNeuralDenoisePreferences(bool enabled, const QString &modelPath)
{
    if (enabled == m_neuralAllowed && modelPath == m_neuralModelPath) return;
    if (modelPath != m_neuralModelPath) {
        m_neuralService.reset(); m_neuralServiceId.clear(); m_neuralError.clear();
    }
    m_neuralModelPath = modelPath;
    m_neuralReason.clear();
    m_neuralAllowed = enabled;
    // Permission changes availability only, never the current photo or its recipe.
    if (enabled) { cancelPrefetch(); neuralService(); }
    emit rawDevelopmentChanged();
}

void ImageEditor::setNeuralDenoiseEnabled(bool enabled)
{
    if (busy() || !rawImage()) return;
    if (enabled && !neuralDenoiseUnavailableReason().isEmpty()) {
        setStatusMessage(neuralDenoiseUnavailableReason()); return;
    }
    if (enabled == neuralDenoiseActive()) return;
    m_neuralStrength = enabled ? 50 : 0;
    m_neuralReason.clear();
    m_neuralModelId = enabled ? m_neuralServiceId : QString();
    m_neuralReload = true;
    reloadRawDevelopment();
}

void ImageEditor::setNeuralDenoiseStrength(int strength)
{
    if (busy() || !m_neuralAllowed || !neuralDenoiseActive()) return;
    strength = std::clamp(strength, 0, 100);
    if (strength == m_neuralStrength) return;
    m_neuralStrength = strength;
    if (strength == 0) m_neuralModelId.clear();
    m_neuralReload = true;
    reloadRawDevelopment();
}

void ImageEditor::setColorMixer(int band, int hue, int saturation, int luminance)
{
    if (band < 0 || band >= static_cast<int>(m_settings.mixerHue.size()))
        return;
    const size_t index = static_cast<size_t>(band);
    hue = qBound(-100, hue, 100);
    saturation = qBound(-100, saturation, 100);
    luminance = qBound(-100, luminance, 100);
    if (m_settings.mixerHue[index] == hue &&
        m_settings.mixerSaturation[index] == saturation &&
        m_settings.mixerLuminance[index] == luminance)
        return;
    m_settings.mixerHue[index] = hue;
    m_settings.mixerSaturation[index] = saturation;
    m_settings.mixerLuminance[index] = luminance;
    scheduleRender();
}

void ImageEditor::setColorGrading(int zone, int hue, int saturation)
{
    if (zone < 0 || zone >= static_cast<int>(m_settings.gradingHue.size()))
        return;
    const size_t index = static_cast<size_t>(zone);
    hue = qBound(0, hue, 359);
    saturation = qBound(0, saturation, 100);
    if (m_settings.gradingHue[index] == hue &&
        m_settings.gradingSaturation[index] == saturation)
        return;
    m_settings.gradingHue[index] = hue;
    m_settings.gradingSaturation[index] = saturation;
    scheduleRender();
}

void ImageEditor::setColorGradingLuminance(int zone, int luminance)
{
    if (zone < 0 || zone >= static_cast<int>(m_settings.gradingLuminance.size()))
        return;
    luminance = qBound(-100, luminance, 100);
    int &target = m_settings.gradingLuminance[static_cast<size_t>(zone)];
    if (target == luminance)
        return;
    target = luminance;
    scheduleRender();
}

void ImageEditor::setColorGradingBlending(int blending)
{
    blending = qBound(0, blending, 100);
    if (m_settings.gradingBlending == blending)
        return;
    m_settings.gradingBlending = blending;
    scheduleRender();
}

void ImageEditor::setColorGradingBalance(int balance)
{
    balance = qBound(-100, balance, 100);
    if (m_settings.gradingBalance == balance)
        return;
    m_settings.gradingBalance = balance;
    scheduleRender();
}
void ImageEditor::setAutomaticWhiteBalance(bool enabled)
{
    if (busy() || m_settings.automaticWhiteBalance == enabled)
        return;
    m_settings.automaticWhiteBalance = enabled;
    emit rawDevelopmentChanged();
    emit adjustmentsReset(m_settings.temperature, m_settings.tint);
    emit pointColorsChanged();
    emit geometryChanged();
    emit localMasksChanged();
    emit retouchesChanged();
    scheduleSidecarSave();

    if (m_original.isNull() || !m_currentFile.isLocalFile())
        return;

    const QString fileName = m_currentFile.toLocalFile();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (!RawDecoder::supportedExtensions().contains(extension))
        return;

    beginLoad(fileName, extension, enabled, false);
}

void ImageEditor::copyAdjustments()
{
    if (!hasImage() || busy() || embeddedPreview()) return;
    m_copiedAdjustments = m_settings;
    m_copiedNeuralStrength = m_neuralApplied ? m_neuralStrength : 0;
    m_copiedNeuralModelId = m_neuralApplied ? m_neuralModelId : QString();
    m_copiedAsShot = asShotWhiteBalance();
    m_hasCopiedAdjustments = true;
    emit copiedAdjustmentsChanged();
    setStatusMessage(tr("Paramètres copiés. Ouvrez une autre image pour les coller."));
}

void ImageEditor::pasteAdjustments()
{
    if (!m_hasCopiedAdjustments || !hasImage() || busy() || embeddedPreview()) return;
    // Transfer edits, not the target's camera development, optics or metadata.
    const bool automaticWhiteBalance = m_settings.automaticWhiteBalance;
    const Geometry::Settings geometry = m_settings.geometry;   // the framing belongs to each photo
    const LocalAdjustments::Masks masks = m_settings.localMasks;   // and so do zones drawn on it
    const Retouch::Operations retouches = m_settings.retouches;
    m_settings = m_copiedAdjustments;
    m_settings.neuralDenoiseApplied = m_neuralApplied;
    m_settings.geometry = geometry;
    m_settings.localMasks = masks;
    m_settings.retouches = retouches;
    m_settings.automaticWhiteBalance = automaticWhiteBalance;
    m_settings.denoiseBeforeProfile = m_denoiseBeforeProfile;
    if (m_copiedAsShot) {   // the target's own as-shot white balance
        m_settings.temperature = static_cast<int>(std::lround(
            m_original.referenceWhiteBalanceKelvin.value_or(5200.0)));
        m_settings.tint = 0;
    }
    emit toneCurvesReset();
    emit adjustmentsReset(static_cast<int>(std::lround(
        m_original.referenceWhiteBalanceKelvin.value_or(5200.0))), 0);
    emit pointColorsChanged();
    emit geometryChanged();
    emit localMasksChanged();
    emit retouchesChanged();
    if ((m_copiedNeuralStrength != m_neuralStrength || m_copiedNeuralModelId != m_neuralModelId) && rawImage() &&
        (m_copiedNeuralStrength == 0 || neuralDenoiseUnavailableReason().isEmpty())) {
        m_neuralStrength = m_copiedNeuralStrength;
        m_neuralModelId = m_copiedNeuralModelId;
        adoptInstalledNeuralModel();
        m_neuralReload = true;
        reloadRawDevelopment();
    } else scheduleRender();
}

void ImageEditor::saveCurrentAdjustments()
{
    if (!hasImage() || busy() || embeddedPreview()) return;
    m_sidecarDebounce.stop();
    if (saveCurrentSidecar())
        setStatusMessage(tr("Réglages enregistrés : %1.ipr").arg(m_currentFile.toLocalFile()));
}

bool ImageEditor::historyEditable() const
{
    return !m_original.isNull() && !m_loadWatcher.isRunning();
}

// Every recipe change reaches scheduleSidecarSave(): outside a gesture or a
// restoration, the recipe it leaves becomes a step.
void ImageEditor::recordHistory()
{
    // A development under way: the step comes with its result, which may refuse it.
    if (m_original.isNull() || m_inAdjustmentGesture || m_restoringHistory || m_loadWatcher.isRunning())
        return;
    if (m_history.record(currentRecipe()))
        emit historyChanged();
}

QCborMap ImageEditor::currentRecipe() const
{
    return RecipeCodec::encodeRecipe(m_settings, rawDevelopmentRecipe());
}

void ImageEditor::setRawDevelopment(const RawDevelopment &raw)
{
    m_neuralStrength = raw.neuralStrength;
    m_neuralModelId = raw.neuralModelId;
    adoptInstalledNeuralModel();
    m_neuralReload = true;
    m_cameraProfile = raw.profile;
    m_profileSha256 = raw.profileSha256;
    m_normalizationManifest = raw.manifest;
    m_manifestSha256 = raw.manifestSha256;
    m_rawNormalizationEnabled = raw.normalizationEnabled;
    m_lensProfile = raw.lensProfile;
    m_lensProfileSha256 = raw.lensProfileSha256;
    m_lensProfileResolved = raw.lensProfileResolved;
    m_lensCorrectionEnabled = raw.lensCorrectionEnabled;
    m_chromaticAberrationEnabled = raw.chromaticAberrationEnabled;
    if (!raw.developmentMode.isEmpty())
        m_denoiseBeforeProfile = raw.developmentMode == EditRecipe::SignedDevelopment;
    m_rawDevelopmentWarning.clear();
}

void ImageEditor::beginAdjustmentGesture()
{
    m_inAdjustmentGesture = true;
}

void ImageEditor::endAdjustmentGesture()
{
    const bool hadFastGeometry = m_geometryGesture;
    m_geometryGesture = false;
    m_geometryProxy = {};
    m_inAdjustmentGesture = false;
    if (hadFastGeometry) emit geometryPreviewChanged();
    recordHistory();
    if (!m_original.isNull() && (hadFastGeometry || originalFramingStale())) {
        ++m_requestedRevision;
        setBusy(true);
        m_renderDebounce.start();
    }
}

bool ImageEditor::originalFramingStale() const
{
    return m_originalGeometry != m_settings.geometry || m_originalWholeCanvas != m_cropEditing;
}

void ImageEditor::restoreRecipe(const std::optional<QCborMap> &recipe)
{
    Settings restored;
    RawDevelopment development = rawDevelopmentRecipe();
    const QCborMap before = RecipeCodec::encodeRaw(development);
    if (!recipe || !RecipeCodec::decodeRecipe(*recipe, restored, development))
        return;
    // The automatic white balance and the development choices are applied by the decoder.
    const bool redevelop = restored.automaticWhiteBalance != m_settings.automaticWhiteBalance ||
                           RecipeCodec::encodeRaw(development) != before;
    if (RecipeCodec::encodeRaw(development) != before)
        setRawDevelopment(development);
    restored.denoiseBeforeProfile = m_denoiseBeforeProfile;
    restored.neuralDenoiseApplied = m_neuralApplied;
    m_restoringHistory = true;
    const auto restoring = qScopeGuard([this] { m_restoringHistory = false; });
    m_settings = std::move(restored);
    emit toneCurvesReset();
    emit adjustmentsReset(static_cast<int>(std::lround(
        m_original.referenceWhiteBalanceKelvin.value_or(5200.0))), m_settings.tint);
    emit pointColorsChanged();
    emit geometryChanged();
    emit localMasksChanged();
    emit retouchesChanged();
    emit rawDevelopmentChanged();
    emit historyChanged();
    const QString fileName = m_currentFile.toLocalFile();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (redevelop && m_currentFile.isLocalFile() && RawDecoder::supportedExtensions().contains(extension))
        beginLoad(fileName, extension, m_settings.automaticWhiteBalance, false);
    else
        scheduleRender();
}

void ImageEditor::undo()
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    restoreRecipe(m_history.undo());
}

void ImageEditor::redo()
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    restoreRecipe(m_history.redo());
}

void ImageEditor::goToHistoryStep(int step)
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    restoreRecipe(m_history.moveTo(step));
}

void ImageEditor::removeHistoryStep(int step)
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    if (!m_history.removeStep(step)) return;
    const std::optional<QCborMap> recipe = m_history.moveTo(m_history.position());
    if (recipe && *recipe != currentRecipe())
        restoreRecipe(recipe);
    emit historyChanged();
    scheduleSidecarSave();
}

void ImageEditor::addSnapshot(const QString &name)
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    const QString trimmed = name.trimmed();
    m_history.addSnapshot(trimmed.isEmpty() ? tr("Instantané %1").arg(m_history.snapshots().size() + 1) : trimmed,
                          QDateTime::currentMSecsSinceEpoch());
    emit historyChanged();
    scheduleSidecarSave();
}

void ImageEditor::removeSnapshot(int index)
{
    if (!historyEditable() || !m_history.removeSnapshot(index)) return;
    emit historyChanged();
    scheduleSidecarSave();
}

void ImageEditor::restoreSnapshot(int index)
{
    if (!historyEditable()) return;
    endAdjustmentGesture();
    restoreRecipe(m_history.snapshot(index));
    recordHistory();
}

QVariantList ImageEditor::historySteps() const
{
    QVariantList steps;
    for (int step = 0; step < m_history.size(); ++step)
        steps.append(QVariantMap{{QStringLiteral("changes"), m_history.changes(step)}});
    return steps;
}

QVariantList ImageEditor::snapshots() const
{
    QVariantList snapshots;
    for (const EditHistory::Snapshot &snapshot : m_history.snapshots())
        snapshots.append(QVariantMap{{QStringLiteral("name"), snapshot.name},
                                     {QStringLiteral("created"), QDateTime::fromMSecsSinceEpoch(snapshot.createdMs)}});
    return snapshots;
}

QVariantMap ImageEditor::geometry() const
{
    const Geometry::Settings &g = m_settings.geometry;
    const ipr::Size source = m_original.isNull() ? ipr::Size() : m_original.linearRgba.size();
    const ipr::Size canvas = Geometry::canvasSize(g, source);
    return {{"quarterTurns", g.quarterTurns}, {"flipHorizontal", g.flipHorizontal}, {"flipVertical", g.flipVertical},
            {"rotation", double(g.rotation)}, {"vertical", g.vertical}, {"horizontal", g.horizontal},
            {"aspect", g.aspect}, {"scale", g.scale}, {"offsetX", g.offsetX}, {"offsetY", g.offsetY},
            {"cropX", g.cropX}, {"cropY", g.cropY}, {"cropWidth", g.cropWidth}, {"cropHeight", g.cropHeight},
            {"cropRatio", g.cropRatio}, {"constrain", g.constrain}, {"canvasWidth", canvas.width},
            {"canvasHeight", canvas.height}, {"hasCrop", Geometry::hasCrop(g)},
            {"identity", Geometry::isIdentity(g)},
            {"emptyZones", !m_original.isNull() && !Geometry::cropInsideImage(g, source)}};
}

bool ImageEditor::cropEditing() const
{
    return m_cropEditing;
}

void ImageEditor::setCropEditing(bool editing)
{
    if (m_cropEditing == editing)
        return;
    m_cropEditing = editing;
    emit cropEditingChanged();
    // The framing of the view, not of the recipe: no history step, no save.
    if (!m_original.isNull()) {
        ++m_requestedRevision;
        setBusy(true);
        m_renderDebounce.start();
    }
}

void ImageEditor::commitGeometry(Geometry::Settings next)
{
    if (m_original.isNull() || !Geometry::bound(next))
        return;
    Geometry::applyConstraint(next, m_original.linearRgba.size());
    if (next == m_settings.geometry)
        return;
    if (m_inAdjustmentGesture && !m_geometryGesture) {
        m_geometryGesture = true;
        emit geometryPreviewChanged();
    }
    m_settings.geometry = next;
    emit geometryChanged();
    emit localMasksChanged(); // frame-to-canvas handles move with the photo
    scheduleRender();
}

void ImageEditor::setLocalMaskOverlay(int mask)
{
    if (mask == m_localMaskOverlay)
        return;
    m_localMaskOverlay = mask;
    emit localMaskOverlayChanged();
    if (!m_original.isNull())
        scheduleRender();
}

QVariantList ImageEditor::localMasks() const
{
    return LocalAdjustments::describeOnCanvas(m_settings.localMasks, m_settings.geometry,
        m_original.isNull() ? ipr::Size() : m_original.linearRgba.size());
}

bool ImageEditor::commitLocalMasks(LocalAdjustments::Masks next)
{
    if (m_original.isNull() || !LocalAdjustments::bound(next))
        return false;
    if (next == m_settings.localMasks)
        return true;
    m_settings.localMasks = std::move(next);
    emit localMasksChanged();
    scheduleRender();
    return true;
}

int ImageEditor::addLocalMask(const QString &kind)
{
    LocalAdjustments::Shape shape;
    if (!LocalAdjustments::kindNamed(kind, shape.kind))
        return -1;
    if (shape.kind == LocalAdjustments::Kind::Triangle || shape.kind == LocalAdjustments::Kind::Trapezoid)
        shape.diffusion = 0.7f;
    LocalAdjustments::Masks next = m_settings.localMasks;
    LocalAdjustments::Mask mask;
    mask.frame = m_settings.geometry;
    mask.name = tr("Masque %1").arg(next.size() + 1);
    mask.shapes.push_back(shape);
    next.push_back(std::move(mask));
    const int index = int(next.size()) - 1;
    return commitLocalMasks(std::move(next)) ? index : -1;
}

void ImageEditor::removeLocalMask(int mask)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next.erase(next.begin() + mask);
    commitLocalMasks(std::move(next));
}

bool ImageEditor::moveLocalMask(int from, int to)
{
    const int count = int(m_settings.localMasks.size());
    if (from < 0 || to < 0 || from >= count || to >= count)
        return false;
    if (from == to)
        return true;
    LocalAdjustments::Masks next = m_settings.localMasks;
    if (from < to)
        std::rotate(next.begin() + from, next.begin() + from + 1, next.begin() + to + 1);
    else
        std::rotate(next.begin() + to, next.begin() + from, next.begin() + from + 1);
    return commitLocalMasks(std::move(next));
}

int ImageEditor::duplicateLocalMask(int mask)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()))
        return -1;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next.insert(next.begin() + mask + 1, next[size_t(mask)]);
    return commitLocalMasks(std::move(next)) ? mask + 1 : -1;
}

void ImageEditor::setLocalMaskName(int mask, const QString &name)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].name = name;
    commitLocalMasks(std::move(next));
}

void ImageEditor::setLocalMaskVisible(int mask, bool visible)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].visible = visible;
    commitLocalMasks(std::move(next));
}

void ImageEditor::setLocalMaskOpacity(int mask, double opacity)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].opacity = float(opacity);
    commitLocalMasks(std::move(next));
}

void ImageEditor::setLocalMaskValue(int mask, const QString &parameter, double value)
{
    const int index = LocalAdjustments::parameterNamed(parameter);
    if (mask < 0 || mask >= int(m_settings.localMasks.size()) || index < 0)
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].values[size_t(index)] = float(value);
    commitLocalMasks(std::move(next));
}

int ImageEditor::addLocalMaskShape(int mask, const QString &kind, const QString &combine)
{
    LocalAdjustments::Shape shape;
    if (mask < 0 || mask >= int(m_settings.localMasks.size()) || !LocalAdjustments::kindNamed(kind, shape.kind) ||
        !LocalAdjustments::combineNamed(combine, shape.combine))
        return -1;
    if (shape.kind == LocalAdjustments::Kind::Triangle || shape.kind == LocalAdjustments::Kind::Trapezoid)
        shape.diffusion = 0.7f;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].shapes.push_back(shape);
    const int index = int(next[size_t(mask)].shapes.size()) - 1;
    return commitLocalMasks(std::move(next)) ? index : -1;
}

void ImageEditor::removeLocalMaskShape(int mask, int shape)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()) || shape < 0 ||
        shape >= int(m_settings.localMasks[size_t(mask)].shapes.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    next[size_t(mask)].shapes.erase(next[size_t(mask)].shapes.begin() + shape);
    commitLocalMasks(std::move(next));
}

void ImageEditor::setLocalMaskShape(int mask, int shape, const QVariantMap &values)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()) || shape < 0 ||
        shape >= int(m_settings.localMasks[size_t(mask)].shapes.size()))
        return;
    LocalAdjustments::Masks next = m_settings.localMasks;
    if (LocalAdjustments::setShapeFields(next[size_t(mask)].shapes[size_t(shape)], values))
        commitLocalMasks(std::move(next));
}

void ImageEditor::addLocalMaskStroke(int mask, int shape, const QVariantList &points, double size, double feather, double flow)
{
    if (mask < 0 || mask >= int(m_settings.localMasks.size()) || shape < 0 ||
        shape >= int(m_settings.localMasks[size_t(mask)].shapes.size()) || points.isEmpty() || points.size() % 2 != 0)
        return;
    LocalAdjustments::BrushStroke stroke;
    stroke.size = float(size);
    stroke.feather = float(feather);
    stroke.flow = float(flow);
    for (qsizetype i = 0; i + 1 < points.size(); i += 2)
        stroke.points.emplace_back(points.at(i).toFloat(), points.at(i + 1).toFloat());
    LocalAdjustments::Masks next = m_settings.localMasks;
    LocalAdjustments::Shape &target = next[size_t(mask)].shapes[size_t(shape)];
    if (target.kind != LocalAdjustments::Kind::Brush)
        return;
    target.strokes.push_back(std::move(stroke));
    commitLocalMasks(std::move(next));
}

QVariantList ImageEditor::retouches() const
{
    return Retouch::describe(m_settings.retouches);
}

void ImageEditor::setRetouchOverlay(int retouch)
{
    if (retouch == m_retouchOverlay)
        return;
    m_retouchOverlay = retouch;
    emit retouchOverlayChanged();
    if (!m_original.isNull())
        scheduleRender();
}

bool ImageEditor::commitRetouches(Retouch::Operations next)
{
    if (m_original.isNull() || !Retouch::bound(next))
        return false;
    if (next == m_settings.retouches)
        return true;
    m_settings.retouches = std::move(next);
    emit retouchesChanged();
    scheduleRender();
    return true;
}

int ImageEditor::addRetouch(const QString &kind)
{
    Retouch::Operation operation;
    if (!Retouch::kindNamed(kind, operation.kind) || m_original.isNull())
        return -1;
    Retouch::Operations next = m_settings.retouches;
    next.push_back(operation);
    return commitRetouches(std::move(next)) ? int(m_settings.retouches.size()) - 1 : -1;
}

void ImageEditor::removeRetouch(int retouch)
{
    if (retouch < 0 || retouch >= int(m_settings.retouches.size()))
        return;
    Retouch::Operations next = m_settings.retouches;
    next.erase(next.begin() + retouch);
    if (m_retouchOverlay >= int(next.size()))
        setRetouchOverlay(-1);
    commitRetouches(std::move(next));
}

void ImageEditor::setRetouchVisible(int retouch, bool visible)
{
    if (retouch < 0 || retouch >= int(m_settings.retouches.size()))
        return;
    Retouch::Operations next = m_settings.retouches;
    next[size_t(retouch)].visible = visible;
    commitRetouches(std::move(next));
}

void ImageEditor::setRetouchOpacity(int retouch, double opacity)
{
    if (retouch < 0 || retouch >= int(m_settings.retouches.size()) || !std::isfinite(opacity))
        return;
    Retouch::Operations next = m_settings.retouches;
    next[size_t(retouch)].opacity = float(opacity);
    commitRetouches(std::move(next));
}

void ImageEditor::setRetouchSource(int retouch, double shiftX, double shiftY)
{
    if (retouch < 0 || retouch >= int(m_settings.retouches.size()) || !std::isfinite(shiftX) || !std::isfinite(shiftY))
        return;
    Retouch::Operations next = m_settings.retouches;
    next[size_t(retouch)].source = ipr::Point2f(float(shiftX), float(shiftY));
    commitRetouches(std::move(next));
}

void ImageEditor::addRetouchStroke(int retouch, const QVariantList &points, double size, double feather)
{
    if (retouch < 0 || retouch >= int(m_settings.retouches.size()) || points.isEmpty() || points.size() % 2 != 0)
        return;
    LocalAdjustments::BrushStroke stroke;
    stroke.size = float(size);
    stroke.feather = float(feather);
    for (qsizetype i = 0; i + 1 < points.size(); i += 2)
        stroke.points.emplace_back(points.at(i).toFloat(), points.at(i + 1).toFloat());
    Retouch::Operations next = m_settings.retouches;
    Retouch::Operation &target = next[size_t(retouch)];
    const bool first = target.strokes.empty();
    target.strokes.push_back(std::move(stroke));
    // A first stroke of a heal or a clone comes with a source; the user can move it.
    if (first && target.kind != Retouch::Kind::Remove) {
        const ColorPipeline::WorkingImage &source = previewSource();
        if (!source.isNull()) {
            const ipr::Image rgb = Geometry::warpRgb(ipr::withoutAlpha(source.linearRgba), m_settings.geometry,
                                                     RenderChain::geometryQuality(previewPixelScale()));
            target.source = Retouch::suggestSource(rgb, target);
        }
    }
    commitRetouches(std::move(next));
}

void ImageEditor::setGeometryValue(const QString &name, double value)
{
    if (!std::isfinite(value))
        return;
    Geometry::Settings next = m_settings.geometry;
    const int rounded = static_cast<int>(std::lround(value));
    if (name == QStringLiteral("rotation")) next.rotation = static_cast<float>(value);
    else if (name == QStringLiteral("vertical")) next.vertical = rounded;
    else if (name == QStringLiteral("horizontal")) next.horizontal = rounded;
    else if (name == QStringLiteral("aspect")) next.aspect = rounded;
    else if (name == QStringLiteral("scale")) next.scale = rounded;
    else if (name == QStringLiteral("offsetX")) next.offsetX = rounded;
    else if (name == QStringLiteral("offsetY")) next.offsetY = rounded;
    else return;
    commitGeometry(next);
}

void ImageEditor::turnGeometry()
{
    if (m_original.isNull()) return;
    Geometry::Settings next = m_settings.geometry;
    Geometry::turn(next, m_original.linearRgba.size());
    commitGeometry(next);
}

void ImageEditor::mirrorGeometry(bool horizontal)
{
    if (m_original.isNull()) return;
    Geometry::Settings next = m_settings.geometry;
    Geometry::mirror(next, horizontal, m_original.linearRgba.size());
    commitGeometry(next);
}

void ImageEditor::setGeometryCrop(double x, double y, double width, double height)
{
    if (m_original.isNull()) return;
    Geometry::Settings next = m_settings.geometry;
    Geometry::setCrop(next, m_original.linearRgba.size(), x, y, width, height);
    commitGeometry(next);
}

void ImageEditor::setGeometryCropRatio(double ratio)
{
    if (m_original.isNull()) return;
    Geometry::Settings next = m_settings.geometry;
    Geometry::setCropRatio(next, m_original.linearRgba.size(), ratio);
    commitGeometry(next);
}

void ImageEditor::setGeometryConstrain(bool constrain)
{
    Geometry::Settings next = m_settings.geometry;
    next.constrain = constrain;
    commitGeometry(next);
}

void ImageEditor::resetGeometryCrop()
{
    Geometry::Settings next = m_settings.geometry;
    next.cropX = next.cropY = 0;
    next.cropWidth = next.cropHeight = 1;
    next.cropRatio = 0;
    commitGeometry(next);
}

void ImageEditor::proposeStraightening(const QString &mode)
{
    Straightening::Mode chosen = Straightening::Mode::Level;
    if (mode == QStringLiteral("vertical")) chosen = Straightening::Mode::Vertical;
    else if (mode == QStringLiteral("full")) chosen = Straightening::Mode::Full;
    else if (mode != QStringLiteral("level")) return;
    const ColorPipeline::WorkingImage &source = previewSource();
    if (source.isNull()) {
        reportError(tr("L'image n'est pas prête : aucune proposition."));
        return;
    }
    const Straightening::Proposal proposal = Straightening::propose(
        Straightening::linesOf(source.linearRgba), source.linearRgba.size(), m_settings.geometry, chosen);
    switch (proposal.refusal) {
    case Straightening::Refusal::None:
        break;
    case Straightening::Refusal::NotEnoughEvidence:
        reportError(tr("Trop peu de lignes pour une correction aussi grande : aucune proposition."));
        return;
    case Straightening::Refusal::NoAgreement:
        reportError(tr("Les lignes de cette photo ne s'accordent pas : aucune proposition."));
        return;
    case Straightening::Refusal::NotEnoughLines:
    case Straightening::Refusal::InvalidInput:
        reportError(tr("Pas assez de lignes droites dans cette photo : aucune proposition."));
        return;
    }
    commitGeometry(Straightening::applied(m_settings.geometry, proposal));
    setStatusMessage(tr("Redressement proposé d'après %n ligne(s) : à vérifier, il ne connaît pas la scène.", nullptr,
                        proposal.supportingLines));
}

void ImageEditor::resetGeometry()
{
    commitGeometry({});
}

void ImageEditor::resetAdjustments()
{
    if (m_original.isNull())
        return;
    const bool automaticWhiteBalance = m_settings.automaticWhiteBalance;
    m_settings = {};
    m_settings.automaticWhiteBalance = automaticWhiteBalance;
    m_settings.denoiseBeforeProfile = m_denoiseBeforeProfile;
    m_settings.temperature = static_cast<int>(std::lround(
        m_original.referenceWhiteBalanceKelvin.value_or(5200.0)));
    emit toneCurvesReset();
    emit adjustmentsReset(m_settings.temperature, m_settings.tint);
    emit pointColorsChanged();
    emit geometryChanged();
    emit localMasksChanged();
    emit retouchesChanged();
    emit rawDevelopmentChanged();
    if (m_neuralApplied) {
        m_neuralStrength = 0;
        m_neuralModelId.clear();
        m_neuralReload = true;
        reloadRawDevelopment();
    } else scheduleRender();
}

void ImageEditor::exportImage(const QUrl &target, const QString &format, int quality)
{
    if (m_loadWatcher.isRunning()) {
        reportError(tr("Attendez la fin du développement RAW avant l'export."));
        return;
    }
    if (m_original.isNull()) {
        reportError(tr("Ouvrez une image avant de l'exporter."));
        return;
    }
    const QString targetPath = ImageExporter::localPath(target);
    if (targetPath.isEmpty()) {
        reportError(tr("La destination d'export doit être un fichier local."));
        return;
    }
    if (m_exportWatcher.isRunning() || batchExporting()) {
        reportError(tr("Un export est déjà en cours."));
        return;
    }

    // Shared with the task, read only: std::function copies what it captures.
    const auto source = std::make_shared<const ColorPipeline::WorkingImage>(ColorPipeline::sharedCopy(m_original));
    const Settings settings = m_settings;
    const QString fileName = targetPath;
    // Never export a restored proxy: rebuild the full master with this recipe.
    const bool rebuild = m_masterIsProxy;
    const QString sourceFile = m_currentFile.toLocalFile();
    const RawDecoder::Options options = developmentOptions();
    setBusy(true);
    setStatusMessage(rebuild ? tr("Export en cours : reconstruction pleine résolution…") : tr("Export en cours…"));
    cancelPrefetch();
    m_exportWatcher.setFuture(m_tasks.submit<ExportResult>(
        QStringLiteral("export"), Tasks::Priority::Foreground,
        [source, settings, fileName, format, quality, resources = m_taskResources, rebuild, sourceFile,
         options](Tasks::Context &) {
            ImageCacheController *controller = &resources->cache;
            const quint64 peak = rebuild ? SystemMemory::estimateDecodePeak(sourceFile)
                                         : SystemMemory::estimateProcessingPeak(
                                               quint64(source->linearRgba.width()) * quint64(source->linearRgba.height()));
            const auto admission = controller->acquire({}, peak, false).takeResult();
            if (!admission.reservation) return ExportResult{false, admission.error};
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            if (!rebuild)
                return writeImage(*source, settings, fileName, format, quality);
            auto master = loadFromDisk(sourceFile, settings.automaticWhiteBalance, options);
            if (master.image.isNull())
                return ExportResult{false, tr("Échec de la reconstruction pleine résolution : %1").arg(master.error)};
            if (settings.neuralDenoiseApplied && !master.neuralApplied)
                return ExportResult{false, master.neuralReason};
            // Same tone weights as the preview the user validated.
            master.image.toneStatistics = source->toneStatistics;
            return writeImage(master.image, settings, fileName, format, quality);
        }).future);
}

namespace {
// Statistics of the neutral rendering (the proxy as loaded), measured once;
// the master shares them, so preview and export apply the same tone weights.
void attachToneStatistics(ColorPipeline::WorkingImage &image, ColorPipeline::WorkingImage &proxy)
{
    if (proxy.isNull()) return;
    const auto statistics = ToneRecovery::measure(proxy.linearRgba);
    proxy.toneStatistics = statistics;
    image.toneStatistics = statistics;
}
}

double ImageEditor::detailRatio() const
{
    // A restored proxy has no full resolution in memory; a small image is its own preview.
    if (m_masterIsProxy || m_original.isNull() || m_editProxy.isNull() || m_editProxy.linearRgba.width() <= 0)
        return 1.0;
    return std::max(1.0, double(m_original.linearRgba.width()) / double(m_editProxy.linearRgba.width()));
}

QByteArray ImageEditor::detailKey() const
{
    return m_currentCacheKey.toUtf8() + '|' + QByteArray::number(m_sourceGeneration) + '|'
        + QCborValue(RecipeCodec::encodeRecipe(m_settings, {})).toCbor();
}

// The overlays and the crop tool draw on the preview: the detail hides while they show.
bool ImageEditor::detailShowable() const
{
    return m_detailWanted && !m_geometryGesture && detailRatio() > 1.0 && !m_cropEditing && m_pointColorOverlay < 0 &&
        m_localMaskOverlay < 0 && m_retouchOverlay < 0 && !m_embeddedPreview && !m_loadWatcher.isRunning();
}

void ImageEditor::setDetailWanted(bool wanted)
{
    if (wanted == m_detailWanted) return;
    m_detailWanted = wanted;
    if (wanted) refreshDetail();
    else dropDetail();
    emit detailChanged();
}

// After each published preview: show the detail of these settings, or render it once they rest.
void ImageEditor::refreshDetail()
{
    const bool showable = detailShowable();
    const QByteArray key = showable ? detailKey() : QByteArray();
    if (showable && !m_detailImageKey.isEmpty() && m_detailImageKey != key) {
        // Other settings: the former pixels are never shown again, free them now.
        m_provider->setDetailImage({});
        m_detailImageKey.clear();
    }
    const QString source = showable && !m_detailImageKey.isEmpty()
        ? QStringLiteral("image://editor/detail?revision=%1").arg(m_detailRevision) : QString();
    if (source != m_detailSource) {
        m_detailSource = source;
        emit detailChanged();
    }
    if (showable && source.isEmpty() && key != m_detailRenderKey && key != m_detailFailedKey)
        m_detailDelay.start();
    else
        m_detailDelay.stop();
}

void ImageEditor::startDetailRender()
{
    // A running render ends in refreshDetail, which asks again when needed.
    if (!detailShowable() || m_detailWatcher.isRunning() || m_renderWatcher.isRunning()) return;
    const QByteArray key = detailKey();
    if (key == m_detailImageKey || key == m_detailFailedKey) return;
    const auto source = std::make_shared<const ColorPipeline::WorkingImage>(ColorPipeline::sharedCopy(m_original));
    m_detailRenderKey = key;
    m_detailWatcher.setFuture(m_tasks.submit<RenderChain::Preview>(
        QStringLiteral("detail"), Tasks::Priority::Foreground,
        [source, settings = m_settings, resources = m_taskResources](Tasks::Context &) {
            ImageCacheController *controller = &resources->cache;
            const auto admission = controller->acquire({}, SystemMemory::estimateProcessingPeak(
                quint64(source->linearRgba.width()) * quint64(source->linearRgba.height())), false).takeResult();
            if (!admission.reservation) { RenderChain::Preview result; result.error = admission.error; return result; }
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            // The export's render: full resolution, no stage cache, no overlay.
            return RenderChain::preview(*source, settings, false, 1.0);
        }).future);
    emit detailChanged();
}

void ImageEditor::dropDetail()
{
    m_detailDelay.stop();
    m_provider->setDetailImage({});
    m_detailImageKey.clear();
    m_detailFailedKey.clear();
    if (!m_detailSource.isEmpty()) {
        m_detailSource.clear();
        emit detailChanged();
    }
}

const ColorPipeline::WorkingImage &ImageEditor::previewSource() const
{
    return m_editProxy.isNull() ? m_original : m_editProxy;
}

double ImageEditor::previewPixelScale() const
{
    const ColorPipeline::WorkingImage &source = previewSource();
    const int fullExtent = std::max(m_fullSize.width(), m_fullSize.height());
    const int sourceExtent = std::max(source.linearRgba.width(), source.linearRgba.height());
    return fullExtent > 0 && sourceExtent > 0 ? std::min(1.0, double(sourceExtent) / double(fullExtent)) : 1.0;
}

ImageEditor::ExportResult ImageEditor::writeImage(const ColorPipeline::WorkingImage &source,
                                                   const Settings &settings, const QString &target,
                                                   const QString &format, int quality)
{
    if (!ImageExporter::supports(format))
        return {false, tr("Format d'export non pris en charge : %1").arg(format)};
    const ColorPipeline::WorkingImage rendered = RenderChain::cropped(RenderChain::workingImage(source, settings), settings.geometry);
    if (rendered.isNull())
        return {false, tr("Échec du rendu avant export.")};
    ExportResult result = ImageExporter::write(rendered, target, format, quality);
    if (result.success && format != QStringLiteral("PNG") && format != QStringLiteral("TIFF") &&
        hasEmptyZones(rendered.linearRgba))
        result.message += tr(" Ce format n'a pas de transparence : les zones vides sont noires.");
    return result;
}

QImage ImageEditor::renderSrgb16(const ColorPipeline::WorkingImage &source,
                                 const Settings &settings, QString *error)
{
    const ColorPipeline::WorkingImage rendered = RenderChain::cropped(RenderChain::workingImage(source, settings), settings.geometry);
    if (rendered.isNull()) {
        if (error) *error = tr("Échec du rendu avant export.");
        return {};
    }
    return ColorPipeline::toSrgb(rendered, ColorPipeline::OutputFormat::HighBitDepth, error);
}

ImageEditor::LoadResult ImageEditor::loadFromDisk(const QString &fileName,
                                                  bool automaticWhiteBalance,
                                                  const RawDecoder::Options &rawOptions)
{
    auto options = rawOptions;
    options.automaticWhiteBalance = automaticWhiteBalance;
    NeuralDevelopment::bindCancellation(options);
    LoadResult result;
    if (options.cfaStageFactory && automaticWhiteBalance) {
        result.error = tr("Le débruitage neuronal expérimental nécessite la balance des blancs du boîtier.");
        return result;
    }
    static_cast<ImageImport::Result &>(result) = ImageImport::load(fileName, options);
    if (const auto *factory = options.cfaStageFactory.target<NeuralDevelopment::Factory>()) {
        result.neuralApplied = factory->report && factory->report->called && factory->report->applied;
        result.neuralReason = factory->report ? factory->report->reason : QString();
        if (!result.neuralApplied && result.neuralReason.isEmpty())
            result.neuralReason = tr("Le débruitage IA ne s’est pas appliqué à ce développement.");
    }
    if (!result.image.isNull()) {
        result.editProxy = RenderChain::editProxy(result.image);
        attachToneStatistics(result.image, result.editProxy);
    }
    return result;
}

// Current recipe, without per-call cancellation or retained camera signal.
RawDecoder::Options ImageEditor::developmentOptions() const
{
    RawDecoder::Options options;
    options.dcpProfileOverride = m_cameraProfile;
    options.lcpProfileOverride = m_lensProfile;
    options.automaticLcpSelection = !m_lensProfileResolved;
    options.lensCorrectionEnabled = m_lensCorrectionEnabled;
    options.chromaticAberrationEnabled = m_chromaticAberrationEnabled;
    if (!m_lensProfileSha256.isEmpty() && resourceDigest(options.lcpProfileOverride) != m_lensProfileSha256) {
        options.lcpProfileOverride.clear();
        options.automaticLcpSelection = false;
    }
    options.normalizationManifest = m_rawNormalizationEnabled ? rawNormalizationManifest() : QString();
    // Never silently apply bytes different from the saved recipe.
    if (!m_profileSha256.isEmpty() && resourceDigest(options.dcpProfileOverride) != m_profileSha256)
        options.dcpProfileOverride.clear();
    if (!m_manifestSha256.isEmpty() && resourceDigest(options.normalizationManifest) != m_manifestSha256)
        options.normalizationManifest.clear();
    options.useNormalizationEnvironment = false;
    options.listDcpProfiles = true;
    options.preferCameraNeutral = true;
    // Experimental camera-signal denoise: the signed development (values below the
    // black kept) and the signal before the lens corrections.
    options.signedDevelopment = m_denoiseBeforeProfile;
    options.retainPreLensSignal = m_denoiseBeforeProfile;
    if (m_neuralAllowed && m_neuralStrength > 0) {
        auto service = neuralService();
        QString error = m_neuralError;
        if (m_neuralModelId != m_neuralServiceId) {
            service.reset();
            error = tr("Le modèle neuronal enregistré ne correspond pas au modèle installé.");
        }
        options.cfaStageFactory = NeuralDevelopment::Factory{service, m_neuralStrength/100.0f, error, {}};
    }
    return options;
}

// Leaving an image keeps only what editing needs: the 2048 px proxy with its
// own camera signal (about 78 MB for 32 MP instead of about 940 MB). Export
// rebuilds the full master; a DCP change redecodes. Small images keep all.
DecodedImageCache::Entry ImageEditor::retainedEntry(const ColorPipeline::WorkingImage &image,
    const ColorPipeline::WorkingImage &proxy, const ImageMetadata &metadata,
    std::shared_ptr<RawDecoder::DcpSource> dcpSource, QSize fullSize, bool imageIsProxy)
{
    const QString digest = image.dcpContext ? resourceDigest(image.dcpContext->profilePath) : QString();
    const bool proxyIsMaster = proxy.linearRgba.block() == image.linearRgba.block();
    if (proxyIsMaster && !imageIsProxy)
        return {ColorPipeline::sharedCopy(image), ColorPipeline::sharedCopy(proxy), metadata, std::move(dcpSource), digest,
                fullSize, false};
    return {ColorPipeline::sharedCopy(proxy), ColorPipeline::sharedCopy(proxy), metadata, nullptr, digest, fullSize, true};
}

ImageEditor::LoadResult ImageEditor::sharedCopy(const LoadResult &loaded)
{
    LoadResult copy;
    copy.image = ColorPipeline::sharedCopy(loaded.image);
    copy.metadata = loaded.metadata;
    copy.dcpSource = loaded.dcpSource;
    copy.provenance = loaded.provenance;
    copy.capabilities = loaded.capabilities;
    copy.warnings = loaded.warnings;
    copy.error = loaded.error;
    copy.neuralApplied = loaded.neuralApplied;
    copy.neuralReason = loaded.neuralReason;
    copy.editProxy = ColorPipeline::sharedCopy(loaded.editProxy);
    copy.cacheKey = loaded.cacheKey;
    copy.memoryPressure = loaded.memoryPressure;
    copy.fullSize = loaded.fullSize;
    copy.proxyOnly = loaded.proxyOnly;
    return copy;
}

DecodedImageCache::Entry ImageEditor::retainedCurrentEntry() const
{
    return retainedEntry(m_original, m_editProxy, m_sourceMetadata, m_dcpSource, m_fullSize, m_masterIsProxy);
}


QString ImageEditor::cacheKeyForFile(const QString &fileName,
                                     bool automaticWhiteBalance,
                                     const QString &profile, const QString &manifest,
                                     const QString &lensProfile, bool lensResolved,
                                     bool lensCorrection, bool chromaticAberration,
                                     bool cameraSignalExperiment, int neuralStrength, const QString &neuralModelId)
{
    const QFileInfo fileInfo(fileName);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return {};

    const QString canonicalPath = fileInfo.canonicalFilePath();
    const QString path = canonicalPath.isEmpty() ? fileInfo.absoluteFilePath()
                                                  : canonicalPath;
    const QFileInfo profileInfo(profile);
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6\x1f%7\x1f%8")
        .arg(path)
        .arg(fileInfo.size())
        .arg(fileInfo.lastModified().toMSecsSinceEpoch())
        .arg(automaticWhiteBalance ? 1 : 0)
        .arg(RawNormalization::cacheToken(manifest))
        .arg(profile + resourceDigest(profile)).arg(profileInfo.size()).arg(profileInfo.lastModified().toMSecsSinceEpoch())
        + QStringLiteral("|engine=") + ColorPipeline::engineIdentity()
        + QStringLiteral("|lcp=%1|resolved=%2|lens=%3|ca=%4")
            .arg(lensProfile + resourceDigest(lensProfile)).arg(lensResolved)
            .arg(lensCorrection).arg(chromaticAberration)
        + (cameraSignalExperiment ? QStringLiteral("|signed-prelens") : QString())
        + QStringLiteral("|neural=%1:%2").arg(neuralStrength).arg(neuralModelId);
}

ImageEditor::RawDevelopment ImageEditor::rawDevelopmentRecipe() const
{
    const QString manifest = m_normalizationManifest.isEmpty() && m_rawNormalizationEnabled
        ? rawNormalizationManifest() : m_normalizationManifest;
    return {m_cameraProfile, m_profileSha256, manifest,
            m_manifestSha256.isEmpty() ? resourceDigest(manifest) : m_manifestSha256,
            m_rawNormalizationEnabled, ColorPipeline::engineIdentity() + QStringLiteral("|libraw=") + RawDecoder::backendVersion(),
            m_lensProfile, m_lensProfileSha256, m_lensProfileResolved,
            m_lensCorrectionEnabled, m_chromaticAberrationEnabled,
            rawImage() ? (m_denoiseBeforeProfile ? EditRecipe::SignedDevelopment : EditRecipe::StandardDevelopment)
                       : QString(), m_neuralApplied ? m_neuralStrength : 0,
            m_neuralApplied ? m_neuralModelId : QString()};
}

QString ImageEditor::currentFileKey() const
{
    const QFileInfo fileInfo(m_currentFile.toLocalFile());
    const QString canonicalPath = fileInfo.canonicalFilePath();
    return canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath;
}

void ImageEditor::beginLoad(const QString &fileName, const QString &extension,
                            bool automaticWhiteBalance, bool freshOpen)
{
    const bool promotePrefetch = freshOpen && m_prefetchTaskActive && m_prefetchFile == fileName &&
        m_prefetchCancel && !m_prefetchCancel->load();
    if (promotePrefetch) {
        ++m_prefetchGeneration; m_prefetchQueue.clear(); m_prefetchTimer.stop();
    } else cancelPrefetch();
    // Keep only a shallow reference in the LRU cache, then drop the editor's
    // references before a potentially large new decode starts. A rendering or
    // export already in flight owns its own shallow reference until completion.
    if (freshOpen && m_sidecarDebounce.isActive())
        saveCurrentSidecar();
    m_sidecarDebounce.stop();
    m_renderDebounce.stop();
    ++m_requestedRevision;
    m_renderPending = false;
    if (freshOpen) {
        dropBeforeDevelopment();
    } else if (!m_neuralApplied && !m_original.isNull()) {
        // The development the network may replace is the comparison's "before".
        m_beforeProxy = ColorPipeline::sharedCopy(previewSource());
        m_beforeKey = m_currentCacheKey;
    }
    if (freshOpen) {
        m_neuralReason.clear();
        // Retain the displayed image until its replacement is ready. Cache
        // eviction can release only the cache reference, never these pixels.
        if (!m_currentCacheKey.isEmpty() && !m_original.isNull())
            m_decodedImageCache.insert(m_currentCacheKey, retainedCurrentEntry());
        m_neuralStrength = 0;
        m_neuralModelId.clear();
        m_cameraProfile.clear();
        m_profileSha256.clear();
        m_normalizationManifest.clear();
        m_manifestSha256.clear();
        m_rawNormalizationEnabled = true;
        m_rawDevelopmentWarning.clear();
        m_lensProfile.clear();
        m_lensProfileSha256.clear();
        m_lensProfileResolved = false;
        m_lensCorrectionEnabled = true;
        m_chromaticAberrationEnabled = true;
        if (const std::optional<Sidecar::Document> saved = Sidecar::read(fileName)) {
            const RawDevelopment &raw = saved->raw;
            m_neuralStrength = raw.neuralStrength;
            m_neuralModelId = raw.neuralModelId;
            adoptInstalledNeuralModel();
            automaticWhiteBalance = saved->settings.automaticWhiteBalance;
            m_cameraProfile = relocateResource(raw.profile, raw.profileSha256, "profiles");
            m_profileSha256 = raw.profileSha256;
            m_normalizationManifest = relocateResource(raw.manifest, raw.manifestSha256, "config");
            m_manifestSha256 = raw.manifestSha256;
            m_rawNormalizationEnabled = raw.normalizationEnabled;
            m_lensProfile = relocateResource(raw.lensProfile, raw.lensProfileSha256, "profiles");
            m_lensProfileSha256 = raw.lensProfileSha256;
            m_lensProfileResolved = raw.lensProfileResolved;
            m_lensCorrectionEnabled = raw.lensCorrectionEnabled;
            m_chromaticAberrationEnabled = raw.chromaticAberrationEnabled;
            if (!m_lensProfile.isEmpty() && (resourceDigest(m_lensProfile).isEmpty() ||
                    (!m_lensProfileSha256.isEmpty() && resourceDigest(m_lensProfile) != m_lensProfileSha256)))
                m_rawDevelopmentWarning += tr("Le LCP enregistré est absent ou modifié : corrections optiques non appliquées. ");
            if (!m_cameraProfile.isEmpty() && (resourceDigest(m_cameraProfile).isEmpty() ||
                    (!m_profileSha256.isEmpty() && resourceDigest(m_cameraProfile) != m_profileSha256)))
                m_rawDevelopmentWarning += tr("Une ressource enregistrée est absente ou modifiée. Repli automatique ; rendu non garanti. ");
            if (!raw.engineVersion.isEmpty() && raw.engineVersion != rawDevelopmentRecipe().engineVersion)
                m_rawDevelopmentWarning += tr("La version du moteur de rendu a changé. ");
            // Developed again as it was: the session's experimental switch follows the photo.
            if (!raw.developmentMode.isEmpty() &&
                (raw.developmentMode == EditRecipe::SignedDevelopment) != m_denoiseBeforeProfile) {
                m_denoiseBeforeProfile = raw.developmentMode == EditRecipe::SignedDevelopment;
                m_rawDevelopmentWarning += m_denoiseBeforeProfile
                    ? tr("Photo développée en mode signé : ce mode est repris. ")
                    : tr("Photo développée en mode standard : ce mode est repris. ");
            }
        }
    } else if (!m_currentCacheKey.isEmpty()) {
        m_decodedImageCache.insert(m_currentCacheKey, retainedCurrentEntry());
        emit decodedImageCacheChanged();
    }

    // The selected manifest is live application/user configuration, not an
    // immutable profile asset. Follow updates automatically; validate its rules
    // in RawNormalization and include the new digest in cache/recipe identity.
    // DCP/LCP fingerprints remain pinned independently.
    if (m_rawNormalizationEnabled) {
        const auto currentDigest = resourceDigest(rawNormalizationManifest());
        if (!currentDigest.isEmpty() && currentDigest != m_manifestSha256) {
            if (!m_manifestSha256.isEmpty())
                m_rawDevelopmentWarning += tr("Configuration de calibration mise à jour automatiquement. ");
            m_manifestSha256 = currentDigest;
        }
    }
    m_currentFile = QUrl::fromLocalFile(fileName);
    m_provider->setUrl(m_currentFile);
    m_pendingLoadIsFreshOpen = freshOpen;
    m_currentCacheKey = cacheKeyForFile(fileName, automaticWhiteBalance, m_cameraProfile,
            m_rawNormalizationEnabled ? rawNormalizationManifest() : QString(),
            m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled,
            m_denoiseBeforeProfile, m_neuralAllowed ? m_neuralStrength : 0, m_neuralModelId);

    setBusy(true);
    setStatusMessage(m_neuralAllowed && m_neuralStrength > 0 ? tr("Débruitage IA en cours…")
                              : freshOpen ? tr("Ouverture de %1...").arg(QFileInfo(fileName).fileName())
                              : tr("Mise à jour du développement RAW…"));
    ++m_requestedLoadRevision;
    m_activeLoadRevision = m_requestedLoadRevision;
    if (freshOpen && RawDecoder::supportedExtensions().contains(extension))
        startEmbeddedPreview(fileName);

    setBusy(true);
    RawDecoder::Options options = developmentOptions();
    const bool fast = !m_neuralApplied && !m_neuralReload && !options.cfaStageFactory && !freshOpen && !m_rawNormalizationEnabled && m_dcpSource &&
        !m_dcpSource->cameraRgb.empty() && m_dcpSource->automaticWhiteBalance == automaticWhiteBalance &&
        m_dcpSource->lensCorrectionEnabled == m_lensCorrectionEnabled &&
        m_dcpSource->chromaticAberrationEnabled == m_chromaticAberrationEnabled &&
        m_dcpSource->preLensSignal == m_denoiseBeforeProfile &&
        m_dcpSource->signedDevelopment == m_denoiseBeforeProfile &&
        m_dcpSource->metadata.lcpPath == options.lcpProfileOverride &&
        m_dcpSource->metadata.lcpSha256 == resourceDigest(options.lcpProfileOverride) &&
        (m_cameraProfile.isEmpty() || !options.dcpProfileOverride.isEmpty());
    m_neuralReload = false;
    const auto source = fast ? m_dcpSource : nullptr;
    m_loadCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_loadCancel;
    options.cancellationRequested = [cancel] { return cancel->load(); };
    options.retainedDcpSource = std::make_shared<RawDecoder::DcpSource>();
    const auto key = m_currentCacheKey;
    auto prefetchFuture = m_prefetchWatcher.future();
    const auto transfer = promotePrefetch ? m_prefetchTransfer : nullptr;
    m_loadWatcher.setFuture(m_tasks.submit<LoadResult>(
        QStringLiteral("load"), Tasks::Priority::Interactive,
        [fileName, automaticWhiteBalance, options, source, resources = m_taskResources, key, cancel, prefetchFuture,
         transfer](Tasks::Context &) mutable {
            ImageCacheController *controller = &resources->cache;
            if (transfer && prefetchFuture.isStarted()) {
                prefetchFuture.waitForFinished();
                auto ready = sharedCopy(*transfer);
                if (!cancel->load() && !ready.image.isNull() && ready.cacheKey == key) return ready;
            }
            const auto peak = SystemMemory::estimateDecodePeak(fileName);
            auto admission = controller->acquire(key, peak, false, cancel).takeResult();
            if (admission.cached) {
                auto &cached = *admission.cached;
                const auto *neural = options.cfaStageFactory.target<NeuralDevelopment::Factory>();
                if (cacheAssetsUnchanged(cached) && (!neural || neural->service)) {
                    LoadResult result;
                    result.image = std::move(cached.image); result.editProxy = std::move(cached.editProxy);
                    result.metadata = std::move(cached.metadata); result.dcpSource = std::move(cached.dcpSource);
                    result.fullSize = cached.fullSize; result.proxyOnly = cached.proxyOnly;
                    result.neuralApplied = bool(options.cfaStageFactory);
                    ImageImport::complete(result);
                    return result;
                }
                admission.cached.reset();
                admission = controller->acquire({}, peak, false, cancel).takeResult();
            }
            if (!admission.reservation || cancel->load()) {
                controller->release(admission.reservation);
                LoadResult result; result.error = admission.error.isEmpty() ? QStringLiteral("Chargement annulé.") : admission.error;
                return result;
            }
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            LoadResult loaded;
            if (source) {
                loaded.dcpSource = source;
                loaded.image = RawDecoder::applyProfile(*source, options.dcpProfileOverride, &loaded.metadata, &loaded.error);
                ImageImport::complete(loaded);
                if (!loaded.image.isNull()) {
                    loaded.editProxy = RenderChain::editProxy(loaded.image);
                    attachToneStatistics(loaded.image, loaded.editProxy);
                }
            } else {
                loaded = loadFromDisk(fileName, automaticWhiteBalance, options);
            }
            loaded.memoryPressure = admission.underPressure;
            return loaded;
        }).future);
}

void ImageEditor::applyLoadedImage(LoadResult loaded)
{
    if (loaded.image.isNull()) {
        m_neuralModelUpdated = false;
        const auto failedName = QFileInfo(m_currentFile.toLocalFile()).fileName();
        if (!m_original.isNull()) {
            m_currentFile = m_displayedFile;
            m_neuralStrength = m_appliedRawDevelopment.neuralStrength;
            m_neuralModelId = m_appliedRawDevelopment.neuralModelId;
            m_provider->setUrl(m_currentFile);
            m_cameraProfile = m_appliedRawDevelopment.profile;
            m_profileSha256 = m_appliedRawDevelopment.profileSha256;
            m_normalizationManifest = m_appliedRawDevelopment.manifest;
            m_manifestSha256 = m_appliedRawDevelopment.manifestSha256;
            m_rawNormalizationEnabled = m_appliedRawDevelopment.normalizationEnabled;
            m_lensProfile = m_appliedRawDevelopment.lensProfile;
            m_lensProfileSha256 = m_appliedRawDevelopment.lensProfileSha256;
            m_lensProfileResolved = m_appliedRawDevelopment.lensProfileResolved;
            m_lensCorrectionEnabled = m_appliedRawDevelopment.lensCorrectionEnabled;
            m_chromaticAberrationEnabled = m_appliedRawDevelopment.chromaticAberrationEnabled;
            m_settings.automaticWhiteBalance = m_appliedAutomaticWhiteBalance;
            m_currentCacheKey = cacheKeyForFile(m_currentFile.toLocalFile(), m_appliedAutomaticWhiteBalance,
                m_cameraProfile, m_rawNormalizationEnabled ? rawNormalizationManifest() : QString(),
                m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled,
                m_denoiseBeforeProfile, m_neuralAllowed ? m_neuralStrength : 0, m_neuralModelId);
            m_rawDevelopmentWarning = tr("Modification RAW refusée : %1. Rendu précédent conservé. ").arg(loaded.error);
            restoreBeforeEmbeddedPreview();
            emit rawDevelopmentChanged();
            emit adjustmentsReset(m_settings.temperature, m_settings.tint);
            emit pointColorsChanged();
            emit geometryChanged();
            emit localMasksChanged();
    emit retouchesChanged();
            if (!m_pendingLoadIsFreshOpen) scheduleRender();
        }
        reportError(tr("Échec du développement de « %1 » : %2")
                             .arg(failedName,
                                  loaded.error));
        emit hasImageChanged();
        refreshBusyState();
        return;
    }

    m_neuralApplied = loaded.neuralApplied;
    m_neuralReason = loaded.neuralReason;
    if (m_neuralApplied && m_neuralModelUpdated)
        m_rawDevelopmentWarning += tr("Débruitage IA mis à jour : la photo utilise le modèle installé. ");
    m_neuralModelUpdated = false;
    if (!m_neuralApplied) {
        if (m_neuralStrength > 0 && !loaded.neuralReason.isEmpty())
            m_rawDevelopmentWarning += loaded.neuralReason + QLatin1Char(' ');
        m_neuralStrength = 0;
        m_neuralModelId.clear();
    }
    m_sourceMetadata = std::move(loaded.metadata);
    m_importProvenance = std::move(loaded.provenance);
    m_importCapabilities = loaded.capabilities;
    m_importWarnings = std::move(loaded.warnings);
    if (!m_lensProfileResolved && RawDecoder::supportedExtensions().contains(
            QFileInfo(m_currentFile.toLocalFile()).suffix().toLower())) {
        m_lensProfile = m_sourceMetadata.lcpPath;
        m_lensProfileSha256 = m_sourceMetadata.lcpSha256;
        m_lensProfileResolved = true; // Also remember an intentionally absent profile.
    }
    // Freeze the automatically resolved DCP into this image's recipe.
    if (m_cameraProfile.isEmpty()) {
        for (const auto &path : m_sourceMetadata.availableDcpProfiles) {
            if (QFileInfo(path).completeBaseName() == m_sourceMetadata.dcpProfile) {
                m_cameraProfile = path;
                m_profileSha256 = resourceDigest(path);
                break;
            }
        }
    }
    m_dcpSource = std::move(loaded.dcpSource);
    m_embeddedPreview = false;
    m_restorePreview = m_restoreOriginal = QImage();
    m_masterIsProxy = loaded.proxyOnly;
    ++m_sourceGeneration;
    m_fullSize = loaded.proxyOnly ? loaded.fullSize
        : QSize(loaded.image.linearRgba.width(), loaded.image.linearRgba.height());
    m_original = std::move(loaded.image);
    m_editProxy = std::move(loaded.editProxy);
    m_geometryProxy = {};
    m_renderStageCache.clear();
    m_displayedFile = m_currentFile;
    // Refresh comparison without blanking the displayed pair while rendering.
    m_originalImageSource.clear();
    emit imageInfoChanged();
    if (m_pendingLoadIsFreshOpen) {
        m_histogram.clear();
        emit histogramChanged();
        m_settings = {};
        m_settings.temperature = static_cast<int>(std::lround(
            m_original.referenceWhiteBalanceKelvin.value_or(5200.0)));
        m_sidecarReadOnly = false;
        m_history = {};
        m_inAdjustmentGesture = false;
        if (m_geometryGesture) { m_geometryGesture = false; emit geometryPreviewChanged(); }
        if (std::optional<Sidecar::Document> saved = Sidecar::read(m_currentFile.toLocalFile())) {
            m_settings = std::move(saved->settings);
            m_userImageMetadata.insert(currentFileKey(), std::move(saved->metadata));
            m_history = std::move(saved->history);
            m_sidecarReadOnly = saved->format == Sidecar::Format::Newer;
        }
        m_settings.denoiseBeforeProfile = m_denoiseBeforeProfile;
        // The opening state, or a recipe saved outside the history (batch application).
        // A development resolved otherwise (a moved resource, a new target) is no edit.
        if (RecipeCodec::settingsOf(m_history.current()) == RecipeCodec::encodeSettings(m_settings))
            m_history.replaceCurrent(currentRecipe());
        else
            m_history.record(currentRecipe());
        emit historyChanged();
        emit adjustmentsReset(m_settings.temperature, m_settings.tint);
        emit pointColorsChanged();
        emit geometryChanged();
        emit localMasksChanged();
    emit retouchesChanged();
    }
    if (m_pendingLoadIsFreshOpen) emit hasImageChanged();
    m_settings.neuralDenoiseApplied = m_neuralApplied;
    // Freeze only after restoring the incoming image's settings (not old WB).
    m_currentCacheKey = cacheKeyForFile(m_currentFile.toLocalFile(), m_settings.automaticWhiteBalance,
        m_cameraProfile, m_rawNormalizationEnabled ? rawNormalizationManifest() : QString(),
        m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled,
        m_denoiseBeforeProfile, m_neuralAllowed ? m_neuralStrength : 0, m_neuralModelId);
    if (m_neuralApplied)
        startBeforeDevelopment();
    else
        dropBeforeDevelopment();
    emit rawDevelopmentChanged();
    m_appliedRawDevelopment = rawDevelopmentRecipe();
    m_appliedAutomaticWhiteBalance = m_settings.automaticWhiteBalance;
    const QString readyName = QFileInfo(m_currentFile.toLocalFile()).fileName();
    setStatusMessage(loaded.memoryPressure
        ? tr("Image prête : %1 — ouverte sous pression mémoire : RAM occupée par d'autres applications, "
             "Windows peut ralentir en utilisant le fichier d'échange.").arg(readyName)
        : m_sidecarReadOnly
        ? tr("Image prête : %1 — réglages enregistrés par une version plus récente d'IPR, "
             "ouverts en lecture seule.").arg(readyName)
        : tr("Image prête : %1").arg(readyName));
    scheduleRender();
}

void ImageEditor::releaseCurrentImage()
{
    const bool hadImage = !m_original.isNull();
    const quint64 cacheSizeBefore = m_decodedImageCache.sizeBytes();
    const qsizetype cacheCountBefore = m_decodedImageCache.count();
    if (hadImage && !m_currentCacheKey.isEmpty()) {
        m_decodedImageCache.insert(m_currentCacheKey, retainedCurrentEntry());
    }

    // release() drops this owner's reference immediately. It does not clone
    // pixels and is safe while background workers retain their own ipr::Image.
    m_original = {};
    m_editProxy = {};
    m_geometryProxy = {};
    if (m_geometryGesture) { m_geometryGesture = false; emit geometryPreviewChanged(); }
    dropBeforeDevelopment();
    ++m_sourceGeneration;
    dropDetail();
    m_renderStageCache.clear();
    m_sourceMetadata = {};
    m_importProvenance = {};
    m_importCapabilities = {};
    m_importWarnings.clear();
    m_dcpSource.reset();
    m_masterIsProxy = false;
    m_fullSize = {};
    m_embeddedPreview = false;
    m_restorePreview = m_restoreOriginal = QImage();
    m_displayedFile.clear();
    emit rawDevelopmentChanged();
    m_currentCacheKey.clear();
    m_imageSource.clear();
    m_originalImageSource.clear();
    m_provider->setImages({}, {});
    emit previewChanged();

    if (hadImage)
        emit hasImageChanged();
    if (cacheSizeBefore != m_decodedImageCache.sizeBytes() ||
        cacheCountBefore != m_decodedImageCache.count())
        emit decodedImageCacheChanged();
}

void ImageEditor::scheduleRender()
{
    if (m_original.isNull())
        return;
    cancelPrefetch();
    ++m_requestedRevision;
    m_renderDebounce.start();
    // Pending work is busy too: otherwise a completed RAW worker briefly
    // reported "ready" while the old preview was still displayed (80 ms gap).
    setBusy(true);
    scheduleSidecarSave();
}

void ImageEditor::scheduleSidecarSave()
{
    recordHistory();
    if (!m_original.isNull() && m_currentFile.isLocalFile())
        m_sidecarDebounce.start();
}

bool ImageEditor::saveCurrentSidecar()
{
    if (m_original.isNull() || !m_currentFile.isLocalFile())
        return false;
    if (m_currentFile != m_displayedFile) return false;
    if (m_loadWatcher.isRunning()) return false;
    if (m_sidecarReadOnly) {
        reportNewError(tr("Réglages enregistrés par une version plus récente d'IPR : "
                            "ouverts en lecture seule, non sauvegardés."));
        return false;
    }
    if (!Sidecar::write(m_currentFile.toLocalFile(),
                        {m_settings, m_userImageMetadata.value(currentFileKey()), rawDevelopmentRecipe(), m_history}))
    {
        reportNewError(tr("Échec de sauvegarde des réglages .ipr : vérifiez les permissions du dossier."));
        return false;
    }
    return true;
}

void ImageEditor::startRender()
{
    if (m_original.isNull())
        return;
    if (m_renderWatcher.isRunning()) {
        m_renderPending = true;
        return;
    }

    // Use the capped-resolution edit proxy for the interactive preview (see
    // RenderChain::editProxy()); m_original is reserved for exportImage()/writeImage()
    // so exports are never affected by this optimization.
    const bool fastGeometry = m_geometryGesture && m_inAdjustmentGesture;
    const auto &normal = previewSource();
    if (fastGeometry && m_geometryProxy.isNull()) m_geometryProxy = RenderChain::editProxy(normal, 1024);
    const auto &input = fastGeometry ? m_geometryProxy : normal;
    const auto sourceCopy = std::make_shared<const ColorPipeline::WorkingImage>(ColorPipeline::sharedCopy(input));
    const double pixelScale = previewPixelScale() * double(input.linearRgba.width()) / normal.linearRgba.width();
    const auto canvas = Geometry::canvasSize(m_settings.geometry, normal.linearRgba.size());
    const auto rectangle = Geometry::cropRect(m_settings.geometry, canvas);
    const ipr::Size displaySize = m_cropEditing ? canvas : ipr::Size(rectangle.width, rectangle.height);
    const Settings settings = m_settings;
    // A point removed or replaced since the overlay was asked shows nothing.
    const int overlay = m_pointColorOverlay < int(std::min(settings.pointColor.count, PointColor::MaxPoints))
        ? m_pointColorOverlay : -1;
    const bool wholeCanvas = m_cropEditing;
    // Within a gesture the original can wait for the release (endAdjustmentGesture).
    // With the network applied, the original is the development without it, once ready
    // (startBeforeDevelopment renders again then); never the denoised source.
    const bool beforeReady = !m_neuralApplied || !m_beforeProxy.isNull();
    const bool includeOriginal = beforeReady && (m_originalImageSource.isEmpty() ||
                                 (!m_inAdjustmentGesture && originalFramingStale()));
    const auto originalCopy = m_neuralApplied && includeOriginal
        ? std::make_shared<const ColorPipeline::WorkingImage>(ColorPipeline::sharedCopy(m_beforeProxy)) : nullptr;
    m_activeRevision = m_requestedRevision;
    setBusy(true);
    m_renderWatcher.setFuture(m_tasks.submit<RenderChain::Preview>(
        QStringLiteral("render"), Tasks::Priority::Interactive,
        [sourceCopy, originalCopy, settings, includeOriginal, pixelScale, resources = m_taskResources, overlay,
         maskOverlay = m_localMaskOverlay, retouchOverlay = m_retouchOverlay, wholeCanvas, fastGeometry, displaySize](Tasks::Context &) {
            ImageCacheController *controller = &resources->cache;
            const auto admission = controller->acquire({}, SystemMemory::estimateProcessingPeak(
                quint64(sourceCopy->linearRgba.width()) * quint64(sourceCopy->linearRgba.height())), false).takeResult();
            if (!admission.reservation) { RenderChain::Preview result; result.error = admission.error; return result; }
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            auto result = RenderChain::preview(*sourceCopy, settings, includeOriginal, pixelScale, &resources->stages, overlay,
                                               maskOverlay, wholeCanvas, retouchOverlay, originalCopy.get());
            return fastGeometry ? RenderChain::displaySized(std::move(result), displaySize) : result;
        }).future);
}

RawDecoder::Options ImageEditor::optionsForFile(const QString &path, bool *automaticWhiteBalance, QString *key)
{
    const std::optional<Sidecar::Document> document = Sidecar::read(path);
    const bool saved = document.has_value();
    const RawDevelopment raw = saved ? document->raw : RawDevelopment{};
    RawDecoder::Options options;
    options.automaticWhiteBalance = saved && document->settings.automaticWhiteBalance;
    *automaticWhiteBalance = options.automaticWhiteBalance;
    options.preferCameraNeutral = true;
    options.listDcpProfiles = true;
    options.useNormalizationEnvironment = false;
    options.dcpProfileOverride = relocateResource(raw.profile, raw.profileSha256, "profiles");
    options.lcpProfileOverride = relocateResource(raw.lensProfile, raw.lensProfileSha256, "profiles");
    options.automaticLcpSelection = !raw.lensProfileResolved;
    options.lensCorrectionEnabled = raw.lensCorrectionEnabled;
    options.chromaticAberrationEnabled = raw.chromaticAberrationEnabled;
    options.signedDevelopment = raw.developmentMode == EditRecipe::SignedDevelopment;
    options.retainPreLensSignal = options.signedDevelopment;
    if (!saved || raw.normalizationEnabled) {
        options.normalizationManifest = relocateResource(raw.manifest, raw.manifestSha256, "config");
        if (options.normalizationManifest.isEmpty()) options.normalizationManifest = defaultNormalizationManifest();
    }
    // Invalid pinned resources are handled with warnings by foreground import;
    // speculative work must never cache a silent replacement.
    if ((!raw.profileSha256.isEmpty() && resourceDigest(options.dcpProfileOverride) != raw.profileSha256) ||
        (!raw.lensProfileSha256.isEmpty() && resourceDigest(options.lcpProfileOverride) != raw.lensProfileSha256)) {
        key->clear(); return options;
    }
    *key = cacheKeyForFile(path, options.automaticWhiteBalance, options.dcpProfileOverride,
        options.normalizationManifest, options.lcpProfileOverride, raw.lensProfileResolved,
        options.lensCorrectionEnabled, options.chromaticAberrationEnabled,
        options.retainPreLensSignal && options.signedDevelopment);
    return options;
}

void ImageEditor::startEmbeddedPreview(const QString &fileName)
{
    // About 13 ms on the CR3 corpus versus about 4 s for the development.
    m_embeddedRevision = m_requestedLoadRevision;
    m_embeddedWatcher.setFuture(m_tasks.submit<QImage>(
        QStringLiteral("embeddedPreview"), Tasks::Priority::Interactive, [fileName](Tasks::Context &) {
            return RawDecoder::decodeEmbeddedThumbnail(fileName, QSize(2048, 2048));
        }).future);
}

void ImageEditor::restoreBeforeEmbeddedPreview()
{
    if (!m_embeddedPreview) return;
    m_embeddedPreview = false;
    m_provider->setImages(m_restorePreview, m_restoreOriginal);
    m_imageSource = m_restoreImageSource;
    m_originalImageSource = m_restoreOriginalSource;
    m_restorePreview = m_restoreOriginal = QImage();
    emit previewChanged();
}

QString ImageEditor::plainCacheKey() const
{
    return cacheKeyForFile(m_currentFile.toLocalFile(), m_settings.automaticWhiteBalance, m_cameraProfile,
        m_rawNormalizationEnabled ? rawNormalizationManifest() : QString(), m_lensProfile, m_lensProfileResolved,
        m_lensCorrectionEnabled, m_chromaticAberrationEnabled, m_denoiseBeforeProfile);
}

void ImageEditor::startBeforeDevelopment()
{
    const QString key = plainCacheKey();
    if (!m_beforeProxy.isNull() && m_beforeKey == key)
        return;
    m_beforeProxy = {};
    m_beforeKey.clear();
    if (m_beforeWatcher.isRunning()) {   // never replace a running watcher's future
        if (m_beforeCancel) m_beforeCancel->store(true);
        m_beforePending = true;
        return;
    }
    m_beforePending = false;
    RawDecoder::Options options = developmentOptions();
    options.cfaStageFactory = {};
    m_beforeCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_beforeCancel;
    options.cancellationRequested = [cancel] { return cancel->load(); };
    m_beforeWatcher.setFuture(m_tasks.submit<LoadResult>(
        QStringLiteral("beforeDevelopment"), Tasks::Priority::Interactive,
        [fileName = m_currentFile.toLocalFile(), automaticWhiteBalance = m_settings.automaticWhiteBalance,
         options, key, cancel, resources = m_taskResources](Tasks::Context &) {
            ImageCacheController *controller = &resources->cache;
            const quint64 peak = SystemMemory::estimateDecodePeak(fileName);
            auto admission = controller->acquire(key, peak, false, cancel).takeResult();
            LoadResult before;
            before.cacheKey = key;
            if (admission.cached && cacheAssetsUnchanged(*admission.cached)) {
                const DecodedImageCache::Entry &cached = *admission.cached;
                before.editProxy = ColorPipeline::sharedCopy(cached.editProxy.isNull() ? cached.image : cached.editProxy);
                controller->insert(key, std::move(*admission.cached));   // only looked at
                return before;
            }
            if (admission.cached) {
                admission.cached.reset();
                admission = controller->acquire({}, peak, false, cancel).takeResult();
            }
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            if (!admission.reservation || cancel->load()) {
                before.error = admission.error;
                return before;
            }
            LoadResult loaded = loadFromDisk(fileName, automaticWhiteBalance, options);
            before.error = loaded.error;
            // Only the proxy is kept: the full master is released here.
            before.editProxy = loaded.editProxy.isNull() ? std::move(loaded.image) : std::move(loaded.editProxy);
            return before;
        }).future);
}

void ImageEditor::dropBeforeDevelopment()
{
    if (m_beforeCancel) m_beforeCancel->store(true);
    m_beforePending = false;
    m_beforeProxy = {};
    m_beforeKey.clear();
}

void ImageEditor::cancelPrefetch()
{
    ++m_prefetchGeneration;
    if (m_prefetchCancel) m_prefetchCancel->store(true);
    m_prefetchTimer.stop();
    m_prefetchQueue.clear();
}

void ImageEditor::schedulePrefetch()
{
    if (m_neuralAllowed) { cancelPrefetch(); return; }
    if (busy() || !hasImage() || !m_currentFile.isLocalFile()) return;
    const auto files = m_imageModel.localFiles(0, m_imageModel.count() - 1);
    const int index = files.indexOf(m_currentFile.toLocalFile());
    if (index < 0) return;
    m_prefetchQueue.clear();
    if (index + 1 < files.size()) m_prefetchQueue.append(files[index + 1]);
    if (index > 0) m_prefetchQueue.append(files[index - 1]);
    if (!m_prefetchQueue.isEmpty()) m_prefetchTimer.start();
}

void ImageEditor::startPrefetch()
{
    if (busy() || m_prefetchTaskActive || m_prefetchQueue.isEmpty() ||
        m_decodedImageCache.statistics().pressure) return;
    const auto file = m_prefetchQueue.takeFirst();
    m_prefetchFile = file;
    m_prefetchTaskActive = true;
    m_activePrefetchGeneration = m_prefetchGeneration;
    m_prefetchCancel = std::make_shared<std::atomic_bool>(false);
    m_prefetchTransfer = std::make_shared<LoadResult>();
    const auto transfer = m_prefetchTransfer;
    const auto cancel = m_prefetchCancel;
    m_prefetchWatcher.setFuture(m_tasks.submit<LoadResult>(
        QStringLiteral("prefetch"), Tasks::Priority::Background,
        [file, cancel, resources = m_taskResources, transfer](Tasks::Context &) {
        ImageCacheController *controller = &resources->cache;
        LoadResult result;
        bool automaticWB = false;
        QString key;
        auto options = optionsForFile(file, &automaticWB, &key);
        if (cancel->load() || key.isEmpty()) return result;
        const auto peak = SystemMemory::estimateDecodePeak(file);
        // Do not spend CPU on an image unlikely to fit the retention cap.
        if (peak / 2 > controller->budgetBytes()) return result;
        auto admission = controller->acquire(key, peak, true).takeResult();
        if (admission.cached && !cacheAssetsUnchanged(*admission.cached)) {
            admission.cached.reset();
            admission = controller->acquire({}, peak, true).takeResult();
        }
        if (admission.cached) {
            auto &entry = *admission.cached;
            result.image = std::move(entry.image); result.editProxy = std::move(entry.editProxy);
            result.metadata = std::move(entry.metadata); result.dcpSource = std::move(entry.dcpSource);
            result.fullSize = entry.fullSize; result.proxyOnly = entry.proxyOnly;
            result.cacheKey = key;
            *transfer = sharedCopy(result);
            return result;
        }
        if (!admission.reservation) return result;
        const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
        if (cancel->load()) return result;
        options.cancellationRequested = [cancel] { return cancel->load(); };
        options.retainedDcpSource = std::make_shared<RawDecoder::DcpSource>();
        result = loadFromDisk(file, automaticWB, options);
        if (cancel->load()) return LoadResult{};
        result.cacheKey = key;
        *transfer = sharedCopy(result);
        return result;
    }).future);
}

void ImageEditor::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ImageEditor::refreshBusyState()
{
    setBusy(m_renderDebounce.isActive() || m_loadWatcher.isRunning() || m_renderWatcher.isRunning() ||
            m_exportWatcher.isRunning());
}

void ImageEditor::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void ImageEditor::reportError(const QString &message)
{
    setStatusMessage(message);
    emit errorReported(message);
}

void ImageEditor::reportNewError(const QString &message)
{
    if (m_statusMessage != message)
        reportError(message);
}
