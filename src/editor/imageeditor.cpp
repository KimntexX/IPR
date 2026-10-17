#include "imageeditor.h"

#include "DetailProcessing.h"
#include "ToneRecovery.h"
#include "RawDecoder.h"
#include "RawNormalization.h"
#include "CameraProfileEngine.h"
#include "imageprovider.h"
#include "ThumbnailProvider.h"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDataStream>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QDebug>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <tiffio.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr uint16_t LinearRawPhotometricInterpretation = 34892;

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

QString normalizedExportFormat(QString format)
{
    format = format.trimmed().toUpper();
    return format == QStringLiteral("JPG") ? QStringLiteral("JPEG") : format;
}

QString exportSuffix(const QString &normalizedFormat)
{
    if (normalizedFormat == QStringLiteral("JPEG"))
        return QStringLiteral("jpg");
    if (normalizedFormat == QStringLiteral("PNG"))
        return QStringLiteral("png");
    if (normalizedFormat == QStringLiteral("TIFF"))
        return QStringLiteral("tiff");
    if (normalizedFormat == QStringLiteral("DNG"))
        return QStringLiteral("dng");
    return {};
}

QString localExportPath(const QUrl &target)
{
    if (target.isLocalFile())
        return target.toLocalFile();

    const QString value = target.toString(QUrl::FullyDecoded);
    const bool nativeWindowsPath =
        value.size() >= 3 && value.at(0).isLetter() &&
        value.at(1) == QLatin1Char(':') &&
        (value.at(2) == QLatin1Char('/') || value.at(2) == QLatin1Char('\\'));
    if ((target.scheme().isEmpty() && QDir::isAbsolutePath(value)) ||
        (target.scheme().size() == 1 && nativeWindowsPath))
        return QDir::toNativeSeparators(value);
    return {};
}

QString normalizedExportPath(const QString &target, const QString &normalizedFormat)
{
    const QString suffix = exportSuffix(normalizedFormat);
    if (suffix.isEmpty())
        return {};
    const QFileInfo info(target);
    const QString existingSuffix = info.suffix().toLower();
    const bool matches = existingSuffix == suffix ||
        (normalizedFormat == QStringLiteral("JPEG") && existingSuffix == QStringLiteral("jpeg")) ||
        (normalizedFormat == QStringLiteral("TIFF") && existingSuffix == QStringLiteral("tif"));
    if (matches)
        return target;
    if (existingSuffix.isEmpty())
        return target + QLatin1Char('.') + suffix;
    return info.dir().filePath(info.completeBaseName() + QLatin1Char('.') + suffix);
}

QString availableExportPath(const QString &target)
{
    if (target.isEmpty())
        return {};
    const auto occupied = [](const QString &path) {
        const QFileInfo info(path);
        return info.exists() || info.isSymLink();
    };
    if (!occupied(target))
        return target;

    const QFileInfo info(target);
    QString baseName = info.completeBaseName();
    qulonglong number = 2;
    int padding = 0;
    static const QRegularExpression numberedName(QStringLiteral("^(.*)-([0-9]+)$"));
    const auto match = numberedName.match(baseName);
    if (match.hasMatch()) {
        bool valid = false;
        const QString digits = match.captured(2);
        const qulonglong previous = digits.toULongLong(&valid);
        if (valid && previous < std::numeric_limits<qulonglong>::max()) {
            baseName = match.captured(1);
            number = previous + 1;
            padding = digits.startsWith(QLatin1Char('0')) ? digits.size() : 0;
        }
    }
    for (;;) {
        const QString counter = QString::number(number).rightJustified(padding, QLatin1Char('0'));
        const QString candidate = info.dir().filePath(
            baseName + QLatin1Char('-') + counter + QLatin1Char('.') + info.suffix());
        if (!occupied(candidate))
            return candidate;
        if (number == std::numeric_limits<qulonglong>::max())
            return {};
        ++number;
    }
}

TIFF *openTiffForWrite(const QString &fileName)
{
#ifdef Q_OS_WIN
    return TIFFOpenW(reinterpret_cast<const wchar_t *>(fileName.utf16()), "w");
#else
    return TIFFOpen(fileName.toUtf8().constData(), "w");
#endif
}

bool writeLinearDng(const ColorPipeline::WorkingImage &image,
                    const QString &fileName, QString *error)
{
    const ColorPipeline::WorkingImage gamutMapped =
        ColorPipeline::mapToSrgbGamut(image, error);
    if (gamutMapped.isNull())
        return false;

    TIFF *tiff = openTiffForWrite(fileName);
    if (!tiff) {
        if (error)
            *error = QStringLiteral("Impossible de créer le fichier DNG.");
        return false;
    }

    constexpr uint8_t dngVersion[] = {1, 4, 0, 0};
    constexpr uint8_t dngBackwardVersion[] = {1, 1, 0, 0};
    constexpr float blackLevel[] = {0.0f, 0.0f, 0.0f};
    constexpr uint32_t whiteLevel[] = {65535, 65535, 65535};
    constexpr float asShotNeutral[] = {1.0f, 1.0f, 1.0f};
    // DNG ColorMatrix maps XYZ at CalibrationIlluminant1 (D65) to the
    // stored linear RGB, not RGB to XYZ. ICC must describe linear samples.
    const cv::Matx33f xyzToStoredRgb(WorkingColor::srgbToXyz().inv());
    const QByteArray uniqueCameraModel =
        QByteArrayLiteral("IPR Rendered Linear DNG");
    const QByteArray software = QByteArrayLiteral("IPR");
    const QByteArray iccProfile =
        QColorSpace(QColorSpace::SRgbLinear).iccProfile();

    bool configured =
        TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH,
                     static_cast<uint32_t>(gamutMapped.linearRgba.cols)) == 1 &&
        TIFFSetField(tiff, TIFFTAG_IMAGELENGTH,
                     static_cast<uint32_t>(gamutMapped.linearRgba.rows)) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
        TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
        TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC,
                     LinearRawPhotometricInterpretation) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1) == 1 &&
        TIFFSetField(tiff, TIFFTAG_MAKE, "IPR") == 1 &&
        TIFFSetField(tiff, TIFFTAG_MODEL, "Rendered Linear DNG") == 1 &&
        TIFFSetField(tiff, TIFFTAG_SOFTWARE, software.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGVERSION, dngVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_DNGBACKWARDVERSION,
                     dngBackwardVersion) == 1 &&
        TIFFSetField(tiff, TIFFTAG_UNIQUECAMERAMODEL,
                     uniqueCameraModel.constData()) == 1 &&
        TIFFSetField(tiff, TIFFTAG_BLACKLEVEL, 3, blackLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_WHITELEVEL, 3, whiteLevel) == 1 &&
        TIFFSetField(tiff, TIFFTAG_COLORMATRIX1, 9, xyzToStoredRgb.val) == 1 &&
        TIFFSetField(tiff, TIFFTAG_ASSHOTNEUTRAL, 3, asShotNeutral) == 1 &&
        TIFFSetField(tiff, TIFFTAG_CALIBRATIONILLUMINANT1, 21) == 1;
    if (!iccProfile.isEmpty()) {
        configured = configured &&
            TIFFSetField(tiff, TIFFTAG_ICCPROFILE,
                         static_cast<uint32_t>(iccProfile.size()),
                         const_cast<char *>(iccProfile.constData())) == 1;
    }
    if (!configured) {
        TIFFClose(tiff);
        QFile::remove(fileName);
        if (error)
            *error = QStringLiteral("Impossible d'écrire les métadonnées DNG.");
        return false;
    }

    std::vector<quint16> row(
        static_cast<size_t>(gamutMapped.linearRgba.cols) * 3);
    for (int y = 0; y < gamutMapped.linearRgba.rows; ++y) {
        const cv::Vec4f *source = gamutMapped.linearRgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < gamutMapped.linearRgba.cols; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                row[static_cast<size_t>(x) * 3 + channel] =
                    static_cast<quint16>(std::lround(
                        std::clamp(source[x][channel], 0.0f, 1.0f) * 65535.0f));
            }
        }
        if (TIFFWriteScanline(tiff, row.data(), static_cast<uint32_t>(y), 0) < 0) {
            TIFFClose(tiff);
            QFile::remove(fileName);
            if (error)
                *error = QStringLiteral("Impossible d'écrire les pixels DNG.");
            return false;
        }
    }

    TIFFClose(tiff);
    return true;
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

