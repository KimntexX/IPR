#ifndef IPR_EDITOR_IMAGEEDITOR_H
#define IPR_EDITOR_IMAGEEDITOR_H

#include "ColorPipeline.h"
#include "ImageExporter.h"
#include "ImageImporter.h"
#include "ColorMixer.h"
#include "EditRecipe.h"
#include "ToneCurve.h"
#include "DecodedImageCache.h"
#include "ImageCacheController.h"
#include "RawDecoder.h"

#include <QAbstractListModel>
#include <QColor>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QSet>
#include <QStringList>

#include <array>
#include <atomic>
#include <QThreadPool>

class ImageProvider;
class ThumbnailProvider;

class ImageListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        FileNameRole = Qt::UserRole + 1,
        FileUrlRole
    };
    Q_ENUM(Role)

    explicit ImageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFolder(const QUrl &folder);
    int count() const;
    QStringList localFiles(int first, int last) const;

private:
    struct Entry
    {
        QString fileName;
        QUrl fileUrl;
    };
    QList<Entry> m_entries;
};

class ImageEditor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *imageModel READ imageModel CONSTANT)
    Q_PROPERTY(QUrl folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY previewChanged)
    Q_PROPERTY(bool embeddedPreview READ embeddedPreview NOTIFY previewChanged)
    Q_PROPERTY(QString originalImageSource READ originalImageSource NOTIFY previewChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString imageInfo READ imageInfo NOTIFY imageInfoChanged)
    Q_PROPERTY(QVariantMap imageDetails READ imageDetails NOTIFY imageInfoChanged)
    Q_PROPERTY(int imageRating READ imageRating NOTIFY imageInfoChanged)
    Q_PROPERTY(QString imageColorLabel READ imageColorLabel NOTIFY imageInfoChanged)
    Q_PROPERTY(bool imageFlagged READ imageFlagged NOTIFY imageInfoChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY histogramChanged)
    Q_PROPERTY(qulonglong decodedImageCacheBudgetBytes
               READ decodedImageCacheBudgetBytes
               WRITE setDecodedImageCacheBudgetBytes
               NOTIFY decodedImageCacheChanged)
    Q_PROPERTY(qulonglong decodedImageCacheSizeBytes
               READ decodedImageCacheSizeBytes NOTIFY decodedImageCacheChanged)
    Q_PROPERTY(int decodedImageCacheEntryCount
               READ decodedImageCacheEntryCount NOTIFY decodedImageCacheChanged)
    Q_PROPERTY(QVariantMap memoryCacheStatus READ memoryCacheStatus NOTIFY decodedImageCacheChanged)
    Q_PROPERTY(int selectedImageCount READ selectedImageCount NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedImageUrls READ selectedImageUrls NOTIFY selectionChanged)
    Q_PROPERTY(bool hasCopiedAdjustments READ hasCopiedAdjustments NOTIFY copiedAdjustmentsChanged)
    Q_PROPERTY(QVariantList cameraProfiles READ cameraProfiles NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool rawImage READ rawImage NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool rawProfileFastPathAvailable READ rawProfileFastPathAvailable NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(QString selectedCameraProfile READ selectedCameraProfile WRITE setCameraProfile NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool rawNormalizationEnabled READ rawNormalizationEnabled WRITE setRawNormalizationEnabled NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(QString rawNormalizationManifest READ rawNormalizationManifest NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(QString rawDevelopmentStatus READ rawDevelopmentStatus NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool automaticWhiteBalance READ automaticWhiteBalance NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool lensCorrectionEnabled READ lensCorrectionEnabled WRITE setLensCorrectionEnabled NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(bool chromaticAberrationEnabled READ chromaticAberrationEnabled WRITE setChromaticAberrationEnabled NOTIFY rawDevelopmentChanged)
    Q_PROPERTY(QString opticalDevelopmentStatus READ opticalDevelopmentStatus NOTIFY rawDevelopmentChanged)

public:
    enum ToneCurveChannel
    {
        LuminanceCurve,
        RedCurve,
        GreenCurve,
        BlueCurve
    };
    Q_ENUM(ToneCurveChannel)
    static_assert(LuminanceCurve == ToneCurve::Luminance && RedCurve == ToneCurve::Red &&
                  GreenCurve == ToneCurve::Green && BlueCurve == ToneCurve::Blue,
                  "QML curve channels follow ToneCurve::Channel");

    explicit ImageEditor(ImageProvider *provider,
                         ThumbnailProvider *thumbnailProvider = nullptr,
                         QObject *parent = nullptr,
                         ImageCacheController::Sampler memorySampler = {});
    // RAW calibration the IHM applies by default (ADR-0007), for offline tools.
    static QString defaultCalibrationManifest();

    // Offline tools (test generator, render server): the export pipeline
    // without sidecars or preferences.
    using Settings = EditRecipe::Settings;
    using ExportResult = ImageExporter::Result;
    struct LoadResult : ImageImport::Result
    {
        ColorPipeline::WorkingImage editProxy;
        QString cacheKey;
        bool memoryPressure = false;
        QSize fullSize;          // true source size, also when image is a proxy
        bool proxyOnly = false;  // image is the edit proxy restored from the cache
    };
    static LoadResult loadFromDisk(const QString &fileName,
                                   bool automaticWhiteBalance,
                                   const RawDecoder::Options &rawOptions = {});
    static ExportResult writeImage(const ColorPipeline::WorkingImage &source,
                                   const Settings &settings,
                                   const QString &target, const QString &format,
                                   int quality);
    // The 16-bit sRGB pixels of a TIFF export, in memory; null with *error set
    // on failure.
    static QImage renderSrgb16(const ColorPipeline::WorkingImage &source,
                               const Settings &settings, QString *error);
    ~ImageEditor() override;

    QAbstractItemModel *imageModel();
    QUrl folder() const;
    QString imageSource() const;
    QString originalImageSource() const;
    QString statusMessage() const;
    bool hasImage() const;
    bool busy() const;
    QString imageInfo() const;
    QVariantMap imageDetails() const;
    int imageRating() const;
    QString imageColorLabel() const;
    bool imageFlagged() const;
    QVariantList histogram() const;
    bool embeddedPreview() const { return m_embeddedPreview; }
    qulonglong decodedImageCacheBudgetBytes() const;
    qulonglong decodedImageCacheSizeBytes() const;
    int decodedImageCacheEntryCount() const;
    QVariantMap memoryCacheStatus() const;
    int selectedImageCount() const;
    QStringList selectedImageUrls() const;
    QVariantList cameraProfiles() const;
    bool rawImage() const;
    bool rawProfileFastPathAvailable() const;
    Q_INVOKABLE void resetRawDevelopment();
    bool lensCorrectionEnabled() const { return m_lensCorrectionEnabled; }
    bool chromaticAberrationEnabled() const { return m_chromaticAberrationEnabled; }
    Q_INVOKABLE void setLensCorrectionEnabled(bool enabled);
    Q_INVOKABLE void setChromaticAberrationEnabled(bool enabled);
    QString opticalDevelopmentStatus() const;
    QString selectedCameraProfile() const { return m_cameraProfile; }
    bool rawNormalizationEnabled() const { return m_rawNormalizationEnabled; }
    bool automaticWhiteBalance() const { return m_settings.automaticWhiteBalance; }
    QString rawNormalizationManifest() const;
    QString rawDevelopmentStatus() const;
    Q_INVOKABLE void setCameraProfile(const QString &path);
    Q_INVOKABLE void setRawNormalizationEnabled(bool enabled);
    Q_INVOKABLE void setRawNormalizationManifest(const QUrl &file);
    Q_INVOKABLE QUrl defaultExportFile(const QString &format,
                                     const QUrl &previousTarget = QUrl()) const;
    Q_INVOKABLE QVariantList toneCurve(int channel) const;
    Q_INVOKABLE QString thumbnailSource(const QUrl &fileUrl) const;

    Q_INVOKABLE void openFolder(const QUrl &folder);
    Q_INVOKABLE void openImage(const QUrl &fileUrl);
    Q_INVOKABLE QStringList imageFileFilters() const;
    Q_INVOKABLE void loadImage(const QUrl &fileUrl);
    Q_INVOKABLE void setThumbnailWindow(int firstVisible, int lastVisible,
                                        int prefetchCount);
    Q_INVOKABLE void setImageSelected(const QUrl &fileUrl, bool selected);
    Q_INVOKABLE bool isImageSelected(const QUrl &fileUrl) const;
    Q_INVOKABLE void clearImageSelection();
    Q_INVOKABLE void applyAdjustmentsToSelection();
    bool hasCopiedAdjustments() const { return m_hasCopiedAdjustments; }
    Q_INVOKABLE void copyAdjustments();
    Q_INVOKABLE void pasteAdjustments();
    Q_INVOKABLE void saveCurrentAdjustments();
    Q_INVOKABLE int colorMixerValue(int band, int component) const;
    // Colour a mixer slider reaches at a value, for the UI tracks (ColorMixer::trackColour).
    Q_INVOKABLE QColor colorMixerTrackColor(int band, int component, int value) const;
    Q_INVOKABLE int colorGradingValue(int zone, int component) const;
    Q_INVOKABLE double adjustmentValue(const QString &name) const;
    Q_INVOKABLE void setImageRating(int rating);
    Q_INVOKABLE void setImageColorLabel(const QString &colorLabel);
    Q_INVOKABLE void setImageFlagged(bool flagged);
    Q_INVOKABLE void setDecodedImageCacheBudgetBytes(qulonglong budgetBytes);
    Q_INVOKABLE void setBrightness(float value);
    Q_INVOKABLE void setContrast(int value);
    Q_INVOKABLE void setBlur(int value);
    Q_INVOKABLE void setShadows(int value);
    Q_INVOKABLE void setBlacks(int value);
    Q_INVOKABLE void setHighlights(int value);
    Q_INVOKABLE void setWhites(int value);
    Q_INVOKABLE void setTemperature(int value);
    Q_INVOKABLE void setTint(int value);
    Q_INVOKABLE void setVibrance(int value);
    Q_INVOKABLE void setSaturation(int value);
    Q_INVOKABLE void setTexture(int value);
    Q_INVOKABLE void setClarity(int value);
    Q_INVOKABLE void setDehaze(int value);
    Q_INVOKABLE void setGrain(int value);
    Q_INVOKABLE void setDenoiseLuminance(int value);
    Q_INVOKABLE void setDenoiseChrominance(int value);
    Q_INVOKABLE void setSharpeningAmount(int value);
    Q_INVOKABLE void setSharpeningRadius(float value);
    Q_INVOKABLE void setSharpeningMasking(int value);
    Q_INVOKABLE void setColorMixer(int band, int hue, int saturation,
                                   int luminance);
    Q_INVOKABLE void setColorGrading(int zone, int hue, int saturation);
    Q_INVOKABLE void setAutomaticWhiteBalance(bool enabled);
    Q_INVOKABLE void setGrayscale(bool enabled);
    Q_INVOKABLE void setToneCurve(int channel, const QVariantList &points);
    Q_INVOKABLE void resetToneCurve(int channel);
    Q_INVOKABLE void resetToneCurves();
    Q_INVOKABLE void resetAdjustments();
    Q_INVOKABLE void exportImage(const QUrl &target, const QString &format,
                                 int quality);

signals:
    void rawDevelopmentChanged();
    void folderChanged();
    void previewChanged();
    void statusMessageChanged();
    void hasImageChanged();
    void busyChanged();
    void imageInfoChanged();
    void histogramChanged();
    void decodedImageCacheChanged();
    void selectionChanged();
    void copiedAdjustmentsChanged();
    void toneCurveChanged(int channel);
    void toneCurvesReset();
    void adjustmentsReset(int temperatureKelvin, int tint);

private:
    // Offline test exporter: reuse the pipeline without loading/saving sidecars.
    friend class IprTestImageGenerator;

    using ToneCurvePoint = ToneCurve::Point;

    using RawDevelopment = EditRecipe::RawDevelopment;

    struct RenderResult
    {
        QImage preview;
        QImage originalPreview;
        QVariantList histogram;
        QString error;
    };

    using UserImageMetadata = EditRecipe::UserImageMetadata;

    static DecodedImageCache::Entry retainedEntry(const ColorPipeline::WorkingImage &image,
        const ColorPipeline::WorkingImage &proxy, const ImageMetadata &metadata,
        std::shared_ptr<RawDecoder::DcpSource> dcpSource, QSize fullSize, bool imageIsProxy);
    DecodedImageCache::Entry retainedCurrentEntry() const;
    RawDecoder::Options developmentOptions() const;
    static ColorPipeline::WorkingImage renderWorkingImage(
        const ColorPipeline::WorkingImage &source, const Settings &settings);
    static RenderResult renderPreview(const ColorPipeline::WorkingImage &source,
                                      const Settings &settings,
                                      bool includeOriginal);
    static ColorPipeline::WorkingImage buildEditProxy(
        const ColorPipeline::WorkingImage &source);
    static QString cacheKeyForFile(const QString &fileName,
                                   bool automaticWhiteBalance,
                                   const QString &profile = {}, const QString &manifest = {},
                                   const QString &lensProfile = {}, bool lensResolved = false,
                                   bool lensCorrection = true, bool chromaticAberration = true);
    void reloadRawDevelopment();
    RawDevelopment rawDevelopmentRecipe() const;
    void beginLoad(const QString &fileName, const QString &extension,
                   bool automaticWhiteBalance, bool freshOpen);
    void applyLoadedImage(LoadResult loaded);
    void releaseCurrentImage();
    void scheduleRender();
    void scheduleSidecarSave();
    bool saveCurrentSidecar();
    void startRender();
    void startEmbeddedPreview(const QString &fileName);
    void restoreBeforeEmbeddedPreview();
    void schedulePrefetch();
    void startPrefetch();
    void cancelPrefetch();
    static RawDecoder::Options optionsForFile(const QString &path, bool *automaticWhiteBalance, QString *key);
    void setBusy(bool busy);
    void refreshBusyState();
    void setStatusMessage(const QString &message);
    QString currentFileKey() const;

    ImageProvider *m_provider;
    ThumbnailProvider *m_thumbnailProvider;
    ImageListModel m_imageModel;
    QUrl m_folder;
    QUrl m_currentFile;
    QUrl m_displayedFile;
    QUrl m_queuedFile;
    ColorPipeline::WorkingImage m_original;
    ColorPipeline::WorkingImage m_editProxy;
    ImageMetadata m_sourceMetadata;
    ImageImport::Provenance m_importProvenance;
    ImageImport::Capabilities m_importCapabilities;
    QStringList m_importWarnings;
    QString m_cameraProfile;
    QString m_normalizationManifest;
    bool m_rawNormalizationEnabled = true;
    QString m_rawDevelopmentWarning;
    QString m_profileSha256;
    QString m_manifestSha256;
    QString m_lensProfile;
    QString m_lensProfileSha256;
    bool m_lensProfileResolved = false;
    bool m_lensCorrectionEnabled = true;
    bool m_chromaticAberrationEnabled = true;
    RawDevelopment m_appliedRawDevelopment;
    bool m_appliedAutomaticWhiteBalance = false;
    std::shared_ptr<RawDecoder::DcpSource> m_dcpSource;
    QHash<QString, UserImageMetadata> m_userImageMetadata;
    QSet<QString> m_selectedImageKeys;
    ImageCacheController m_decodedImageCache;
    QString m_currentCacheKey;
    Settings m_settings;
    Settings m_copiedAdjustments;
    bool m_hasCopiedAdjustments = false;
    QString m_imageSource;
    QString m_originalImageSource;
    QString m_statusMessage = tr("Choisissez un dossier contenant des images.");
    QString m_memoryWaitMessage;
    // Camera-rendered embedded JPEG shown while the IPR development runs.
    QFutureWatcher<QImage> m_embeddedWatcher;
    QThreadPool m_embeddedPool;
    quint64 m_embeddedRevision = 0, m_embeddedSerial = 0;
    bool m_embeddedPreview = false;
    QImage m_restorePreview, m_restoreOriginal;
    QString m_restoreImageSource, m_restoreOriginalSource;
    bool m_masterIsProxy = false; // restored from a proxy-only cache entry
    QSize m_fullSize;
    QTimer m_renderDebounce;
    QTimer m_sidecarDebounce;
    QFutureWatcher<RenderResult> m_renderWatcher;
    QFutureWatcher<ExportResult> m_exportWatcher;
    QFutureWatcher<LoadResult> m_loadWatcher;
    QFutureWatcher<LoadResult> m_prefetchWatcher;
    QThreadPool m_prefetchPool;
    QTimer m_prefetchTimer;
    QStringList m_prefetchQueue;
    QString m_prefetchFile;
    bool m_prefetchTaskActive = false;
    std::shared_ptr<LoadResult> m_prefetchTransfer;
    bool m_prefetchSuspended = false;
    quint64 m_prefetchGeneration = 0, m_activePrefetchGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_prefetchCancel;
    std::shared_ptr<std::atomic_bool> m_loadCancel;
    quint64 m_requestedRevision = 0;
    quint64 m_activeRevision = 0;
    quint64 m_requestedLoadRevision = 0;
    quint64 m_activeLoadRevision = 0;
    bool m_pendingLoadIsFreshOpen = true;
    bool m_renderPending = false;
    bool m_busy = false;
    QVariantList m_histogram;
};

#endif // IPR_EDITOR_IMAGEEDITOR_H
