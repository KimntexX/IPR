// A retouch in the editor: its commands, the history, the recipe and what the preview shows.
#include "imageeditor.h"
#include "RecipeCodec.h"
#include "Sidecar.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cmath>
#include <functional>
#include <iostream>
#include <random>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 60000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(10);
    if (!done())
        loop.exec();
    return done();
}

// A grainy ground with a bright spot in the middle, to remove.
QString writePhoto(const QDir &folder, const QString &name)
{
    QImage image(400, 300, QImage::Format_RGB32);
    std::mt19937 random(11);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int grain = int(random() % 13) - 6;
            image.setPixel(x, y, qRgb(90 + grain + x / 16, 110 + grain + y / 16, 70 + grain));
        }
    }
    for (int y = 140; y < 160; ++y)
        for (int x = 190; x < 210; ++x)
            image.setPixel(x, y, qRgb(250, 250, 250));
    const QString path = folder.filePath(name);
    image.save(path);
    return path;
}

void codec()
{
    EditRecipe::Settings settings;
    QCborMap plain = RecipeCodec::encodeSettings(settings);
    check(!plain.contains(QStringLiteral("retouches")), "a recipe without a retouch has no such section");

    Retouch::Operation operation;
    operation.kind = Retouch::Kind::Heal;
    operation.opacity = 0.75f;
    operation.visible = false;
    operation.source = {0.25f, -0.125f};
    LocalAdjustments::BrushStroke stroke;
    stroke.size = 0.03f;
    stroke.feather = 0.4f;
    stroke.points = {{0.1f, 0.2f}, {0.3f, 0.4f}};
    operation.strokes.push_back(stroke);
    settings.retouches.push_back(operation);
    const QCborMap map = RecipeCodec::encodeSettings(settings);
    EditRecipe::Settings read;
    check(RecipeCodec::decodeSettings(map, read), "a recipe with a retouch reads back");
    check(read.retouches.size() == 1 && read.retouches[0] == operation, "the same retouch");
    EditRecipe::Settings throughBytes;
    check(RecipeCodec::decodeSettings(QCborValue::fromCbor(QCborValue(map).toCbor()).toMap(), throughBytes) && throughBytes.retouches == settings.retouches, "through its bytes too");

    // What this version cannot do is refused: dropping it would bring back what was removed.
    QCborMap unknown = map;
    QCborArray list = unknown.value(QStringLiteral("retouches")).toArray();
    QCborMap entry = list.at(0).toMap();
    entry.insert(QStringLiteral("kind"), QStringLiteral("hologram"));
    list[0] = entry;
    unknown.insert(QStringLiteral("retouches"), list);
    EditRecipe::Settings refused;
    check(!RecipeCodec::decodeSettings(unknown, refused), "an unknown kind of retouch is refused");

    // Older recipes read as having none.
    QCborMap older = map;
    older.remove(QStringLiteral("retouches"));
    EditRecipe::Settings fromOlder;
    fromOlder.retouches.push_back(operation);
    check(RecipeCodec::decodeSettings(older, fromOlder) && fromOlder.retouches.empty(), "an older recipe has no retouch");
}

