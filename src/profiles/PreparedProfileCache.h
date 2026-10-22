#ifndef IPR_PROFILES_PREPAREDPROFILECACHE_H
#define IPR_PROFILES_PREPAREDPROFILECACHE_H

#include <QString>
#include <QByteArray>
#include <functional>
#include <memory>

// Internal process-wide store for immutable parsed profiles, never image buffers.
namespace PreparedProfileCache {
struct Statistics
{
    quint64 budgetBytes = 64ULL * 1024 * 1024;
    int entryLimit = 16;
    quint64 retainedBytes = 0;
    int entries = 0;
    quint64 hits = 0, misses = 0, preparations = 0, waits = 0, evictions = 0;
};
struct Loaded
{
    std::shared_ptr<const void> data;
    quint64 accountedBytes = 0;
};
using Loader = std::function<Loaded(const QByteArray &, QString *)>;

// kind identifies both the concrete type and parser revision. Reads/hashes the
// current file bytes on every call, including hits; no timestamp-only shortcut.
std::shared_ptr<const void> acquire(const QString &path, const QByteArray &kind,
                                   const Loader &loader, QString *error);
Statistics statistics();
void configure(quint64 budgetBytes, int entryLimit);
// Clears retained entries/counters, not live reader references or active work.
void clear();
}

#endif // IPR_PROFILES_PREPAREDPROFILECACHE_H
