#include "StackSession.h"

#include "CameraDng.h"
#include "CameraSignal.h"
#include "ColorPipeline.h"
#include "ImageCacheController.h"
#include "ImageExporter.h"
#include "PanoramaJob.h"
#include "SystemMemory.h"

#include "Resize.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScopeGuard>
#include <QThread>

#include <algorithm>
#include <vector>

namespace {

constexpr int previewSide = 1600;
constexpr double inventedWarning = 0.25;  // of a filled panorama, beyond which the message warns
// The bands held while merging, on top of what a decode needs.
quint64 mergeBytes() { return StackJob::Request{}.memoryBudgetBytes; }

ColorPipeline::WorkingImage rendered(const ipr::Image &cameraRgb, const CameraProfileEngine::DcpWorkingContext &context,
                                     const ImageMetadata &metadata, QString *error)
{
    CameraSignal::Source signal;
    signal.rgb = cameraRgb.clone();
    signal.cameraMake = metadata.cameraMake;
    signal.cameraModel = metadata.cameraModel;
    signal.whiteBalanceKelvin = context.sourceKelvin;
    signal.whiteBalanceApplied = true;
    signal.cameraNeutral = context.cameraNeutral;
    return CameraSignal::render(std::move(signal), context.profilePath, context.options, error);
}

// Memory set aside against the editor's images, until it is released. 0 with a reason when
// refused, cancelled or when the memory does not come in time: a job never waits for ever.
quint64 reserve(ImageCacheController &cache, quint64 bytes, Tasks::Context &context, int patienceMs, QString *refusal)
{
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    QFuture<ImageCacheController::Admission> waiting = cache.acquire(QString(), bytes, false, cancelToken);
    QElapsedTimer waited;
    waited.start();
    bool tooLong = false;
    while (!waiting.isFinished()) {
        if (context.cancelled())
            cancelToken->store(true);
        if (!tooLong && waited.elapsed() > patienceMs) {
            tooLong = true;
            cancelToken->store(true);
        }
        QThread::msleep(20);
    }
    const ImageCacheController::Admission admission = waiting.takeResult();
    if (!admission.reservation && refusal)
        *refusal = tooLong ? StackSession::tr("Mémoire insuffisante pour le moment : fermez d'autres applications, puis recommencez.") : admission.error;
    return admission.reservation;
}

QImage previewOf(const ipr::Image &cameraRgb, const CameraProfileEngine::DcpWorkingContext &context, const ImageMetadata &metadata)
{
    const double factor = std::min(1.0, double(previewSide) / std::max(cameraRgb.width(), cameraRgb.height()));
    const ipr::Image small = ipr::resizedBy(cameraRgb, factor, factor, ipr::Interpolation::Area);
    QString error;
    const ColorPipeline::WorkingImage image = rendered(small, context, metadata, &error);
    return image.isNull() ? QImage() : ColorPipeline::toSrgb(image, ColorPipeline::OutputFormat::Jpeg8Bit, &error);
}

} // namespace

StackSession::StackSession(Decoder decoder, std::shared_ptr<ImageCacheController> cache, QObject *parent)
    : QObject(parent), m_decoder(std::move(decoder)), m_cache(std::move(cache))
{
    connect(&m_watcher, &QFutureWatcher<Outcome>::finished, this, &StackSession::finished);
    connect(&m_exportWatcher, &QFutureWatcher<Export>::finished, this, &StackSession::exportFinished);
    connect(&m_watcher, &QFutureWatcher<Outcome>::progressValueChanged, this, [this](int done) {
        const int total = m_watcher.progressMaximum();
        m_progress = total > 0 ? double(done) / total : 0.0;
        emit progressChanged();
    });
}

StackSession::~StackSession()
{
    m_watcher.disconnect(this);
    m_exportWatcher.disconnect(this);
    m_tasks.shutdown();
}

void StackSession::setStatus(const QString &status)
{
    m_status = status;
    emit stateChanged();
}

