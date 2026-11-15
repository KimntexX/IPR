// The Retouch tab in a window: the real editor, on a generated photo.
#include "controlled_memory.h"
#include "imageeditor.h"
#include "imageprovider.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <cmath>
#include <functional>
#include <iostream>
#include <random>

namespace {

bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
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

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
}

QQuickItem *named(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = named(child, name))
            return found;
    return nullptr;
}

void click(QQuickItem *button)
{
    if (button)
        QMetaObject::invokeMethod(button, "click");
    QCoreApplication::processEvents();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir folder;
    if (!check(folder.isValid(), "a temporary folder")) return 1;
    QImage image(400, 300, QImage::Format_RGB32);
    std::mt19937 random(3);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const int grain = int(random() % 11) - 5;
            image.setPixel(x, y, qRgb(100 + grain, 120 + grain, 80 + grain));
        }
    for (int y = 140; y < 160; ++y)
        for (int x = 190; x < 210; ++x)
            image.setPixel(x, y, qRgb(250, 250, 250));
    const QString photo = QDir(folder.path()).filePath(QStringLiteral("photo.png"));
    image.save(photo);

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/RetouchPanel.qml")));
    QScopedPointer<QObject> object(component.createWithInitialProperties({{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!object) {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    auto *panel = qobject_cast<QQuickItem *>(object.data());
    QQuickWindow window;
    window.resize(360, 1000);
    panel->setParentItem(window.contentItem());
    panel->setWidth(360);
    panel->setHeight(1000);
    window.show();
    settle();

    auto *remove = named(panel, QStringLiteral("retouchNew_remove"));
    auto *heal = named(panel, QStringLiteral("retouchNew_heal"));
    auto *clone = named(panel, QStringLiteral("retouchNew_clone"));
    auto *details = named(panel, QStringLiteral("retouchDetails"));
    auto *zone = named(panel, QStringLiteral("retouchShowZone"));
    auto *source = named(panel, QStringLiteral("retouchPlaceSource"));
    auto *opacity = named(panel, QStringLiteral("retouchOpacity"));
    auto *hint = named(panel, QStringLiteral("retouchHint"));
    if (!check(remove && heal && clone && details && zone && source && opacity && hint, "the panel has its controls")) return 1;

    // Before a photo, nothing can start.
    if (!check(!remove->isEnabled(), "without a photo no retouch can start")) return 1;
    if (!check(!details->isVisible(), "and there is nothing in hand")) return 1;
    if (!check(panel->property("currentShape").isNull() || !panel->property("currentShape").isValid() || panel->property("currentShape").toMap().isEmpty(), "and nothing to paint")) return 1;

    editor.loadImage(QUrl::fromLocalFile(photo));
    if (!check(waitFor([&] { return editor.hasImage() && !editor.busy() && editor.imageSource().size() > 0; }), "the photo loads")) return 1;
    settle();
    if (!check(remove->isEnabled(), "a photo can be retouched")) return 1;

    // A retouch comes in hand, ready to paint.
    click(remove);
    settle();
    if (!check(editor.retouches().size() == 1 && panel->property("selected").toInt() == 0, "the button adds a retouch and selects it")) return 1;
    if (!check(details->isVisible(), "its details show")) return 1;
    if (!check(panel->property("currentShape").toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("brush"), "the viewport paints")) return 1;
    if (!check(hint->property("text").toString().contains(QStringLiteral("inventée")), "and says that the zone is invented")) return 1;
    if (!check(!source->isVisible(), "erasing has no source to move")) return 1;

    // The viewport's stroke reaches the engine.
    const QVariant centre(QVariantList{0.5, 0.5});
    QMetaObject::invokeMethod(panel, "applyStroke", Q_ARG(QVariant, centre));
    if (!check(waitFor([&] { return !editor.busy(); }), "the stroke renders")) return 1;
    settle();
    if (!check(editor.retouches().at(0).toMap().value(QStringLiteral("strokes")).toInt() == 1, "the stroke is kept")) return 1;
    if (!check(qRed(provider.image().pixel(provider.image().width() / 2, provider.image().height() / 2)) < 180, "the spot is gone")) return 1;

    // The opacity goes both ways.
    opacity->setProperty("value", 40);
    QMetaObject::invokeMethod(opacity, "valueEdited", Q_ARG(double, 40.0));
    if (!check(std::abs(editor.retouches().at(0).toMap().value(QStringLiteral("opacity")).toDouble() - 0.4) < 1e-6, "the opacity reaches the engine")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "it renders")) return 1;
    editor.undo();
    if (!check(waitFor([&] { return !editor.busy(); }), "an undo renders")) return 1;
    settle();
    if (!check(std::abs(opacity->property("value").toDouble() - 100.0) < 1e-6, "an undo reaches the slider")) return 1;

    // The zone shows on demand, while the tab is open.
    click(zone);
    settle();
    if (!check(editor.retouchOverlay() == 0, "the box shows the zone")) return 1;
    panel->setVisible(false);
    settle();
    if (!check(editor.retouchOverlay() == -1, "leaving the tab hides it")) return 1;
    panel->setVisible(true);
    settle();
    if (!check(editor.retouchOverlay() == 0, "coming back shows it")) return 1;
    click(zone);
    settle();
    if (!check(editor.retouchOverlay() == -1, "the box hides it")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles")) return 1;

    // A clone has a source: suggested, and moved through the viewport's handle.
    click(clone);
    settle();
    if (!check(panel->property("selected").toInt() == 1, "a second retouch comes in hand")) return 1;
    if (!check(source->isVisible(), "a clone has a source to move")) return 1;
    const QVariant corner(QVariantList{0.2, 0.2});
    QMetaObject::invokeMethod(panel, "applyStroke", Q_ARG(QVariant, corner));
    if (!check(waitFor([&] { return !editor.busy(); }), "its stroke renders")) return 1;
    settle();
    const QVariantMap cloned = editor.retouches().at(1).toMap();
    if (!check(cloned.value(QStringLiteral("sourceX")).toDouble() != 0.0 || cloned.value(QStringLiteral("sourceY")).toDouble() != 0.0, "it comes with a source")) return 1;
    click(source);
    settle();
    const QVariantMap handle = panel->property("currentShape").toMap();
    if (!check(handle.value(QStringLiteral("kind")).toString() == QStringLiteral("point"), "the viewport shows the source's handle")) return 1;
    if (!check(std::abs(handle.value(QStringLiteral("center")).toList().at(0).toDouble() - (0.2 + cloned.value(QStringLiteral("sourceX")).toDouble())) < 1e-6, "at the source")) return 1;
    QVariantMap movedTo;
    movedTo.insert(QStringLiteral("center"), QVariantList{0.7, 0.6});
    QMetaObject::invokeMethod(panel, "applyShape", Q_ARG(QVariant, QVariant(movedTo)));
    if (!check(waitFor([&] { return !editor.busy(); }), "the moved source renders")) return 1;
    settle();
    if (!check(std::abs(editor.retouches().at(1).toMap().value(QStringLiteral("sourceX")).toDouble() - 0.5) < 1e-5, "the handle moves the source")) return 1;
    click(source);
    settle();

    // A retouch can be hidden, chosen and removed from its row.
    click(named(panel, QStringLiteral("retouchSelect_0")));
    settle();
    if (!check(panel->property("selected").toInt() == 0, "a row selects its retouch")) return 1;
    click(named(panel, QStringLiteral("retouchVisible_0")));
    if (!check(!editor.retouches().at(0).toMap().value(QStringLiteral("visible")).toBool(), "a row's box hides it")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "it renders")) return 1;
    settle();
    if (!check(qRed(provider.image().pixel(provider.image().width() / 2, provider.image().height() / 2)) > 200, "and the spot is back")) return 1;

    click(named(panel, QStringLiteral("retouchRemove_1")));
    settle();
    if (!check(editor.retouches().size() == 1 && panel->property("selected").toInt() == 0, "a removal falls back on what exists")) return 1;
    click(named(panel, QStringLiteral("retouchRemove_0")));
    settle();
    if (!check(editor.retouches().isEmpty() && !details->isVisible(), "nothing left, nothing in hand")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles at the end")) return 1;

    std::cout << "retouch panel: ok\n";
    return 0;
}
