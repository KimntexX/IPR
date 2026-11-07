#include "CameraDng.h"
#include "ImageCacheController.h"
#include "StackSession.h"
#include "controlled_memory.h"
#include "stack_fixture.h"
#include "synthetic_dcp.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 60000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(10);
    if (!done()) loop.exec();
    return done();
}

using StackFixture::Source;
using StackFixture::frames;

void session(const QDir &folder, const std::shared_ptr<CameraProfileEngine::DcpWorkingContext> &context)
{
    Source source;
    source.context = context;
    StackSession stacking(source);

    // Too little to stack: said at once.
    stacking.start({});
    check(!stacking.running() && !stacking.status().isEmpty(), "no frames: a message");
    stacking.start(frames(folder, 1));
    check(!stacking.running() && !stacking.hasResult(), "one frame: no job");
    check(stacking.suggestedExport(QStringLiteral("DNG")).isEmpty(), "no result, no name to suggest");
    stacking.exportResult(QUrl::fromLocalFile(folder.filePath(QStringLiteral("x.dng"))), QStringLiteral("DNG"));
    check(!stacking.exporting() && !QFile::exists(folder.filePath(QStringLiteral("x.dng"))), "nothing to export");

    // The whole path: a result, shown, ready to export.
    const QStringList urls = frames(folder, 7);
    stacking.start(urls, urls.at(2));
    check(stacking.running(), "the job starts");
    stacking.start(urls);
    check(*source.calls <= 7, "a second start waits its turn");
    check(waitFor([&] { return !stacking.running(); }), "the job ends");
    check(stacking.hasResult(), "with a result");
    if (!stacking.hasResult()) {
        std::cerr << stacking.status().toStdString() << '\n';
        return;
    }
    check(stacking.status().contains(QStringLiteral("7")), "the status tells the frames");
    check(stacking.progress() == 1.0, "the progress ends complete");
    const QString preview = stacking.preview().toLocalFile();
    const QImage shown(preview);
    check(!shown.isNull() && shown.width() > 100, "a preview is shown");
    check(stacking.exportedFile().isEmpty(), "nothing exported yet");
    const QUrl suggested = stacking.suggestedExport(QStringLiteral("DNG"));
    check(suggested.toLocalFile().contains(QStringLiteral("frame2-empilement.dng")), "the name comes from the reference");

    // Refused without touching the result.
    stacking.exportResult(QUrl::fromLocalFile(folder.filePath(QStringLiteral("x.jpg"))), QStringLiteral("JPEG"));
    check(!stacking.exporting() && stacking.hasResult(), "another format is refused");
    stacking.exportResult(QUrl(), QStringLiteral("DNG"));
    check(!stacking.exporting(), "so is no destination");

    // DNG: the signal, read back as such.
    const QString dng = folder.filePath(QStringLiteral("stack.dng"));
    stacking.exportResult(QUrl::fromLocalFile(dng), QStringLiteral("DNG"));
    check(stacking.exporting(), "an export starts");
    check(waitFor([&] { return !stacking.exporting(); }), "and ends");
    check(stacking.exportedFile().toLocalFile() == dng && QFile::exists(dng), "the DNG is there");
    CameraDng::Content read;
    QString error;
    check(CameraDng::read(dng, read, &error), "it is a camera signal");
    check(read.provenance.contains(QStringLiteral("7 frames")) && read.provenance.contains(QStringLiteral("frame2")), "it tells where it comes from");
    check(read.cameraModel == QStringLiteral("EOS R6 Mark III") && read.cameraNeutral == context->cameraNeutral, "with its camera");

    // Again: never over the first.
    stacking.exportResult(QUrl::fromLocalFile(dng), QStringLiteral("DNG"));
    check(waitFor([&] { return !stacking.exporting(); }), "a second export ends");
    check(stacking.exportedFile().toLocalFile() != dng && QFile::exists(stacking.exportedFile().toLocalFile()), "under another name");

    // TIFF: rendered by the profile.
    const QString tiff = folder.filePath(QStringLiteral("stack.tiff"));
    stacking.exportResult(QUrl::fromLocalFile(tiff), QStringLiteral("TIFF"));
    check(waitFor([&] { return !stacking.exporting(); }), "the TIFF export ends");
    const QImage rendered(stacking.exportedFile().toLocalFile());
    check(!rendered.isNull() && rendered.width() > 100, "the TIFF is an image");

    // The result can go, and with it what depends on it.
    stacking.discard();
    check(!stacking.hasResult() && stacking.preview().isEmpty() && stacking.exportedFile().isEmpty(), "discarding clears");
}

