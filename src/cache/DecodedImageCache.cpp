#include "DecodedImageCache.h"
#include "CameraProfileEngine.h"
#include "RawDecoder.h"

#include <QSet>

#include <limits>

DecodedImageCache::DecodedImageCache(quint64 budgetBytes)
    : m_budgetBytes(budgetBytes)
{
}

std::optional<DecodedImageCache::Entry> DecodedImageCache::find(const QString &key)
{
    auto iterator = m_entries.find(key);
    if (iterator == m_entries.end())
        return std::nullopt;

    touch(iterator);
    return iterator->entry;
}

void DecodedImageCache::insert(const QString &key, Entry entry)
{
    if (key.isEmpty() || entry.image.isNull() || m_budgetBytes == 0)
        return;

    const quint64 entryBytes = entrySizeBytes(entry);
    if (entryBytes > m_budgetBytes)
        return;

    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        m_sizeBytes -= existing->sizeBytes;
        m_lru.erase(existing->lruPosition);
        m_entries.erase(existing);
    }

    m_lru.push_front(key);
    StoredEntry stored;
    stored.entry = std::move(entry);
    stored.sizeBytes = entryBytes;
    stored.lruPosition = m_lru.begin();
    m_entries.insert(key, std::move(stored));
    m_sizeBytes += entryBytes;
    trim();
}

std::optional<DecodedImageCache::Entry> DecodedImageCache::take(const QString &key)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return std::nullopt;
    auto entry = std::move(it->entry);
    m_sizeBytes -= it->sizeBytes;
    m_lru.erase(it->lruPosition);
    m_entries.erase(it);
    return entry;
}

void DecodedImageCache::clear()
{
    m_entries.clear();
    m_lru.clear();
    m_sizeBytes = 0;
}

bool DecodedImageCache::evictOldest()
{
    if (m_lru.empty()) return false;
    const QString key = m_lru.back();
    auto it = m_entries.find(key);
    m_sizeBytes -= it->sizeBytes;
    m_entries.erase(it);
    m_lru.pop_back();
    ++m_evictions;
    return true;
}

void DecodedImageCache::setBudgetBytes(quint64 budgetBytes)
{
    m_budgetBytes = budgetBytes;
    trim();
}

quint64 DecodedImageCache::budgetBytes() const
{
    return m_budgetBytes;
}

quint64 DecodedImageCache::sizeBytes() const
{
    return m_sizeBytes;
}

qsizetype DecodedImageCache::count() const
{
    return m_entries.size();
}

quint64 DecodedImageCache::entrySizeBytes(const Entry &entry)
{
    // A small image uses its original as edit proxy. Count each allocation
    // only once rather than charging the same shallow cv::Mat twice.
    QSet<quintptr> buffers;
    quint64 bytes = 0;
    const auto add = [&buffers, &bytes](const cv::Mat &image) {
        if (image.empty() || !image.data)
            return;
        const quintptr address = reinterpret_cast<quintptr>(image.datastart);
        if (!buffers.contains(address)) {
            buffers.insert(address);
            const size_t imageBytes = image.total() * image.elemSize();
            const quint64 cappedBytes = imageBytes >
                    static_cast<size_t>(std::numeric_limits<quint64>::max())
                ? std::numeric_limits<quint64>::max()
                : static_cast<quint64>(imageBytes);
            bytes = cappedBytes > std::numeric_limits<quint64>::max() - bytes
                ? std::numeric_limits<quint64>::max()
                : bytes + cappedBytes;
        }
    };
    add(entry.image.linearRgba);
    add(entry.editProxy.linearRgba);
    if (entry.image.dcpContext)
        add(entry.image.dcpContext->cameraRgb);
    if (entry.editProxy.dcpContext)
        add(entry.editProxy.dcpContext->cameraRgb);
    if (entry.dcpSource) {
        add(entry.dcpSource->cameraRgb);
        if (entry.dcpSource->workingContext) add(entry.dcpSource->workingContext->cameraRgb);
    }
    return bytes;
}

void DecodedImageCache::touch(QHash<QString, StoredEntry>::iterator iterator)
{
    m_lru.erase(iterator->lruPosition);
    m_lru.push_front(iterator.key());
    iterator->lruPosition = m_lru.begin();
}

void DecodedImageCache::trim()
{
    while (!m_lru.empty() && m_sizeBytes > m_budgetBytes) {
        evictOldest();
    }
}
