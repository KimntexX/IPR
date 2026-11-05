#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "Sidecar.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include <cmath>
#include <QTimer>

#include <functional>
#include <iostream>
#include <limits>

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

bool settled(ImageEditor &editor)
{
    return waitFor([&] { return !editor.busy(); });
}

bool awaitLoad(ImageEditor &editor, const QString &path)
{
    editor.loadImage(QUrl::fromLocalFile(path));
    return waitFor([&] { return editor.hasImage() && !editor.busy() && editor.imageSource().size() > 0; });
}

// A horizontal ramp on red and a vertical one on green, opaque.
QString writeImage(const QString &directory, const QString &name, int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor(40 + 180 * x / width, 40 + 180 * y / height, 120, 255));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

// A horizon leaning by this many degrees, clockwise.
QString writeHorizon(const QString &directory, const QString &name, double degrees)
{
    QImage image(1200, 800, QImage::Format_RGBA8888);
    const double slope = std::tan(degrees * 3.14159265358979323846 / 180.0);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const bool below = y > 400.0 + (x - 600.0) * slope;
            image.setPixelColor(x, y, below ? QColor(40, 60, 90, 255) : QColor(210, 220, 235, 255));
        }
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Awaits the end of the export, whose result the status message tells.
QString exported(ImageEditor &editor, const QString &target, const QString &format)
{
    QString status;
    QEventLoop loop;
    const auto connection = QObject::connect(&editor, &ImageEditor::statusMessageChanged, &loop, [&] {
        const QString message = editor.statusMessage();
        if (message.startsWith(QStringLiteral("Image exportée")) || message.startsWith(QStringLiteral("Échec"))) {
            status = message;
            loop.quit();
        }
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    editor.exportImage(QUrl::fromLocalFile(target), format, 92);
    if (status.isEmpty())
        loop.exec();
    QObject::disconnect(connection);
    return status;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    const QString photo = writeImage(directory.path(), QStringLiteral("photo.png"), 120, 80);
    const QString other = writeImage(directory.path(), QStringLiteral("other.png"), 120, 80);
    if (!check(!photo.isEmpty() && !other.isEmpty(), "inputs written")) return 1;
    const QByteArray photoBytes = bytesOf(photo);

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    if (!check(awaitLoad(editor, photo), "photo loads")) return 1;
    const QImage neutralPreview = provider.image();
    if (!check(editor.geometry().value("identity").toBool() && editor.geometry().value("canvasWidth").toInt() == 120,
               "a photo opens with a neutral geometry")) return 1;
    if (!check(neutralPreview.size() == QSize(120, 80), "the neutral preview has the photo's size")) return 1;

    // A quarter turn: the canvas, the preview and the original turn.
    editor.turnGeometry();
    if (!check(settled(editor) && provider.image().size() == QSize(80, 120), "a turn swaps the preview's sides")) return 1;
    if (!check(provider.originalImage().size() == QSize(80, 120), "the original turns with it, to stay comparable")) return 1;
    if (!check(editor.geometry().value("canvasWidth").toInt() == 80, "the panel reads the turned canvas")) return 1;
    if (!check(editor.historySteps().size() == 2 && editor.canUndo(), "a turn is a history step")) return 1;
    editor.undo();
    if (!check(settled(editor) && provider.image() == neutralPreview, "undoing the turn gives the same preview back")) return 1;
    if (!check(editor.geometry().value("identity").toBool(), "and the same geometry")) return 1;

    // Within a gesture the original waits for the release: a drag stays fast.
    editor.beginAdjustmentGesture();
    editor.turnGeometry();
    if (!check(settled(editor) && provider.image().size() == QSize(80, 120) &&
               provider.originalImage().size() == QSize(120, 80), "a gesture does not render the original")) return 1;
    editor.endAdjustmentGesture();
    if (!check(settled(editor) && provider.originalImage().size() == QSize(80, 120), "its release does")) return 1;
    editor.undo();
    if (!check(settled(editor) && provider.originalImage().size() == QSize(120, 80), "and an undo follows")) return 1;

    // A crop: the preview shows it, the edit view shows the whole canvas.
    editor.setGeometryCrop(0.25, 0.25, 0.5, 0.5);
    if (!check(settled(editor) && provider.image().size() == QSize(60, 40), "the preview shows the crop")) return 1;
    if (!check(provider.originalImage().size() == QSize(60, 40), "the original shows the same frame")) return 1;
    const qsizetype stepsBeforeView = editor.historySteps().size();
    editor.setCropEditing(true);
    if (!check(settled(editor) && provider.image().size() == QSize(120, 80), "editing the crop shows the whole canvas")) return 1;
    if (!check(provider.originalImage().size() == QSize(120, 80), "the original too")) return 1;
    if (!check(editor.historySteps().size() == stepsBeforeView, "showing the canvas is no history step")) return 1;
    editor.setCropEditing(false);
    if (!check(settled(editor) && provider.image().size() == QSize(60, 40), "leaving the edit view shows the crop again")) return 1;

    // A rotation leaves empty zones, reported to the panel and transparent.
    editor.resetGeometryCrop();
    editor.setGeometryValue(QStringLiteral("rotation"), 15);
    if (!check(settled(editor) && editor.geometry().value("emptyZones").toBool(), "a rotation reports empty zones")) return 1;
    const QImage rotated = provider.image();
    if (!check(rotated.size() == QSize(120, 80) && qAlpha(rotated.pixel(0, 0)) == 0, "they are transparent")) return 1;
    if (!check(qAlpha(rotated.pixel(60, 40)) == 255, "the image stays opaque")) return 1;
    if (!check(editor.histogram().size() == 4, "the histogram is computed")) return 1;

    // The constraint removes them by cropping, not by filling.
    editor.setGeometryConstrain(true);
    if (!check(settled(editor) && !editor.geometry().value("emptyZones").toBool() &&
               editor.geometry().value("hasCrop").toBool(), "the constraint brings the crop on the image")) return 1;
    const QImage constrained = provider.image();
    if (!check(constrained.width() < 120 && qAlpha(constrained.pixel(0, 0)) == 255, "no empty corner remains")) return 1;
    editor.setGeometryConstrain(false);
    editor.resetGeometryCrop();
    if (!check(settled(editor) && editor.geometry().value("emptyZones").toBool(), "without it the empty zones return")) return 1;

    // Export: the formats with transparency keep it, the others say what they do.
    const QString png = QDir(directory.path()).filePath(QStringLiteral("out.png"));
    QString status = exported(editor, png, QStringLiteral("PNG"));
    if (!check(status.startsWith(QStringLiteral("Image exportée")) && !status.contains(QStringLiteral("zones vides")),
               "a PNG exports silently")) return 1;
    const QImage pngImage(png);
    if (!check(pngImage.size() == QSize(120, 80) && qAlpha(pngImage.pixel(0, 0)) == 0, "with its transparency")) return 1;
    const QString jpeg = QDir(directory.path()).filePath(QStringLiteral("out.jpg"));
    status = exported(editor, jpeg, QStringLiteral("JPEG"));
    if (!check(status.contains(QStringLiteral("zones vides")), "a JPEG tells its empty zones are black")) return 1;
    const QImage jpegImage(jpeg);
    if (!check(!jpegImage.isNull() && qRed(jpegImage.pixel(1, 1)) < 16 && qGreen(jpegImage.pixel(1, 1)) < 16,
               "and they are black, not repeated pixels")) return 1;

    editor.setGeometryCrop(0.5, 0.5, 0.5, 0.5);
    const QString tiff = QDir(directory.path()).filePath(QStringLiteral("crop.tif"));
    status = exported(editor, tiff, QStringLiteral("TIFF"));
    QImageReader reader(tiff);
    if (!check(reader.size() == QSize(60, 40), "an export is cut to the crop")) return 1;

    // Reopening brings the geometry back.
    if (!check(settled(editor), "the preview settles")) return 1;
    editor.saveCurrentAdjustments();
    const auto saved = Sidecar::read(photo);
    if (!check(saved && saved->settings.geometry.rotation == 15.f && saved->settings.geometry.cropWidth == 0.5,
               "the geometry is saved in the sidecar")) return 1;
    if (!check(awaitLoad(editor, other) && editor.geometry().value("identity").toBool(),
               "another photo has its own geometry")) return 1;
    editor.setContrast(30);
    if (!check(settled(editor), "the other photo renders")) return 1;
    editor.copyAdjustments();
    if (!check(awaitLoad(editor, photo) && editor.geometry().value("rotation").toDouble() == 15,
               "reopening a photo restores its geometry")) return 1;
    if (!check(provider.image().size() == QSize(60, 40), "and its crop")) return 1;

    // Pasting edits leaves the framing of the target.
    editor.pasteAdjustments();
    if (!check(settled(editor) && editor.geometry().value("rotation").toDouble() == 15 &&
               editor.adjustmentValue(QStringLiteral("contrast")) == 30, "a paste keeps the target's framing")) return 1;

    // Removing a step of the history replays around the geometry.
    editor.setGeometryValue(QStringLiteral("rotation"), 0);
    if (!check(settled(editor), "the rotation is cleared")) return 1;
    editor.resetGeometry();
    if (!check(settled(editor) && editor.geometry().value("identity").toBool(), "the panel's reset clears everything")) return 1;
    if (!check(provider.image().size() == QSize(120, 80), "and the preview follows")) return 1;

    // Invalid values change nothing.
    editor.setGeometryValue(QStringLiteral("rotation"), std::numeric_limits<double>::quiet_NaN());
    editor.setGeometryValue(QStringLiteral("nothing"), 5);
    editor.setGeometryCrop(std::numeric_limits<double>::quiet_NaN(), 0, 1, 1);
    if (!check(editor.geometry().value("identity").toBool(), "invalid commands change nothing")) return 1;
    editor.setGeometryValue(QStringLiteral("scale"), 1000);
    if (!check(editor.geometry().value("scale").toInt() == 150, "values are bounded")) return 1;

    // Straightening proposes from the photo's lines, and says why when it does not.
    editor.setGeometryValue(QStringLiteral("scale"), 100);
    if (!check(settled(editor), "the scale is back")) return 1;
    const QVariantMap before = editor.geometry();
    if (!check(!editor.proposeStraightening(QStringLiteral("level")), "a ramp has no line: refused")) return 1;
    if (!check(editor.geometry() == before, "a refusal changes nothing")) return 1;
    if (!check(editor.statusMessage().contains(QStringLiteral("aucune proposition")), "and it says so")) return 1;
    if (!check(!editor.proposeStraightening(QStringLiteral("nothing")), "an unknown mode is refused")) return 1;
    if (!check(editor.geometry() == before, "an unknown mode does nothing")) return 1;
    {
        ImageProvider emptyProvider;
        ImageEditor empty(&emptyProvider, nullptr, nullptr, controlledMemory);
        if (!check(!empty.proposeStraightening(QStringLiteral("level")), "without a photo: refused")) return 1;
        if (!check(empty.statusMessage().contains(QStringLiteral("aucune proposition")), "and it says so")) return 1;
    }

    const QString horizon = writeHorizon(directory.path(), QStringLiteral("horizon.png"), 3.0);
    const QByteArray horizonBytes = bytesOf(horizon);
    if (!check(!horizon.isEmpty() && awaitLoad(editor, horizon), "the horizon loads")) return 1;
    editor.setGeometryValue(QStringLiteral("scale"), 110);
    if (!check(editor.proposeStraightening(QStringLiteral("level")), "a horizon gets a proposal")) return 1;
    if (!check(settled(editor), "the proposal renders")) return 1;
    if (!check(std::abs(editor.geometry().value("rotation").toDouble() + 3.0) < 0.1, "the proposal levels it")) return 1;
    if (!check(editor.geometry().value("scale").toInt() == 110, "the rest of the geometry stays")) return 1;
    if (!check(editor.statusMessage().contains(QStringLiteral("Redressement proposé")), "and it says what happened")) return 1;
    if (!check(bytesOf(horizon) == horizonBytes, "the horizon is never written")) return 1;
    for (const char *mode : {"vertical", "full"}) {
        editor.resetGeometry();
        if (!check(settled(editor), "reset before a mode")) return 1;
        if (!check(editor.proposeStraightening(QString::fromLatin1(mode)), mode)) return 1;
        if (!check(settled(editor) && std::abs(editor.geometry().value("rotation").toDouble() + 3.0) < 0.1, mode)) return 1;
    }
    editor.resetGeometry();
    if (!check(settled(editor), "reset")) return 1;

    if (!check(bytesOf(photo) == photoBytes, "the photo is never written")) return 1;
    std::cout << "editor geometry: ok\n";
    return 0;
}
