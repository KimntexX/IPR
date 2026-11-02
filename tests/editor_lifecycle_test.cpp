#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "Sidecar.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
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

bool waitFor(const std::function<bool()> &done, int milliseconds = 8000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(5);
    if (!done()) loop.exec();
    return done();
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    {
        ImageProvider provider;
        // No worker has started. Destruction must not wait on an empty future.
        ImageEditor editor(&provider);
    }

    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    // Large enough for a render to be running when the editor closes.
    QImage image(1600, 1200, QImage::Format_RGBA8888);
    image.fill(QColor(90, 120, 150));
    const QString photo = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    if (!check(image.save(photo, "PNG"), "input written")) return 1;
    const QByteArray original = bytesOf(photo);
    const QUrl url = QUrl::fromLocalFile(photo);

    QElapsedTimer timer;
    timer.start();
    {
        // Closed while the photo opens: nothing was edited, so nothing is saved.
        ImageProvider provider;
        ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
        editor.loadImage(url);
    }
    if (!check(!Sidecar::read(photo), "closing during the opening writes no recipe")) return 1;

    {
        // Closed while an edit renders: the edit is kept, not lost with the render.
        ImageProvider provider;
        ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
        editor.loadImage(url);
        if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }), "photo loads")) return 1;
        editor.setClarity(40);
        waitFor([&] { return editor.busy(); }, 1000);
    }
    const auto closedDuringRender = Sidecar::read(photo);
    if (!check(closedDuringRender && closedDuringRender->settings.clarity == 40,
               "closing during a render saves the last edit")) return 1;

    const QString exported = QDir(directory.path()).filePath(QStringLiteral("exported.tif"));
    {
        // Closed during an export: it ends, or it leaves no partial file.
        ImageProvider provider;
        ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
        editor.loadImage(url);
        if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }), "photo reloads")) return 1;
        editor.exportImage(QUrl::fromLocalFile(exported), QStringLiteral("TIFF"), 92);
    }
    if (!check(!QFile::exists(exported) || !QImage(exported).isNull(),
               "closing during an export leaves no partial file")) return 1;

    if (!check(timer.elapsed() < 8000, "closing during a task is bounded")) return 1;
    if (!check(bytesOf(photo) == original, "the photo itself is never written")) return 1;
    std::cout << "editor lifecycle: ok\n";
    return 0;
}
