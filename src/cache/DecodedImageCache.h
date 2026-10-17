#ifndef DECODEDIMAGECACHE_H
#define DECODEDIMAGECACHE_H

#include "ColorPipeline.h"
#include "ImageMetadata.h"

#include <QHash>
#include <QString>

#include <list>
#include <optional>
namespace RawDecoder { struct DcpSource; }

// Keeps decoded source images that are not currently being edited.  cv::Mat
// copies are shallow, so retaining an entry never copies the pixel buffer.
class DecodedImageCache final
{
public:
    struct Entry
    {
        ColorPipeline::WorkingImage image;
        ColorPipeline::WorkingImage editProxy;
        ImageMetadata metadata;
        std::shared_ptr<RawDecoder::DcpSource> dcpSource;
        QString profileDigest;
        // Proxy-only entry: image is the edit proxy; the full master is rebuilt
        // on export. fullSize keeps the true source dimensions.
        QSize fullSize;
        bool proxyOnly = false;
    };

    explicit DecodedImageCache(quint64 budgetBytes = 256ULL * 1024 * 1024);

    std::optional<Entry> find(const QString &key);
    std::optional<Entry> take(const QString &key);
    void insert(const QString &key, Entry entry);
    void clear();
    bool evictOldest();

    void setBudgetBytes(quint64 budgetBytes);
    quint64 budgetBytes() const;
    quint64 sizeBytes() const;
    qsizetype count() const;
    quint64 evictions() const { return m_evictions; }

    static quint64 entrySizeBytes(const Entry &entry);

private:
    struct StoredEntry
    {
        Entry entry;
        quint64 sizeBytes = 0;
        std::list<QString>::iterator lruPosition;
    };

    void touch(QHash<QString, StoredEntry>::iterator iterator);
    void trim();

    quint64 m_budgetBytes;
    quint64 m_sizeBytes = 0;
    quint64 m_evictions = 0;
    std::list<QString> m_lru;
    QHash<QString, StoredEntry> m_entries;
};

#endif // DECODEDIMAGECACHE_H
