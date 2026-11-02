#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
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

void settleLayout()
{
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
}

QQuickItem *visualItemNamed(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = visualItemNamed(child, name))
            return found;
    return nullptr;
}

QObject *controlWithLabel(QObject *root, const QString &label)
{
    for (QObject *child : root->findChildren<QObject *>())
        if (child->property("minimumValue").isValid() && child->property("label").toString() == label)
            return child;
    return nullptr;
}

void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &point, unsigned long &timestamp)
{
    QMouseEvent event(type, point, QPointF(window.mapToGlobal(point.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(Qt::LeftButton),
                      Qt::NoModifier);
    event.setTimestamp(timestamp);
    timestamp += 50;
    QCoreApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();
}

void drag(QQuickWindow &window, const QPointF &from, const QPointF &by, unsigned long &timestamp)
{
    mouse(window, QEvent::MouseButtonPress, from, timestamp);
    for (int step = 1; step <= 4; ++step)
        mouse(window, QEvent::MouseMove, from + by * (step / 4.0), timestamp);
    mouse(window, QEvent::MouseButtonRelease, from + by, timestamp);
}

void click(QQuickItem *button)
{
    if (button) QMetaObject::invokeMethod(button, "click");
    QCoreApplication::processEvents();
}

QPointF centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    QImage image(240, 160, QImage::Format_RGBA8888);
    image.fill(QColor(90, 120, 150));
    const QString photo = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    if (!check(image.save(photo, "PNG"), "input written")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QQmlEngine engine;
    QQmlComponent panelComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/GeometryPanel.qml")));
    QScopedPointer<QObject> panelObject(panelComponent.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!panelObject) {
        std::cerr << panelComponent.errorString().toStdString();
        return 1;
    }
    QQmlComponent overlayComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/CropOverlay.qml")));
    QScopedPointer<QObject> overlayObject(overlayComponent.create());
    if (!overlayObject) {
        std::cerr << overlayComponent.errorString().toStdString();
        return 1;
    }
    QQmlComponent tabsComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/PanelTabBar.qml")));
    QScopedPointer<QObject> tabsObject(tabsComponent.create());
    if (!tabsObject) {
        std::cerr << tabsComponent.errorString().toStdString();
        return 1;
    }

    auto *panel = qobject_cast<QQuickItem *>(panelObject.data());
    auto *overlay = qobject_cast<QQuickItem *>(overlayObject.data());
    auto *tabs = qobject_cast<QQuickItem *>(tabsObject.data());
    QQuickWindow window;
    window.resize(1000, 1400);
    panel->setParentItem(window.contentItem());
    panel->setWidth(320);
    panel->setHeight(1400);
    overlay->setParentItem(window.contentItem());
    overlay->setX(340);
    overlay->setWidth(600);
    overlay->setHeight(400);
    overlay->setProperty("imageX", 0);
    overlay->setProperty("imageY", 0);
    overlay->setProperty("imageWidth", 600);
    overlay->setProperty("imageHeight", 400);
    tabs->setParentItem(window.contentItem());
    tabs->setX(340);
    tabs->setY(420);
    tabs->setWidth(320);
    window.show();
    settleLayout();

    // The tabs choose by themselves, one at a time.
    auto *geometryTab = visualItemNamed(tabs, QStringLiteral("panelTabGeometry"));
    if (!check(geometryTab != nullptr && visualItemNamed(tabs, QStringLiteral("panelTabSettings")), "the two tabs exist")) return 1;
    click(geometryTab);
    if (!check(tabs->property("currentIndex").toInt() == 1 && geometryTab->property("checked").toBool(),
               "a click selects a tab")) return 1;

    // Before any photo, the panel is disabled, and its commands do nothing.
    if (!check(!visualItemNamed(panel, QStringLiteral("geometryTurnButton"))->isEnabled(), "without a photo the panel is off")) return 1;

    editor.loadImage(QUrl::fromLocalFile(QFileInfo(photo).canonicalFilePath()));
    if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }), "photo loads")) return 1;
    settleLayout();

    // Buttons reach the engine.
    click(visualItemNamed(panel, QStringLiteral("geometryTurnButton")));
    if (!check(editor.geometry().value("quarterTurns").toInt() == 1, "the turn button turns")) return 1;
    click(visualItemNamed(panel, QStringLiteral("geometryMirrorHorizontalButton")));
    if (!check(editor.geometry().value("flipHorizontal").toBool(), "the mirror button mirrors")) return 1;
    click(visualItemNamed(panel, QStringLiteral("geometryResetButton")));
    if (!check(editor.geometry().value("identity").toBool(), "the reset button clears")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles")) return 1;

    // A slider follows the engine, and the engine follows it.
    QObject *rotation = controlWithLabel(panelObject.data(), QStringLiteral("Rotation"));
    if (!check(rotation != nullptr, "the rotation control exists")) return 1;
    editor.setGeometryValue(QStringLiteral("rotation"), 5);
    if (!check(rotation->property("value").toDouble() == 5, "the panel shows the engine's value")) return 1;
    rotation->setProperty("value", 8.5);
    if (!check(editor.geometry().value("rotation").toDouble() == 8.5, "the engine takes the panel's value")) return 1;
    const qsizetype before = editor.historySteps().size();
    auto *slider = rotation->findChild<QQuickItem *>(QStringLiteral("adjustmentSlider"));
    auto *knob = slider ? slider->property("handle").value<QQuickItem *>() : nullptr;
    if (!check(knob != nullptr, "the rotation slider has a handle")) return 1;
    unsigned long timestamp = 1000;
    drag(window, knob->mapToScene(QPointF(5, 5)), QPointF(30, 0), timestamp);
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the drag")) return 1;
    if (!check(editor.geometry().value("rotation").toDouble() > 8.5, "a drag turns the image")) return 1;
    if (!check(editor.historySteps().size() <= before + 2, "a drag is one step, whatever its moves")) return 1;
    click(visualItemNamed(panel, QStringLiteral("geometryResetButton")));
    settleLayout();
    if (!check(rotation->property("value").toDouble() == 0, "the panel follows a reset")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the reset")) return 1;

    // The crop's numeric entry, and the note on empty zones.
    QObject *width = controlWithLabel(panelObject.data(), QStringLiteral("Largeur (%)"));
    if (!check(width != nullptr, "the width control exists")) return 1;
    width->setProperty("value", 50);
    if (!check(std::abs(editor.geometry().value("cropWidth").toDouble() - 0.5) < 1e-9 &&
               std::abs(editor.geometry().value("cropHeight").toDouble() - 1) < 1e-9,
               "a typed width changes only the width")) return 1;
    editor.setGeometryValue(QStringLiteral("rotation"), 10);
    if (!check(visualItemNamed(panel, QStringLiteral("geometryEmptyZonesNote"))->isVisible(), "empty zones are told")) return 1;
    editor.resetGeometry();
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles without geometry")) return 1;
    settleLayout();
    if (!check(!visualItemNamed(panel, QStringLiteral("geometryEmptyZonesNote"))->isVisible(), "and no longer when there are none")) return 1;

    // The frame's handles: a drag is local, and sent once at the release.
    const auto readCrop = [&] {
        return QRectF(overlay->property("cropX").toDouble(), overlay->property("cropY").toDouble(),
                      overlay->property("cropWidth").toDouble(), overlay->property("cropHeight").toDouble());
    };
    settleLayout();
    auto *corner = visualItemNamed(overlay, QStringLiteral("cropHandle_1_1"));
    if (!check(corner != nullptr, "the frame has handles")) return 1;
    const QPointF cornerAt = centreOf(corner);
    if (!check(std::abs(cornerAt.x() - 940) < 2 && std::abs(cornerAt.y() - 400) < 2, "the frame fits the image")) return 1;
    mouse(window, QEvent::MouseButtonPress, cornerAt, timestamp);
    mouse(window, QEvent::MouseMove, cornerAt + QPointF(-150, -100), timestamp);
    auto *frame = visualItemNamed(overlay, QStringLiteral("cropFrame"));
    if (!check(std::abs(frame->width() - 450) < 2 && std::abs(frame->height() - 300) < 2, "the frame follows the hand")) return 1;
    if (!check(readCrop() == QRectF(0, 0, 1, 1), "nothing is sent while it moves")) return 1;
    mouse(window, QEvent::MouseButtonRelease, cornerAt + QPointF(-150, -100), timestamp);
    if (!check(overlay->property("dragging").toBool() == false, "the release ends the gesture")) return 1;
    if (!check(std::abs(frame->width() - 600) < 2, "with no engine answer the frame shows the engine's crop")) return 1;

    // The body moves the frame, inside the canvas, and sends it once at the release.
    overlay->setProperty("cropWidth", 0.5);
    overlay->setProperty("cropHeight", 0.5);
    overlay->setProperty("cropX", 0.25);
    overlay->setProperty("cropY", 0.25);
    settleLayout();
    const QPointF bodyAt = centreOf(frame);
    mouse(window, QEvent::MouseButtonPress, bodyAt, timestamp);
    mouse(window, QEvent::MouseMove, bodyAt + QPointF(60, -40), timestamp);
    if (!check(std::abs(frame->x() - 210) < 2 && std::abs(frame->y() - 60) < 2 && std::abs(frame->width() - 300) < 2,
               "a drag of the body moves the frame and keeps its size")) return 1;
    mouse(window, QEvent::MouseMove, bodyAt + QPointF(900, 900), timestamp);
    if (!check(frame->x() + frame->width() <= 601 && frame->y() + frame->height() <= 401,
               "a drag of the body stays on the canvas")) return 1;
    if (!check(readCrop().x() == 0.25, "nothing is sent while the body moves")) return 1;
    QQmlComponent listener(&engine);
    listener.setData("import QtQuick\nQtObject { id: self; property var watched; property int count: 0\n"
                     "property Connections c: Connections { target: self.watched; function onCropEdited() { ++self.count } } }", QUrl());
    QScopedPointer<QObject> listening(listener.createWithInitialProperties({{QStringLiteral("watched"), QVariant::fromValue(overlay)}}));
    if (!check(listening != nullptr, "the listener loads")) return 1;
    mouse(window, QEvent::MouseButtonRelease, bodyAt + QPointF(900, 900), timestamp);
    if (!check(listening->property("count").toInt() == 1, "the release sends the frame once")) return 1;

    // With a ratio, a side keeps the proportions, and the frame stays on the canvas.
    overlay->setProperty("cropWidth", 0.5);
    overlay->setProperty("cropHeight", 0.5);
    overlay->setProperty("cropX", 0.25);
    overlay->setProperty("cropY", 0.25);
    overlay->setProperty("normalizedRatio", 1.0);
    settleLayout();
    auto *right = visualItemNamed(overlay, QStringLiteral("cropHandle_1_0.5"));
    if (!check(right != nullptr, "the frame has side handles")) return 1;
    const QPointF rightAt = centreOf(right);
    mouse(window, QEvent::MouseButtonPress, rightAt, timestamp);
    mouse(window, QEvent::MouseMove, rightAt + QPointF(60, 0), timestamp);
    if (!check(std::abs(frame->width() - 360) < 2 && std::abs(frame->height() - 240) < 2, "a ratio holds on a side drag")) return 1;
    mouse(window, QEvent::MouseMove, rightAt + QPointF(600, 0), timestamp);
    if (!check(frame->x() >= -1 && frame->y() >= -1 && frame->x() + frame->width() <= 601 &&
               frame->y() + frame->height() <= 401, "the frame stays on the canvas")) return 1;
    mouse(window, QEvent::MouseButtonRelease, rightAt + QPointF(600, 0), timestamp);

    std::cout << "geometry panel: ok\n";
    return 0;
}