QStringList StackSession::localFiles(const QStringList &urls) const
{
    QStringList files;
    for (const QString &url : urls) {
        const QString file = ImageExporter::localPath(QUrl(url));
        if (!file.isEmpty())
            files.append(file);
    }
    files.removeDuplicates();
    files.sort(Qt::CaseInsensitive);
    return files;
}

void StackSession::start(const QStringList &urls, const QString &reference)
{
    if (m_running || m_exporting)
        return;
    const QStringList files = localFiles(urls);
    if (files.size() < 2 || !m_decoder) {
        setStatus(tr("Sélectionnez au moins deux photos dans le ruban."));
        return;
    }
    const QString referenceFile = reference.isEmpty() ? QString() : ImageExporter::localPath(QUrl(reference));
    const int found = int(files.indexOf(referenceFile));
    const int referenceIndex = found >= 0 ? found : int(files.size()) / 2;

    m_running = true;
    m_progress = 0.0;
    m_exportedFile = QUrl();
    emit progressChanged();
    setStatus(tr("Empilement de %1 photos…").arg(files.size()));
    m_watcher.setFuture(m_tasks.submit<Outcome>(
        QStringLiteral("stack"), Tasks::Priority::Foreground,
        [files, referenceIndex, decoder = m_decoder, cache = m_cache, patience = m_reservationPatienceMs](Tasks::Context &context) {
            return compute(context, files, referenceIndex, decoder, cache, patience);
        }).future);
}

void StackSession::startPanorama(const QStringList &urls, const QString &projection, const QString &edges)
{
    if (m_running || m_exporting)
        return;
    const QStringList files = localFiles(urls);
    if (files.size() < 2 || !m_decoder) {
        setStatus(tr("Sélectionnez au moins deux photos dans le ruban."));
        return;
    }
    const PanoramaGeometry::Projection chosen =
        projection == QStringLiteral("cylindrical") ? PanoramaGeometry::Projection::Cylindrical
        : projection == QStringLiteral("spherical") ? PanoramaGeometry::Projection::Spherical
        : projection == QStringLiteral("rectilinear") ? PanoramaGeometry::Projection::Rectilinear
                                                      : PanoramaGeometry::Projection::Automatic;
    m_running = true;
    m_progress = 0.0;
    m_exportedFile = QUrl();
    emit progressChanged();
    setStatus(tr("Assemblage de %1 photos en panorama…").arg(files.size()));
    m_watcher.setFuture(m_tasks.submit<Outcome>(
        QStringLiteral("stack"), Tasks::Priority::Foreground,
        [files, chosen, edges, decoder = m_decoder, cache = m_cache, patience = m_reservationPatienceMs](Tasks::Context &context) {
            return computePanorama(context, files, chosen, edges, decoder, cache, patience);
        }).future);
}