void commands(const QDir &folder)
{
    const QString photo = writePhoto(folder, QStringLiteral("photo.png"));
    const QString other = writePhoto(folder, QStringLiteral("other.png"));
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    const auto settled = [&] { return waitFor([&] { return !editor.busy(); }); };
    const auto load = [&](const QString &path) {
        editor.loadImage(QUrl::fromLocalFile(path));
        return waitFor([&] { return editor.hasImage() && !editor.busy() && editor.imageSource().size() > 0; });
    };
    const auto spot = [&] { return qRed(provider.image().pixel(provider.image().width() / 2, provider.image().height() / 2)); };

    check(editor.addRetouch(QStringLiteral("remove")) == -1, "no photo, no retouch");
    check(load(photo), "the photo loads");
    check(editor.retouches().isEmpty(), "it starts without a retouch");
    const QImage before = provider.image();
    check(spot() > 200, "the spot shows");

    // Added but not painted, it changes nothing.
    check(editor.addRetouch(QStringLiteral("hologram")) == -1, "an unknown kind is refused");
    const int remove = editor.addRetouch(QStringLiteral("remove"));
    check(remove == 0 && editor.retouches().size() == 1, "a retouch is added");
    check(settled() && provider.image() == before, "without a stroke it changes no pixel");

    // Painted, the spot goes; elsewhere nothing moves.
    const qsizetype steps = editor.historySteps().size();
    editor.addRetouchStroke(remove, {0.5, 0.5}, 0.05, 0.3);
    check(settled(), "the stroke renders");
    const QImage removed = provider.image();
    check(spot() < 150, "the spot is gone");
    check(removed.pixel(10, 10) == before.pixel(10, 10), "nothing moves elsewhere");
    check(editor.historySteps().size() == steps + 1, "a stroke is one step");
    editor.addRetouchStroke(remove, {}, 0.05, 0.3);
    editor.addRetouchStroke(remove, {0.5}, 0.05, 0.3);
    editor.addRetouchStroke(7, {0.5, 0.5}, 0.05, 0.3);
    check(settled() && editor.retouches().at(0).toMap().value(QStringLiteral("strokes")).toInt() == 1, "a stroke that makes no sense is ignored");

    // A setting after it: the retouch goes with the image, and the slider's cache sees it.
    editor.setAdjustment(QStringLiteral("contrast"), 20);
    check(settled() && spot() < 170, "another setting keeps the retouch");
    editor.setAdjustment(QStringLiteral("contrast"), 0);
    check(settled() && provider.image() == removed, "and comes back to the same pixels");

    // Off, undone, redone.
    editor.setRetouchVisible(remove, false);
    check(settled() && provider.image() == before, "a hidden retouch does not act");
    editor.undo();
    check(settled() && provider.image() == removed && editor.retouches().at(0).toMap().value(QStringLiteral("visible")).toBool(), "an undo brings it back");
    editor.setRetouchOpacity(remove, 0.5);
    check(settled() && spot() > qRed(removed.pixel(removed.width() / 2, removed.height() / 2)) + 20, "opacity shows the object through");
    editor.undo();
    check(settled() && provider.image() == removed, "and can be undone");

    // Its zone can be shown; it is never kept.
    editor.setRetouchOverlay(remove);
    check(settled() && provider.image() != removed, "its zone shows");
    editor.setRetouchOverlay(-1);
    check(settled() && provider.image() == removed, "and goes");

    // A heal comes with a source of its own, that can be moved.
    const int heal = editor.addRetouch(QStringLiteral("heal"));
    editor.addRetouchStroke(heal, {0.2, 0.2}, 0.03, 0.3);
    check(settled(), "a heal renders");
    const QVariantMap healing = editor.retouches().at(heal).toMap();
    check(healing.value(QStringLiteral("sourceX")).toDouble() != 0.0 || healing.value(QStringLiteral("sourceY")).toDouble() != 0.0, "a first stroke suggests a source");
    editor.setRetouchSource(heal, 0.3, 0.1);
    check(settled() && std::abs(editor.retouches().at(heal).toMap().value(QStringLiteral("sourceX")).toDouble() - 0.3) < 1e-6, "the source moves");
    editor.setRetouchSource(heal, std::nan(""), 0.1);
    check(std::abs(editor.retouches().at(heal).toMap().value(QStringLiteral("sourceY")).toDouble() - 0.1) < 1e-6, "not to nowhere");

    // What cannot be done is left as it is, and said rather than hidden.
    const int tooMuch = editor.addRetouch(QStringLiteral("remove"));
    const QImage withoutIt = provider.image();
    editor.addRetouchStroke(tooMuch, {0.5, 0.5}, 1.0, 0.0);
    check(waitFor([&] { return !editor.busy() && editor.statusMessage().contains(QStringLiteral("sans effet")); }), "a zone too large is reported");
    check(provider.image() == withoutIt, "and changes nothing");
    editor.removeRetouch(tooMuch);
    check(settled(), "it can be taken away");

    // Saved with the photo; the other photo has its own, and pasting edits keeps them apart.
    editor.saveCurrentAdjustments();
    const auto saved = Sidecar::read(photo);
    check(saved && saved->settings.retouches.size() == 2, "retouches are saved in the sidecar");
    check(load(other) && editor.retouches().isEmpty(), "another photo has its own");
    editor.setAdjustment(QStringLiteral("contrast"), 25);
    check(settled(), "the other photo renders");
    editor.copyAdjustments();
    check(load(photo) && editor.retouches().size() == 2, "reopening a photo restores its retouches");
    check(settled(), "and renders them");
    editor.pasteAdjustments();
    check(settled() && editor.retouches().size() == 2 && editor.adjustmentValue(QStringLiteral("contrast")) == 25, "a paste keeps the target's retouches");

    // The source is never changed: gone with its retouches, the photo is as it was.
    editor.removeRetouch(1);
    editor.removeRetouch(0);
    editor.setAdjustment(QStringLiteral("contrast"), 0);
    check(settled() && editor.retouches().isEmpty(), "they can be removed");
    check(provider.image() == before, "and the photo is as it was");
    QImage file(photo);
    check(qRed(file.pixel(200, 150)) == 250, "the file itself never changed");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "a temporary folder");
    codec();
    commands(QDir(directory.path()));
    if (passed)
        std::cout << "editor retouch: ok\n";
    return passed ? 0 : 1;
}
