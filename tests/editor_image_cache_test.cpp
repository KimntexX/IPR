#include "imageeditor.h"
#include "imageprovider.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>
#include <iostream>
#include <limits>

namespace {
bool waitFor(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    if (!condition()) std::cerr << "Timed out waiting for editor/cache state\n";
    return condition();
}
bool write(const QString &file) { QFile f(file); return f.open(QIODevice::WriteOnly) && f.write("fixture") == 7; }
}
// Friend of ImageEditor for its private proxy builder.
class IprTestImageGenerator
{
public:
    static ColorPipeline::WorkingImage proxy(const ColorPipeline::WorkingImage &image)
    {
        return ImageEditor::buildEditProxy(image);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    {
        // A restored proxy is revalidated as a master: INTER_AREA must not push
        // alpha above 1 (real 32 MP ratio 6959 -> 2048, smooth gradient).
        ColorPipeline::WorkingImage master;
        master.colorSpace = ColorPipeline::workingColorSpace();
        master.linearRgba = cv::Mat(97, 6959, CV_32FC4);
        for (int y = 0; y < master.linearRgba.rows; ++y)
            for (int x = 0; x < master.linearRgba.cols; ++x)
                master.linearRgba.at<cv::Vec4f>(y, x) = {x / 6959.f, y / 97.f, .37f, 1.f};
        ImageImport::Result restored;
        restored.image = IprTestImageGenerator::proxy(master);
        if (restored.image.linearRgba.cols != 2048 || !ImageImport::complete(restored)) {
            std::cerr << "Edit proxy fails master validation: " << restored.error.toStdString() << '\n';
            return 1;
        }
    }
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const auto a = dir.filePath("A.cachemock"), b = dir.filePath("B.cachemock"), c = dir.filePath("C.cachemock");
    if (!write(a) || !write(b) || !write(c)) return 1;
    std::atomic_int aCalls{0}, bCalls{0}, cCalls{0}, canceled{0};
    std::atomic<quint64> available{24ULL << 30};
    RawDecoder::FormatOptions format;
    format.decoderId = "cache-test"; format.decoderVersion = "1";
    format.dimensions = [](const QString &) { return QSize(120, 80); };
    QString error;
    if (!RawDecoder::registerFormatHandler({"cachemock"},
        [&](const QString &file, const RawDecoder::Options &options, ImageMetadata *, QString *failure) {
            if (file == a) ++aCalls;
            if (file == b) ++bCalls;
            if (file == c) ++cCalls;
            for (int i = 0; i < 40; ++i) {
                if (options.cancellationRequested && options.cancellationRequested()) {
                    ++canceled; if (failure) *failure = "Canceled fixture";
                    return ColorPipeline::WorkingImage{};
                }
                QThread::msleep(5);
            }
            ColorPipeline::WorkingImage image;
            image.linearRgba = cv::Mat(80, 120, CV_32FC4, cv::Scalar(.2f, .3f, .4f, 1.f));
            return image;
        }, &error, format)) return 2;
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, [&] {
        return SystemMemory::Snapshot{32ULL << 30, available.load(), 0, true, false};
    });
    const auto ready = [&] { return editor.hasImage() && !editor.busy() && !editor.imageSource().isEmpty(); };
    editor.openImage(QUrl::fromLocalFile(a));
    if (!waitFor(ready)) return 3;
    // Select B during its speculative decode: promote the same work, never decode twice.
    if (!waitFor([&] { return bCalls.load() == 1; })) return 4;
    editor.loadImage(QUrl::fromLocalFile(b));
    if (!waitFor(ready) || bCalls != 1) return 4;
    // Changing image resets edits, and returning restores that image's .ipr.
    editor.setBrightness(.5f);
    if (!waitFor(ready)) return 5;
    editor.loadImage(QUrl::fromLocalFile(a));
    if (!waitFor(ready) || editor.adjustmentValue("brightness") != 0) return 5;
    editor.loadImage(QUrl::fromLocalFile(b));
    if (!waitFor(ready) || editor.adjustmentValue("brightness") != .5) return 5;
    // C may be prefetched but must have no sidecar until opened by the user.
    if (!waitFor([&] { return editor.decodedImageCacheEntryCount() >= 2; })) return 6;
    if (QFileInfo::exists(c + ".ipr")) return 6;
    const int before = cCalls;
    editor.loadImage(QUrl::fromLocalFile(c));
    if (!waitFor(ready) || cCalls != before) return 6;
    // Generation cancellation and latest-request-wins, no stale publication.
    editor.setDecodedImageCacheBudgetBytes(0);
    if (!waitFor([&] { return editor.decodedImageCacheBudgetBytes() == 0; })) return 7;
    const int beforeRapid = aCalls.load();
    editor.loadImage(QUrl::fromLocalFile(a));
    if (!waitFor([&] { return aCalls.load() > beforeRapid; })) return 7;
    editor.loadImage(QUrl::fromLocalFile(b));
    editor.loadImage(QUrl::fromLocalFile(c));
    if (!waitFor(ready) || !editor.imageInfo().contains("C.cachemock") || !canceled.load()) return 7;
    available.store(5ULL << 30);
    if (!waitFor([&] { return editor.memoryCacheStatus().value("pressure").toBool(); })) return 8;
    if (editor.decodedImageCacheEntryCount() || !editor.hasImage() || provider.image().isNull()) return 8;
    QFile priorRecipe(a + ".ipr");
    if (!priorRecipe.open(QIODevice::ReadOnly)) return 8;
    const auto priorBytes = priorRecipe.readAll(); priorRecipe.close();
    const auto displayed = provider.image();
    available.store(512ULL << 20); // Not even the pressure path fits: work + safety > RAM.
    editor.loadImage(QUrl::fromLocalFile(a)); // Waits for memory; C must remain displayed.
    if (!waitFor([&] { return editor.statusMessage().contains("attente de libération"); }) ||
        !editor.imageInfo().contains("C.cachemock") || provider.image() != displayed) return 9;
    if (!priorRecipe.open(QIODevice::ReadOnly) || priorRecipe.readAll() != priorBytes) return 9;
    priorRecipe.close();
    // Navigating away cancels A's wait; B then waits in its place.
    const int aBeforeWait = aCalls.load(), bBeforeWait = bCalls.load();
    editor.loadImage(QUrl::fromLocalFile(b));
    for (QElapsedTimer pause; !pause.isValid() || pause.elapsed() < 1200; QThread::msleep(2)) {
        if (!pause.isValid()) pause.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (!editor.busy() || !editor.imageInfo().contains("C.cachemock") || aCalls != aBeforeWait) return 10;
    // Still above 80%, but IPR holds nothing to release: B opens under pressure.
    available.store(5ULL << 30);
    if (!waitFor(ready) || !editor.imageInfo().contains("B.cachemock") || aCalls != aBeforeWait ||
        bCalls <= bBeforeWait || !editor.statusMessage().contains("sous pression")) return 10;
    if (!priorRecipe.open(QIODevice::ReadOnly) || priorRecipe.readAll() != priorBytes) return 10;
    if (!waitFor([&] { return editor.memoryCacheStatus().value("reservedBytes").toULongLong() == 0; })) return 10;
    // Large images leave only their edit proxy in the cache; export rebuilds the master.
    QTemporaryDir bigDir;
    const auto x = bigDir.filePath("X.bigmock"), y = bigDir.filePath("Y.bigmock"),
        z = bigDir.filePath("Z.bigmock"), w = bigDir.filePath("W.bigmock");
    if (!write(x) || !write(y) || !write(z) || !write(w)) return 11;
    std::atomic_int xCalls{0};
    RawDecoder::FormatOptions big;
    big.decoderId = "cache-test-big"; big.decoderVersion = "1";
    big.dimensions = [](const QString &) { return QSize(3000, 200); };
    big.thumbnail = [](const QString &, const QSize &, QString *) {
        QImage camera(300, 20, QImage::Format_RGB32);
        camera.fill(Qt::green);
        return camera;
    };
    if (!RawDecoder::registerFormatHandler({"bigmock"},
        [&](const QString &file, const RawDecoder::Options &, ImageMetadata *, QString *) {
            if (file == x) ++xCalls;
            if (file == z || file == w) QThread::msleep(600); // slow development
            if (file == w) return ColorPipeline::WorkingImage{};
            ColorPipeline::WorkingImage image;
            image.linearRgba = cv::Mat(200, 3000, CV_32FC4, cv::Scalar(.2f, .3f, .4f, 1.f));
            return image;
        }, &error, big)) return 11;
    available.store(24ULL << 30); // Pressure from the previous scenario has ended.
    editor.setDecodedImageCacheBudgetBytes(std::numeric_limits<quint64>::max());
    editor.openImage(QUrl::fromLocalFile(x));
    if (!waitFor(ready) || !editor.imageInfo().contains("3000 × 200") || xCalls != 1) return 11;
    editor.loadImage(QUrl::fromLocalFile(y));
    if (!waitFor(ready) || !waitFor([&] { return editor.decodedImageCacheEntryCount() >= 1; })) return 12;
    const quint64 fullBytes = 3000ULL * 200 * 16;
    if (editor.decodedImageCacheSizeBytes() >= fullBytes) return 12; // proxy-only, not the 3000 px master
    editor.loadImage(QUrl::fromLocalFile(x));
    if (!waitFor(ready) || xCalls != 1 || !editor.imageInfo().contains("3000 × 200")) return 13;
    const auto exported = bigDir.filePath("X-export.tif");
    editor.exportImage(QUrl::fromLocalFile(exported), "TIFF", 100);
    if (!waitFor([&] { return !editor.busy() && QFileInfo::exists(exported); }, 15000) || xCalls != 2 ||
        QImageReader(exported).size() != QSize(3000, 200)) return 14;
    // The camera's embedded preview is shown at once, then replaced by IPR's render.
    editor.setDecodedImageCacheBudgetBytes(0); // no cache hit, no prefetch
    if (!waitFor([&] { return editor.decodedImageCacheBudgetBytes() == 0; })) return 15;
    const auto green = [&] { return provider.image().pixelColor(0, 0) == QColor(Qt::green); };
    editor.loadImage(QUrl::fromLocalFile(z));
    if (!waitFor([&] { return editor.embeddedPreview() && green(); }) || !editor.busy()) return 15;
    if (!waitFor(ready) || editor.embeddedPreview() || green() || !editor.imageInfo().contains("Z.bigmock")) return 15;
    // A failed development restores the image that was displayed before.
    const auto beforeFailure = provider.image();
    editor.loadImage(QUrl::fromLocalFile(w));
    if (!waitFor([&] { return editor.embeddedPreview(); })) return 16;
    if (!waitFor([&] { return !editor.busy(); }) || editor.embeddedPreview() ||
        provider.image() != beforeFailure || !editor.imageInfo().contains("Z.bigmock")) return 16;
    std::cout << "Editor: preload promotion, cache reuse, recipe isolation, latest request, pressure wait, cancellation, proxy-only retention and embedded preview passed\n";
    return 0;
}