StackSession::Outcome StackSession::computePanorama(Tasks::Context &context, const QStringList &files, PanoramaGeometry::Projection projection, const QString &edges,
                                                    const Decoder &decoder, const std::shared_ptr<ImageCacheController> &cache, int patienceMs)
{
    Outcome outcome;
    const auto stopped = [&context] { return context.cancelled(); };
    quint64 reservation = 0;
    const auto release = qScopeGuard([&] {
        if (cache && reservation)
            cache->release(reservation);
    });
    PanoramaJob::Request request;
    if (cache) {
        QString refusal;
        reservation = reserve(*cache, SystemMemory::estimateDecodePeak(files.first()) + request.memoryBudgetBytes, context, patienceMs, &refusal);
        if (!reservation) {
            outcome.cancelled = context.cancelled();
            outcome.error = refusal.isEmpty() ? tr("Mémoire insuffisante pour le panorama.") : refusal;
            return outcome;
        }
        // The decoding is over: its share gives way to the canvas's, in one reservation. Under
        // pressure the cache admits nothing while another is held, this job's own included.
        const quint64 bands = request.memoryBudgetBytes;
        request.reserveCanvas = [&, bands](quint64 bytes) {
            cache->release(reservation);
            QString canvasRefusal;
            reservation = reserve(*cache, bytes + bands, context, patienceMs, &canvasRefusal);
            if (!reservation)
                return canvasRefusal.isEmpty() ? tr("Mémoire insuffisante pour un panorama de cette taille.") : canvasRefusal;
            return QString();
        };
    }

    auto result = std::make_shared<Result>();
    result->panorama = true;
    // The photo in the middle of the series speaks for it: profile, balance, metadata.
    const int middle = int(files.size()) / 2;
    request.frameCount = int(files.size());
    request.projection = projection;
    request.edges = edges == QStringLiteral("fill") ? PanoramaJob::Edges::Fill
        : edges == QStringLiteral("keep") ? PanoramaJob::Edges::Keep : PanoramaJob::Edges::Crop;
    request.cancelled = stopped;
    request.progress = [&context](int done, int total) { context.progress(done, total); };
    request.frame = [&](int index) {
        PanoramaJob::Frame frame;
        Decoded decoded = decoder(files[index], stopped);
        frame.error = decoded.error;
        if (!decoded.context) {
            if (frame.error.isEmpty())
                frame.error = tr("Aucun profil de couleur pour ce boîtier : le panorama a besoin de son signal.");
            return frame;
        }
        if (index == middle || !result->context) {
            result->context = decoded.context;
            result->metadata = decoded.metadata;
            result->referenceFile = files[index];
        }
        frame.focalMm = decoded.metadata.focalLengthMm;
        frame.sensorWidthMm = decoded.metadata.sensorWidthMm;
        frame.lensGeometryCorrected = decoded.metadata.lensGeometryApplied;
        frame.cameraRgb = std::move(decoded.cameraRgb);
        return frame;
    };
    PanoramaJob::Result job = PanoramaJob::run(request);
    outcome.cancelled = job.cancelled;
    if (!job.success) {
        outcome.error = job.error;
        return outcome;
    }
    result->usedFrames = job.usedFrames;
    result->leftOut = int(files.size()) - job.usedFrames;
    result->areaKept = job.canvas.area() > 0 ? double(job.kept.area()) / double(job.canvas.area()) : 0.0;
    result->rows = job.shape.rows;
    result->filledShare = job.filledShare;
    result->projection = job.shape.projection == PanoramaGeometry::Projection::Rectilinear ? tr("plane")
        : job.shape.projection == PanoramaGeometry::Projection::Spherical ? tr("sphérique") : tr("cylindrique");
    result->cameraRgb = std::move(job.cameraRgb);
    outcome.preview = previewOf(result->cameraRgb, *result->context, result->metadata);
    outcome.result = std::move(result);
    return outcome;
}

StackSession::Outcome StackSession::compute(Tasks::Context &context, const QStringList &files, int referenceIndex,
                                            const Decoder &decoder, const std::shared_ptr<ImageCacheController> &cache, int patienceMs)
{
    Outcome outcome;
    const auto stopped = [&context] { return context.cancelled(); };
    quint64 reservation = 0;
    const auto release = qScopeGuard([&] {
        if (cache && reservation)
            cache->release(reservation);
    });
    if (cache) {
        QString refusal;
        reservation = reserve(*cache, SystemMemory::estimateDecodePeak(files.first()) + mergeBytes(), context, patienceMs, &refusal);
        if (!reservation) {
            outcome.cancelled = context.cancelled();
            outcome.error = refusal.isEmpty() ? tr("Mémoire insuffisante pour l'empilement.") : refusal;
            return outcome;
        }
    }

    auto result = std::make_shared<Result>();
    result->referenceFile = files[referenceIndex];
    StackJob::Request request;
    request.frameCount = int(files.size());
    request.referenceIndex = referenceIndex;
    request.cancelled = stopped;
    request.progress = [&context](int done, int total) { context.progress(done, total); };
    request.frame = [&](int index) {
        StackJob::Frame frame;
        Decoded decoded = decoder(files[index], stopped);
        if (index == referenceIndex) {
            result->context = decoded.context;
            result->metadata = decoded.metadata;
            if (!decoded.context) {
                frame.error = decoded.error.isEmpty()
                    ? tr("Aucun profil de couleur pour ce boîtier : l'empilement a besoin de son signal.") : decoded.error;
                return frame;
            }
        }
        frame.cameraRgb = std::move(decoded.cameraRgb);
        frame.error = decoded.error;
        return frame;
    };
    StackJob::Result job = StackJob::run(request);
    outcome.cancelled = job.cancelled;
    if (!job.success) {
        outcome.error = job.error;
        return outcome;
    }
    result->usedFrames = job.usedFrames;
    result->leftOut = int(files.size()) - job.usedFrames;
    const double whole = double(job.commonArea.area());
    const ipr::Size frame = job.frameSize;
    result->areaKept = frame.area() > 0 ? whole / double(frame.area()) : 0.0;
    result->cameraRgb = std::move(job.cameraRgb);
    result->sharpestViews = job.turbulence.applied ? job.turbulence.kept : 0;
    outcome.preview = previewOf(result->cameraRgb, *result->context, result->metadata);
    outcome.result = std::move(result);
    return outcome;
}

