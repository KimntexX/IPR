// What a module costs in a real export, and what it writes: exports each photo as a 16-bit TIFF,
// neutral then with texture and clarity, three times each, through the real editor. Reads private
// copies only, since the editor writes its .ipr beside a photo.
// usage: iprExportModuleProbe <revision> <new json> <folder for the exports> <photo>...

#include "controlled_memory.h"
#include "imageeditor.h"
#include "imageprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

namespace {

bool waitFor(const std::function<bool()> &done, int milliseconds = 600000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(5);
    if (!done()) loop.exec();
    return done();
}

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

double medianOf(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// Milliseconds of each export; empty when one fails. The last file stays at target.
std::vector<double> exports(ImageEditor &editor, const QString &target)
{
    std::vector<double> times;
    for (int i = 0; i < 3; ++i) {
        QFile::remove(target);
        QElapsedTimer clock;
        clock.start();
        editor.exportImage(QUrl::fromLocalFile(target), QStringLiteral("TIFF"), 100);
        if (!waitFor([&] { return !editor.busy(); }) || !QFileInfo::exists(target))
            return {};
        times.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    return times;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    if (argc < 5 || QFile::exists(QString::fromLocal8Bit(argv[2]))) {
        std::cerr << "usage: iprExportModuleProbe <revision> <new json> <folder> <photo>...\n";
        return 2;
    }
    const QDir folder(QString::fromLocal8Bit(argv[3]));
    if (!QDir().mkpath(folder.path())) {
        std::cerr << "Cannot create the folder\n";
        return 2;
    }
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QJsonArray photos;
    for (int a = 4; a < argc; ++a) {
        const QFileInfo photo(QString::fromLocal8Bit(argv[a]));
        editor.loadImage(QUrl::fromLocalFile(photo.canonicalFilePath()));
        if (!waitFor([&] { return editor.hasImage() && !editor.busy() && !editor.imageSource().isEmpty(); })) {
            std::cerr << "Cannot open " << argv[a] << '\n';
            return 1;
        }
        editor.setTexture(0);
        editor.setClarity(0);
        waitFor([&] { return !editor.busy(); });
        const QString neutral = folder.filePath(photo.completeBaseName() + QStringLiteral("-neutral.tif"));
        const std::vector<double> neutralTimes = exports(editor, neutral);
        editor.setTexture(50);
        editor.setClarity(30);
        waitFor([&] { return !editor.busy(); });
        const QString effect = folder.filePath(photo.completeBaseName() + QStringLiteral("-effect.tif"));
        const std::vector<double> effectTimes = exports(editor, effect);
        if (neutralTimes.empty() || effectTimes.empty()) {
            std::cerr << "An export failed: " << editor.statusMessage().toStdString() << '\n';
            return 1;
        }
        editor.setTexture(0);
        editor.setClarity(0);
        waitFor([&] { return !editor.busy(); });
        const double withEffect = medianOf(effectTimes), without = medianOf(neutralTimes);
        std::cout << photo.fileName().toStdString() << ": neutral " << without << " ms, with texture and clarity "
                  << withEffect << " ms\n";
        photos.append(QJsonObject{{"photo", photo.fileName()},
                                  {"neutral_ms", without},
                                  {"effect_ms", withEffect},
                                  {"effect_sha256", sha256Of(effect)},
                                  {"neutral_sha256", sha256Of(neutral)}});
    }
    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly)) {
        std::cerr << "Cannot write the proof\n";
        return 1;
    }
    out.write(QJsonDocument(QJsonObject{{"revision", QString::fromLocal8Bit(argv[1])},
                                        {"settings", "texture 50, clarity 30, against neutral"},
                                        {"timing", "median of three exports, on a machine busy with other work"},
                                        {"photos", photos}}).toJson());
    return 0;
}
