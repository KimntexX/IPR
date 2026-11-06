#ifndef IPR_CACHE_DECODEDIMAGECACHE_H
#define IPR_CACHE_DECODEDIMAGECACHE_H

#include "ColorPipeline.h"
#include "ImageMetadata.h"

#include <QString>

#include <list>
#include <optional>
#include <unordered_map>
namespace RawDecoder { struct DcpSource; }

// Keeps decoded source images that are not currently being edited. What it gives
// back shares the pixels it keeps, read only: retaining an entry never copies them.
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
    // The same entry on the same pixels, shared read only.
    static Entry sharedCopy(const Entry &entry);

private:
    struct StoredEntry
    {
        Entry entry;
        quint64 sizeBytes = 0;
        std::list<QString>::iterator lruPosition;
    };

    using Entries = std::unordered_map<QString, StoredEntry>;
    void touch(Entries::iterator iterator);
    void trim();

    quint64 m_budgetBytes;
    quint64 m_sizeBytes = 0;
    quint64 m_evictions = 0;
    std::list<QString> m_lru;
    Entries m_entries;
};

#endif // IPR_CACHE_DECODEDIMAGECACHE_H
