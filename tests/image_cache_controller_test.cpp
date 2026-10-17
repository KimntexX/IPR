#include "ImageCacheController.h"
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <functional>
#include <mutex>
#include <iostream>
#include <thread>
#include <vector>

namespace {
constexpr quint64 MiB = 1024 * 1024, GiB = 1024 * MiB;
bool check(bool condition, const char *message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::atomic<quint64> available{24 * GiB};
    std::atomic_bool valid{true}, wrongThread{false};
    auto *caller = QThread::currentThread();
    ImageCacheController controller(nullptr, [&] {
        if (QThread::currentThread() == caller) wrongThread.store(true);
        return SystemMemory::Snapshot{32 * GiB, available.load(), 0, valid.load(), false};
    });
    const auto admit = [&](const QString &key, quint64 size = 1, bool background = false) {
        return controller.acquire(key, size, background).result();
    };
    // A waiting foreground request stays unresolved across several 500 ms samples.
    const auto stillWaiting = [&](const QFuture<ImageCacheController::Admission> &future) {
        for (int i = 0; i < 70; ++i) {
            if (future.isFinished()) return false;
            QThread::msleep(10);
        }
        return !future.isFinished();
    };
    const auto waitStats = [&](const std::function<bool(const ImageCacheController::Statistics &)> &done) {
        for (int i = 0; i < 300 && !done(controller.statistics()); ++i) QThread::msleep(10);
        return done(controller.statistics());
    };
    auto token = admit({});
    if (!check(token.reservation && !wrongThread, "sampling/admission must use the dedicated thread")) return 1;
    controller.release(token.reservation);
    if (!check(controller.statistics().budgetBytes == 8 * GiB, "automatic retention cap")) return 1;
    ColorPipeline::WorkingImage image;
    image.linearRgba = cv::Mat::zeros(100, 100, CV_32FC4);
    const DecodedImageCache::Entry entry{image, image, {}};
    const auto size = DecodedImageCache::entrySizeBytes(entry);
    controller.setBudgetBytes(size * 2);
    controller.insert("A", entry); controller.insert("B", entry);
    auto a = admit("A");
    if (!check(a.cached.has_value() && !a.reservation, "cache hit transfers pixels without a decode reservation")) return 2;
    controller.insert("A", std::move(*a.cached));
    controller.insert("C", entry);
    auto b = admit("B");
    if (!check(!b.cached && b.reservation, "least recently used B must be evicted")) return 2;
    controller.release(b.reservation);
    auto c = admit("C");
    if (!check(c.cached.has_value(), "most recent entry must survive")) return 2;
    // External holders (render/export) keep pixels valid after controller eviction.
    auto active = c.cached->image;
    available.store(5 * GiB);
    auto denied = admit("absent", MiB, true);
    if (!check(!denied.reservation && !denied.error.isEmpty() && controller.count() == 0 &&
        !active.isNull(), "80% pressure evicts retained entries but never destroys active pixels")) return 3;
    available.store(7 * GiB); // <80%, still >75%: hysteresis must hold.
    denied = admit({}, MiB, true);
    if (!check(!denied.reservation && controller.statistics().pressure, "75% hysteresis")) return 3;
    available.store(24 * GiB);
    token = admit({}, MiB, true);
    if (!check(token.reservation && !controller.statistics().pressure, "preload resumes below 75%")) return 3;
    controller.release(token.reservation);
    token = admit({}, 12 * GiB);
    if (!check(token.reservation, "foreground large reservation")) return 4;
    auto queued = controller.acquire({}, 8 * GiB, false);
    if (!check(stillWaiting(queued) && waitStats([](const auto &s) { return s.waitingRequests == 1 && s.waitingBytes == 8 * GiB; }),
        "over-admission waits instead of being refused")) return 4;
    denied = admit({}, 1, true);
    if (!check(!denied.reservation, "foreground, even waiting, takes priority over preload")) return 4;
    controller.release(token.reservation);
    token = queued.result();
    if (!check(token.reservation && !controller.statistics().waitingRequests,
        "finished work returns its reservation and wakes the waiting request")) return 4;
    controller.release(token.reservation);
    // Leaving image A for B while A waits: the stale wait ends without a reservation.
    available.store(2 * GiB); // Below even the pressure path: work + safety > RAM.
    auto cancel = std::make_shared<std::atomic_bool>(false);
    auto stale = controller.acquire({}, 2 * GiB, false, cancel);
    auto next = controller.acquire({}, 2 * GiB, false);
    if (!check(stillWaiting(stale) && stillWaiting(next), "FIFO waiting under pressure")) return 4;
    cancel->store(true);
    if (!check(stale.result().reservation == 0 && !stale.result().error.isEmpty() && !next.isFinished(),
        "cancelled wait ends while the newer request keeps waiting")) return 4;
    available.store(24 * GiB);
    token = next.result();
    if (!check(token.reservation, "freed external memory resumes the waiting open")) return 4;
    controller.release(token.reservation);
    // Other applications cause the pressure and IPR holds nothing: proceed, flagged.
    available.store(6 * GiB);
    token = admit({}, 2 * GiB);
    if (!check(token.reservation && token.underPressure &&
        waitStats([](const auto &s) { return s.pressureAdmissions == 1; }),
        "external pressure with nothing to release admits under pressure")) return 4;
    queued = controller.acquire({}, 2 * GiB, false);
    if (!check(stillWaiting(queued), "IPR's own reservation is released before any further pressure admission")) return 4;
    controller.release(token.reservation);
    token = queued.result();
    if (!check(token.reservation && token.underPressure, "waiting request proceeds once IPR released its work")) return 4;
    controller.release(token.reservation);
    available.store(24 * GiB);
    denied = admit({}, 30 * GiB);
    if (!check(!denied.reservation && denied.error.contains("volumineuse"),
        "an image that could never fit under 80% is refused immediately")) return 4;
    controller.setBudgetBytes(0);
    denied = admit({}, 1, true);
    if (!check(!denied.reservation, "disabled cache must disable speculative work")) return 4;
    valid.store(false);
    denied = admit({}, MiB);
    if (!check(!denied.reservation && !controller.statistics().memory.valid, "missing OS information fails closed")) return 5;
    valid.store(true);
    available.store(0);
    queued = controller.acquire({}, MiB, false);
    if (!check(stillWaiting(queued), "no available RAM is not admission")) return 5;
    controller.abortWaiting();
    if (!check(!queued.result().reservation, "abort ends waits for shutdown")) return 5;
    available.store(24 * GiB);
    std::vector<QFuture<ImageCacheController::Admission>> admissions;
    std::vector<std::thread> threads;
    std::mutex lock;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        auto future = controller.acquire({}, 4 * GiB, false);
        std::lock_guard guard(lock); admissions.push_back(future);
    });
    for (auto &thread : threads) thread.join();
    if (!check(waitStats([](const auto &s) { return s.waitingRequests == 4; }),
        "eight concurrent requests must share one reservation ledger")) return 5;
    // Snapshot first: each release below may admit a waiting request at once.
    std::vector<QFuture<ImageCacheController::Admission>> acceptedHalf, waitingHalf;
    for (auto &admission : admissions)
        (admission.isFinished() ? acceptedHalf : waitingHalf).push_back(admission);
    const auto accepted = acceptedHalf.size();
    for (auto &admission : acceptedHalf) controller.release(admission.result().reservation);
    for (auto &admission : waitingHalf) controller.release(admission.result().reservation);
    if (!check(accepted == 4 && waitStats([](const auto &s) { return !s.waitingRequests && !s.reservedBytes; }),
        "released reservations admit the waiting half, then everything is returned")) return 5;
    {
        QFuture<ImageCacheController::Admission> pending;
        {
            ImageCacheController lowCommit(nullptr, [] {
                return SystemMemory::Snapshot{32 * GiB, 24 * GiB, MiB, true, true};
            });
            pending = lowCommit.acquire({}, 2 * MiB, false);
            if (!check(stillWaiting(pending), "commit availability is also enforced")) return 5;
        }
        if (!check(pending.isFinished() && !pending.result().reservation, "destroying the controller ends waits")) return 5;
    }
    {
        // IPR's own admitted peak is reserved: it must not evict retained images.
        std::atomic<quint64> free{24 * GiB};
        ImageCacheController own(nullptr, [&] { return SystemMemory::Snapshot{32 * GiB, free.load(), 0, true, false}; });
        const auto ownStats = [&](const std::function<bool(const ImageCacheController::Statistics &)> &done) {
            for (int i = 0; i < 300 && !done(own.statistics()); ++i) QThread::msleep(10);
            return done(own.statistics());
        };
        own.insert("kept", entry);
        if (!ownStats([](const auto &s) { return s.entries == 1; })) return 7;
        auto work = own.acquire({}, 12 * GiB, false).result();
        free.store(5 * GiB); // 84% raw use, 47% once the reserved work is excluded
        QThread::msleep(700);
        if (!check(work.reservation && own.statistics().entries == 1 && !own.statistics().pressure,
            "the reserved peak of admitted work does not evict the cache")) return 7;
        own.release(work.reservation);
        free.store(24 * GiB);
        if (!ownStats([](const auto &s) { return !s.pressure && !s.reservedBytes && s.entries == 1; })) return 7;
        // 78%, no pressure, but no margin: evicting a small entry cannot fit 4 GiB.
        free.store(7 * GiB);
        auto big = own.acquire({}, 4 * GiB, false).result();
        if (!check(big.reservation && big.underPressure && own.statistics().entries == 1,
            "pointless eviction is skipped; unfittable work proceeds under pressure")) return 7;
        own.release(big.reservation);
        free.store(5 * GiB);
        if (!check(ownStats([](const auto &s) { return s.pressure && s.entries == 0; }),
            "settled external pressure still evicts retained images")) return 7;
    }
    // Same policy on all platforms: native sampler smoke check and peak estimator.
    const auto native = SystemMemory::read();
    if (!check(native.valid && native.totalBytes && native.availableBytes <= native.totalBytes, "native RAM sampler")) return 6;
    QTemporaryDir directory;
    const auto file = directory.filePath("dimensions.png");
    if (!QImage(100, 200, QImage::Format_RGB32).save(file)) return 6;
    if (!check(SystemMemory::estimateDecodePeak(file) == SystemMemory::estimateProcessingPeak(20000),
               "raster header determines reservation before decoding")) return 6;
    if (!check(SystemMemory::estimateProcessingPeak(std::numeric_limits<quint64>::max()) ==
        std::numeric_limits<quint64>::max(), "peak estimate overflow saturates")) return 6;
    std::cout << "Memory controller: thread, cap, LRU, pins, hysteresis, reservation, waiting, cancellation, pressure, native sampler passed\n";
    return 0;
}