void cancelling(const QDir &folder, const std::shared_ptr<CameraProfileEngine::DcpWorkingContext> &context)
{
    Source source;
    source.context = context;
    source.slowMs = 400;
    StackSession stacking(source);
    stacking.start(frames(folder, 7));
    check(waitFor([&] { return *source.calls >= 2; }), "the job is under way");
    stacking.cancel();
    check(waitFor([&] { return !stacking.running(); }), "a cancelled job ends");
    check(!stacking.hasResult() && stacking.status().contains(QStringLiteral("annulé")), "and says so");
    check(*source.calls < 7, "before the end of the frames");

    // A running job does not hold up the closing beyond its next check.
    QElapsedTimer watch;
    watch.start();
    {
        Source slow = source;
        slow.calls = std::make_shared<std::atomic_int>(0);
        StackSession closing(slow);
        closing.start(frames(folder, 7));
        waitFor([&] { return *slow.calls >= 1; });
    }
    check(watch.elapsed() < 20000, "closing cancels a running job");
}

void failing(const QDir &folder)
{
    // No profile for this camera: no context, so no stack.
    Source source;
    StackSession stacking(source);
    stacking.start(frames(folder, 5));
    check(waitFor([&] { return !stacking.running(); }), "the job ends");
    check(!stacking.hasResult() && stacking.status().contains(QStringLiteral("profil")), "without a profile it says why");

    StackSession none(StackSession::Decoder{});
    none.start(frames(folder, 5));
    check(!none.running() && !none.hasResult(), "without a decoder nothing runs");

    // Every frame refuses: the reason reaches the panel.
    StackSession broken([](const QString &, const std::function<bool()> &) {
        StackSession::Decoded decoded;
        decoded.error = QStringLiteral("illisible");
        return decoded;
    });
    broken.start(frames(folder, 5));
    check(waitFor([&] { return !broken.running(); }), "the broken job ends");
    check(!broken.hasResult() && broken.status().contains(QStringLiteral("illisible")), "with the decoder's reason");
}

