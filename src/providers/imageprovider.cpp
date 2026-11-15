#include "imageprovider.h"

#include <QReadLocker>
#include <QWriteLocker>
#include <QDebug>

ImageProvider::ImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
}

void ImageProvider::setImage(const QImage &image)
{
    QWriteLocker locker(&m_lock);
    qDebug() << "setimage";
    m_image = image;
}

QImage ImageProvider::image() const
{
    QReadLocker locker(&m_lock);
    return m_image;
}

void ImageProvider::setImages(const QImage &current, const QImage &original)
{
    QWriteLocker locker(&m_lock);
    m_image = current;
    m_originalImage = original;
}

QImage ImageProvider::originalImage() const
{
    QReadLocker locker(&m_lock);
    return m_originalImage;
}

void ImageProvider::setDetailImage(const QImage &image)
{
    QWriteLocker locker(&m_lock);
    m_detailImage = image;
}

QImage ImageProvider::detailImage() const
{
    QReadLocker locker(&m_lock);
    return m_detailImage;
}

void ImageProvider::setDetailTile(const QString &name, const QImage &image)
{
    QWriteLocker locker(&m_lock);
    if (image.isNull())
        m_detailTiles.remove(name);
    else
        m_detailTiles.insert(name, image);
}

QImage ImageProvider::detailTile(const QString &name) const
{
    QReadLocker locker(&m_lock);
    return m_detailTiles.value(name);
}

void ImageProvider::clearDetailTiles(const QString &prefix)
{
    QWriteLocker locker(&m_lock);
    for (auto it = m_detailTiles.begin(); it != m_detailTiles.end();)
        it = it.key().startsWith(prefix) ? m_detailTiles.erase(it) : std::next(it);
}

int ImageProvider::detailTileCount() const
{
    QReadLocker locker(&m_lock);
    return int(m_detailTiles.size());
}

void ImageProvider::setUrl(const QUrl &url)
{
    QWriteLocker locker(&m_lock);
    qDebug() << "seturl";
    m_url = url;
}

QUrl ImageProvider::url() const
{
    QReadLocker locker(&m_lock);
    return m_url;
}

QImage ImageProvider::requestImage(const QString &id, QSize *size,
                                   const QSize &requestedSize)
{
    // Loading is now fully asynchronous (see ImageEditor::loadImage()): by
    // the time QML requests this image, the decoded preview has already
    // been pushed via setImage(). Re-decoding the RAW file here as a
    // fallback was redundant (every image was decoded twice) and is no
    // longer needed; an empty transparent pixel is returned until the
    // first render completes.
    const QString kind = id.section(QLatin1Char('?'), 0, 0);
    QImage result = kind == QStringLiteral("original") ? originalImage()
                  : kind == QStringLiteral("detail") ? detailImage()
                  : kind.startsWith(QStringLiteral("tile-")) || kind.startsWith(QStringLiteral("otile-"))
                        ? detailTile(kind) : image();
    if (result.isNull()) {
        result = QImage(1, 1, QImage::Format_RGBA8888);
        result.fill(Qt::transparent);
    }

    if (requestedSize.isValid()){
        result = result.scaled(requestedSize, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }

    if (size)
        *size = result.size();
    return result;
}
