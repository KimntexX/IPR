#include "ThumbnailProvider.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQuickTextureFactory>
#include <QScopeGuard>
#include <QThread>
#include <QUrl>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
    QElapsedTimer clock; clock.start();
    while (!done() && clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return done();
}
QString id(const QString &file) { return QString::fromLatin1(QUrl::toPercentEncoding(file)); }
// Collects finished() like the QML engine does, then disposes the response.
struct Request {
    QQuickImageResponse *response = nullptr;
    std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
    QImage image() const {
        std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
        return factory ? factory->image() : QImage();
    }
};
Request request(ThumbnailProvider &provider, const QString &file,
                std::vector<QQuickImageResponse *> &responses)
{
    Request r;
    r.response = provider.requestImageResponse(id(file), QSize(116, 49));
    responses.push_back(r.response);
    QObject::connect(r.response, &QQuickImageResponse::finished, [done = r.done] { done->store(true); });
    return r;
}
}

int main(int argc, char **argv)
{
    // Texture factories need a GUI application; no window is ever shown.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::atomic_int running{0}, peak{0}, calls{0};
    std::atomic_bool release{false};
    auto ownedProvider = std::make_unique<ThumbnailProvider>([&](const QString &, const QSize &size) {
        ++calls;
        // Atomic maximum: a plain load-then-store lets a slower thread
        // overwrite the true peak with its own lower count.
        const int now = ++running;
        int previous = peak.load();
        while (previous < now && !peak.compare_exchange_weak(previous, now)) {}
        while (!release.load()) QThread::msleep(1);
        --running;
        QImage image(size, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        return image;
    }, 3);
    auto &provider = *ownedProvider;
    std::vector<QQuickImageResponse *> responses;
    // On every exit, unblock and join the workers BEFORE deleting responses.
    const auto cleanup = qScopeGuard([&] {
        release.store(true);
        ownedProvider.reset();
        for (auto *response : responses) delete response;
    });
    if (!check(provider.maxThreadCount() == 3, "dedicated bounded pool")) return 1;
    const QStringList files{"C:/ipr/A.CR3", "C:/ipr/B.CR3", "C:/ipr/C.CR3", "C:/ipr/D.CR3"};
    provider.setActiveFiles(files);
    std::vector<Request> requests;
    for (const auto &file : files) requests.push_back(request(provider, file, responses));
    // Never on the caller's (QML reader) thread: the request returns at once.
    if (!check(waitFor([&] { return running.load() == 3; }) && peak.load() == 3,
        "thumbnails decode in parallel, bounded by the pool")) return 2;
    release.store(true);
    if (!check(waitFor([&] { for (auto &r : requests) if (!r.done->load()) return false; return true; }),
        "every response finishes")) return 2;
    if (!check(requests[0].image().pixelColor(0, 0) == Qt::red, "decoded image is delivered")) return 2;
    const int beforeHit = calls.load();
    auto hit = request(provider, files[0], responses);
    if (!check(waitFor([&] { return hit.done->load(); }) && calls.load() == beforeHit &&
        hit.image().pixelColor(0, 0) == Qt::red, "active cache hit avoids a second decode")) return 3;
    // Left the ribbon window: decoded once, never cached again.
    provider.setActiveFiles({files[1]});
    auto stale = request(provider, files[0], responses);
    if (!check(waitFor([&] { return stale.done->load(); }) && calls.load() == beforeHit + 1,
        "evicted file is decoded again")) return 4;
    auto again = request(provider, files[0], responses);
    if (!check(waitFor([&] { return again.done->load(); }) && calls.load() == beforeHit + 2,
        "inactive file is not repopulated in the cache")) return 4;
    // A delegate scrolled away before its turn: skip the decode, still finish.
    release.store(false);
    provider.setActiveFiles(files);
    std::vector<Request> busy;
    for (int i = 0; i < 3; ++i) busy.push_back(request(provider, QStringLiteral("C:/ipr/busy%1.CR3").arg(i), responses));
    if (!waitFor([&] { return running.load() == 3; })) return 5;
    const int beforeCancel = calls.load();
    auto canceled = request(provider, "C:/ipr/E.CR3", responses);
    canceled.response->cancel();
    release.store(true);
    if (!check(waitFor([&] { return canceled.done->load(); }) && calls.load() == beforeCancel,
        "canceled request finishes without decoding")) return 5;
    if (!check(waitFor([&] { for (auto &r : busy) if (!r.done->load()) return false; return true; }),
               "active responses finish before cleanup")) return 5;
    {
        ThumbnailProvider throwing([](const QString &, const QSize &) -> QImage {
            throw std::runtime_error("synthetic decode failure");
        }, 1);
        auto failed = request(throwing, "C:/ipr/failure.CR3", responses);
        if (!check(waitFor([&] { return failed.done->load(); }) &&
                   !failed.response->errorString().isEmpty(), "loader exception finishes with an error")) return 6;
    }
    // Destruction with one active decode and queued responses: all must finish,
    // even though no QML event loop is running during the destructor's join.
    release.store(false);
    std::atomic_bool shutdownEntered{false};
    auto shutdown = std::make_unique<ThumbnailProvider>([&](const QString &, const QSize &) {
        shutdownEntered.store(true);
        while (!release.load()) QThread::msleep(1);
        return QImage{};
    }, 1);
    const auto shutdownCleanup = qScopeGuard([&] { release.store(true); shutdown.reset(); });
    std::vector<Request> queued;
    for (int i = 0; i < 12; ++i)
        queued.push_back(request(*shutdown, QStringLiteral("C:/ipr/close%1.CR3").arg(i), responses));
    if (!check(waitFor([&] { return shutdownEntered.load(); }), "shutdown test has an active decode")) return 7;
    std::thread unblock([&] { QThread::msleep(20); release.store(true); });
    const auto joinUnblock = qScopeGuard([&] { release.store(true); unblock.join(); });
    shutdown.reset();
    for (const auto &r : queued)
        if (!check(r.done->load(), "destruction completes every queued response")) return 7;
    std::cout << "Thumbnails: dedicated parallel pool, cache hit, window eviction, cancellation passed\n";
    return 0;
}
