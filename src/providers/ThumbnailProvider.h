#ifndef THUMBNAILPROVIDER_H
#define THUMBNAILPROVIDER_H

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSet>
#include <QStringList>

// The thumbnail cache is intentionally a viewport cache, rather than a
// global LRU: setActiveFiles() evicts each image that leaves the visible
// range plus its small prefetch margin.
class ThumbnailProvider final : public QQuickImageProvider
{
public:
    ThumbnailProvider();

    void setActiveFiles(const QStringList &fileNames);
    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

private:
    struct CachedThumbnail
    {
        QString fileName;
        QImage image;
    };

    static QImage loadThumbnail(const QString &fileName, const QSize &size);
    static QString cacheKey(const QString &fileName, const QSize &size);

    mutable QMutex m_mutex;
    QSet<QString> m_activeFiles;
    QHash<QString, CachedThumbnail> m_cache;
};

#endif // THUMBNAILPROVIDER_H
