#include "ImageCacheController.h"
#include <QTimer>
#include <QMutexLocker>
#include <QPromise>
#include <algorithm>
#include <deque>

namespace {
constexpr quint64 MiB = 1024 * 1024;
quint64 percent(quint64 value, quint64 amount) { return value / 100 * amount + value % 100 * amount / 100; }
quint64 subtract(quint64 value, quint64 amount) { return value > amount ? value - amount : 0; }
using Promise = std::shared_ptr<QPromise<ImageCacheController::Admission>>;
void fulfil(const Promise &promise, ImageCacheController::Admission admission)
{
    promise->addResult(std::move(admission));
    promise->finish();
}
}

struct ImageCacheController::Worker : QObject {
    struct Waiting { quint64 bytes; CancelToken cancel; Promise promise; };
    ImageCacheController *owner;
    Sampler sampler;
    DecodedImageCache cache{0};
    Statistics stats;
    quint64 cap = std::numeric_limits<quint64>::max(), nextId = 0;
    QHash<quint64, quint64> reservations;
    QHash<quint64, bool> foregroundReservations;
    std::deque<Waiting> waiting;

    Worker(ImageCacheController *o, Sampler s) : owner(o), sampler(std::move(s)) {}
    void refreshMemory() {
        try { stats.memory = sampler(); } catch (...) { stats.memory = {}; }
        if (!stats.memory.totalBytes || stats.memory.availableBytes > stats.memory.totalBytes)
            stats.memory.valid = false;
    }
    void publish() {
        stats.retainedBytes = cache.sizeBytes(); stats.entries = cache.count();
        stats.budgetBytes = cache.budgetBytes();
        stats.evictions = cache.evictions();
        stats.admissibleBytes = headroom();
        stats.waitingRequests = qsizetype(waiting.size());
        stats.waitingBytes = 0;
        for (const auto &request : waiting) stats.waitingBytes += request.bytes;
        owner->publish(stats);
    }
    void evaluate() {
        refreshMemory();
        if (!stats.memory.valid || !stats.memory.totalBytes) {
            stats.pressure = true;
            while (cache.evictOldest()) {}
            cache.setBudgetBytes(0); return;
        }
        const auto total = stats.memory.totalBytes;
        // The planned peak of admitted work is already reserved: it must not
        // evict retained images by itself. Settled use excludes reservations.
        const auto inFlight = std::min(stats.reservedBytes, total - stats.memory.availableBytes);
        const auto settledAvailable = stats.memory.availableBytes + inFlight;
        const auto used = total - settledAvailable;
        if (used >= percent(total, 80)) stats.pressure = true;
        else if (used <= percent(total, 75)) stats.pressure = false;
        cache.setBudgetBytes(std::min(cap, std::min(percent(total, 25), 8192 * MiB)));
        if (stats.pressure) {
            // Never pretend an eviction immediately freed RSS: resample the OS.
            while (cache.count() && stats.memory.availableBytes + inFlight < percent(total, 25)) {
                cache.evictOldest();
                refreshMemory();
                if (!stats.memory.valid) break;
            }
        }
    }
    void sample() { evaluate(); serveWaiting(); publish(); }
    quint64 safety() const { return std::max(128 * MiB, percent(stats.memory.totalBytes, 2)); }
    quint64 headroom() const {
        if (!stats.memory.valid) return 0;
        auto available = subtract(stats.memory.availableBytes, percent(stats.memory.totalBytes, 20));
        if (stats.memory.commitKnown) available = std::min(available, stats.memory.availableCommitBytes);
        return subtract(subtract(available, stats.reservedBytes), safety());
    }
    // Even an idle machine could not run this under the 80% target.
    bool neverFits(quint64 bytes) const {
        return bytes > subtract(percent(stats.memory.totalBytes, 80), safety());
    }
    // External applications cause the pressure: the OS can still page idle
    // memory out, as long as the commit limit (or plain RAM) covers the work.
    bool fitsUnderPressure(quint64 bytes) const {
        if (reservations.size()) return false;
        const auto limit = stats.memory.commitKnown ? stats.memory.availableCommitBytes : stats.memory.availableBytes;
        return bytes <= subtract(limit, safety());
    }
    quint64 reserve(quint64 bytes, bool background) {
        const auto id = ++nextId;
        reservations.insert(id, bytes); stats.reservedBytes += bytes;
        if (!background) foregroundReservations.insert(id, true);
        return id;
    }
    bool makeRoom(quint64 bytes) {
        // Evicting is pointless when even an empty cache could not fit the work.
        if (bytes > headroom() + cache.sizeBytes()) return false;
        while (bytes > headroom() && cache.count()) {
            cache.evictOldest();
            refreshMemory();
        }
        return bytes <= headroom();
    }
    // FIFO: a large waiting image is not starved by later small requests.
    void serveWaiting() {
        while (!waiting.empty()) {
            auto &front = waiting.front();
            if ((front.cancel && front.cancel->load()) || !stats.memory.valid) {
                ++stats.denied;
                fulfil(front.promise, {{}, 0, stats.memory.valid
                    ? QStringLiteral("Chargement annulé.")
                    : QStringLiteral("Mémoire système illisible : traitement refusé par précaution.")});
                waiting.pop_front();
                continue;
            }
            const bool fits = makeRoom(front.bytes);
            if (!fits && !fitsUnderPressure(front.bytes)) return;
            if (!fits) ++stats.pressureAdmissions;
            fulfil(front.promise, {{}, reserve(front.bytes, false), {}, !fits});
            waiting.pop_front();
        }
    }
    void admit(const QString &key, quint64 bytes, bool background, CancelToken cancel, Promise promise) {
        evaluate();
        serveWaiting();
        if (!key.isEmpty()) {
            if (auto hit = cache.take(key)) {
                ++stats.hits; publish(); return fulfil(promise, {std::move(hit), 0, {}});
            }
            ++stats.misses;
        }
        const auto deny = [&](const QString &error) { ++stats.denied; publish(); fulfil(promise, {{}, 0, error}); };
        if (background) {
            if (stats.pressure || !cache.budgetBytes() || !foregroundReservations.isEmpty() ||
                !waiting.empty() || !makeRoom(bytes))
                return deny(QStringLiteral("Préchargement suspendu : pression mémoire ou traitement prioritaire."));
            publish(); return fulfil(promise, {{}, reserve(bytes, true), {}});
        }
        if (!stats.memory.valid)
            return deny(QStringLiteral("Mémoire système illisible : traitement refusé par précaution."));
        if (!bytes || neverFits(bytes))
            return deny(QStringLiteral("Image trop volumineuse pour la mémoire de cette machine."));
        waiting.push_back({bytes, std::move(cancel), std::move(promise)});
        serveWaiting();
        publish();
    }
    void abort() {
        for (auto &request : waiting)
            fulfil(request.promise, {{}, 0, QStringLiteral("Chargement annulé.")});
        stats.denied += waiting.size();
        waiting.clear();
        publish();
    }
};

