#ifndef THUMBNAILPROVIDER_H
#define THUMBNAILPROVIDER_H

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include <functional>

// The thumbnail cache is intentionally a viewport cache, rather than a
// global LRU: setActiveFiles() evicts each image that leaves the visible
// range plus its small prefetch margin.
//
// Asynchronous provider with its own low-priority pool: synchronous providers
// share QML's single image reader thread, so a queue of RAW thumbnails would
// delay the preview of the image being opened.
class ThumbnailProvider final : public QQuickAsyncImageProvider
{
public:
    using Loader = std::function<QImage(const QString &fileName, const QSize &size)>;

    explicit ThumbnailProvider(Loader loader = {}, int maxThreads = 0);
    ~ThumbnailProvider() override;

    void setActiveFiles(const QStringList &fileNames);
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
    int maxThreadCount() const { return m_pool.maxThreadCount(); }

private:
    class Response;
    struct CachedThumbnail
    {
        QString fileName;
        QImage image;
    };

    QImage produce(const QString &fileName, const QSize &size);
    static QImage loadThumbnail(const QString &fileName, const QSize &size);
    static QString cacheKey(const QString &fileName, const QSize &size);

    Loader m_loader;
    mutable QMutex m_mutex;
    QSet<QString> m_activeFiles;
    QHash<QString, CachedThumbnail> m_cache;
    QThreadPool m_pool;
};

#endif // THUMBNAILPROVIDER_H
