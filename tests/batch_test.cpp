#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "ImageImporter.h"
#include "Sidecar.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>

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
    return waitFor([&] { return editor.hasImage() && !editor.busy() &&
                                editor.imageSource().size() > 0; });
}

// A small gradient carrying its as-shot white balance in its text, as a camera
// raster would; the editor takes it as the image's reference.
QString writeImage(const QString &directory, const QString &name, int kelvin)
{
    QImage image(48, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(60 + 3 * x, 80 + 2 * y, 120, 255));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.setText(QStringLiteral("WhiteBalanceTemperature"), QString::number(kelvin));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    const QString source = writeImage(directory.path(), QStringLiteral("source.png"), 4500);
    const QString warm = writeImage(directory.path(), QStringLiteral("warm.png"), 6000);
    const QString annotated = writeImage(directory.path(), QStringLiteral("annotated.png"), 5000);
    const QString locked = writeImage(directory.path(), QStringLiteral("locked.png"), 5000);
    if (!check(!source.isEmpty() && !warm.isEmpty() && !annotated.isEmpty() && !locked.isEmpty(), "inputs written"))
        return 1;
    if (!check(ImageImport::referenceWhiteBalanceKelvin(warm).value_or(0) == 6000.0,
               "the reference white balance is read from the metadata alone")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);

    // A target with its own annotations and automatic white balance.
    if (!check(awaitLoad(editor, annotated), "annotated target loads")) return 1;
    editor.setImageRating(4);
    editor.setImageColorLabel(QStringLiteral("green"));
    editor.saveCurrentAdjustments();
    EditRecipe::Settings annotatedBefore;
    EditRecipe::UserImageMetadata annotatedMetadata;
    if (!check(Sidecar::read(annotated, &annotatedBefore, &annotatedMetadata) && annotatedMetadata.rating == 4,
               "target annotations saved")) return 1;
    // A target whose sidecar cannot be written.
    {
        EditRecipe::Settings neutral;
        neutral.temperature = 5000;
        if (!check(Sidecar::write(locked, neutral, {}, {}), "locked sidecar written")) return 1;
        QFile::setPermissions(locked + QStringLiteral(".ipr"), QFileDevice::ReadOwner);
    }

    // The source: as-shot white balance, some edits.
    if (!check(awaitLoad(editor, source), "source loads")) return 1;
    editor.setContrast(37);
    editor.setColorGrading(3, 40, 20);
    for (const QString &target : {warm, annotated, locked, source})
        editor.setImageSelected(QUrl::fromLocalFile(target), true);
    // Applying waits for the preview of the last edit, as the button does.
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles")) return 1;
    editor.applyAdjustmentsToSelection();
    if (!check(waitFor([&] { return !editor.batchApplying() &&
                                    editor.statusMessage().contains(QStringLiteral("appliqués")); }),
               "the batch application runs in the background and ends")) return 1;
    EditRecipe::Settings warmSaved, annotatedSaved, sourceSaved;
    EditRecipe::UserImageMetadata annotatedAfter, ignored;
    if (!check(Sidecar::read(warm, &warmSaved, &ignored) && warmSaved.contrast == 37 && warmSaved.gradingSaturation[3] == 20,
               "the edits reach a target")) return 1;
    if (!check(warmSaved.temperature == 6000 && warmSaved.tint == 0,
               "an as-shot white balance stays the target's own")) return 1;
    if (!check(Sidecar::read(annotated, &annotatedSaved, &annotatedAfter) && annotatedSaved.contrast == 37 &&
               annotatedAfter.rating == 4 && annotatedAfter.colorLabel == QStringLiteral("green"),
               "a target keeps its annotations")) return 1;
    if (!check(Sidecar::read(source, &sourceSaved, &ignored) && sourceSaved.contrast == 37 && sourceSaved.temperature == 4500,
               "the open image is saved with its own settings")) return 1;
    if (!check(editor.statusMessage().contains(QStringLiteral("locked.png")),
               "a failed target is named in the status")) return 1;
    QFile::setPermissions(locked + QStringLiteral(".ipr"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    // A custom white balance is copied as such.
    editor.setTemperature(5100);
    editor.clearImageSelection();
    editor.setImageSelected(QUrl::fromLocalFile(warm), true);
    waitFor([&] { return !editor.busy(); });
    editor.applyAdjustmentsToSelection();
    if (!check(waitFor([&] { return !editor.batchApplying(); }) && Sidecar::read(warm, &warmSaved, &ignored) &&
               warmSaved.temperature == 5100, "a custom white balance is copied")) return 1;

    // Copy and paste: as shot stays as shot on the target.
    editor.setTemperature(4500);
    waitFor([&] { return !editor.busy(); });
    editor.copyAdjustments();
    if (!check(awaitLoad(editor, annotated), "paste target loads")) return 1;
    editor.pasteAdjustments();
    if (!check(editor.adjustmentValue(QStringLiteral("temperature")) == 5000 &&
               editor.adjustmentValue(QStringLiteral("contrast")) == 37,
               "paste keeps the target's as-shot white balance")) return 1;

    // Batch export: every selected image, with its own recipe, under unique names.
    const QString exports = QDir(directory.path()).filePath(QStringLiteral("exports"));
    QDir().mkpath(exports);
    QFile existing(QDir(exports).filePath(QStringLiteral("warm-IPR.png")));
    if (!check(existing.open(QIODevice::WriteOnly) && existing.write("keep") == 4, "existing export written")) return 1;
    existing.close();
    editor.clearImageSelection();
    for (const QString &target : {warm, annotated, source})
        editor.setImageSelected(QUrl::fromLocalFile(target), true);
    editor.exportSelection(QUrl::fromLocalFile(exports), QStringLiteral("PNG"), 92);
    if (!check(editor.batchExporting() && editor.batchExportTotal() == 3, "the batch export starts")) return 1;
    if (!check(waitFor([&] { return !editor.batchExporting(); }) && editor.batchExportDone() == 3,
               "the batch export ends with every image")) return 1;
    const QImage warmOut(QDir(exports).filePath(QStringLiteral("warm-IPR-2.png")));
    const QImage sourceOut(QDir(exports).filePath(QStringLiteral("source-IPR.png")));
    QFile kept(QDir(exports).filePath(QStringLiteral("warm-IPR.png")));
    if (!check(!warmOut.isNull() && !sourceOut.isNull() && QFile::exists(QDir(exports).filePath(QStringLiteral("annotated-IPR.png"))),
               "each image is exported")) return 1;
    if (!check(kept.open(QIODevice::ReadOnly) && kept.readAll() == "keep", "an existing file is never replaced")) return 1;
    // The source's contrast is in its export: its gradient spans more than the plain image's.
    const auto span = [](const QImage &image) {
        const QColor a = image.pixelColor(2, 2), b = image.pixelColor(image.width() - 3, image.height() - 3);
        return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green());
    };
    const QImage plain(source);
    if (!check(span(sourceOut) > span(plain), "an export carries its image's recipe")) return 1;

    // Cancel: stops between images, reports what was written.
    editor.exportSelection(QUrl::fromLocalFile(exports), QStringLiteral("PNG"), 92);
    editor.cancelBatchExport();
    if (!check(waitFor([&] { return !editor.batchExporting(); }) &&
               editor.statusMessage().contains(QStringLiteral("annulé")), "a cancelled batch export stops")) {
        std::cerr << editor.statusMessage().toStdString() << '\n';
        return 1;
    }

    std::cout << "batch: ok\n";
    return 0;
}
