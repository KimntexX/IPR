#include "ThumbnailProvider.h"

#include "RawDecoder.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QUrl>

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

ThumbnailProvider::ThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
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

QImage ThumbnailProvider::requestImage(const QString &id, QSize *size,
                                       const QSize &requestedSize)
{
    const QString fileName = QFileInfo(
        QUrl::fromPercentEncoding(id.toUtf8())).absoluteFilePath();
    const QSize targetSize = requestedSize.isValid() ? requestedSize : DefaultThumbnailSize;
    const QString key = cacheKey(fileName, targetSize);
    {
        QMutexLocker locker(&m_mutex);
        const auto cached = m_cache.constFind(key);
        if (cached != m_cache.cend()) {
            if (size)
                *size = cached->image.size();
            return cached->image;
        }
    }

    QImage image = loadThumbnail(fileName, targetSize);
    if (image.isNull())
        image = placeholder(targetSize);

    {
        QMutexLocker locker(&m_mutex);
        // A queued request can complete after the ListView moved on. Do not
        // repopulate the cache for an item that has already left its window.
        if (m_activeFiles.contains(fileName))
            m_cache.insert(key, {fileName, image});
    }
    if (size)
        *size = image.size();
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
