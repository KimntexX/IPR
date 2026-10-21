#include "ThumbnailProvider.h"

#include "RawDecoder.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <atomic>

namespace {

constexpr QSize DefaultThumbnailSize(96, 64);

QImage placeholder(const QSize &requestedSize)
{
    const QSize size = requestedSize.isValid() ? requestedSize : DefaultThumbnailSize;
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(QColor(QStringLiteral("#26303c")));
    return image;
}

}

// Qt deletes a response after finished(); the pool never auto-deletes it.
class ThumbnailProvider::Response final : public QQuickImageResponse, public QRunnable
{
public:
    Response(ThumbnailProvider *provider, QString fileName, QSize size)
        : m_provider(provider), m_fileName(std::move(fileName)), m_size(size)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        // A delegate scrolled out of view: skip the decode, still finish.
        if (!m_canceled.load())
            m_image = m_provider->produce(m_fileName, m_size);
        emit finished();
    }
    void finishWith(QImage image)
    {
        m_image = std::move(image);
        emit finished();
    }
    void cancel() override { m_canceled.store(true); }
    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    ThumbnailProvider *m_provider;
    QString m_fileName;
    QSize m_size;
    QImage m_image;
    std::atomic_bool m_canceled{false};
};

ThumbnailProvider::ThumbnailProvider(Loader loader, int maxThreads)
    : m_loader(loader ? std::move(loader) : Loader(&ThumbnailProvider::loadThumbnail))
{
    // Thumbnails decode in parallel, but leave most cores and CPU priority to
    // the full-resolution decode of the image the user is opening.
    m_pool.setMaxThreadCount(maxThreads > 0 ? maxThreads
        : std::clamp(QThread::idealThreadCount() / 4, 2, 4));
    m_pool.setThreadPriority(QThread::LowPriority);
    m_pool.setObjectName(QStringLiteral("IPR thumbnails"));
}

ThumbnailProvider::~ThumbnailProvider()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void ThumbnailProvider::setActiveFiles(const QStringList &fileNames)
{
    QSet<QString> activeFiles;
    for (const QString &fileName : fileNames)
        activeFiles.insert(QFileInfo(fileName).absoluteFilePath());

    QMutexLocker locker(&m_mutex);
    m_activeFiles = std::move(activeFiles);
    for (auto iterator = m_cache.begin(); iterator != m_cache.end();) {
        if (!m_activeFiles.contains(iterator->fileName))
            iterator = m_cache.erase(iterator);
        else
            ++iterator;
    }
}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    const QString fileName = QFileInfo(
        QUrl::fromPercentEncoding(id.toUtf8())).absoluteFilePath();
    const QSize targetSize = requestedSize.isValid() ? requestedSize : DefaultThumbnailSize;
    auto *response = new Response(this, fileName, targetSize);
    {
        QMutexLocker locker(&m_mutex);
        const auto cached = m_cache.constFind(cacheKey(fileName, targetSize));
        if (cached != m_cache.cend()) {
            const QImage image = cached->image;
            locker.unlock();
            // Queued: the engine connects to finished() after this returns.
            QMetaObject::invokeMethod(response, [response, image] { response->finishWith(image); },
                                      Qt::QueuedConnection);
            return response;
        }
    }
    m_pool.start(response);
    return response;
}

QImage ThumbnailProvider::produce(const QString &fileName, const QSize &size)
{
    QImage image = m_loader(fileName, size);
    if (image.isNull())
        image = placeholder(size);

    QMutexLocker locker(&m_mutex);
    // A queued request can complete after the ListView moved on. Do not
    // repopulate the cache for an item that has already left its window.
    if (m_activeFiles.contains(fileName))
        m_cache.insert(cacheKey(fileName, size), {fileName, image});
    return image;
}

QImage ThumbnailProvider::loadThumbnail(const QString &fileName, const QSize &size)
{
    const QString extension = QFileInfo(fileName).suffix().toLower();
    QImage thumbnail;
    if (RawDecoder::supportedExtensions().contains(extension)) {
        thumbnail = RawDecoder::decodeEmbeddedThumbnail(fileName, size);
    } else {
        QImageReader reader(fileName);
        reader.setAutoTransform(true);
        const QSize sourceSize = reader.size();
        if (sourceSize.isValid() && size.isValid())
            reader.setScaledSize(sourceSize.scaled(size, Qt::KeepAspectRatio));
        thumbnail = reader.read();
    }

    return thumbnail;
}

QString ThumbnailProvider::cacheKey(const QString &fileName, const QSize &size)
{
    return fileName + QLatin1Char('|') + QString::number(size.width()) +
           QLatin1Char('x') + QString::number(size.height());
}