void panoramas(const QDir &folder, const std::shared_ptr<CameraProfileEngine::DcpWorkingContext> &context)
{
    // Views along one scene, a third of each shared with the next.
    const auto world = std::make_shared<const ipr::Image>(StackFixture::scene(ipr::Size(1500, 420)));
    const auto decoder = [world, context](const QString &file, const std::function<bool()> &) {
        const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
        StackSession::Decoded decoded;
        decoded.cameraRgb = world->copy(ipr::Recti(index * 430, 0, 640, 420));
        decoded.context = context;
        decoded.metadata.cameraMake = QStringLiteral("Canon");
        decoded.metadata.cameraModel = QStringLiteral("EOS R6 Mark III");
        return decoded;
    };
    auto cache = std::make_shared<ImageCacheController>(nullptr, controlledMemory);
    {
        StackSession assembling(decoder, cache);
        assembling.startPanorama({});
        check(!assembling.running() && !assembling.hasResult() && assembling.status().contains(QStringLiteral("deux")), "a panorama asks for two photos");
        assembling.startPanorama(frames(folder, 3), QStringLiteral("cylindrical"));
        check(assembling.running(), "a panorama starts");
        check(waitFor([&] { return !assembling.running(); }), "a panorama ends");
        check(assembling.hasResult(), "with a result");
        check(assembling.status().contains(QStringLiteral("Panorama de 3 photos")) && assembling.status().contains(QStringLiteral("cylindrique")), "and says what it made");
        check(assembling.preview().isLocalFile(), "with a preview");
        const QUrl suggested = assembling.suggestedExport(QStringLiteral("DNG"));
        check(suggested.toLocalFile().contains(QStringLiteral("-panorama.")), "named as a panorama");

        const QUrl dng = QUrl::fromLocalFile(folder.filePath(QStringLiteral("panorama.dng")));
        assembling.exportResult(dng, QStringLiteral("DNG"));
        check(waitFor([&] { return !assembling.exporting(); }), "its export ends");
        check(QFile::exists(dng.toLocalFile()) && assembling.exportedFile() == dng, "the DNG is written");
        CameraDng::Content read;
        QString error;
        check(CameraDng::read(dng.toLocalFile(), read, &error) && read.provenance.contains(QStringLiteral("panorama")), "and tells its origin");
        const int cropped = read.cameraRgb.width();

        assembling.startPanorama(frames(folder, 3), QStringLiteral("automatic"), QStringLiteral("keep"));
        check(waitFor([&] { return !assembling.running(); }), "a panorama with its edges ends");
        check(assembling.hasResult(), "with a result too");
        const QUrl whole = QUrl::fromLocalFile(folder.filePath(QStringLiteral("whole.dng")));
        assembling.exportResult(whole, QStringLiteral("DNG"));
        check(waitFor([&] { return !assembling.exporting(); }), "its export ends too");
        check(CameraDng::read(whole.toLocalFile(), read, &error) && read.cameraRgb.width() >= cropped, "keeping the edges keeps at least as much");
    }
    check(waitFor([&] { return cache->statistics().reservedBytes == 0; }), "a panorama gives its memory back");

    // Memory under pressure, the usual state of a busy machine: the cache admits nothing while
    // another reservation is held, so a job that kept its own would wait for itself.
    {
        const auto pressured = [] {
            SystemMemory::Snapshot memory = controlledMemory();
            memory.availableBytes = 6ULL * 1024 * 1024 * 1024;
            return memory;
        };
        auto busy = std::make_shared<ImageCacheController>(nullptr, pressured);
        {
            StackSession assembling(decoder, busy);
            assembling.startPanorama(frames(folder, 3));
            check(waitFor([&] { return !assembling.running(); }), "under pressure a panorama does not wait for itself");
            check(assembling.hasResult(), "and reaches its result");
        }
        check(waitFor([&] { return busy->statistics().reservedBytes == 0; }), "and gives its memory back");

        // Memory that never comes: the job gives up in time and says so, and holds nothing.
        const auto starved = [] {
            SystemMemory::Snapshot memory = controlledMemory();
            memory.availableBytes = 1024ULL * 1024;
            return memory;
        };
        auto empty = std::make_shared<ImageCacheController>(nullptr, starved);
        {
            StackSession assembling(decoder, empty);
            assembling.setReservationPatience(300);
            assembling.startPanorama(frames(folder, 3));
            check(waitFor([&] { return !assembling.running(); }, 20000), "without memory a panorama does not wait for ever");
            check(!assembling.hasResult() && assembling.status().contains(QStringLiteral("Mémoire")), "and says why");
        }
        check(waitFor([&] { return empty->statistics().reservedBytes == 0 && empty->statistics().waitingRequests == 0; }), "and leaves nothing behind");
    }

    // Frames far apart leave much of the canvas to invent: the message warns, and still delivers.
    {
        const auto tall = std::make_shared<const ipr::Image>(StackFixture::scene(ipr::Size(1500, 1000)));
        StackSession assembling([tall, context](const QString &file, const std::function<bool()> &) {
            const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
            StackSession::Decoded decoded;
            decoded.cameraRgb = tall->copy(ipr::Recti(index * 430, index * 280, 640, 420));
            decoded.context = context;
            decoded.metadata.cameraMake = QStringLiteral("Canon");
            decoded.metadata.cameraModel = QStringLiteral("EOS R6 Mark III");
            return decoded;
        });
        assembling.startPanorama(frames(folder, 3), QStringLiteral("cylindrical"), QStringLiteral("fill"));
        check(waitFor([&] { return !assembling.running(); }), "a much invented panorama ends");
        check(assembling.hasResult(), "with a result: the warning does not block");
        check(assembling.status().contains(QStringLiteral("Attention")) && assembling.status().contains(QStringLiteral("inventés")), "and warns of what it invented");

        // Close frames invent little, and only say so.
        StackSession close(decoder);
        close.startPanorama(frames(folder, 3), QStringLiteral("cylindrical"), QStringLiteral("fill"));
        check(waitFor([&] { return !close.running(); }), "a little invented panorama ends");
        check(close.hasResult() && !close.status().contains(QStringLiteral("Attention")), "without alarm");
    }

    // No profile for this camera: no context, so no panorama, and the reason reaches the panel.
    StackSession noProfile([world](const QString &file, const std::function<bool()> &) {
        const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
        StackSession::Decoded decoded;
        decoded.cameraRgb = world->copy(ipr::Recti(index * 430, 0, 640, 420));
        return decoded;
    });
    noProfile.startPanorama(frames(folder, 3));
    check(waitFor([&] { return !noProfile.running(); }), "a panorama without a profile ends");
    check(!noProfile.hasResult() && !noProfile.status().isEmpty(), "and says why");

    // Cancelled at once: no result, and the session stays usable.
    StackSession cancelled(decoder);
    cancelled.startPanorama(frames(folder, 3));
    cancelled.cancel();
    check(waitFor([&] { return !cancelled.running(); }), "a cancelled panorama ends");
    cancelled.startPanorama(frames(folder, 3));
    check(waitFor([&] { return !cancelled.running(); }), "and another can follow");
    check(cancelled.hasResult(), "to its end");
}

void reserving(const QDir &folder, const std::shared_ptr<CameraProfileEngine::DcpWorkingContext> &context)
{
    // The memory is reserved during the job, and given back on every path.
    auto cache = std::make_shared<ImageCacheController>(nullptr, controlledMemory);
    Source source;
    source.context = context;
    {
        StackSession stacking(source, cache);
        stacking.start(frames(folder, 7));
        check(waitFor([&] { return !stacking.running(); }), "a reserved job ends");
        check(stacking.hasResult(), "with a result");
    }
    check(waitFor([&] { return cache->statistics().reservedBytes == 0; }), "its memory is given back");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "a temporary folder");
    const QDir folder(directory.path());
    const QString profiles = folder.filePath(QStringLiteral("R6.dcp"));
    QFile file(profiles);
    check(file.open(QIODevice::WriteOnly) && file.write(SyntheticDcp::make(true)) > 0, "a synthetic profile");
    file.close();
    auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>();
    context->profilePath = profiles;
    context->sourceKelvin = 5000.0;
    context->cameraNeutral = ipr::Vec3d(0.6, 1.0, 0.8);

    session(folder, context);
    cancelling(folder, context);
    failing(folder);
    reserving(folder, context);
    panoramas(folder, context);
    if (passed)
        std::cout << "stack session: ok\n";
    return passed ? 0 : 1;
}
