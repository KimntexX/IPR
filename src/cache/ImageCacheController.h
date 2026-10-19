#ifndef IPR_CACHE_IMAGECACHECONTROLLER_H
#define IPR_CACHE_IMAGECACHECONTROLLER_H

#include "DecodedImageCache.h"
#include "SystemMemory.h"
#include <QObject>
#include <QMutex>
#include <QThread>
#include <QFuture>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>

// The worker thread exclusively owns the LRU and its reservation ledger.
// Signals run on this object's thread; futures are fulfilled on the worker.
class ImageCacheController final : public QObject
{
    Q_OBJECT
public:
    using Sampler = std::function<SystemMemory::Snapshot()>;
    using CancelToken = std::shared_ptr<std::atomic_bool>;
    struct Statistics {
        quint64 budgetBytes = 0, retainedBytes = 0, reservedBytes = 0;
        quint64 hits = 0, misses = 0, evictions = 0, denied = 0, pressureAdmissions = 0;
        quint64 admissibleBytes = 0, waitingBytes = 0;
        qsizetype entries = 0, waitingRequests = 0;
        bool pressure = true;
        SystemMemory::Snapshot memory;
    };
    struct Admission {
        std::optional<DecodedImageCache::Entry> cached;
        quint64 reservation = 0;
        QString error;
        bool underPressure = false; // admitted above the 80% target: nothing left to release
    };
    explicit ImageCacheController(QObject *parent = nullptr, Sampler sampler = {});
    ~ImageCacheController() override;
    // Consume on a decoding/render worker, never block the GUI on this future.
    // Background requests are refused under pressure. Foreground requests wait,
    // in FIFO order, while IPR can still release cache entries or reservations.
    // With nothing of IPR left to release, they proceed under pressure if the
    // OS commit limit (or available RAM) still covers them. A set cancel token
    // or abortWaiting() ends a wait without a reservation.
    QFuture<Admission> acquire(const QString &key, quint64 peakBytes, bool background,
                               CancelToken cancel = {});
    void release(quint64 reservation);
    void abortWaiting();
    void insert(const QString &key, DecodedImageCache::Entry entry);
    void clear();
    void setBudgetBytes(quint64 cap); // hard user cap; default automatic <=25% RAM / 8 GiB
    Statistics statistics() const;
    quint64 budgetBytes() const { return statistics().budgetBytes; }
    quint64 sizeBytes() const { return statistics().retainedBytes; }
    qsizetype count() const { return statistics().entries; }
signals:
    void changed();
private:
    struct Worker;
    void publish(const Statistics &stats);
    QThread m_thread;
    Worker *m_worker;
    mutable QMutex m_mutex;
    Statistics m_statistics;
};

#endif // IPR_CACHE_IMAGECACHECONTROLLER_H
