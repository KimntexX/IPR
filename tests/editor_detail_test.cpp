#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>

// The full-resolution view of the zoom: asked for, rendered once the settings rest, the
// file's pixels, hidden at once when the settings change,
// freed when no longer wanted or when the photo changes.
namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
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

bool awaitLoad(ImageEditor &editor, const QString &path)
{
    editor.loadImage(QUrl::fromLocalFile(path));
    return waitFor([&] { return editor.hasImage() && !editor.busy() && editor.imageSource().size() > 0; });
}

// Larger than the 2048-pixel preview, with detail a reduction would lose.
QString writeImage(const QString &directory, const QString &name, int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, ((x / 2 + y / 2) % 2) ? QColor(200, 120, 60) : QColor(40, 90, 160));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

bool settled(const ImageEditor &editor)
{
    return !editor.busy() && !editor.detailRendering();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    const QString large = writeImage(directory.path(), QStringLiteral("large.png"), 3000, 2000);
    const QString small = writeImage(directory.path(), QStringLiteral("small.png"), 600, 400);
    if (!check(!large.isEmpty() && !small.isEmpty(), "inputs written")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    if (!check(awaitLoad(editor, large), "large photo loads")) return 1;
    const double ratio = editor.detailRatio();
    if (!check(ratio > 1.4 && ratio < 1.5, "full-resolution pixels per preview pixel")) return 1;
    if (!check(editor.detailImageSource().isEmpty(), "no detail until it is wanted")) return 1;

    editor.setDetailWanted(true);
    if (!check(waitFor([&] { return !editor.detailImageSource().isEmpty(); }), "the detail is rendered")) return 1;
    const QImage detail = provider.detailImage();
    if (!check(detail.size() == QSize(3000, 2000), "the detail has the file's pixels")) return 1;
    if (!check(provider.image().width() < detail.width(), "the preview stays reduced")) return 1;

    // Changed settings: the former detail is hidden at the next preview, then rendered again.
    const QString first = editor.detailImageSource();
    const QString previewBefore = editor.imageSource();
    editor.setAdjustment(QStringLiteral("contrast"), 40);
    if (!check(waitFor([&] { return editor.imageSource() != previewBefore; }), "the new preview is published")) return 1;
    if (!check(editor.detailImageSource() != first, "a detail of other settings is never shown")) return 1;
    if (!check(waitFor([&] { return !editor.detailImageSource().isEmpty() && settled(editor); }),
               "the detail of the new settings comes")) return 1;
    if (!check(provider.detailImage() != detail, "the new detail shows the new settings")) return 1;

    // The crop tool draws on the preview: the detail hides, and comes back unchanged after.
    const QString rested = editor.detailImageSource();
    editor.setCropEditing(true);
    if (!check(waitFor([&] { return settled(editor); }) && editor.detailImageSource().isEmpty(),
               "the crop tool hides the detail")) return 1;
    editor.setCropEditing(false);
    if (!check(waitFor([&] { return settled(editor) && editor.detailImageSource() == rested; }),
               "the same detail comes back without a new render")) return 1;

    // No longer wanted: freed at once.
    editor.setDetailWanted(false);
    if (!check(editor.detailImageSource().isEmpty() && provider.detailImage().isNull(), "freed when not wanted"))
        return 1;

    // A photo smaller than the preview has nothing finer to show.
    editor.setDetailWanted(true);
    if (!check(awaitLoad(editor, small), "small photo loads")) return 1;
    if (!check(editor.detailRatio() == 1.0, "a small photo is its own preview")) return 1;
    if (!check(waitFor([&] { return settled(editor); }) && editor.detailImageSource().isEmpty() &&
               provider.detailImage().isNull(), "no detail for a small photo, the former one freed")) return 1;

    std::cout << "Full-resolution detail: rendered on demand, never stale, freed when not wanted\n";
    return 0;
}
