#include "PreparedProfileCache.h"
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <algorithm>
#include <list>

namespace PreparedProfileCache {
namespace {
struct Entry { QByteArray key; Loaded profile; };
struct Pending {
    QWaitCondition ready;
    bool done = false;
    Loaded profile;
    QString error;
};
struct Store {
    QMutex mutex;
    Statistics stats;
    quint64 generation = 0;
    std::list<Entry> lru;
    QHash<QByteArray, std::list<Entry>::iterator> index;
    QHash<QByteArray, std::shared_ptr<Pending>> pending;
};
Store &store() { static Store value; return value; }
void reset(Store &s)
{
    const auto budget = s.stats.budgetBytes;
    const auto limit = s.stats.entryLimit;
    s.index.clear();s.lru.clear();++s.generation;
    s.stats = {};s.stats.budgetBytes = budget;s.stats.entryLimit = limit;
}
}

std::shared_ptr<const void> acquire(const QString &path, const QByteArray &kind,
                                   const Loader &loader, QString *error)
{
    if (error) error->clear();
    QFile file(path);
    constexpr qint64 maxFileBytes = 64LL * 1024 * 1024;
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxFileBytes) {
        if (error) *error = QStringLiteral("Profil illisible ou trop volumineux.");
        return {};
    }
    const auto bytes = file.read(maxFileBytes + 1);
    if (bytes.size() > maxFileBytes || file.error() != QFileDevice::NoError) {
        if (error) *error = QStringLiteral("Lecture du profil incomplète.");
        return {};
    }
    const auto key = kind + ':' + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    auto &s = store();
    QMutexLocker lock(&s.mutex);
    auto hit = s.index.find(key);
    if (hit != s.index.end()) {
        ++s.stats.hits;
        s.lru.splice(s.lru.begin(), s.lru, hit.value());
        return hit.value()->profile.data;
    }
    ++s.stats.misses;
    const auto found = s.pending.constFind(key);
    if (found != s.pending.cend()) {
        const auto pending = found.value();
        ++s.stats.waits;
        while (!pending->done) pending->ready.wait(&s.mutex);
        if (error) *error = pending->error;
        return pending->profile.data;
    }
    const auto generation = s.generation;
    const auto pending = std::make_shared<Pending>();
    s.pending.insert(key, pending);
    ++s.stats.preparations;
    lock.unlock();
    Loaded profile;
    QString failure;
    try {
        profile = loader(bytes, &failure);
    } catch (...) {
        failure = QStringLiteral("Échec de préparation du profil.");
    }
    if (!profile.data && failure.isEmpty()) failure = QStringLiteral("Profil invalide.");
    if (!failure.isEmpty()) profile = {};
    lock.relock();
    if (generation == s.generation && profile.data && profile.accountedBytes > 0 &&
        profile.accountedBytes <= s.stats.budgetBytes && s.stats.entryLimit > 0) {
        while (!s.lru.empty() && (int(s.lru.size()) >= s.stats.entryLimit ||
            s.stats.retainedBytes > s.stats.budgetBytes - profile.accountedBytes)) {
            const auto &old = s.lru.back();
            s.stats.retainedBytes -= old.profile.accountedBytes;
            s.index.remove(old.key);s.lru.pop_back();++s.stats.evictions;
        }
        s.lru.push_front({key, profile});s.index.insert(key, s.lru.begin());
        s.stats.retainedBytes += profile.accountedBytes;
        s.stats.entries = int(s.lru.size());
    }
    pending->profile = profile;pending->error = failure;pending->done = true;
    s.pending.remove(key);pending->ready.wakeAll();
    if (error) *error = failure;
    return profile.data;
}

Statistics statistics()
{
    auto &s = store();QMutexLocker lock(&s.mutex);return s.stats;
}
void configure(quint64 budgetBytes, int entryLimit)
{
    auto &s = store();QMutexLocker lock(&s.mutex);
    s.stats.budgetBytes = budgetBytes;s.stats.entryLimit = std::max(0, entryLimit);reset(s);
}
void clear()
{
    auto &s = store();QMutexLocker lock(&s.mutex);reset(s);
}
}
