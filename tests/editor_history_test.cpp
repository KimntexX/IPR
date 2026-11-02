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
#include <QScopeGuard>
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

QString writeImage(const QString &directory, const QString &name)
{
    QImage image(48, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(60 + 3 * x, 80 + 2 * y, 120, 255));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

double value(const ImageEditor &editor, const char *name)
{
    return editor.adjustmentValue(QString::fromLatin1(name));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    const QString photo = writeImage(directory.path(), QStringLiteral("photo.png"));
    const QString other = writeImage(directory.path(), QStringLiteral("other.png"));
    if (!check(!photo.isEmpty() && !other.isEmpty(), "inputs written")) return 1;
    const QByteArray photoBytes = bytesOf(photo);
    const QByteArray otherBytes = bytesOf(other);

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    if (!check(awaitLoad(editor, photo), "photo loads")) return 1;
    if (!check(!editor.canUndo() && !editor.canRedo() && editor.historySteps().size() == 1,
               "an opened photo starts with its opening state")) return 1;

    // A slider drag, framed by the interface, is one step whatever its intermediate values.
    editor.beginAdjustmentGesture();
    for (int contrast = 5; contrast <= 25; contrast += 5)
        editor.setContrast(contrast);
    editor.endAdjustmentGesture();
    if (!check(editor.historySteps().size() == 2 && editor.canUndo(), "a gesture is one step")) return 1;
    // A change outside a gesture (typed value, reset, paste) is a step of its own.
    editor.setSaturation(10);
    if (!check(editor.historySteps().size() == 3 && editor.historyPosition() == 2,
               "a change outside a gesture is a step")) return 1;
    if (!check(editor.historySteps().at(2).toMap().value(QStringLiteral("changes")).toStringList() ==
                   QStringList{QStringLiteral("saturation")},
               "a step names what it changed")) return 1;

    editor.undo();
    if (!check(value(editor, "saturation") == 0 && value(editor, "contrast") == 25 && editor.canRedo(),
               "undo restores the previous step")) return 1;
    editor.undo();
    if (!check(value(editor, "contrast") == 0 && !editor.canUndo(), "undo reaches the opening state")) return 1;
    editor.redo();
    if (!check(value(editor, "contrast") == 25 && editor.historyPosition() == 1, "redo replays a step")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }) && editor.historySteps().size() == 3,
               "restoring a step does not record a new one")) return 1;
    editor.setVibrance(5);
    if (!check(!editor.canRedo() && editor.historySteps().size() == 3, "a new edit drops the redo steps")) return 1;

    // Removing a step takes its edit out of the render.
    editor.setClarity(15);
    editor.setDehaze(25);
    const int stepsWithClarity = int(editor.historySteps().size());
    editor.removeHistoryStep(stepsWithClarity - 2);
    if (!check(value(editor, "clarity") == 0 && value(editor, "dehaze") == 25 &&
               editor.historySteps().size() == stepsWithClarity - 1 && editor.historyPosition() == stepsWithClarity - 2,
               "removing a step takes its edit out and keeps the later ones")) return 1;
    editor.removeHistoryStep(0);
    if (!check(editor.historySteps().size() == stepsWithClarity - 1, "the opening state cannot be removed")) return 1;
    editor.removeHistoryStep(stepsWithClarity - 2);
    if (!check(value(editor, "dehaze") == 0 && editor.historySteps().size() == 3, "the last step can be removed too"))
        return 1;

    // A named snapshot comes back as a new step.
    editor.addSnapshot(QStringLiteral("Vibrant"));
    editor.setVibrance(40);
    editor.restoreSnapshot(0);
    if (!check(editor.snapshots().size() == 1 && value(editor, "vibrance") == 5 &&
               editor.historySteps().size() == 5,
               "restoring a snapshot is a step")) return 1;

    // The history is saved in the .ipr and comes back with the photo.
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles")) return 1;
    editor.saveCurrentAdjustments();
    const auto saved = Sidecar::read(photo);
    if (!check(saved && saved->history.size() == 5 && saved->history.snapshots().size() == 1,
               "the history is saved with the recipe")) return 1;
    if (!check(awaitLoad(editor, other) && editor.historySteps().size() == 1 && editor.snapshots().isEmpty(),
               "another photo has its own history")) return 1;
    if (!check(awaitLoad(editor, photo) && editor.historySteps().size() == 5 && editor.historyPosition() == 4 &&
               editor.snapshots().size() == 1 && value(editor, "vibrance") == 5,
               "reopening a photo restores its history")) return 1;
    editor.undo();
    if (!check(value(editor, "vibrance") == 40, "the restored history can be undone")) return 1;

    // A sidecar that cannot be written: the failure is told, the edits and the file stay.
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles again")) return 1;
    editor.saveCurrentAdjustments();
    const QString sidecar = photo + QStringLiteral(".ipr");
    const QByteArray beforeFailure = bytesOf(sidecar);
    if (!check(!beforeFailure.isEmpty() &&
               QFile::setPermissions(sidecar, QFileDevice::ReadOwner), "sidecar locked")) return 1;
    const auto unlock = qScopeGuard([&] {
        QFile::setPermissions(sidecar, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    });
    editor.setVibrance(60);
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles while locked")) return 1;
    editor.saveCurrentAdjustments();
    if (!check(editor.statusMessage().contains(QStringLiteral("Échec")) && bytesOf(sidecar) == beforeFailure,
               "a failed save is reported and leaves the sidecar untouched")) return 1;
    if (!check(value(editor, "vibrance") == 60 && editor.canUndo(), "a failed save keeps the edit and its history"))
        return 1;
    QFile::setPermissions(sidecar, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    editor.saveCurrentAdjustments();
    const auto recovered = Sidecar::read(photo);
    if (!check(recovered && recovered->settings.vibrance == 60, "the next save succeeds once the disk allows it"))
        return 1;

    // The photos stay as they were, and no temporary file remains beside them.
    if (!check(bytesOf(photo) == photoBytes && bytesOf(other) == otherBytes, "the photos are never written"))
        return 1;
    const QStringList leftovers = QDir(directory.path()).entryList(QDir::Files);
    for (const QString &name : leftovers) {
        if (!check(name.startsWith(QStringLiteral("photo.png")) || name.startsWith(QStringLiteral("other.png")),
                   "no temporary file is left in the folder")) return 1;
        if (!check(!name.contains(QStringLiteral(".ipr.")), "no partial sidecar is left in the folder")) return 1;
    }

    std::cout << "editor history: ok\n";
    return 0;
}