ImageCacheController::ImageCacheController(QObject *parent, Sampler sampler) : QObject(parent)
{
    if (!sampler) sampler = SystemMemory::read;
    m_worker = new Worker(this, std::move(sampler));
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_thread, &QThread::started, m_worker, [worker = m_worker] {
        auto *timer = new QTimer(worker);
        QObject::connect(timer, &QTimer::timeout, worker, [worker] { worker->sample(); });
        timer->start(500);
        worker->sample();
    });
    m_thread.setObjectName(QStringLiteral("IPR memory and image cache"));
    m_thread.start();
}
ImageCacheController::~ImageCacheController()
{
    // Waiting workers must never block forever on a destroyed controller.
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->abort(); }, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
}
void ImageCacheController::publish(const Statistics &stats)
{
    { QMutexLocker lock(&m_mutex); m_statistics = stats; }
    QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
}
ImageCacheController::Statistics ImageCacheController::statistics() const
{
    QMutexLocker lock(&m_mutex); return m_statistics;
}
QFuture<ImageCacheController::Admission> ImageCacheController::acquire(const QString &key, quint64 bytes,
                                                                       bool background, CancelToken cancel)
{
    auto promise = std::make_shared<QPromise<Admission>>();
    promise->start();
    auto future = promise->future();
    QMetaObject::invokeMethod(m_worker, [this, key, bytes, background, cancel, promise] {
        m_worker->admit(key, bytes, background, cancel, promise);
    }, Qt::QueuedConnection);
    return future;
}
void ImageCacheController::release(quint64 id)
{
    if (!id) return;
    QMetaObject::invokeMethod(m_worker, [this, id] {
        m_worker->stats.reservedBytes -= m_worker->reservations.take(id);
        m_worker->foregroundReservations.remove(id);
        m_worker->sample();
    }, Qt::QueuedConnection);
}
void ImageCacheController::abortWaiting()
{
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->abort(); }, Qt::QueuedConnection);
}
void ImageCacheController::insert(const QString &key, DecodedImageCache::Entry entry)
{
    QMetaObject::invokeMethod(m_worker, [this, key, entry = std::move(entry)]() mutable {
        m_worker->evaluate();
        // A waiting foreground request needs this memory more than retention.
        if (m_worker->stats.memory.valid && !m_worker->stats.pressure && m_worker->waiting.empty())
            m_worker->cache.insert(key, std::move(entry));
        m_worker->serveWaiting();
        m_worker->publish();
    }, Qt::QueuedConnection);
}
void ImageCacheController::clear()
{
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->cache.clear(); m_worker->publish(); }, Qt::QueuedConnection);
}
void ImageCacheController::setBudgetBytes(quint64 cap)
{
    QMetaObject::invokeMethod(m_worker, [this, cap] { m_worker->cap = cap; m_worker->sample(); }, Qt::QueuedConnection);
}