void StackSession::finished()
{
    m_running = false;
    Outcome outcome;
    try {
        outcome = Tasks::takeResult(m_watcher.future());
    } catch (const std::exception &exception) {
        outcome.error = QString::fromLocal8Bit(exception.what());
    }
    if (!outcome.result) {
        setStatus(outcome.error.isEmpty() || outcome.cancelled ? tr("Assemblage annulé.")
                                                               : tr("Échec de l'assemblage : %1").arg(outcome.error));
        return;
    }
    m_result = outcome.result;
    m_preview = QUrl();
    if (!outcome.preview.isNull() && m_previewFolder.isValid()) {
        // A new name each time: a QML image keeps the one it loaded under a same address.
        const QString path = m_previewFolder.filePath(QStringLiteral("stack-%1.jpg").arg(++m_previewNumber));
        if (outcome.preview.save(path, "JPEG", 90))
            m_preview = QUrl::fromLocalFile(path);
    }
    m_progress = 1.0;
    emit progressChanged();
    if (m_result->panorama) {
        setStatus(tr("Panorama de %1 photos, %2 rangée(s), projection %3 : %4 × %5 pixels%6. Choisissez un format pour exporter.")
                      .arg(m_result->usedFrames).arg(m_result->rows).arg(m_result->projection)
                      .arg(m_result->cameraRgb.width()).arg(m_result->cameraRgb.height())
                      .arg((m_result->leftOut > 0 ? tr(", %1 écartée(s)").arg(m_result->leftOut) : QString())
                           + (m_result->filledShare > 0.0 ? tr(", dont %1 % de pixels inventés").arg(m_result->filledShare * 100.0, 0, 'f', 1) : QString())));
        if (m_result->filledShare > inventedWarning) {
            setStatus(tr("Attention : %1 % de cette image sont inventés, ce qui se verra probablement. Recadrez, ou retirez les photos les plus décalées.\n%2")
                          .arg(m_result->filledShare * 100.0, 0, 'f', 1).arg(m_status));
        }
        return;
    }
    setStatus(tr("%1 photos empilées, %2 % de la surface conservée%3. Choisissez un format pour exporter.")
                  .arg(m_result->usedFrames).arg(qRound(m_result->areaKept * 100.0))
                  .arg((m_result->leftOut > 0 ? tr(", %1 écartée(s)").arg(m_result->leftOut) : QString())
                       + (m_result->sharpestViews > 0
                              ? tr(", turbulence corrigée sur le sujet avec les %1 vues les plus nettes").arg(m_result->sharpestViews)
                              : QString())));
}

void StackSession::cancel()
{
    if (m_running) {
        m_tasks.cancel(QStringLiteral("stack"));
        setStatus(tr("Annulation…"));
    }
}

