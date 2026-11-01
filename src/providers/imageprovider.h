#ifndef IPR_PROVIDERS_IMAGEPROVIDER_H
#define IPR_PROVIDERS_IMAGEPROVIDER_H

#include <QImage>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <QUrl>
class ImageProvider final : public QQuickImageProvider
{
public:
    ImageProvider();

    Q_PROPERTY(QUrl url READ url SET setUrl)
    void setImage(const QImage &image);
    QImage image() const;
    void setImages(const QImage &current, const QImage &original);
    QImage originalImage() const;
    Q_INVOKABLE void setUrl(const QUrl &url);
    QUrl url() const;

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

private:
    mutable QReadWriteLock m_lock;
    QImage m_image;
    QImage m_originalImage;
    QUrl m_url;
};

#endif // IPR_PROVIDERS_IMAGEPROVIDER_H
