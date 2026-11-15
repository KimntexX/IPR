#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

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

bool waitFor(const std::function<bool()> &done, int milliseconds = 10000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(10);
    if (!done()) loop.exec();
    return done();
}

QString writeImage(const QString &directory, const QString &name)
{
    QImage image(16, 12, QImage::Format_RGBA8888);
    image.fill(QColor(90, 120, 150));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? path : QString();
}

QStringList names(QAbstractItemModel *model)
{
    QStringList result;
    for (int row = 0; row < model->rowCount(); ++row)
        result.append(model->data(model->index(row, 0), ImageListModel::FileNameRole).toString());
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    const QString first = writeImage(directory.path(), QStringLiteral("a.png"));
    const QString second = writeImage(directory.path(), QStringLiteral("c.png"));
    if (!check(!first.isEmpty() && !second.isEmpty(), "inputs written")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QAbstractItemModel *model = editor.imageModel();
    if (!check(model, "the editor exposes its image list")) return 1;
    editor.openFolder(QUrl::fromLocalFile(directory.path()));
    if (!check(names(model) == QStringList({"a.png", "c.png"}), "the folder is listed when opened")) return 1;

    // Rows are inserted and removed in place, never by a reset that would
    // lose the filmstrip's position.
    int resets = 0;
    QObject::connect(model, &QAbstractItemModel::modelReset, [&] { ++resets; });

    if (!check(!writeImage(directory.path(), QStringLiteral("b.png")).isEmpty(), "new image written")) return 1;
    if (!check(waitFor([&] { return names(model) == QStringList({"a.png", "b.png", "c.png"}); }),
               "an image added outside IPR appears in its sorted place")) return 1;

    // Files that are not images, such as sidecars, change nothing.
    QFile sidecar(first + QStringLiteral(".ipr"));
    if (!check(sidecar.open(QIODevice::WriteOnly) && sidecar.write("x") == 1, "sidecar written")) return 1;
    sidecar.close();
    waitFor([] { return false; }, 600);
    if (!check(names(model) == QStringList({"a.png", "b.png", "c.png"}), "a sidecar is not an image")) return 1;

    editor.setImageSelected(QUrl::fromLocalFile(second), true);
    if (!check(editor.selectedImageCount() == 1, "an image is selected")) return 1;
    if (!check(QFile::remove(second), "selected image removed")) return 1;
    if (!check(waitFor([&] { return names(model) == QStringList({"a.png", "b.png"}); }),
               "an image removed outside IPR leaves the filmstrip")) return 1;
    if (!check(editor.selectedImageCount() == 0, "a removed image leaves the selection")) return 1;

    if (!check(QFile::rename(QDir(directory.path()).filePath(QStringLiteral("b.png")),
                             QDir(directory.path()).filePath(QStringLiteral("d.png"))), "image renamed")) return 1;
    if (!check(waitFor([&] { return names(model) == QStringList({"a.png", "d.png"}); }),
               "a renamed image keeps one entry, under its new name")) return 1;

    // The open image vanishing is said, not hidden.
    editor.loadImage(QUrl::fromLocalFile(first));
    if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }, 60000), "image opened")) return 1;
    QFile::remove(first + QStringLiteral(".ipr"));
    if (!check(QFile::remove(first), "open image removed")) return 1;
    if (!check(waitFor([&] { return names(model) == QStringList({"d.png"}); }) &&
               editor.statusMessage().contains(QStringLiteral("a.png")),
               "the status names the open image removed from the folder")) return 1;
    if (!check(resets == 0, "updates never reset the whole list")) return 1;

    std::cout << "folder refresh: ok\n";
    return 0;
}
