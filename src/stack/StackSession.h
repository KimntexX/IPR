#ifndef IPR_STACK_STACKSESSION_H
#define IPR_STACK_STACKSESSION_H

#include "CameraProfileEngine.h"
#include "ImageMetadata.h"
#include "StackJob.h"
#include "TaskRunner.h"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <memory>

class ImageCacheController;

// One stack at a time for the panel: runs it in the background, shows its result and
// exports it on request. The result stays in memory until it is replaced or discarded.
class StackSession final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY stateChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QUrl preview READ preview NOTIFY stateChanged)
    Q_PROPERTY(QUrl exportedFile READ exportedFile NOTIFY stateChanged)

public:
    struct Decoded
    {
        cv::Mat cameraRgb;
        std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> context;
        ImageMetadata metadata;
        QString error;
    };
    // Called on the job's thread, one frame at a time.
    using Decoder = std::function<Decoded(const QString &file, const std::function<bool()> &cancelled)>;

    // cache: reserves the job's memory against the editor's images; may be null.
    explicit StackSession(Decoder decoder, std::shared_ptr<ImageCacheController> cache = {}, QObject *parent = nullptr);
    ~StackSession() override;

    bool running() const { return m_running; }
    bool exporting() const { return m_exporting; }
    bool hasResult() const { return bool(m_result); }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QUrl preview() const { return m_preview; }
    QUrl exportedFile() const { return m_exportedFile; }

    // urls: the frames, at least two; reference: one of them, the middle one when empty.
    Q_INVOKABLE void start(const QStringList &urls, const QString &reference = {});
    Q_INVOKABLE void cancel();
    // format: "DNG" (the camera signal, editable) or "TIFF" (rendered by the profile).
    Q_INVOKABLE void exportResult(const QUrl &target, const QString &format);
    Q_INVOKABLE QUrl suggestedExport(const QString &format) const;
    Q_INVOKABLE void discard();

signals:
    void stateChanged();
    void progressChanged();

private:
    struct Result
    {
        cv::Mat cameraRgb;
        std::shared_ptr<const CameraProfileEngine::DcpWorkingContext> context;
        ImageMetadata metadata;
        QString referenceFile;
        int usedFrames = 0;
        int leftOut = 0;
        double areaKept = 0.0;
    };
    struct Outcome
    {
        std::shared_ptr<const Result> result;
        QImage preview;
        QString error;
        bool cancelled = false;
    };
    struct Export
    {
        bool success = false;
        QString message;
        QString path;
    };

    static Outcome compute(Tasks::Context &context, const QStringList &files, int referenceIndex, const Decoder &decoder,
                           const std::shared_ptr<ImageCacheController> &cache);
    static Export writeResult(const Result &result, const QString &target, const QString &format);
    void finished();
    void exportFinished();
    void setStatus(const QString &status);

    Decoder m_decoder;
    std::shared_ptr<ImageCacheController> m_cache;
    Tasks::TaskRunner m_tasks{2, 60000};
    QFutureWatcher<Outcome> m_watcher;
    QFutureWatcher<Export> m_exportWatcher;
    std::shared_ptr<const Result> m_result;
    QTemporaryDir m_previewFolder;
    int m_previewNumber = 0;
    QUrl m_preview;
    QUrl m_exportedFile;
    QString m_status;
    double m_progress = 0.0;
    bool m_running = false;
    bool m_exporting = false;
};

#endif // IPR_STACK_STACKSESSION_H
