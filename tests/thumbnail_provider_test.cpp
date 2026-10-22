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
Request request(ThumbnailProvider &provider, const QString &file)
{
    Request r;
    r.response = provider.requestImageResponse(id(file), QSize(116, 49));
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
    ThumbnailProvider provider([&](const QString &, const QSize &size) {
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
    // On any early return, free the blocked decodes before the pool waits for them.
    const auto releaseOnExit = qScopeGuard([&] { release.store(true); });
    if (!check(provider.maxThreadCount() == 3, "dedicated bounded pool")) return 1;
    const QStringList files{"C:/ipr/A.CR3", "C:/ipr/B.CR3", "C:/ipr/C.CR3", "C:/ipr/D.CR3"};
    provider.setActiveFiles(files);
    std::vector<Request> requests;
    for (const auto &file : files) requests.push_back(request(provider, file));
    // Never on the caller's (QML reader) thread: the request returns at once.
    if (!check(waitFor([&] { return running.load() == 3; }) && peak.load() == 3,
        "thumbnails decode in parallel, bounded by the pool")) return 2;
    release.store(true);
    if (!check(waitFor([&] { for (auto &r : requests) if (!r.done->load()) return false; return true; }),
        "every response finishes")) return 2;
    if (!check(requests[0].image().pixelColor(0, 0) == Qt::red, "decoded image is delivered")) return 2;
    for (auto &r : requests) delete r.response;
    const int beforeHit = calls.load();
    auto hit = request(provider, files[0]);
    if (!check(waitFor([&] { return hit.done->load(); }) && calls.load() == beforeHit &&
        hit.image().pixelColor(0, 0) == Qt::red, "active cache hit avoids a second decode")) return 3;
    delete hit.response;
    // Left the ribbon window: decoded once, never cached again.
    provider.setActiveFiles({files[1]});
    auto stale = request(provider, files[0]);
    if (!check(waitFor([&] { return stale.done->load(); }) && calls.load() == beforeHit + 1,
        "evicted file is decoded again")) return 4;
    delete stale.response;
    auto again = request(provider, files[0]);
    if (!check(waitFor([&] { return again.done->load(); }) && calls.load() == beforeHit + 2,
        "inactive file is not repopulated in the cache")) return 4;
    delete again.response;
    // A delegate scrolled away before its turn: skip the decode, still finish.
    release.store(false);
    provider.setActiveFiles(files);
    std::vector<Request> busy;
    for (int i = 0; i < 3; ++i) busy.push_back(request(provider, QStringLiteral("C:/ipr/busy%1.CR3").arg(i)));
    if (!waitFor([&] { return running.load() == 3; })) return 5;
    const int beforeCancel = calls.load();
    auto canceled = request(provider, "C:/ipr/E.CR3");
    canceled.response->cancel();
    release.store(true);
    if (!check(waitFor([&] { return canceled.done->load(); }) && calls.load() == beforeCancel,
        "canceled request finishes without decoding")) return 5;
    waitFor([&] { for (auto &r : busy) if (!r.done->load()) return false; return true; });
    for (auto &r : busy) delete r.response;
    delete canceled.response;
    std::cout << "Thumbnails: dedicated parallel pool, cache hit, window eviction, cancellation passed\n";
    return 0;
}
