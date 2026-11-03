#include "CameraDng.h"
#include "ImageCacheController.h"
#include "StackSession.h"
#include "controlled_memory.h"
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
#include <opencv2/imgproc.hpp>

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

cv::Mat scene(const cv::Size &size)
{
    cv::RNG random(7);
    cv::Mat image = cv::Mat::zeros(size, CV_32FC3);
    for (const double sigma : {1.5, 5.0, 18.0}) {
        cv::Mat layer(size, CV_32FC3);
        random.fill(layer, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(1));
        cv::GaussianBlur(layer, layer, cv::Size(), sigma);
        cv::Scalar mean, deviation;
        cv::meanStdDev(layer, mean, deviation);
        image += layer * (0.05 / std::max(1e-6, deviation[0]));
    }
    return image + cv::Scalar::all(0.3);
}

// Frames named frame0, frame1... in place of a RAW decoder: small views of one scene.
struct Source
{
    cv::Mat base = scene(cv::Size(500, 340));
    std::shared_ptr<CameraProfileEngine::DcpWorkingContext> context;
    std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0);
    int slowMs = 0;

    StackSession::Decoded operator()(const QString &file, const std::function<bool()> &cancelled) const
    {
        ++*calls;
        for (int waited = 0; waited < slowMs && !cancelled(); waited += 5)
            QThread::msleep(5);
        const int index = QFileInfo(file).baseName().remove(QStringLiteral("frame")).toInt();
        cv::Mat moved;
        const cv::Matx23d shift(1, 0, (index - 3) * 2.3, 0, 1, (index - 3) * -1.7);
        cv::warpAffine(base, moved, shift, base.size(), cv::INTER_LANCZOS4, cv::BORDER_REPLICATE);
        cv::RNG random(uint64_t(100 + index));
        cv::Mat noise(base.size(), CV_32FC3);
        random.fill(noise, cv::RNG::NORMAL, cv::Scalar::all(0), cv::Scalar::all(0.01));
        StackSession::Decoded decoded;
        decoded.cameraRgb = moved + noise;
        decoded.context = context;
        decoded.metadata.cameraMake = QStringLiteral("Canon");
        decoded.metadata.cameraModel = QStringLiteral("EOS R6 Mark III");
        return decoded;
    }
};

QStringList frames(const QDir &folder, int count)
{
    QStringList urls;
    for (int i = 0; i < count; ++i)
        urls.append(QUrl::fromLocalFile(folder.filePath(QStringLiteral("frame%1.CR3").arg(i))).toString());
    return urls;
}

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
    context->cameraNeutral = cv::Vec3d(0.6, 1.0, 0.8);

    session(folder, context);
    cancelling(folder, context);
    failing(folder);
    reserving(folder, context);
    if (passed)
        std::cout << "stack session: ok\n";
    return passed ? 0 : 1;
}
