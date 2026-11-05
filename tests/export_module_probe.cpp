// What a module costs in a real export and on the preview, and what it writes: exports each photo
// as a 16-bit TIFF, neutral then with the adjustments given, three times each, through the real
// editor, and times the preview's answer to the same change. Each photo is copied first into the
// folder of the run, without its .ipr: the editor writes one beside the photo it opens, and a run
// must not start from what an earlier one left. A name starting with "geometry." sets that geometry
// value, "geometry.turn" turns by that many quarters and "geometry.mirror" mirrors left to right
// (above zero) or top to bottom; going back to neutral resets the geometry.
// usage: iprExportModuleProbe <revision> <new json> <folder for the exports> <name=value,...> <photo>...

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
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <iostream>
#include <utility>
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

using Adjustments = std::vector<std::pair<QString, double>>;

Adjustments adjustmentsFrom(const QString &text)
{
    Adjustments result;
    for (const QString &item : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = item.split(QLatin1Char('='));
        bool valid = parts.size() == 2;
        const double value = valid ? parts[1].toDouble(&valid) : 0.0;
        if (!valid)
            return {};
        result.emplace_back(parts[0], value);
    }
    return result;
}

void apply(ImageEditor &editor, const Adjustments &adjustments, bool on)
{
    const QString geometry = QStringLiteral("geometry.");
    bool geometryReset = false;
    for (const auto &[name, value] : adjustments) {
        if (!name.startsWith(geometry))
            editor.setAdjustment(name, on ? value : 0.0);
        else if (on && name == geometry + QStringLiteral("turn"))
            for (int turn = 0; turn < int(value); ++turn)
                editor.turnGeometry();
        else if (on && name == geometry + QStringLiteral("mirror"))
            editor.mirrorGeometry(value > 0);
        else if (on)
            editor.setGeometryValue(name.mid(geometry.size()), value);
        else if (!geometryReset) {
            editor.resetGeometry();
            geometryReset = true;
        }
    }
}

// Milliseconds from the change to the new preview, three times; empty when one does not come.
std::vector<double> previewAnswers(ImageEditor &editor, const Adjustments &adjustments)
{
    std::vector<double> times;
    int previews = 0;
    const QMetaObject::Connection counting =
        QObject::connect(&editor, &ImageEditor::previewChanged, &editor, [&] { ++previews; });
    for (int i = 0; i < 3; ++i) {
        apply(editor, adjustments, false);
        waitFor([&] { return !editor.busy(); });
        QEventLoop settle;
        QTimer::singleShot(300, &settle, &QEventLoop::quit);
        settle.exec();
        const int before = previews;
        QElapsedTimer clock;
        clock.start();
        apply(editor, adjustments, true);
        if (!waitFor([&] { return previews > before && !editor.busy(); }, 60000))
            break;
        times.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    QObject::disconnect(counting);
    return times.size() == 3 ? times : std::vector<double>{};
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    const Adjustments adjustments = argc >= 6 ? adjustmentsFrom(QString::fromLocal8Bit(argv[4])) : Adjustments{};
    if (argc < 6 || adjustments.empty() || QFile::exists(QString::fromLocal8Bit(argv[2]))) {
        std::cerr << "usage: iprExportModuleProbe <revision> <new json> <folder> <name=value,...> <photo>...\n";
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
    const QDir sources(folder.filePath(QStringLiteral("sources")));
    if (!QDir().mkpath(sources.path())) {
        std::cerr << "Cannot create the folder of the copies\n";
        return 2;
    }
    for (int a = 5; a < argc; ++a) {
        const QFileInfo original(QString::fromLocal8Bit(argv[a]));
        const QString copy = sources.filePath(original.fileName());
        if (QFile::exists(copy) || !QFile::copy(original.absoluteFilePath(), copy)) {
            std::cerr << "Cannot make a fresh copy of " << argv[a] << '\n';
            return 2;
        }
        const QFileInfo photo(copy);
        editor.loadImage(QUrl::fromLocalFile(photo.canonicalFilePath()));
        if (!waitFor([&] { return editor.hasImage() && !editor.busy() && !editor.imageSource().isEmpty(); })) {
            std::cerr << "Cannot open " << argv[a] << '\n';
            return 1;
        }
        apply(editor, adjustments, false);
        waitFor([&] { return !editor.busy(); });
        const QString neutral = folder.filePath(photo.completeBaseName() + QStringLiteral("-neutral.tif"));
        const std::vector<double> neutralTimes = exports(editor, neutral);
        const std::vector<double> previewTimes = previewAnswers(editor, adjustments);
        apply(editor, adjustments, true);
        waitFor([&] { return !editor.busy(); });
        const QString effect = folder.filePath(photo.completeBaseName() + QStringLiteral("-effect.tif"));
        const std::vector<double> effectTimes = exports(editor, effect);
        if (neutralTimes.empty() || effectTimes.empty() || previewTimes.empty()) {
            std::cerr << "An export failed: " << editor.statusMessage().toStdString() << '\n';
            return 1;
        }
        apply(editor, adjustments, false);
        waitFor([&] { return !editor.busy(); });
        const double withEffect = medianOf(effectTimes), without = medianOf(neutralTimes);
        std::cout << photo.fileName().toStdString() << ": neutral " << without << " ms, with the adjustments "
                  << withEffect << " ms, preview " << medianOf(previewTimes) << " ms\n";
        photos.append(QJsonObject{{"photo", photo.fileName()},
                                  {"neutral_ms", without},
                                  {"effect_ms", withEffect},
                                  {"preview_ms", medianOf(previewTimes)},
                                  {"effect_sha256", sha256Of(effect)},
                                  {"neutral_sha256", sha256Of(neutral)}});
    }
    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly)) {
        std::cerr << "Cannot write the proof\n";
        return 1;
    }
    out.write(QJsonDocument(QJsonObject{{"revision", QString::fromLocal8Bit(argv[1])},
                                        {"settings", QString::fromLocal8Bit(argv[4])},
                                        {"timing", "median of three exports, on a machine busy with other work"},
                                        {"photos", photos}}).toJson());
    return 0;
}
