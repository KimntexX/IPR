#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "RenderChain.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>

// ImageEditor's friend for its test tools: what the editor renders from.
class IprTestImageGenerator
{
public:
    static QImage wholeRender(const ImageEditor &editor)
    {
        return RenderChain::preview(editor.m_original, editor.m_settings, false, 1.0).preview;
    }
};

// The full-resolution view of the zoom: asked for, rendered once the settings rest, the
// file's pixels, hidden at once when the settings change,
// freed when no longer wanted or when the photo changes. A recipe that renders by region
// comes as tiles of the visible part and its ring, each equal to that part of the whole render.
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

// Each tile shown is that part of the whole full-resolution render, pixel for pixel.
bool tilesMatch(const ImageEditor &editor, const ImageProvider &provider)
{
    const QImage whole = IprTestImageGenerator::wholeRender(editor);
    if (whole.isNull() || editor.detailTiles().isEmpty())
        return false;
    for (const QVariant &value : editor.detailTiles()) {
        const QVariantMap tile = value.toMap();
        const QString name = tile.value(QStringLiteral("source")).toString().section(QLatin1Char('/'), -1)
                                 .section(QLatin1Char('?'), 0, 0);
        const QRect rect(tile.value(QStringLiteral("x")).toInt(), tile.value(QStringLiteral("y")).toInt(),
                         tile.value(QStringLiteral("width")).toInt(), tile.value(QStringLiteral("height")).toInt());
        if (provider.detailTile(name) != whole.copy(rect))
            return false;
    }
    return true;
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

    // Tones only: the visible part comes as tiles, no whole image.
    if (!check(editor.detailSize() == QSize(3000, 2000), "the displayed image at full resolution")) return 1;
    editor.setAdjustment(QStringLiteral("contrast"), 25);
    if (!check(waitFor([&] { return settled(editor); }), "the contrast is rendered")) return 1;
    editor.setDetailWanted(true);
    editor.setDetailView(0, 0, 3000, 2000, 1500, 1000);
    if (!check(waitFor([&] { return editor.detailTiles().size() == 24 && settled(editor); }),
               "the 24 tiles of the view come")) return 1;
    if (!check(editor.detailImageSource().isEmpty() && provider.detailImage().isNull(), "no whole image with tiles"))
        return 1;
    if (!check(tilesMatch(editor, provider), "each tile is that part of the whole render")) return 1;

    // A move to the top left corner: the tiles beyond the view and its 512-pixel ring are freed.
    editor.setDetailView(0, 0, 600, 400, 300, 200);
    if (!check(editor.detailTiles().size() == 6 && provider.detailTileCount() == 6, "far tiles freed")) return 1;

    // Changed settings: every tile hides at the next preview, then those of the new settings come.
    const QImage firstTile = provider.detailTile(QStringLiteral("tile-0-0"));
    const QString previewOfTiles = editor.imageSource();
    editor.setAdjustment(QStringLiteral("contrast"), 60);
    if (!check(waitFor([&] { return editor.imageSource() != previewOfTiles; }), "the new preview is published")) return 1;
    if (!check(editor.detailTiles().isEmpty() || provider.detailTile(QStringLiteral("tile-0-0")) != firstTile,
               "a tile of other settings is never shown")) return 1;
    if (!check(waitFor([&] { return editor.detailTiles().size() == 6 && settled(editor); }),
               "the tiles of the new settings come")) return 1;
    if (!check(tilesMatch(editor, provider) && provider.detailTile(QStringLiteral("tile-0-0")) != firstTile,
               "the new tiles show the new settings")) return 1;

    // The comparison: the original's tiles come after the current ones, the photo's own pixels at
    // full resolution, and stay through a slider change (they do not depend on the settings).
    editor.setDetailOriginalWanted(true);
    if (!check(waitFor([&] { return editor.originalDetailTiles().size() == 6 && settled(editor); }),
               "the original's tiles come")) return 1;
    const QImage originalTile = provider.detailTile(QStringLiteral("otile-0-0"));
    const QImage photo = QImage(large).convertToFormat(QImage::Format_RGBA8888).copy(0, 0, 512, 512);
    bool samePixels = originalTile.size() == photo.size();
    for (int y = 0; samePixels && y < photo.height(); y += 7)
        for (int x = 0; samePixels && x < photo.width(); x += 7) {
            const QColor a = originalTile.pixelColor(x, y), b = photo.pixelColor(x, y);
            samePixels = std::abs(a.red() - b.red()) <= 1 && std::abs(a.green() - b.green()) <= 1 &&
                         std::abs(a.blue() - b.blue()) <= 1;
        }
    if (!check(samePixels, "an original tile shows the photo's own pixels")) return 1;
    const QString previewOfComparison = editor.imageSource();
    editor.setAdjustment(QStringLiteral("contrast"), 30);
    if (!check(waitFor([&] { return editor.imageSource() != previewOfComparison; }), "the preview is published")) return 1;
    if (!check(waitFor([&] { return editor.detailTiles().size() == 6 && settled(editor); }),
               "the current tiles come again")) return 1;
    if (!check(editor.originalDetailTiles().size() == 6 && provider.detailTile(QStringLiteral("otile-0-0")) == originalTile,
               "a slider leaves the original's tiles")) return 1;
    editor.setDetailOriginalWanted(false);
    if (!check(editor.originalDetailTiles().isEmpty() &&
               provider.detailTileCount() == int(editor.detailTiles().size()), "the comparison closed frees them")) return 1;

    // The camera-signal noise reduction measures its model on the whole source and renders whole:
    // no tile.
    const QString previewBefore = editor.imageSource();
    editor.setDenoiseBeforeProfile(true);
    editor.setAdjustment(QStringLiteral("denoiseLuminance"), 30);
    if (!check(waitFor([&] { return editor.imageSource() != previewBefore; }), "the noise reduction preview is published")) return 1;
    if (!check(waitFor([&] { return !editor.detailImageSource().isEmpty() && settled(editor); }),
               "the whole detail is rendered")) return 1;
    if (!check(editor.detailTiles().isEmpty() && provider.detailTileCount() == 0, "no tile with the whole image")) return 1;
    const QImage detail = provider.detailImage();
    if (!check(detail.size() == QSize(3000, 2000), "the detail has the file's pixels")) return 1;
    if (!check(provider.image().width() < detail.width(), "the preview stays reduced")) return 1;

    // Changed settings: the former detail is hidden at the next preview, then rendered again.
    const QString first = editor.detailImageSource();
    const QString previewOfDetail = editor.imageSource();
    editor.setAdjustment(QStringLiteral("contrast"), -40);   // the noise reduction stays: still whole
    if (!check(waitFor([&] { return editor.imageSource() != previewOfDetail; }), "the new preview is published")) return 1;
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
    if (!check(editor.detailImageSource().isEmpty() && provider.detailImage().isNull() &&
               provider.detailTileCount() == 0, "freed when not wanted"))
        return 1;

    // A photo smaller than the preview has nothing finer to show.
    editor.setDetailWanted(true);
    if (!check(awaitLoad(editor, small), "small photo loads")) return 1;
    if (!check(editor.detailRatio() == 1.0, "a small photo is its own preview")) return 1;
    if (!check(waitFor([&] { return settled(editor); }) && editor.detailImageSource().isEmpty() &&
               provider.detailImage().isNull() && editor.detailTiles().isEmpty(),
               "no detail for a small photo, the former one freed")) return 1;

    std::cout << "Full-resolution detail: tiles or whole image on demand, never stale, freed when not wanted\n";
    return 0;
}
