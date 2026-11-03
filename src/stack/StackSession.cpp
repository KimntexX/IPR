#include "StackSession.h"

#include "CameraDng.h"
#include "CameraSignal.h"
#include "ColorPipeline.h"
#include "ImageCacheController.h"
#include "ImageExporter.h"
#include "SystemMemory.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QThread>
#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace {

constexpr int previewSide = 1600;
// The bands held while merging, on top of what a decode needs.
quint64 mergeBytes() { return StackJob::Request{}.memoryBudgetBytes; }

ColorPipeline::WorkingImage rendered(const cv::Mat &cameraRgb, const CameraProfileEngine::DcpWorkingContext &context,
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

QImage previewOf(const cv::Mat &cameraRgb, const CameraProfileEngine::DcpWorkingContext &context, const ImageMetadata &metadata)
{
    const double factor = std::min(1.0, double(previewSide) / std::max(cameraRgb.cols, cameraRgb.rows));
    cv::Mat small;
    cv::resize(cameraRgb, small, cv::Size(), factor, factor, cv::INTER_AREA);
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

void StackSession::start(const QStringList &urls, const QString &reference)
{
    if (m_running || m_exporting)
        return;
    QStringList files;
    for (const QString &url : urls) {
        const QString file = ImageExporter::localPath(QUrl(url));
        if (!file.isEmpty())
            files.append(file);
    }
    files.removeDuplicates();
    files.sort(Qt::CaseInsensitive);
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
        [files, referenceIndex, decoder = m_decoder, cache = m_cache](Tasks::Context &context) {
            return compute(context, files, referenceIndex, decoder, cache);
        }).future);
}

StackSession::Outcome StackSession::compute(Tasks::Context &context, const QStringList &files, int referenceIndex,
                                            const Decoder &decoder, const std::shared_ptr<ImageCacheController> &cache)
{
    Outcome outcome;
    const auto stopped = [&context] { return context.cancelled(); };
    quint64 reservation = 0;
    const auto release = qScopeGuard([&] {
        if (cache && reservation)
            cache->release(reservation);
    });
    if (cache) {
        auto cancelToken = std::make_shared<std::atomic_bool>(false);
        QFuture<ImageCacheController::Admission> waiting =
            cache->acquire(QString(), SystemMemory::estimateDecodePeak(files.first()) + mergeBytes(), false, cancelToken);
        while (!waiting.isFinished()) {
            if (context.cancelled())
                cancelToken->store(true);
            QThread::msleep(20);
        }
        const ImageCacheController::Admission admission = waiting.result();
        if (!admission.reservation) {
            outcome.cancelled = context.cancelled();
            outcome.error = admission.error.isEmpty() ? tr("Mémoire insuffisante pour l'empilement.") : admission.error;
            return outcome;
        }
        reservation = admission.reservation;
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
    const cv::Size frame = job.frameSize;
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
        setStatus(outcome.error.isEmpty() || outcome.cancelled ? tr("Empilement annulé.")
                                                               : tr("Échec de l'empilement : %1").arg(outcome.error));
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
    const QString name = info.completeBaseName() + QStringLiteral("-empilement.") + ImageExporter::suffix(normalized);
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
        content.cameraRgb = result.cameraRgb;
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
        content.colorMatrix = camera.colorMatrix;
        content.illuminant = camera.illuminant;
        content.provenance = QStringLiteral("IPR stack of %1 frames, reference %2")
                                 .arg(result.usedFrames).arg(QFileInfo(result.referenceFile).fileName());
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