QUrl StackSession::suggestedExport(const QString &format) const
{
    if (!m_result)
        return {};
    const QFileInfo info(m_result->referenceFile);
    const QString normalized = ImageExporter::normalizedFormat(format);
    const QString name = info.completeBaseName() + (m_result->panorama ? QStringLiteral("-panorama.") : QStringLiteral("-empilement."))
        + ImageExporter::suffix(normalized);
    return QUrl::fromLocalFile(ImageExporter::availablePath(info.dir().filePath(name)));
}

void StackSession::exportResult(const QUrl &target, const QString &format)
{
    if (!m_result || m_running || m_exporting)
        return;
    const QString path = ImageExporter::localPath(target);
    const QString normalized = ImageExporter::normalizedFormat(format);
    if (path.isEmpty() || (normalized != QStringLiteral("DNG") && normalized != QStringLiteral("TIFF"))) {
        setStatus(tr("Destination ou format d'export invalide."));
        return;
    }
    m_exporting = true;
    setStatus(tr("Export en cours…"));
    m_exportWatcher.setFuture(m_tasks.submit<Export>(
        QStringLiteral("stackExport"), Tasks::Priority::Foreground,
        [result = m_result, path, normalized](Tasks::Context &) { return writeResult(*result, path, normalized); }).future);
}

StackSession::Export StackSession::writeResult(const Result &result, const QString &target, const QString &format)
{
    Export written;
    ImageExporter::Result outcome;
    if (format == QStringLiteral("DNG")) {
        const CameraProfileEngine::DcpCameraData camera = CameraProfileEngine::dcpCameraData(result.context->profilePath);
        CameraDng::Content content;
        content.cameraRgb = result.cameraRgb.share();
        content.cameraMake = result.metadata.cameraMake;
        content.cameraModel = result.metadata.cameraModel;
        content.lensMake = result.metadata.lensMake;
        content.lensModel = result.metadata.lensModel;
        content.iso = result.metadata.iso;
        content.exposureSeconds = result.metadata.exposureSeconds;
        content.focalLengthMm = result.metadata.focalLengthMm;
        content.captureDate = result.metadata.captureDate;
        content.cameraNeutral = result.context->cameraNeutral;
        content.whiteBalanceKelvin = result.context->sourceKelvin;
        content.renderOptions = result.context->options;
        content.calibratedWhite = result.metadata.normalizationWhiteLevel;
        content.colorMatrix = camera.colorMatrix;
        content.illuminant = camera.illuminant;
        content.provenance = (result.panorama ? QStringLiteral("IPR panorama of %1 frames, middle %2")
                                              : QStringLiteral("IPR stack of %1 frames, reference %2"))
                                 .arg(result.usedFrames).arg(QFileInfo(result.referenceFile).fileName());
        if (result.filledShare > 0.0)
            content.provenance += QStringLiteral(", %1 % of the pixels invented to fill the edges").arg(result.filledShare * 100.0, 0, 'f', 1);
        outcome = ImageExporter::publishEncoded(target, format, [&](const QString &staged, QString *error) {
            return CameraDng::write(content, staged, error);
        });
    } else {
        QString error;
        const ColorPipeline::WorkingImage image = rendered(result.cameraRgb, *result.context, result.metadata, &error);
        if (image.isNull()) {
            written.message = tr("Échec du rendu : %1").arg(error);
            return written;
        }
        outcome = ImageExporter::write(image, target, format, 100);
    }
    written.success = outcome.success;
    written.message = outcome.message;
    written.path = outcome.path;
    return written;
}

void StackSession::exportFinished()
{
    m_exporting = false;
    Export written;
    try {
        written = Tasks::takeResult(m_exportWatcher.future());
    } catch (const std::exception &exception) {
        written.message = QString::fromLocal8Bit(exception.what());
    }
    if (written.success)
        m_exportedFile = QUrl::fromLocalFile(written.path);
    setStatus(written.message.isEmpty() ? tr("Export annulé.") : written.message);
}

void StackSession::discard()
{
    if (m_running || m_exporting)
        return;
    m_result.reset();
    m_preview = QUrl();
    m_exportedFile = QUrl();
    m_progress = 0.0;
    emit progressChanged();
    setStatus(QString());
}