void ImageListModel::setFolder(const QUrl &folder)
{
    beginResetModel();
    m_entries.clear();

    const QDir directory(folder.toLocalFile());
    const QFileInfoList files = directory.entryInfoList(
        supportedImagePatterns(), QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files)
        m_entries.append({file.fileName(), QUrl::fromLocalFile(file.absoluteFilePath())});

    endResetModel();
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
    , m_decodedImageCache(nullptr, std::move(memorySampler))
{
    m_embeddedPool.setMaxThreadCount(1);
    connect(&m_embeddedWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        QImage preview;
        try { preview = m_embeddedWatcher.result(); } catch (...) { /* optional interim view */ }
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
    m_prefetchPool.setMaxThreadCount(1);
    m_prefetchPool.setExpiryTimeout(5000);
    m_prefetchTimer.setSingleShot(true);
    m_prefetchTimer.setInterval(250);
    connect(&m_prefetchTimer, &QTimer::timeout, this, &ImageEditor::startPrefetch);
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
        try { loaded = m_prefetchWatcher.future().takeResult(); } catch (...) { /* speculative result discarded */ }
        m_prefetchTaskActive = false;
        m_prefetchTransfer.reset();
        if (m_activePrefetchGeneration == m_prefetchGeneration && m_prefetchCancel &&
            !m_prefetchCancel->load() && !loaded.image.isNull()) {
            const QSize fullSize = loaded.proxyOnly ? loaded.fullSize
                : QSize(loaded.image.linearRgba.cols, loaded.image.linearRgba.rows);
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

    connect(&m_renderWatcher, &QFutureWatcher<RenderResult>::finished,
            this, [this] {
        RenderResult rendered;
        try { rendered = m_renderWatcher.result(); }
        catch (...) { rendered.error = tr("Échec du rendu : ressources insuffisantes ou traitement interrompu."); }
        if (!rendered.error.isEmpty() && m_activeRevision == m_requestedRevision)
            setStatusMessage(rendered.error);
        if (m_activeRevision == m_requestedRevision &&
            !rendered.preview.isNull()) {
            // Publish the pair together. The neutral baseline is encoded only
            // once per decoded source, never on each slider change.
            const QImage original = rendered.originalPreview.isNull()
                ? m_provider->originalImage() : rendered.originalPreview;
            m_provider->setImages(rendered.preview, original);
            if (!rendered.originalPreview.isNull())
                m_originalImageSource = QStringLiteral("image://editor/original?revision=%1")
                                            .arg(m_activeRevision);
            m_imageSource = QStringLiteral("image://editor/preview?revision=%1")
                                .arg(m_activeRevision);
            m_histogram = rendered.histogram;
            emit previewChanged();
            emit histogramChanged();
        }

        if (m_renderPending) {
            m_renderPending = false;
            startRender();
        } else {
            refreshBusyState();
            if (!busy()) schedulePrefetch();
        }
    });

    connect(&m_exportWatcher, &QFutureWatcher<ExportResult>::finished, this, [this] {
        ExportResult result;
        try { result = m_exportWatcher.result(); }
        catch (...) { result = {false, tr("Échec de l'export : ressources insuffisantes ou traitement interrompu.")}; }
        setStatusMessage(result.message);
        refreshBusyState();
        if (!busy()) schedulePrefetch();
    });

    // Decoding (especially RAW demosaicing) can take seconds on large files;
    // running it on QtConcurrent keeps the UI thread responsive, mirroring
    // the pattern already used for rendering and export.
    connect(&m_loadWatcher, &QFutureWatcher<LoadResult>::finished, this, [this] {
        LoadResult loaded;
        try { loaded = m_loadWatcher.future().takeResult(); }
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
    // Workers waiting for memory must not delay or deadlock closing.
    m_decodedImageCache.abortWaiting();
    if (m_prefetchWatcher.future().isStarted()) m_prefetchWatcher.waitForFinished();
    // Final flush, even when the 450 ms debounce has not elapsed yet.
    m_sidecarDebounce.stop();
    // Qt 6.4 can wait forever on an unstarted/consumed future (takeResult).
    if (m_loadWatcher.future().isStarted())
        m_loadWatcher.waitForFinished();
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
    if (m_renderWatcher.future().isStarted())
        m_renderWatcher.waitForFinished();
    if (m_exportWatcher.future().isStarted())
        m_exportWatcher.waitForFinished();
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
        setStatusMessage(tr("Manifeste invalide ou supérieur à 1 Mio."));
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

    const QString suffix = exportSuffix(normalizedExportFormat(format));
    if (suffix.isEmpty())
        return {};

    const QFileInfo source(m_currentFile.toLocalFile());
    const QString baseName = source.completeBaseName().isEmpty()
                                 ? QStringLiteral("image")
                                 : source.completeBaseName();
    QDir destination = source.dir();
    if (!previousTarget.isEmpty()) {
        const QString previousPath = localExportPath(previousTarget);
        if (previousPath.isEmpty())
            return {};
        destination = QFileInfo(previousPath).dir();
    }
    return QUrl::fromLocalFile(availableExportPath(destination.filePath(
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

void ImageEditor::setToneCurve(int channel, const QVariantList &points)
{
    if (channel < LuminanceCurve || channel > BlueCurve)
        return;

    QVector<ToneCurvePoint> normalized;
    normalized.reserve(points.size() + 2);
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        bool xOk = false;
        bool yOk = false;
        const float x = point.value(QStringLiteral("x")).toFloat(&xOk);
        const float y = point.value(QStringLiteral("y")).toFloat(&yOk);
        if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y) ||
            x <= 0.0f || x >= 1.0f)
            continue;
        normalized.append({x, std::clamp(y, 0.0f, 1.0f)});
    }
    std::sort(normalized.begin(), normalized.end(),
              [](const ToneCurvePoint &left, const ToneCurvePoint &right) {
                  return left.x < right.x;
              });

    QVector<ToneCurvePoint> curve;
    curve.reserve(normalized.size() + 2);
    curve.append({0.0f, 0.0f});
    for (const ToneCurvePoint &point : normalized) {
        if (point.x - curve.constLast().x >= 0.001f)
            curve.append(point);
    }
    curve.append({1.0f, 1.0f});

    m_settings.toneCurves[static_cast<size_t>(channel)] = std::move(curve);
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
        setStatusMessage(tr("Le dossier sélectionné n'est pas un dossier local."));
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
        setStatusMessage(tr("Le fichier sélectionné n'est pas local."));
        return;
    }

    const QFileInfo fileInfo(fileUrl.toLocalFile());
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        setStatusMessage(tr("Le fichier sélectionné est introuvable."));
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
    if (!fileUrl.isLocalFile())
        return;
    const QFileInfo fileInfo(fileUrl.toLocalFile());
    const QString key = fileInfo.canonicalFilePath().isEmpty()
                            ? fileInfo.absoluteFilePath()
                            : fileInfo.canonicalFilePath();
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

void ImageEditor::clearImageSelection()
{
    if (m_selectedImageKeys.isEmpty())
        return;
    m_selectedImageKeys.clear();
    emit selectionChanged();
}

void ImageEditor::applyAdjustmentsToSelection()
{
    if (m_selectedImageKeys.isEmpty() || m_currentFile.isEmpty())
        return;
    int written = 0;
    for (const QString &fileName : std::as_const(m_selectedImageKeys)) {
        Settings targetSettings;
        UserImageMetadata targetMetadata;
        RawDevelopment targetRaw;
        if (!readSidecar(fileName, &targetSettings, &targetMetadata, &targetRaw))
            targetMetadata = m_userImageMetadata.value(fileName);
        if (QFileInfo(fileName).canonicalFilePath() == currentFileKey()) {
            targetRaw = rawDevelopmentRecipe();
            targetSettings.automaticWhiteBalance = m_settings.automaticWhiteBalance;
        }
        // Retouches can be copied across cameras; RAW profiles cannot.
        Settings copied = m_settings;
        copied.automaticWhiteBalance = targetSettings.automaticWhiteBalance;
        if (writeSidecar(fileName, copied, targetMetadata, targetRaw))
            ++written;
    }
    setStatusMessage(tr("Réglages appliqués à %n image(s).", nullptr, written));
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
    return component == 0 ? m_settings.gradingHue[static_cast<size_t>(zone)]
                          : m_settings.gradingSaturation[static_cast<size_t>(zone)];
}

double ImageEditor::adjustmentValue(const QString &name) const
{
    if (name == QStringLiteral("brightness")) return m_settings.brightness;
    if (name == QStringLiteral("contrast")) return m_settings.contrast;
    if (name == QStringLiteral("blur")) return m_settings.blur;
    if (name == QStringLiteral("shadows")) return m_settings.shadows;
    if (name == QStringLiteral("blacks")) return m_settings.blacks;
    if (name == QStringLiteral("highlights")) return m_settings.highlights;
    if (name == QStringLiteral("whites")) return m_settings.whites;
    if (name == QStringLiteral("temperature")) return m_settings.temperature;
    if (name == QStringLiteral("tint")) return m_settings.tint;
    if (name == QStringLiteral("vibrance")) return m_settings.vibrance;
    if (name == QStringLiteral("saturation")) return m_settings.saturation;
    if (name == QStringLiteral("texture")) return m_settings.texture;
    if (name == QStringLiteral("clarity")) return m_settings.clarity;
    if (name == QStringLiteral("dehaze")) return m_settings.dehaze;
    if (name == QStringLiteral("grain")) return m_settings.grain;
    if (name == QStringLiteral("denoiseLuminance")) return m_settings.denoiseLuminance;
    if (name == QStringLiteral("denoiseChrominance")) return m_settings.denoiseChrominance;
    if (name == QStringLiteral("sharpeningAmount")) return m_settings.sharpeningAmount;
    if (name == QStringLiteral("sharpeningRadius")) return m_settings.sharpeningRadius;
    if (name == QStringLiteral("sharpeningMasking")) return m_settings.sharpeningMasking;
    if (name == QStringLiteral("grayscale")) return m_settings.grayscale ? 1.0 : 0.0;
    if (name == QStringLiteral("automaticWhiteBalance"))
        return m_settings.automaticWhiteBalance ? 1.0 : 0.0;
    return 0.0;
}

void ImageEditor::loadImage(const QUrl &fileUrl)
{
    if (!fileUrl.isLocalFile()) {
        setStatusMessage(tr("Le fichier sélectionné n'est pas local."));
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


void ImageEditor::setBrightness(float value)
{
    value = qBound(-5.00, value, 5.00);
    if (m_settings.brightness == value)
        return;
    m_settings.brightness = value;
    scheduleRender();
}

void ImageEditor::setContrast(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.contrast == value)
        return;
    m_settings.contrast = value;
    scheduleRender();
}

void ImageEditor::setBlur(int value)
{
    value = qBound(0, value, 20);
    if (m_settings.blur == value)
        return;
    m_settings.blur = value;
    scheduleRender();
}

void ImageEditor::setGrayscale(bool enabled)
{
    if (m_settings.grayscale == enabled)
        return;
    m_settings.grayscale = enabled;
    scheduleRender();
}

void ImageEditor::setShadows(int value) {
    value = qBound(-100, value, 100);
    if (m_settings.shadows == value)
        return;
    m_settings.shadows = value;
    scheduleRender();
}
void ImageEditor::setBlacks(int value) {
    value = qBound(-100, value, 100);
    if (m_settings.blacks == value)
        return;
    m_settings.blacks = value;
    scheduleRender();
}
void ImageEditor::setHighlights(int value) {
    value = qBound(-100, value, 100);
    if (m_settings.highlights == value)
        return;
    m_settings.highlights = value;
    scheduleRender();
}
void ImageEditor::setWhites(int value) {
    value = qBound(-100, value, 100);
    if (m_settings.whites == value)
        return;
    m_settings.whites = value;
    scheduleRender();
}
void ImageEditor::setTemperature(int value)
{
    value = qBound(2000, value, 50000);
    if (m_settings.temperature == value)
        return;
    m_settings.temperature = value;
    scheduleRender();
}
void ImageEditor::setTint(int value) {
    value = qBound(-150, value, 150);
    if (m_settings.tint == value)
        return;
    m_settings.tint = value;
    scheduleRender();
}
void ImageEditor::setVibrance(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.vibrance == value)
        return;
    m_settings.vibrance = value;
    scheduleRender();
}
void ImageEditor::setSaturation(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.saturation == value)
        return;
    m_settings.saturation = value;
    scheduleRender();
}
void ImageEditor::setTexture(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.texture == value)
        return;
    m_settings.texture = value;
    scheduleRender();
}
void ImageEditor::setClarity(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.clarity == value)
        return;
    m_settings.clarity = value;
    scheduleRender();
}
void ImageEditor::setDehaze(int value)
{
    value = qBound(-100, value, 100);
    if (m_settings.dehaze == value)
        return;
    m_settings.dehaze = value;
    scheduleRender();
}
void ImageEditor::setGrain(int value)
{
    value = qBound(0, value, 100);
    if (m_settings.grain == value)
        return;
    m_settings.grain = value;
    scheduleRender();
}

void ImageEditor::setDenoiseLuminance(int value)
{
    value = qBound(0, value, 100);
    if (m_settings.denoiseLuminance == value)
        return;
    m_settings.denoiseLuminance = value;
    scheduleRender();
}

void ImageEditor::setDenoiseChrominance(int value)
{
    value = qBound(0, value, 100);
    if (m_settings.denoiseChrominance == value)
        return;
    m_settings.denoiseChrominance = value;
    scheduleRender();
}

void ImageEditor::setSharpeningAmount(int value)
{
    value = qBound(0, value, 200);
    if (m_settings.sharpeningAmount == value)
        return;
    m_settings.sharpeningAmount = value;
    scheduleRender();
}

void ImageEditor::setSharpeningRadius(float value)
{
    value = qBound(0.3f, value, 3.0f);
    if (std::abs(m_settings.sharpeningRadius - value) < 0.0001f)
        return;
    m_settings.sharpeningRadius = value;
    scheduleRender();
}

void ImageEditor::setSharpeningMasking(int value)
{
    value = qBound(0, value, 100);
    if (m_settings.sharpeningMasking == value)
        return;
    m_settings.sharpeningMasking = value;
    scheduleRender();
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
void ImageEditor::setAutomaticWhiteBalance(bool enabled)
{
    if (busy() || m_settings.automaticWhiteBalance == enabled)
        return;
    m_settings.automaticWhiteBalance = enabled;
    emit rawDevelopmentChanged();
    emit adjustmentsReset(m_settings.temperature, m_settings.tint);
    scheduleSidecarSave();

    if (m_original.isNull() || !m_currentFile.isLocalFile())
        return;

    const QString fileName = m_currentFile.toLocalFile();
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (!RawDecoder::supportedExtensions().contains(extension))
        return;

    beginLoad(fileName, extension, enabled, false);
}

void ImageEditor::resetAdjustments()
{
    if (m_original.isNull())
        return;
    const bool automaticWhiteBalance = m_settings.automaticWhiteBalance;
    m_settings = {};
    m_settings.automaticWhiteBalance = automaticWhiteBalance;
    m_settings.temperature = static_cast<int>(std::lround(
        m_original.referenceWhiteBalanceKelvin.value_or(5200.0)));
    emit toneCurvesReset();
    emit adjustmentsReset(m_settings.temperature, m_settings.tint);
    emit rawDevelopmentChanged();
    scheduleRender();
}

void ImageEditor::exportImage(const QUrl &target, const QString &format, int quality)
{
    if (m_loadWatcher.isRunning()) {
        setStatusMessage(tr("Attendez la fin du développement RAW avant l'export."));
        return;
    }
    if (m_original.isNull()) {
        setStatusMessage(tr("Ouvrez une image avant de l'exporter."));
        return;
    }
    const QString targetPath = localExportPath(target);
    if (targetPath.isEmpty()) {
        setStatusMessage(tr("La destination d'export doit être un fichier local."));
        return;
    }
    if (m_exportWatcher.isRunning()) {
        setStatusMessage(tr("Un export est déjà en cours."));
        return;
    }

    const ColorPipeline::WorkingImage source = m_original;
    const Settings settings = m_settings;
    const QString fileName = targetPath;
    // Never export a restored proxy: rebuild the full master with this recipe.
    const bool rebuild = m_masterIsProxy;
    const QString sourceFile = m_currentFile.toLocalFile();
    const RawDecoder::Options options = developmentOptions();
    setBusy(true);
    setStatusMessage(rebuild ? tr("Export en cours : reconstruction pleine résolution…") : tr("Export en cours…"));
    cancelPrefetch();
    auto *controller = &m_decodedImageCache;
    m_exportWatcher.setFuture(QtConcurrent::run(
        [source, settings, fileName, format, quality, controller, rebuild, sourceFile, options] {
            const quint64 peak = rebuild ? SystemMemory::estimateDecodePeak(sourceFile)
                                         : SystemMemory::estimateProcessingPeak(source.linearRgba.total());
            const auto admission = controller->acquire({}, peak, false).result();
            if (!admission.reservation) return ExportResult{false, admission.error};
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            if (!rebuild)
                return writeImage(source, settings, fileName, format, quality);
            auto master = loadFromDisk(sourceFile, settings.automaticWhiteBalance, options);
            if (master.image.isNull())
                return ExportResult{false, tr("Échec de la reconstruction pleine résolution : %1").arg(master.error)};
            return writeImage(master.image, settings, fileName, format, quality);
        }));
}

ColorPipeline::WorkingImage ImageEditor::renderWorkingImage(
    const ColorPipeline::WorkingImage &source, const Settings &settings)
{
    if (source.isNull() || source.colorSpace != ColorPipeline::workingColorSpace() ||
        source.linearRgba.type() != CV_32FC4 || !cv::checkRange(source.linearRgba, true))
        return {};

    const int referenceTemperature = static_cast<int>(std::lround(
        source.referenceWhiteBalanceKelvin.value_or(5200.0)));
    const bool hasToneCurves = std::any_of(
        settings.toneCurves.cbegin(), settings.toneCurves.cend(),
        [](const QVector<ToneCurvePoint> &curve) { return !curve.isEmpty(); });
    // A neutral pipeline returns its shallow source and therefore avoids all
    // RGB split/merge buffers and per-pixel passes.
    if (!settings.grayscale && settings.brightness == 0.0f &&
        settings.contrast == 0 && settings.blur == 0 && settings.shadows == 0 &&
        settings.blacks == 0 && settings.highlights == 0 && settings.whites == 0 &&
        settings.temperature == referenceTemperature && settings.tint == 0 &&
        settings.vibrance == 0 && settings.saturation == 0 &&
        settings.texture == 0 && settings.clarity == 0 && settings.dehaze == 0 &&
        settings.grain == 0 && settings.denoiseLuminance == 0 &&
        settings.denoiseChrominance == 0 && settings.sharpeningAmount == 0 &&
        std::all_of(settings.mixerHue.cbegin(), settings.mixerHue.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.mixerSaturation.cbegin(), settings.mixerSaturation.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.mixerLuminance.cbegin(), settings.mixerLuminance.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.gradingSaturation.cbegin(), settings.gradingSaturation.cend(),
                    [](int value) { return value == 0; }) && !hasToneCurves)
        return source;

    std::vector<cv::Mat> channels;
    cv::split(source.linearRgba, channels);
    cv::Mat adjusted;
    cv::merge(std::vector<cv::Mat>{channels[0], channels[1], channels[2]}, adjusted);

    const bool whiteBalanceEdit = settings.temperature != referenceTemperature || settings.tint != 0;
    if (source.dcpContext && whiteBalanceEdit) {
        QString error;
        if (source.dcpContext->cameraRgb.size() != adjusted.size() ||
            !CameraProfileEngine::renderWhiteBalance(*source.dcpContext,
                settings.temperature, settings.tint, adjusted, &error)) {
            qWarning() << "Échec de la balance capteur avant DCP :" << error;
            return {};
        }
    }

    if (settings.grayscale) {
        for (int y = 0; y < adjusted.rows; ++y) {
            auto *row = adjusted.ptr<cv::Vec3f>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                const float value = WorkingColor::luminance(row[x]);
                row[x] = {value, value, value};
            }
        }
    }

    // One brightness unit is one EV in linear light.
    if (settings.brightness != 0.0f) {
        const float exposureFactor = static_cast<float>(
            std::exp2(static_cast<double>(settings.brightness)));
        adjusted *= exposureFactor;
    }

    // Contrast is an asymmetric S curve around 18% linear gray.  A single
    // factor from 0 to 2 made -100 collapse every pixel to the pivot and
    // made +100 crush too much of the toe.  Keep the midtone anchored while
    // controlling the toe and shoulder independently.
    const float contrastAmount =
        static_cast<float>(settings.contrast) / 100.0f;
    const auto luminanceOf = [](const cv::Vec3f &pixel) {
        return std::max(0.0f, WorkingColor::luminance(pixel));
    };
    const auto applyLuminance = [](cv::Vec3f &pixel, float sourceLuminance,
                                   float targetLuminance, bool boundedShadowLift = false) {
        // The bounded positive-shadow gain is safe below the legacy floor;
        // do not retain a discontinuity at Y=1e-6 for the corrected branch.
        if (sourceLuminance > 1e-6f || (boundedShadowLift && sourceLuminance > 0.0f))
            pixel *= std::max(0.0f, targetLuminance) / sourceLuminance;
    };
    const auto smoothStep = [](float edge0, float edge1, float value) {
        const float t = std::clamp((value - edge0) / (edge1 - edge0),
                                   0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const auto applyContrast = [contrastAmount, smoothStep](float luminance) {
        constexpr float pivot = 0.18f;
        if (std::abs(contrastAmount) <= 1e-6f)
            return luminance;

        const float amount = std::abs(contrastAmount);
        float contrastFactor = 1.0f;
        if (luminance <= pivot) {
            // Only the deepest portion of the toe reaches the full extreme;
            // tones approaching the pivot stay visually neutral.
            const float toeWeight = 1.0f - smoothStep(0.01f, 0.09f,
                                                       luminance);
            contrastFactor = contrastAmount < 0.0f
                ? 1.0f - 0.25f * amount * toeWeight
                : 1.0f + 0.40f * amount * toeWeight;
        } else {
            // A flattened image needs a restrained shoulder, whereas added
            // contrast can use a stronger upper slope without crushing the
            // toe (which is handled by the branch above).
            contrastFactor = contrastAmount < 0.0f
                ? 1.0f - 0.40f * amount
                : 1.0f + 1.30f * amount;
        }
        if (luminance <= pivot)
            return pivot * std::pow(luminance / pivot, contrastFactor);
        if (luminance < 1.0f) {
            const float normalized = (luminance - pivot) / (1.0f - pivot);
            return pivot + (1.0f - pivot) *
                               (1.0f - std::pow(1.0f - normalized,
                                                contrastFactor));
        }
        return 1.0f + (luminance - 1.0f) * contrastFactor;
    };
    if (settings.contrast != 0) {
        for (int y = 0; y < adjusted.rows; ++y) {
            cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                const float luminance = luminanceOf(row[x]);
                applyLuminance(row[x], luminance, applyContrast(luminance));
            }
        }
    }

    QString colorError;
    if (!source.dcpContext && whiteBalanceEdit &&
        !ColorPipeline::applyTemperatureTint(
            adjusted, settings.temperature,
            source.referenceWhiteBalanceKelvin.value_or(5200.0),
            settings.tint, &colorError)) {
        qWarning() << "Échec de la balance température/nuance :" << colorError;
        return {};
    }

    const float shadowAdjustment =
        static_cast<float>(settings.shadows) / 100.0f;
    const float blackAdjustment =
        static_cast<float>(settings.blacks) / 100.0f;
    const float highlightAdjustment =
        static_cast<float>(settings.highlights) / 100.0f;
    const float whiteAdjustment =
        static_cast<float>(settings.whites) / 100.0f;
    if (settings.shadows != 0 || settings.blacks != 0 ||
        settings.highlights != 0 || settings.whites != 0) {
    for (int y = 0; y < adjusted.rows; ++y) {
        for (int x = 0; x < adjusted.cols; ++x) {
            cv::Vec3f &pixel = adjusted.at<cv::Vec3f>(y, x);
            const float luminance = luminanceOf(pixel);
            const float shadowsMask =
                1.0f - smoothStep(0.10f, 0.56f, luminance);
            // Blacks is deliberately confined to the toe.  Shadows covers a
            // wider range, but both masks are evaluated from the source
            // luminance so an adjustment cannot extend its own range.
            const float blacksMask =
                1.0f - smoothStep(0.00f, 0.18f, luminance);
            // The former upper-tone masks began so late that almost no
            // photographic tone activated them.  Highlights now covers the
            // upper midtones; Whites remains distinct, but starts at the
            // luminous end of the same range using the brightest channel.
            const float highlightsMask =
                smoothStep(0.08f, 0.70f, luminance);
            // The positive branches are deliberately broader than recovery:
            // pushing a slider to +100 must visibly open bright midtones,
            // whereas the -100 branches stay confined to the v2 recovery
            // masks that were validated against the reference corpus.
            const float highlightLiftMask =
                smoothStep(0.02f, 0.50f, luminance);
            const float maximumChannel =
                std::max({pixel[0], pixel[1], pixel[2], 0.0f});
            // The lift guard is defined against the fixed sRGB delivery gamut,
            // not the larger working cube: Y<1 and Rec.2020 RGB<1 do not imply
            // that a saturated colour still has headroom in the sRGB export.
            // This scalar guard does not clip or convert the master buffer.
            float deliveryPeak = maximumChannel;
            if (shadowAdjustment > 0.f || highlightAdjustment > 0.f) {
                const cv::Vec3d delivery = WorkingColor::toSrgb() * cv::Vec3d(pixel);
                deliveryPeak = float(std::max({delivery[0], delivery[1], delivery[2], 0.0}));
            }
            const float whitesMask =
                smoothStep(0.10f, 0.65f, maximumChannel);
            const float whiteLiftMask =
                smoothStep(0.02f, 0.50f, maximumChannel);

            const float blackLift =
                std::max(0.0f, blackAdjustment) * blacksMask;
            const float blackDarkening =
                std::max(0.0f, -blackAdjustment) * blacksMask;
            // The darkening branch is deliberately more concentrated than
            // the lifting branch.  A uniform multiplier would crush the
            // low and middle ranges together at -100.
            constexpr float shadowDarkeningRange = 0.45f;
            constexpr float shadowDarkeningAmplitude = 0.57f;
            const float shadowDarkeningShape = 1.0f - std::sqrt(
                std::clamp(luminance / shadowDarkeningRange, 0.0f, 1.0f));
            const float shadowDarkening = shadowDarkeningAmplitude *
                std::max(0.0f, -shadowAdjustment) * shadowsMask *
                shadowDarkeningShape;
            const float highlightDarkening =
                std::max(0.0f, -highlightAdjustment) * highlightsMask;
            const float whiteLift =
                std::max(0.0f, whiteAdjustment) * whiteLiftMask;
            const float whiteDarkening =
                std::max(0.0f, -whiteAdjustment) * whitesMask;

            float targetLuminance = luminance;

            // Blacks moves only the foot of the curve.  Its positive branch
            // is zero at the exact black point and at the end of the toe,
            // while its negative branch can establish a deeper black point.
            // This avoids treating "Blacks" as a generic shadow lift.
            constexpr float blackToeEnd = 0.14f;
            constexpr float blackLiftAmplitude = 0.08f;
            const float toePosition = std::clamp(
                luminance / blackToeEnd, 0.0f, 1.0f);
            const float blackToeShape = std::sqrt(toePosition) *
                (1.0f - toePosition);
            targetLuminance +=
                blackLiftAmplitude * blackLift * blackToeShape;

            // Bounded gain replaces an additive pedestal with unbounded gain
            // and reversed deepest-tone ordering. Absolute black is unchanged.
            targetLuminance += ToneRecovery::positiveShadowDelta(
                luminance, deliveryPeak, shadowAdjustment);

            // When both sliders darken, retain the strongest local intent
            // rather than multiplying two near-black reductions together.
            // This keeps usable shadow detail while the controls remain
            // semantically independent.
            const float darkening = std::max(
                blackDarkening, shadowDarkening);
            targetLuminance *= 1.0f - darkening;

            // Highlights shapes the upper midtones.  Whites has its own
            // point-white curve; applying it after Highlights lets either
            // control stand alone while still composing predictably when
            // both are used.
            constexpr float highlightDarkeningAmplitude = 0.52f;
            constexpr float whiteLiftAmplitude = 0.95f;
            constexpr float whiteDarkeningAmplitude = 0.22f;
            targetLuminance += ToneRecovery::positiveHighlightDelta(
                luminance, targetLuminance, deliveryPeak,
                highlightAdjustment, highlightLiftMask);
            targetLuminance *=
                1.0f - highlightDarkeningAmplitude * highlightDarkening;
            targetLuminance += (1.0f - std::min(targetLuminance, 1.0f)) *
                whiteLiftAmplitude * whiteLift;
            targetLuminance *=
                1.0f - whiteDarkeningAmplitude * whiteDarkening;
            applyLuminance(pixel, luminance, targetLuminance, settings.shadows > 0);
        }
    }
    }

    const float saturationFactor =
        1.0f + static_cast<float>(settings.saturation) / 100.0f;
    const float vibranceAmount =
        static_cast<float>(settings.vibrance) / 100.0f;
    if (settings.saturation != 0 || settings.vibrance != 0) {
    for (int y = 0; y < adjusted.rows; ++y) {
        cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
        for (int x = 0; x < adjusted.cols; ++x) {
            cv::Vec3f &pixel = row[x];
            const float luminance = luminanceOf(pixel);
            const float maximum = std::max({pixel[0], pixel[1], pixel[2]});
            const float minimum = std::min({pixel[0], pixel[1], pixel[2]});
            const float chroma = maximum > 1e-6f
                                     ? std::clamp((maximum - minimum) / maximum,
                                                  0.0f, 1.0f)
                                     : 0.0f;
            const float vibranceFactor =
                1.0f + vibranceAmount * (1.0f - chroma);
            pixel = cv::Vec3f(luminance, luminance, luminance) +
                    (pixel - cv::Vec3f(luminance, luminance, luminance)) *
                        std::max(0.0f, saturationFactor * vibranceFactor);
        }
    }

    }

    const bool hasColorMixer =
        std::any_of(settings.mixerHue.cbegin(), settings.mixerHue.cend(),
                    [](int value) { return value != 0; }) ||
        std::any_of(settings.mixerSaturation.cbegin(), settings.mixerSaturation.cend(),
                    [](int value) { return value != 0; }) ||
        std::any_of(settings.mixerLuminance.cbegin(), settings.mixerLuminance.cend(),
                    [](int value) { return value != 0; });
    const bool hasColorGrading = std::any_of(
        settings.gradingSaturation.cbegin(), settings.gradingSaturation.cend(),
        [](int value) { return value != 0; });
    if (hasColorMixer || hasColorGrading) {
        const auto hsvToRgb = [](float hue, float saturation, float value) {
            hue = std::fmod(hue + 360.0f, 360.0f);
            saturation = std::clamp(saturation, 0.0f, 1.0f);
            const float chroma = value * saturation;
            const float secondary = chroma *
                (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
            const float offset = value - chroma;
            cv::Vec3f rgb;
            if (hue < 60.0f) rgb = {chroma, secondary, 0.0f};
            else if (hue < 120.0f) rgb = {secondary, chroma, 0.0f};
            else if (hue < 180.0f) rgb = {0.0f, chroma, secondary};
            else if (hue < 240.0f) rgb = {0.0f, secondary, chroma};
            else if (hue < 300.0f) rgb = {secondary, 0.0f, chroma};
            else rgb = {chroma, 0.0f, secondary};
            return rgb + cv::Vec3f(offset, offset, offset);
        };
        const ColorMixer::Parameters mixerParameters{
            settings.mixerHue, settings.mixerSaturation, settings.mixerLuminance};
        for (int y = 0; y < adjusted.rows; ++y) {
            cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                cv::Vec3f pixel = row[x];
                const float originalLuminance = luminanceOf(pixel);
                if (hasColorMixer) {
                    pixel = ColorMixer::apply(pixel, mixerParameters);
                }
                if (hasColorGrading) {
                    const float shadows = 1.0f - smoothStep(0.12f, 0.42f, originalLuminance);
                    const float highlights = smoothStep(0.58f, 0.88f, originalLuminance);
                    const float midtones = std::clamp(1.0f - shadows - highlights, 0.0f, 1.0f);
                    const std::array<float, 3> weights{shadows, midtones, highlights};
                    for (size_t zone = 0; zone < weights.size(); ++zone) {
                        if (settings.gradingSaturation[zone] == 0 || weights[zone] == 0.0f)
                            continue;
                        const cv::Vec3f color = hsvToRgb(
                            static_cast<float>(settings.gradingHue[zone]), 1.0f, 1.0f);
                        const float colorLuminance = luminanceOf(color);
                        pixel += (color - cv::Vec3f(colorLuminance, colorLuminance,
                                                    colorLuminance)) *
                                 (settings.gradingSaturation[zone] * 0.0035f * weights[zone]);
                    }
                }
                row[x] = pixel;
            }
        }
    }

    // Denoise before local-detail enhancement and sharpening. The operation
    // runs in linear RGB and keeps luminance and chrominance independent so
    // coloured speckles can be removed without flattening edge detail.
    if (settings.denoiseLuminance != 0 || settings.denoiseChrominance != 0) {
        DetailProcessing::reduceNoise(
            adjusted, {settings.denoiseLuminance, settings.denoiseChrominance});
    }

    const auto applyLocalContrast = [&adjusted, &luminanceOf, &applyLuminance,
                                     &smoothStep](int adjustment, double sigma,
                                                  bool protectExtremes) {
        if (adjustment == 0)
            return;

        cv::Mat luminance(adjusted.rows, adjusted.cols, CV_32F);
        for (int y = 0; y < adjusted.rows; ++y) {
            const cv::Vec3f *sourceRow = adjusted.ptr<cv::Vec3f>(y);
            float *luminanceRow = luminance.ptr<float>(y);
            for (int x = 0; x < adjusted.cols; ++x)
                luminanceRow[x] = luminanceOf(sourceRow[x]);
        }
        cv::Mat localBase;
        cv::GaussianBlur(luminance, localBase, cv::Size(0, 0), sigma);
        const float amount = static_cast<float>(adjustment) / 100.0f;
        for (int y = 0; y < adjusted.rows; ++y) {
            cv::Vec3f *pixelRow = adjusted.ptr<cv::Vec3f>(y);
            const float *luminanceRow = luminance.ptr<float>(y);
            const float *baseRow = localBase.ptr<float>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                const float luminance = luminanceRow[x];
                float protection = 1.0f;
                if (protectExtremes) {
                    protection =
                        smoothStep(0.03f, 0.16f, luminance) *
                        (1.0f - smoothStep(0.72f, 0.98f, luminance));
                }
                const float target = std::max(
                    0.0f, luminance + (luminance - baseRow[x]) * amount *
                                      protection);
                applyLuminance(pixelRow[x], luminance, target);
            }
        }
    };
    // Texture targets fine details; clarity uses a wider neighbourhood and
    // protects the deepest shadows and brightest highlights.
    applyLocalContrast(settings.texture, 1.2, false);
    applyLocalContrast(settings.clarity, 8.0, true);

    if (settings.dehaze != 0) {
        cv::Mat darkChannel(adjusted.rows, adjusted.cols, CV_32F);
        std::vector<float> brightSamples;
        const int sampleStep = std::max(
            1, static_cast<int>(std::sqrt(
                   static_cast<double>(adjusted.total()) / 8192.0)));
        for (int y = 0; y < adjusted.rows; ++y) {
            const cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
            float *darkRow = darkChannel.ptr<float>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                darkRow[x] = std::max(0.0f, std::min({row[x][0], row[x][1],
                                                       row[x][2]}));
                if (x % sampleStep == 0 && y % sampleStep == 0)
                    brightSamples.push_back(std::max(
                        {row[x][0], row[x][1], row[x][2], 0.0f}));
            }
        }
        const int kernelSize = std::max(
            1, std::min(15, std::min(adjusted.cols, adjusted.rows) | 1));
        cv::erode(darkChannel, darkChannel,
                  cv::getStructuringElement(
                      cv::MORPH_RECT, cv::Size(kernelSize, kernelSize)));
        const size_t percentile = (brightSamples.size() - 1) * 99 / 100;
        std::nth_element(brightSamples.begin(),
                         brightSamples.begin() +
                             static_cast<qsizetype>(percentile),
                         brightSamples.end());
        const float airlight = std::max(0.10f, brightSamples[percentile]);
        const float amount = static_cast<float>(settings.dehaze) / 100.0f;
        const cv::Vec3f airlightRgb(airlight, airlight, airlight);
        for (int y = 0; y < adjusted.rows; ++y) {
            cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
            const float *darkRow = darkChannel.ptr<float>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                const float haze =
                    std::clamp(darkRow[x] / airlight, 0.0f, 1.0f);
                const float transmission =
                    std::clamp(1.0f - 0.85f * amount * haze, 0.12f, 2.0f);
                row[x] = (row[x] - airlightRgb) / transmission + airlightRgb;
                row[x][0] = std::max(0.0f, row[x][0]);
                row[x][1] = std::max(0.0f, row[x][1]);
                row[x][2] = std::max(0.0f, row[x][2]);
            }
        }
    }

    const auto applyToneCurve = [](const QVector<ToneCurvePoint> &curve,
                                   float value) {
        if (curve.isEmpty())
            return value;

        std::vector<double> weights(static_cast<size_t>(curve.size()), 1.0);
        for (qsizetype index = 0; index < curve.size(); ++index) {
            for (qsizetype other = 0; other < curve.size(); ++other) {
                if (index != other) {
                    weights[static_cast<size_t>(index)] /=
                        static_cast<double>(curve.at(index).x -
                                            curve.at(other).x);
                }
            }
        }

        const auto evaluateInsideRange = [&curve, &weights](double sample) {
            double numerator = 0.0;
            double denominator = 0.0;
            for (qsizetype index = 0; index < curve.size(); ++index) {
                const double distance = sample - curve.at(index).x;
                if (std::abs(distance) < 1e-9)
                    return static_cast<double>(curve.at(index).y);
                const double term = weights[static_cast<size_t>(index)] / distance;
                numerator += term * curve.at(index).y;
                denominator += term;
            }
            return numerator / denominator;
        };

        if (value <= 1.0f)
            return static_cast<float>(evaluateInsideRange(value));

        const qsizetype endpoint = curve.size() - 1;
        double derivative = 0.0;
        for (qsizetype index = 0; index < endpoint; ++index) {
            derivative +=
                weights[static_cast<size_t>(index)] /
                weights[static_cast<size_t>(endpoint)] *
                (curve.at(index).y - curve.at(endpoint).y) /
                (curve.at(endpoint).x - curve.at(index).x);
        }
        return curve.constLast().y +
               (value - curve.constLast().x) * static_cast<float>(derivative);
    };
    const QVector<ToneCurvePoint> &luminanceCurve =
        settings.toneCurves[LuminanceCurve];
    const QVector<ToneCurvePoint> &redCurve = settings.toneCurves[RedCurve];
    const QVector<ToneCurvePoint> &greenCurve =
        settings.toneCurves[GreenCurve];
    const QVector<ToneCurvePoint> &blueCurve = settings.toneCurves[BlueCurve];
    if (hasToneCurves) {
    for (int y = 0; y < adjusted.rows; ++y) {
        cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
        for (int x = 0; x < adjusted.cols; ++x) {
            const float luminance = luminanceOf(row[x]);
            applyLuminance(row[x], luminance,
                           applyToneCurve(luminanceCurve, luminance));
            row[x][0] = applyToneCurve(redCurve, row[x][0]);
            row[x][1] = applyToneCurve(greenCurve, row[x][1]);
            row[x][2] = applyToneCurve(blueCurve, row[x][2]);
        }
    }
    }

    if (settings.blur > 0) {
        const double sigma = static_cast<double>(settings.blur) / 2.0;
        cv::GaussianBlur(adjusted, adjusted, cv::Size(0, 0), sigma);
    }

    // Capture/output sharpening follows denoise and all contrast operations.
    // Its adaptive mask suppresses low-contrast noise and the local bound
    // limits the bright/dark halos produced by a naive unsharp mask.
    if (settings.sharpeningAmount > 0) {
        DetailProcessing::sharpen(adjusted, {settings.sharpeningAmount,
                                              settings.sharpeningRadius,
                                              settings.sharpeningMasking});
    }

    if (settings.grain > 0) {
        const float amount = 0.06f * static_cast<float>(settings.grain) / 100.0f;
        for (int y = 0; y < adjusted.rows; ++y) {
            cv::Vec3f *row = adjusted.ptr<cv::Vec3f>(y);
            for (int x = 0; x < adjusted.cols; ++x) {
                quint32 hash = static_cast<quint32>(x) * 0x9e3779b9u +
                               static_cast<quint32>(y) * 0x85ebca6bu;
                hash ^= hash >> 16;
                hash *= 0x7feb352du;
                hash ^= hash >> 15;
                const float noise =
                    static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
                const float luminance = luminanceOf(row[x]);
                const float shadowsWeight =
                    0.45f + 0.55f * (1.0f - std::clamp(luminance, 0.0f, 1.0f));
                applyLuminance(row[x], luminance,
                               std::max(0.0f, luminance + noise * amount *
                                                  shadowsWeight));
            }
        }
    }

    const cv::Mat alpha = channels[3];
    cv::split(adjusted, channels);
    channels.push_back(alpha);
    cv::Mat rgbaOutput;
    cv::merge(channels, rgbaOutput);

    return {rgbaOutput, source.colorSpace, source.sourceProfile,
            source.referenceWhiteBalanceKelvin};
}

ImageEditor::RenderResult ImageEditor::renderPreview(
    const ColorPipeline::WorkingImage &source, const Settings &settings,
    bool includeOriginal)
{
    RenderResult result;
    if (includeOriginal) {
        // The decoded source includes camera/profile transforms and its decode
        // white balance, but none of the editor settings (including sidecars).
        QString error;
        result.originalPreview = ColorPipeline::toSrgb(
            source, ColorPipeline::OutputFormat::Preview8Bit, &error);
        if (result.originalPreview.isNull())
            qWarning() << "Échec du rendu de l'origine :" << error;
    }
    const ColorPipeline::WorkingImage rendered =
        renderWorkingImage(source, settings);
    if (rendered.isNull())
        return result;

    QString error;
    result.preview = ColorPipeline::toSrgb(
        rendered, ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (result.preview.isNull()) {
        qWarning() << "Échec du rendu de l'aperçu :" << error;
        return result;
    }

    std::array<std::array<quint64, 256>, 4> bins{};
    const auto srgbBin = [](float linear) {
        const float bounded = std::clamp(linear, 0.0f, 1.0f);
        const float encoded = bounded <= 0.0031308f
                                  ? bounded * 12.92f
                                  : 1.055f * std::pow(bounded, 1.0f / 2.4f) -
                                        0.055f;
        return std::clamp(
            static_cast<int>(std::lround(encoded * 255.0f)), 0, 255);
    };
    // Histogram of the actual sRGB preview, after gamut mapping. Do not
    // encode Rec.2020 master channels with the sRGB transfer function.
    const auto decode = [](uchar code) {
        const float v = code / 255.f;
        return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
    };
    for (int y = 0; y < result.preview.height(); ++y) {
        const uchar *row = result.preview.constScanLine(y);
        for (int x = 0; x < result.preview.width(); ++x) {
            const uchar *p = row + 4 * x;
            const float luminance = .2126f * decode(p[0]) +
                .7152f * decode(p[1]) + .0722f * decode(p[2]);
            ++bins[0][static_cast<size_t>(srgbBin(luminance))];
            ++bins[1][p[0]];
            ++bins[2][p[1]];
            ++bins[3][p[2]];
        }
    }
    result.histogram.reserve(4);
    for (const auto &channel : bins) {
        QVariantList values;
        values.reserve(256);
        for (const quint64 value : channel)
            values.append(static_cast<double>(value));
        result.histogram.append(QVariant::fromValue(values));
    }

    return result;
}

ImageEditor::ExportResult ImageEditor::writeImage(
                                                   const ColorPipeline::WorkingImage &source,
                                                   const Settings &settings,
                                                   const QString &target,
                                                   const QString &format,
                                                   int quality)
{
    const QString normalized = normalizedExportFormat(format);
    const QString suffix = exportSuffix(normalized);
    if (suffix.isEmpty())
        return {false, tr("Format d'export non pris en charge : %1").arg(format)};

    const ColorPipeline::WorkingImage rendered =
        renderWorkingImage(source, settings);
    if (rendered.isNull())
        return {false, tr("Échec du rendu avant export.")};

    const QString normalizedTarget = normalizedExportPath(target, normalized);
    // Encode beside the destination first. Publishing via QFile::rename does
    // not replace an occupied name, unlike a QSaveFile commit to that name.
    // This also covers a file appearing while the dialog/encoder was running.
    QString stagedPath;
    {
        QTemporaryFile reservation(QFileInfo(normalizedTarget).dir().filePath(
            QStringLiteral(".ipr-export-XXXXXX")));
        if (!reservation.open())
            return {false, tr("Échec de l'export (création du temporaire) : %1")
                .arg(reservation.errorString())};
        stagedPath = reservation.fileName();
        reservation.setAutoRemove(false);
        // QTemporaryFile::close() keeps its native handle open internally.
        // Destroy the object to release the Windows sharing lock before a
        // QSaveFile commit or TIFFOpenW touches this reserved temporary path.
    }
    auto cleanup = qScopeGuard([&] { QFile::remove(stagedPath); });
    const auto publish = [&]() -> ExportResult {
        QFile completed(stagedPath);
        // Bound retries if another process continually takes proposed names.
        for (int attempt = 0; attempt < 32; ++attempt) {
            const QString finalPath = availableExportPath(normalizedTarget);
            if (finalPath.isEmpty())
                return {false, tr("Échec de l'export : aucun nom de fichier disponible.")};
            if (completed.rename(finalPath)) {
                cleanup.dismiss();
                return {true, normalized == QStringLiteral("DNG")
                    ? tr("Image exportée (DNG linéaire) : %1").arg(finalPath)
                    : tr("Image exportée : %1").arg(finalPath)};
            }
            const QFileInfo collision(finalPath);
            if (!collision.exists() && !collision.isSymLink())
                return {false, tr("Échec de l'export (publication de %1) : %2")
                    .arg(finalPath, completed.errorString())};
        }
        return {false, tr("Échec de l'export : noms occupés par un autre processus, réessayez.")};
    };

    if (normalized == QStringLiteral("DNG")) {
        QString dngError;
        if (!writeLinearDng(rendered, stagedPath, &dngError))
            return {false, tr("Échec de l'export DNG : %1").arg(dngError)};
        return publish();
    }

    QSaveFile destination(stagedPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        return {false, tr("Échec de l'export (écriture du temporaire) : %1")
                           .arg(destination.errorString())};
    }
    QImageWriter writer(&destination, normalized.toLatin1());
    const bool jpeg = normalized == QStringLiteral("JPEG");
    if (jpeg)
        writer.setQuality(qBound(0, quality, 100));
    QString colorError;
    const QImage output = ColorPipeline::toSrgb(
        rendered,
        jpeg ? ColorPipeline::OutputFormat::Jpeg8Bit
             : ColorPipeline::OutputFormat::HighBitDepth,
        &colorError);
    if (output.isNull()) {
        destination.cancelWriting();
        return {false, tr("Échec de la conversion couleur : %1").arg(colorError)};
    }
    if (!writer.write(output)) {
        destination.cancelWriting();
        return {false, tr("Échec de l'export : %1").arg(writer.errorString())};
    }
    if (!destination.commit())
        return {false, tr("Échec de l'export (validation du temporaire) : %1").arg(destination.errorString())};

    return publish();
}

ImageEditor::LoadResult ImageEditor::loadFromDisk(const QString &fileName,
                                                  bool automaticWhiteBalance,
                                                  const RawDecoder::Options &rawOptions)
{
    auto options = rawOptions;
    options.automaticWhiteBalance = automaticWhiteBalance;
    LoadResult result;
    static_cast<ImageImport::Result &>(result) = ImageImport::load(fileName, options);
    if (!result.image.isNull()) result.editProxy = buildEditProxy(result.image);
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
    const bool proxyIsMaster = proxy.linearRgba.datastart == image.linearRgba.datastart;
    if (proxyIsMaster && !imageIsProxy)
        return {image, proxy, metadata, std::move(dcpSource), digest, fullSize, false};
    return {proxy, proxy, metadata, nullptr, digest, fullSize, true};
}

DecodedImageCache::Entry ImageEditor::retainedCurrentEntry() const
{
    return retainedEntry(m_original, m_editProxy, m_sourceMetadata, m_dcpSource, m_fullSize, m_masterIsProxy);
}

ColorPipeline::WorkingImage ImageEditor::buildEditProxy(
    const ColorPipeline::WorkingImage &source)
{
    // Interactive preview rendering (every slider drag) re-applies the whole
    // tonal/color/effects pipeline from scratch on the next render, so its
    // cost scales directly with pixel count. Camera RAW files routinely
    // exceed 24-50 Mpx while the on-screen preview (Main.qml's "canvas"
    // Image, fitted inside the editor pane) is at most a few Mpx, so editing
    // on a capped-resolution proxy removes most of that redundant work
    // without any visible quality loss, while export keeps using the full
    // resolution m_original untouched.
    if (source.isNull())
        return {};

    constexpr int kEditProxyMaxDimension = 2048;
    const int longestSide =
        std::max(source.linearRgba.cols, source.linearRgba.rows);
    if (longestSide <= kEditProxyMaxDimension)
        return source; // Shallow cv::Mat copy: no extra pixel data is allocated.

    const double scale =
        static_cast<double>(kEditProxyMaxDimension) / static_cast<double>(longestSide);
    const cv::Size proxySize(
        std::max(1, static_cast<int>(std::lround(source.linearRgba.cols * scale))),
        std::max(1, static_cast<int>(std::lround(source.linearRgba.rows * scale))));

    ColorPipeline::WorkingImage proxy;
    cv::resize(source.linearRgba, proxy.linearRgba, proxySize, 0, 0, cv::INTER_AREA);
    // Area averaging rounds alpha slightly above 1; a proxy restored from the
    // cache is revalidated as a master, which requires alpha within [0, 1].
    for (int y = 0; y < proxy.linearRgba.rows; ++y) {
        auto *row = proxy.linearRgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < proxy.linearRgba.cols; ++x)
            row[x][3] = std::clamp(row[x][3], 0.f, 1.f);
    }
    proxy.colorSpace = source.colorSpace;
    proxy.sourceProfile = source.sourceProfile;
    proxy.referenceWhiteBalanceKelvin = source.referenceWhiteBalanceKelvin;
    if (source.dcpContext) {
        auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>(*source.dcpContext);
        cv::resize(source.dcpContext->cameraRgb, context->cameraRgb, proxySize, 0, 0, cv::INTER_AREA);
        proxy.dcpContext = context;
    }
    return proxy;
}

QString ImageEditor::cacheKeyForFile(const QString &fileName,
                                     bool automaticWhiteBalance,
                                     const QString &profile, const QString &manifest,
                                     const QString &lensProfile, bool lensResolved,
                                     bool lensCorrection, bool chromaticAberration)
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
            .arg(lensCorrection).arg(chromaticAberration);
}

QString ImageEditor::sidecarPathForFile(const QString &fileName)
{
    return fileName + QStringLiteral(".ipr");
}

bool ImageEditor::readSidecar(const QString &fileName, Settings *settings,
                              UserImageMetadata *metadata, RawDevelopment *raw)
{
    if (!settings || !metadata)
        return false;
    const QFileInfo sourceInfo(fileName);
    QFile file(sidecarPathForFile(fileName));
    if (!sourceInfo.exists() || !file.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_4);
    quint32 magic = 0;
    quint16 version = 0;
    qint64 sourceSize = -1;
    qint64 sourceModified = -1;
    stream >> magic >> version >> sourceSize >> sourceModified;
    if (magic != 0x49505231u || version < 1 || version > 6 || file.size() > 1024 * 1024 ||
        sourceSize != sourceInfo.size() ||
        sourceModified != sourceInfo.lastModified().toMSecsSinceEpoch())
        return false;

    Settings loaded;
    qint32 value = 0;
    stream >> loaded.brightness;
    const auto readInt = [&stream, &value](int &target) {
        stream >> value;
        target = value;
    };
    readInt(loaded.contrast); readInt(loaded.blur); stream >> loaded.grayscale;
    readInt(loaded.shadows); readInt(loaded.blacks); readInt(loaded.highlights);
    readInt(loaded.whites); readInt(loaded.temperature); readInt(loaded.tint);
    readInt(loaded.vibrance); readInt(loaded.saturation); readInt(loaded.texture);
    readInt(loaded.clarity); readInt(loaded.dehaze); readInt(loaded.grain);
    if (version >= 2) {
        readInt(loaded.denoiseLuminance);
        readInt(loaded.denoiseChrominance);
        readInt(loaded.sharpeningAmount);
        stream >> loaded.sharpeningRadius;
        readInt(loaded.sharpeningMasking);
        if (loaded.denoiseLuminance < 0 || loaded.denoiseLuminance > 100 ||
            loaded.denoiseChrominance < 0 || loaded.denoiseChrominance > 100 ||
            loaded.sharpeningAmount < 0 || loaded.sharpeningAmount > 200 ||
            !std::isfinite(loaded.sharpeningRadius) ||
            loaded.sharpeningRadius < 0.3f || loaded.sharpeningRadius > 3.0f ||
            loaded.sharpeningMasking < 0 || loaded.sharpeningMasking > 100)
            return false;
    }
    const size_t storedMixerBands = version >= 4 ? 8 : (version == 3 ? 7 : 6);
    for (size_t storedIndex = 0; storedIndex < storedMixerBands; ++storedIndex) {
        // Preserve technical band identity: legacy last band was magenta at
        // 300 degrees, not violet. Newly inserted bands start neutral.
        const size_t oldIndex = version >= 3 || storedIndex == 0
            ? storedIndex : storedIndex + 1;
        const size_t index = version < 4 && oldIndex == 6 ? 7 : oldIndex;
        readInt(loaded.mixerHue[index]); readInt(loaded.mixerSaturation[index]);
        readInt(loaded.mixerLuminance[index]);
    }
    for (size_t index = 0; index < loaded.gradingHue.size(); ++index) {
        readInt(loaded.gradingHue[index]); readInt(loaded.gradingSaturation[index]);
    }
    stream >> loaded.automaticWhiteBalance;
    for (QVector<ToneCurvePoint> &curve : loaded.toneCurves) {
        qint32 count = 0;
        stream >> count;
        if (count < 0 || count > 64)
            return false;
        for (qint32 index = 0; index < count; ++index) {
            ToneCurvePoint point;
            stream >> point.x >> point.y;
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return false;
            curve.append(point);
        }
    }
    UserImageMetadata loadedMetadata;
    stream >> loadedMetadata.rating >> loadedMetadata.colorLabel >> loadedMetadata.flagged
           >> loadedMetadata.ratingSet >> loadedMetadata.colorLabelSet;
    RawDevelopment loadedRaw;
    if (version >= 5) {
        stream >> loadedRaw.profile >> loadedRaw.profileSha256 >> loadedRaw.manifest
               >> loadedRaw.manifestSha256 >> loadedRaw.normalizationEnabled >> loadedRaw.engineVersion;
    }
    if (version >= 6) {
        stream >> loadedRaw.lensProfile >> loadedRaw.lensProfileSha256 >> loadedRaw.lensProfileResolved
               >> loadedRaw.lensCorrectionEnabled >> loadedRaw.chromaticAberrationEnabled;
    }
    if (stream.status() != QDataStream::Ok)
        return false;
    *settings = std::move(loaded);
    *metadata = std::move(loadedMetadata);
    if (raw) *raw = std::move(loadedRaw);
    return true;
}

bool ImageEditor::writeSidecar(const QString &fileName, const Settings &settings,
                               const UserImageMetadata &metadata, const RawDevelopment &raw)
{
    const QFileInfo sourceInfo(fileName);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return false;
    QSaveFile file(sidecarPathForFile(fileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << quint32(0x49505231u) << quint16(6) << qint64(sourceInfo.size())
           << qint64(sourceInfo.lastModified().toMSecsSinceEpoch());
    stream << settings.brightness;
    const auto writeInt = [&stream](int value) { stream << qint32(value); };
    writeInt(settings.contrast); writeInt(settings.blur); stream << settings.grayscale;
    writeInt(settings.shadows); writeInt(settings.blacks); writeInt(settings.highlights);
    writeInt(settings.whites); writeInt(settings.temperature); writeInt(settings.tint);
    writeInt(settings.vibrance); writeInt(settings.saturation); writeInt(settings.texture);
    writeInt(settings.clarity); writeInt(settings.dehaze); writeInt(settings.grain);
    writeInt(settings.denoiseLuminance);
    writeInt(settings.denoiseChrominance);
    writeInt(settings.sharpeningAmount);
    stream << settings.sharpeningRadius;
    writeInt(settings.sharpeningMasking);
    for (size_t index = 0; index < settings.mixerHue.size(); ++index) {
        writeInt(settings.mixerHue[index]); writeInt(settings.mixerSaturation[index]);
        writeInt(settings.mixerLuminance[index]);
    }
    for (size_t index = 0; index < settings.gradingHue.size(); ++index) {
        writeInt(settings.gradingHue[index]); writeInt(settings.gradingSaturation[index]);
    }
    stream << settings.automaticWhiteBalance;
    for (const QVector<ToneCurvePoint> &curve : settings.toneCurves) {
        stream << qint32(curve.size());
        for (const ToneCurvePoint &point : curve)
            stream << point.x << point.y;
    }
    stream << metadata.rating << metadata.colorLabel << metadata.flagged
           << metadata.ratingSet << metadata.colorLabelSet;
    stream << raw.profile << raw.profileSha256 << raw.manifest << raw.manifestSha256
           << raw.normalizationEnabled << raw.engineVersion;
    stream << raw.lensProfile << raw.lensProfileSha256 << raw.lensProfileResolved
           << raw.lensCorrectionEnabled << raw.chromaticAberrationEnabled;
    return stream.status() == QDataStream::Ok && file.commit();
}

ImageEditor::RawDevelopment ImageEditor::rawDevelopmentRecipe() const
{
    const QString manifest = m_normalizationManifest.isEmpty() && m_rawNormalizationEnabled
        ? rawNormalizationManifest() : m_normalizationManifest;
    return {m_cameraProfile, m_profileSha256, manifest,
            m_manifestSha256.isEmpty() ? resourceDigest(manifest) : m_manifestSha256,
            m_rawNormalizationEnabled, ColorPipeline::engineIdentity() + QStringLiteral("|libraw=") + RawDecoder::backendVersion(),
            m_lensProfile, m_lensProfileSha256, m_lensProfileResolved,
            m_lensCorrectionEnabled, m_chromaticAberrationEnabled};
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
        // Retain the displayed image until its replacement is ready. Cache
        // eviction can release only the cache reference, never these pixels.
        if (!m_currentCacheKey.isEmpty() && !m_original.isNull())
            m_decodedImageCache.insert(m_currentCacheKey, retainedCurrentEntry());
        Settings persisted;
        UserImageMetadata metadata;
        RawDevelopment raw;
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
        if (readSidecar(fileName, &persisted, &metadata, &raw)) {
            automaticWhiteBalance = persisted.automaticWhiteBalance;
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
            m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled);

    setBusy(true);
    setStatusMessage(freshOpen ? tr("Ouverture de %1...").arg(QFileInfo(fileName).fileName())
                              : tr("Mise à jour du développement RAW…"));
    ++m_requestedLoadRevision;
    m_activeLoadRevision = m_requestedLoadRevision;
    if (freshOpen && RawDecoder::supportedExtensions().contains(extension))
        startEmbeddedPreview(fileName);

    setBusy(true);
    RawDecoder::Options options = developmentOptions();
    const bool fast = !freshOpen && !m_rawNormalizationEnabled && m_dcpSource &&
        !m_dcpSource->cameraRgb.empty() && m_dcpSource->automaticWhiteBalance == automaticWhiteBalance &&
        m_dcpSource->lensCorrectionEnabled == m_lensCorrectionEnabled &&
        m_dcpSource->chromaticAberrationEnabled == m_chromaticAberrationEnabled &&
        m_dcpSource->metadata.lcpPath == options.lcpProfileOverride &&
        m_dcpSource->metadata.lcpSha256 == resourceDigest(options.lcpProfileOverride) &&
        (m_cameraProfile.isEmpty() || !options.dcpProfileOverride.isEmpty());
    const auto source = fast ? m_dcpSource : nullptr;
    m_loadCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_loadCancel;
    options.cancellationRequested = [cancel] { return cancel->load(); };
    options.retainedDcpSource = std::make_shared<RawDecoder::DcpSource>();
    auto *controller = &m_decodedImageCache;
    const auto key = m_currentCacheKey;
    auto prefetchFuture = m_prefetchWatcher.future();
    const auto transfer = promotePrefetch ? m_prefetchTransfer : nullptr;
    m_loadWatcher.setFuture(QtConcurrent::run(
        [fileName, automaticWhiteBalance, options, source, controller, key, cancel, prefetchFuture, transfer]() mutable {
            if (transfer && prefetchFuture.isStarted()) {
                prefetchFuture.waitForFinished();
                auto ready = *transfer;
                if (!cancel->load() && !ready.image.isNull() && ready.cacheKey == key) return ready;
            }
            const auto peak = SystemMemory::estimateDecodePeak(fileName);
            auto admission = controller->acquire(key, peak, false, cancel).result();
            if (admission.cached) {
                auto &cached = *admission.cached;
                if (cacheAssetsUnchanged(cached)) {
                    LoadResult result;
                    result.image = std::move(cached.image); result.editProxy = std::move(cached.editProxy);
                    result.metadata = std::move(cached.metadata); result.dcpSource = std::move(cached.dcpSource);
                    result.fullSize = cached.fullSize; result.proxyOnly = cached.proxyOnly;
                    ImageImport::complete(result);
                    return result;
                }
                admission.cached.reset();
                admission = controller->acquire({}, peak, false, cancel).result();
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
                if (!loaded.image.isNull()) loaded.editProxy = buildEditProxy(loaded.image);
            } else {
                loaded = loadFromDisk(fileName, automaticWhiteBalance, options);
            }
            loaded.memoryPressure = admission.underPressure;
            return loaded;
        }));
}

void ImageEditor::applyLoadedImage(LoadResult loaded)
{
    if (loaded.image.isNull()) {
        const auto failedName = QFileInfo(m_currentFile.toLocalFile()).fileName();
        if (!m_original.isNull()) {
            m_currentFile = m_displayedFile;
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
                m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled);
            m_rawDevelopmentWarning = tr("Modification RAW refusée : %1. Rendu précédent conservé. ").arg(loaded.error);
            restoreBeforeEmbeddedPreview();
            emit rawDevelopmentChanged();
            emit adjustmentsReset(m_settings.temperature, m_settings.tint);
            if (!m_pendingLoadIsFreshOpen) scheduleRender();
        }
        setStatusMessage(tr("Échec du développement de « %1 » : %2")
                             .arg(failedName,
                                  loaded.error));
        emit hasImageChanged();
        refreshBusyState();
        return;
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
    m_fullSize = loaded.proxyOnly ? loaded.fullSize
        : QSize(loaded.image.linearRgba.cols, loaded.image.linearRgba.rows);
    m_original = std::move(loaded.image);
    m_editProxy = std::move(loaded.editProxy);
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
        UserImageMetadata metadata;
        Settings persisted;
        if (readSidecar(m_currentFile.toLocalFile(), &persisted, &metadata)) {
            m_settings = std::move(persisted);
            m_userImageMetadata.insert(currentFileKey(), std::move(metadata));
        }
        emit adjustmentsReset(m_settings.temperature, m_settings.tint);
    }
    if (m_pendingLoadIsFreshOpen) emit hasImageChanged();
    // Freeze only after restoring the incoming image's settings (not old WB).
    m_currentCacheKey = cacheKeyForFile(m_currentFile.toLocalFile(), m_settings.automaticWhiteBalance,
        m_cameraProfile, m_rawNormalizationEnabled ? rawNormalizationManifest() : QString(),
        m_lensProfile, m_lensProfileResolved, m_lensCorrectionEnabled, m_chromaticAberrationEnabled);
    emit rawDevelopmentChanged();
    m_appliedRawDevelopment = rawDevelopmentRecipe();
    m_appliedAutomaticWhiteBalance = m_settings.automaticWhiteBalance;
    const QString readyName = QFileInfo(m_currentFile.toLocalFile()).fileName();
    setStatusMessage(loaded.memoryPressure
        ? tr("Image prête : %1 — ouverte sous pression mémoire : RAM occupée par d'autres applications, "
             "Windows peut ralentir en utilisant le fichier d'échange.").arg(readyName)
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
    // pixels and is safe while background workers retain their own cv::Mat.
    m_original = {};
    m_editProxy = {};
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
    if (!m_original.isNull() && m_currentFile.isLocalFile())
        m_sidecarDebounce.start();
}

void ImageEditor::saveCurrentSidecar()
{
    if (m_original.isNull() || !m_currentFile.isLocalFile())
        return;
    if (m_currentFile != m_displayedFile) return;
    if (m_loadWatcher.isRunning()) return;
    if (!writeSidecar(m_currentFile.toLocalFile(), m_settings,
                 m_userImageMetadata.value(currentFileKey()), rawDevelopmentRecipe()))
        setStatusMessage(tr("Échec de sauvegarde des réglages .ipr : vérifiez les permissions du dossier."));
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
    // buildEditProxy()); m_original is reserved for exportImage()/writeImage()
    // so exports are never affected by this optimization.
    const ColorPipeline::WorkingImage &source =
        m_editProxy.isNull() ? m_original : m_editProxy;
    const ColorPipeline::WorkingImage sourceCopy = source;
    const Settings settings = m_settings;
    const bool includeOriginal = m_originalImageSource.isEmpty();
    m_activeRevision = m_requestedRevision;
    setBusy(true);
    auto *controller = &m_decodedImageCache;
    m_renderWatcher.setFuture(QtConcurrent::run(
        [sourceCopy, settings, includeOriginal, controller] {
            const auto admission = controller->acquire({}, SystemMemory::estimateProcessingPeak(sourceCopy.linearRgba.total()), false).result();
            if (!admission.reservation) { RenderResult result; result.error = admission.error; return result; }
            const auto release = qScopeGuard([&] { controller->release(admission.reservation); });
            return renderPreview(sourceCopy, settings, includeOriginal);
        }));
}

RawDecoder::Options ImageEditor::optionsForFile(const QString &path, bool *automaticWhiteBalance, QString *key)
{
    Settings settings;
    UserImageMetadata metadata;
    RawDevelopment raw;
    const bool saved = readSidecar(path, &settings, &metadata, &raw);
    RawDecoder::Options options;
    options.automaticWhiteBalance = saved && settings.automaticWhiteBalance;
    *automaticWhiteBalance = options.automaticWhiteBalance;
    options.preferCameraNeutral = true;
    options.listDcpProfiles = true;
    options.useNormalizationEnvironment = false;
    options.dcpProfileOverride = relocateResource(raw.profile, raw.profileSha256, "profiles");
    options.lcpProfileOverride = relocateResource(raw.lensProfile, raw.lensProfileSha256, "profiles");
    options.automaticLcpSelection = !raw.lensProfileResolved;
    options.lensCorrectionEnabled = raw.lensCorrectionEnabled;
    options.chromaticAberrationEnabled = raw.chromaticAberrationEnabled;
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
        options.lensCorrectionEnabled, options.chromaticAberrationEnabled);
    return options;
}

void ImageEditor::startEmbeddedPreview(const QString &fileName)
{
    // About 13 ms on the CR3 corpus versus about 4 s for the development.
    m_embeddedRevision = m_requestedLoadRevision;
    m_embeddedWatcher.setFuture(QtConcurrent::run(&m_embeddedPool, [fileName] {
        return RawDecoder::decodeEmbeddedThumbnail(fileName, QSize(2048, 2048));
    }));
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

void ImageEditor::cancelPrefetch()
{
    ++m_prefetchGeneration;
    if (m_prefetchCancel) m_prefetchCancel->store(true);
    m_prefetchTimer.stop();
    m_prefetchQueue.clear();
}

void ImageEditor::schedulePrefetch()
{
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
    auto *controller = &m_decodedImageCache;
    m_prefetchWatcher.setFuture(QtConcurrent::run(&m_prefetchPool, [file, cancel, controller, transfer] {
        LoadResult result;
        bool automaticWB = false;
        QString key;
        auto options = optionsForFile(file, &automaticWB, &key);
        if (cancel->load() || key.isEmpty()) return result;
        const auto peak = SystemMemory::estimateDecodePeak(file);
        // Do not spend CPU on an image unlikely to fit the retention cap.
        if (peak / 2 > controller->budgetBytes()) return result;
        auto admission = controller->acquire(key, peak, true).result();
        if (admission.cached && !cacheAssetsUnchanged(*admission.cached)) {
            admission.cached.reset();
            admission = controller->acquire({}, peak, true).result();
        }
        if (admission.cached) {
            auto &entry = *admission.cached;
            result.image = std::move(entry.image); result.editProxy = std::move(entry.editProxy);
            result.metadata = std::move(entry.metadata); result.dcpSource = std::move(entry.dcpSource);
            result.fullSize = entry.fullSize; result.proxyOnly = entry.proxyOnly;
            result.cacheKey = key;
            *transfer = result;
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
        *transfer = result;
        return result;
    }));
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
