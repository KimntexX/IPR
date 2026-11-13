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

void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &point, unsigned long &timestamp,
           Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent event(type, point, QPointF(window.mapToGlobal(point.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button),
                      Qt::NoModifier);
    event.setTimestamp(timestamp);
    timestamp += 50;
    QCoreApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();
}

QVariantMap maskAt(ImageEditor &editor, int index)
{
    return editor.localMasks().at(index).toMap();
}

QVariantMap shapeAt(ImageEditor &editor, int mask, int shape)
{
    return maskAt(editor, mask).value(QStringLiteral("shapes")).toList().at(shape).toMap();
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
    if (!check(directory.isValid(), "a temporary folder")) return 1;
    QImage image(240, 160, QImage::Format_RGBA8888);
    image.fill(QColor(90, 120, 150));
    const QString photo = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    if (!check(image.save(photo, "PNG"), "the photo is written")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QQmlEngine engine;
    QQmlComponent panelComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/MasksPanel.qml")));
    QScopedPointer<QObject> panelObject(panelComponent.createWithInitialProperties({{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!panelObject) {
        std::cerr << panelComponent.errorString().toStdString();
        return 1;
    }
    QQmlComponent overlayComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/MaskOverlay.qml")));
    QScopedPointer<QObject> overlayObject(overlayComponent.create());
    if (!overlayObject) {
        std::cerr << overlayComponent.errorString().toStdString();
        return 1;
    }
    auto *panel = qobject_cast<QQuickItem *>(panelObject.data());
    auto *overlay = qobject_cast<QQuickItem *>(overlayObject.data());
    QQuickWindow window;
    window.resize(1000, 1600);
    panel->setParentItem(window.contentItem());
    panel->setWidth(340);
    panel->setHeight(1600);
    overlay->setParentItem(window.contentItem());
    overlay->setX(360);
    overlay->setWidth(600);
    overlay->setHeight(400);
    overlay->setProperty("imageWidth", 600);
    overlay->setProperty("imageHeight", 400);
    window.show();
    settle();

    // Without a photo, nothing can be added.
    auto *newBrush = named(panel, QStringLiteral("maskNew_brush"));
    auto *newRadial = named(panel, QStringLiteral("maskNew_radial"));
    auto *newLinear = named(panel, QStringLiteral("maskNew_linear"));
    auto *newRange = named(panel, QStringLiteral("maskNew_luminanceRange"));
    if (!check(newBrush && newRadial && newLinear && newRange, "the four kinds of mask are offered")) return 1;
    if (!check(!newBrush->isEnabled(), "without a photo they are off")) return 1;
    auto *details = named(panel, QStringLiteral("maskDetails"));
    if (!check(details && !details->isVisible(), "no mask, no details")) return 1;

    editor.loadImage(QUrl::fromLocalFile(QFileInfo(photo).canonicalFilePath()));
    if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }), "the photo loads")) return 1;
    settle();
    if (!check(newBrush->isEnabled(), "with a photo they are on")) return 1;

    // A new mask is taken in hand, and its details show.
    click(newRadial);
    settle();
    if (!check(editor.localMasks().size() == 1 && panel->property("selectedMask").toInt() == 0, "a new mask is in hand")) return 1;
    if (!check(details->isVisible(), "its details show")) return 1;
    if (!check(panel->property("tool").toString() == QStringLiteral("radial"), "with its tool")) return 1;
    if (!check(named(panel, QStringLiteral("maskFeather"))->isVisible() && !named(panel, QStringLiteral("maskBrushSize"))->isVisible(), "and the fields of its kind")) return 1;

    // The colour sliders show their colours, as in the Settings tab; the others do not.
    for (const QString &name : {QStringLiteral("temperature"), QStringLiteral("tint"), QStringLiteral("vibrance"), QStringLiteral("saturation")}) {
        auto *coloured = named(panel, QStringLiteral("maskValue_") + name);
        if (!check(coloured && coloured->property("hasColorTrack").toBool(), "a colour slider has its colours")) return 1;
    }
    if (!check(!named(panel, QStringLiteral("maskValue_contrast"))->property("hasColorTrack").toBool(), "a tone slider has none")) return 1;

    // A value slider reaches the engine, and shows what the engine holds.
    QQuickItem *exposure = named(panel, QStringLiteral("maskValue_exposure"));
    if (!check(exposure != nullptr, "the exposure control exists")) return 1;
    exposure->setProperty("value", 1.5);
    if (!check(maskAt(editor, 0).value(QStringLiteral("values")).toMap().value(QStringLiteral("exposure")).toDouble() == 1.5, "a value reaches the engine")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles")) return 1;
    editor.setLocalMaskValue(0, QStringLiteral("exposure"), 0.5);
    settle();
    if (!check(exposure->property("value").toDouble() == 0.5, "the panel shows the engine's value")) return 1;
    const qsizetype steps = editor.historySteps().size();
    editor.setLocalMaskValue(0, QStringLiteral("exposure"), 0.5);
    if (!check(editor.historySteps().size() == steps, "showing a value sends nothing back")) return 1;

    // Two masks: the sliders follow the one in hand.
    click(newLinear);
    settle();
    if (!check(panel->property("selectedMask").toInt() == 1 && exposure->property("value").toDouble() == 0, "a second mask has its own values")) return 1;
    if (!check(editor.localMasks().at(0).toMap().value(QStringLiteral("values")).toMap().value(QStringLiteral("exposure")).toDouble() == 0.5, "and the first keeps its own")) return 1;
    click(named(panel, QStringLiteral("maskSelect_0")));
    settle();
    if (!check(panel->property("selectedMask").toInt() == 0 && exposure->property("value").toDouble() == 0.5, "selecting a mask shows its values")) return 1;

    // Opacity, visibility, name.
    named(panel, QStringLiteral("maskOpacity"))->setProperty("value", 40);
    if (!check(std::abs(maskAt(editor, 0).value(QStringLiteral("opacity")).toDouble() - 0.4) < 1e-6, "the opacity reaches the engine")) return 1;
    click(named(panel, QStringLiteral("maskVisible_0")));
    settle();
    if (!check(!maskAt(editor, 0).value(QStringLiteral("visible")).toBool(), "the box hides a mask")) return 1;
    click(named(panel, QStringLiteral("maskVisible_0")));
    settle();
    editor.setLocalMaskName(0, QStringLiteral("Ciel"));
    settle();
    if (!check(named(panel, QStringLiteral("maskSelect_0"))->property("text").toString() == QStringLiteral("Ciel"), "a name shows in the list")) return 1;

    // The zone shows only on demand, and while the tab is open.
    auto *zone = named(panel, QStringLiteral("maskShowZone"));
    click(zone);
    settle();
    if (!check(editor.localMaskOverlay() == 0, "the box shows the zone")) return 1;
    panel->setVisible(false);
    settle();
    if (!check(editor.localMaskOverlay() == -1, "leaving the tab hides it")) return 1;
    panel->setVisible(true);
    settle();
    if (!check(editor.localMaskOverlay() == 0, "coming back shows it")) return 1;
    click(zone);
    settle();
    if (!check(editor.localMaskOverlay() == -1, "the box hides it again")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the zone")) return 1;

    // A combination: the zone gets a second shape, subtracted.
    named(panel, QStringLiteral("maskCombine"))->setProperty("currentIndex", 1);
    named(panel, QStringLiteral("maskKind"))->setProperty("currentIndex", 1);
    click(named(panel, QStringLiteral("maskAddShape")));
    settle();
    const QVariantList shapes = maskAt(editor, 0).value(QStringLiteral("shapes")).toList();
    if (!check(shapes.size() == 2 && shapes.at(1).toMap().value(QStringLiteral("combine")).toString() == QStringLiteral("subtract"), "a shape is subtracted")) return 1;
    if (!check(panel->property("selectedShape").toInt() == 1 && panel->property("tool").toString() == QStringLiteral("linear"), "and it comes in hand")) return 1;
    auto *removeShape = named(panel, QStringLiteral("maskShapeRemove_1"));
    click(removeShape);
    settle();
    if (!check(maskAt(editor, 0).value(QStringLiteral("shapes")).toList().size() == 1 && panel->property("selectedShape").toInt() == 0, "and it can go")) return 1;
    if (!check(!named(panel, QStringLiteral("maskShapeRemove_0"))->isEnabled(), "a mask keeps at least one shape")) return 1;

    // A range has its own fields.
    click(newRange);
    settle();
    if (!check(panel->property("selectedMask").toInt() == 2 && panel->property("tool").toString() == QStringLiteral("luminanceRange"), "a range mask")) return 1;
    if (!check(named(panel, QStringLiteral("maskRangeLow"))->isVisible() && !named(panel, QStringLiteral("maskFeather"))->isVisible(), "shows the fields of a range")) return 1;
    named(panel, QStringLiteral("maskRangeHigh"))->setProperty("value", 40);
    if (!check(std::abs(shapeAt(editor, 2, 0).value(QStringLiteral("rangeHigh")).toDouble() - 0.4) < 1e-6, "they reach the engine")) return 1;

    // Holding a range slider shows the zone, and letting go gives back what the box asks.
    const auto hold = [&](const QString &control, bool held) {
        named(named(panel, control), QStringLiteral("adjustmentSlider"))->setProperty("pressed", held);
        settle();
    };
    for (const QString &control : {QStringLiteral("maskRangeLow"), QStringLiteral("maskRangeHigh"), QStringLiteral("maskRangeFeather")}) {
        hold(control, true);
        if (!check(editor.localMaskOverlay() == 2, "a held range slider shows the zone")) return 1;
        hold(control, false);
        if (!check(editor.localMaskOverlay() == -1, "letting go hides it")) return 1;
    }
    hold(QStringLiteral("maskOpacity"), true);
    if (!check(editor.localMaskOverlay() == -1, "another slider does not show it")) return 1;
    hold(QStringLiteral("maskOpacity"), false);
    click(zone);
    settle();
    hold(QStringLiteral("maskRangeLow"), true);
    hold(QStringLiteral("maskRangeLow"), false);
    if (!check(editor.localMaskOverlay() == 2, "letting go keeps the zone when the box asks for it")) return 1;
    click(zone);
    settle();
    if (!check(editor.localMaskOverlay() == -1, "the box still hides it")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after holding")) return 1;

    // Undo can take away the mask in hand.
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles before undoing")) return 1;
    click(named(panel, QStringLiteral("maskDuplicate_2")));
    settle();
    if (!check(editor.localMasks().size() == 4 && panel->property("selectedMask").toInt() == 3, "a duplicate comes in hand")) return 1;
    // A step enters the history once its render is done.
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the duplicate")) return 1;
    editor.undo();
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the undo")) return 1;
    settle();
    if (!check(editor.localMasks().size() == 3 && panel->property("selectedMask").toInt() == 2, "an undo falls back on a mask that exists")) return 1;
    click(named(panel, QStringLiteral("maskRemove_2")));
    settle();
    if (!check(editor.localMasks().size() == 2 && panel->property("selectedMask").toInt() == 1, "so does a removal")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles after the removal")) return 1;

    // The overlay's gradient: handles follow the hand, and one edit goes out at the release.
    unsigned long timestamp = 1000;
    QQmlComponent listener(&engine);
    listener.setData("import QtQuick\nQtObject { id: self; property var watched; property var lastValues; property var lastPoints; property int edits: 0; property int strokes: 0\n"
                     "property Connections c: Connections { target: self.watched; function onShapeEdited(values) { self.lastValues = values; ++self.edits }\n"
                     "function onStrokeDrawn(points) { self.lastPoints = points; ++self.strokes } } }", QUrl());
    QScopedPointer<QObject> listening(listener.createWithInitialProperties({{QStringLiteral("watched"), QVariant::fromValue(overlay)}}));
    if (!check(listening != nullptr, "the listener loads")) return 1;

    overlay->setProperty("shape", maskAt(editor, 1).value(QStringLiteral("shapes")).toList().at(0));
    settle();
    auto *start = named(overlay, QStringLiteral("maskHandle_start"));
    if (!check(start != nullptr, "a linear gradient has its handles")) return 1;
    if (!check(named(overlay, QStringLiteral("maskHandle_end")) != nullptr && named(overlay, QStringLiteral("maskHandle_center")) == nullptr, "its own")) return 1;
    const QPointF startAt = centreOf(start);
    if (!check(std::abs(startAt.x() - (360 + 300)) < 2 && std::abs(startAt.y() - 100) < 2, "where the shape says, at (0.5, 0.25)")) return 1;
    mouse(window, QEvent::MouseButtonPress, startAt, timestamp);
    mouse(window, QEvent::MouseMove, startAt + QPointF(60, 40), timestamp);
    if (!check(std::abs(centreOf(named(overlay, QStringLiteral("maskHandle_start"))).x() - (startAt.x() + 60)) < 2, "the handle follows the hand")) return 1;
    if (!check(listening->property("edits").toInt() == 0, "nothing is sent while it moves")) return 1;
    mouse(window, QEvent::MouseButtonRelease, startAt + QPointF(60, 40), timestamp);
    if (!check(listening->property("edits").toInt() == 1, "the release sends one edit")) return 1;
    const QVariantList sentStart = listening->property("lastValues").toMap().value(QStringLiteral("start")).toList();
    if (!check(sentStart.size() == 2 && std::abs(sentStart.at(0).toDouble() - 0.6) < 0.01 && std::abs(sentStart.at(1).toDouble() - 0.35) < 0.01, "on the normalized canvas")) return 1;

    // With a crop shown, the same place on the screen is another place on the canvas.
    overlay->setProperty("cropX", 0.5);
    overlay->setProperty("cropY", 0.5);
    overlay->setProperty("cropWidth", 0.5);
    overlay->setProperty("cropHeight", 0.5);
    QVariantMap inCrop = maskAt(editor, 1).value(QStringLiteral("shapes")).toList().at(0).toMap();
    inCrop.insert(QStringLiteral("start"), QVariantList{0.75, 0.75});
    overlay->setProperty("shape", inCrop);
    settle();
    auto *cropped = named(overlay, QStringLiteral("maskHandle_start"));
    const QPointF croppedAt = centreOf(cropped);
    if (!check(std::abs(croppedAt.x() - (360 + 300)) < 2 && std::abs(croppedAt.y() - 200) < 2, "a handle follows the crop")) return 1;
    mouse(window, QEvent::MouseButtonPress, croppedAt, timestamp);
    mouse(window, QEvent::MouseMove, croppedAt + QPointF(150, 100), timestamp);
    mouse(window, QEvent::MouseButtonRelease, croppedAt + QPointF(150, 100), timestamp);
    const QVariantList sentCropped = listening->property("lastValues").toMap().value(QStringLiteral("start")).toList();
    if (!check(sentCropped.size() == 2 && std::abs(sentCropped.at(0).toDouble() - 0.875) < 0.01 && std::abs(sentCropped.at(1).toDouble() - 0.875) < 0.01, "and sends the canvas's place")) return 1;
    overlay->setProperty("cropX", 0.0);
    overlay->setProperty("cropY", 0.0);
    overlay->setProperty("cropWidth", 1.0);
    overlay->setProperty("cropHeight", 1.0);

    // A radial: its edge changes its radius along one axis only.
    overlay->setProperty("shape", maskAt(editor, 0).value(QStringLiteral("shapes")).toList().at(0));
    settle();
    auto *edge = named(overlay, QStringLiteral("maskHandle_radiusX"));
    if (!check(edge && named(overlay, QStringLiteral("maskHandle_center")) && !named(overlay, QStringLiteral("maskHandle_start")), "a radial has its handles")) return 1;
    const int editsBefore = listening->property("edits").toInt();
    const QPointF edgeAt = centreOf(edge);
    mouse(window, QEvent::MouseButtonPress, edgeAt, timestamp);
    mouse(window, QEvent::MouseMove, edgeAt + QPointF(60, 0), timestamp);
    mouse(window, QEvent::MouseButtonRelease, edgeAt + QPointF(60, 0), timestamp);
    if (!check(listening->property("edits").toInt() == editsBefore + 1, "an edge sends one edit")) return 1;
    const QVariantMap sentEdge = listening->property("lastValues").toMap();
    if (!check(std::abs(sentEdge.value(QStringLiteral("radiusX")).toDouble() - 0.35) < 0.01 && !sentEdge.contains(QStringLiteral("radiusY")), "its radius, and no other")) return 1;

    // A brush: a stroke goes out once, at the release, on the canvas.
    QVariantMap brushShape;
    brushShape.insert(QStringLiteral("kind"), QStringLiteral("brush"));
    overlay->setProperty("shape", brushShape);
    settle();
    if (!check(!named(overlay, QStringLiteral("maskHandle_center")), "a brush has no handles")) return 1;
    const QPointF from(360 + 150, 100);
    mouse(window, QEvent::MouseButtonPress, from, timestamp);
    mouse(window, QEvent::MouseMove, from + QPointF(150, 0), timestamp);
    mouse(window, QEvent::MouseMove, from + QPointF(300, 100), timestamp);
    if (!check(listening->property("strokes").toInt() == 0, "nothing is sent while it paints")) return 1;
    mouse(window, QEvent::MouseButtonRelease, from + QPointF(300, 100), timestamp);
    if (!check(listening->property("strokes").toInt() == 1, "the release sends the stroke")) return 1;
    const QVariantList points = listening->property("lastPoints").toList();
    if (!check(points.size() == 6, "with each point")) return 1;
    if (!check(std::abs(points.at(0).toDouble() - 0.25) < 0.01 && std::abs(points.at(1).toDouble() - 0.25) < 0.01 &&
               std::abs(points.at(4).toDouble() - 0.75) < 0.01 && std::abs(points.at(5).toDouble() - 0.5) < 0.01, "on the normalized canvas")) return 1;

    // The panel draws what the viewport sends, on the brush of the mask in hand.
    click(newBrush);
    settle();
    const int brushMask = panel->property("selectedMask").toInt();
    panel->setProperty("brushSize", 0.1);
    QMetaObject::invokeMethod(panel, "applyStroke", Q_ARG(QVariant, QVariant(QVariantList{0.2, 0.2, 0.4, 0.4})));
    settle();
    if (!check(shapeAt(editor, brushMask, 0).value(QStringLiteral("strokes")).toInt() == 1, "a stroke reaches the mask in hand")) return 1;
    // Not on a gradient.
    click(named(panel, QStringLiteral("maskSelect_0")));
    settle();
    QMetaObject::invokeMethod(panel, "applyStroke", Q_ARG(QVariant, QVariant(QVariantList{0.2, 0.2})));
    settle();
    if (!check(shapeAt(editor, 0, 0).value(QStringLiteral("strokes")).toInt() == 0, "and not to a gradient")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "the preview settles at the end")) return 1;

    // Tapered cones are real mask kinds, with editable widths and one edit per drag.
    auto *newTriangle = named(panel, QStringLiteral("maskNew_triangle"));
    auto *newTrapezoid = named(panel, QStringLiteral("maskNew_trapezoid"));
    if (!check(newTriangle && newTrapezoid, "both cone presets are offered")) return 1;
    click(newTriangle); settle();
    if (!check(panel->property("tool").toString() == "triangle" &&
               named(panel, "maskEndWidth")->isVisible() && !named(panel, "maskStartWidth")->isVisible(),
               "triangle exposes its far width")) return 1;
    const int triangleIndex = panel->property("selectedMask").toInt();
    auto *diffusion = named(panel, QStringLiteral("maskDiffusion"));
    if (!check(diffusion && diffusion->isVisible() &&
               std::abs(diffusion->property("value").toDouble() - 70) < .01,
               "new cones offer a default spot falloff")) return 1;
    diffusion->setProperty("value", 95.0);
    QMetaObject::invokeMethod(diffusion, "valueEdited", Q_ARG(double, 95.0));
    settle();
    if (!check(std::abs(shapeAt(editor, triangleIndex, 0).value("diffusion").toDouble() - .95) < .001,
               "diffusion slider updates the recipe")) return 1;
    overlay->setProperty("shape", shapeAt(editor, triangleIndex, 0)); settle();
    if (!check(named(overlay,"maskHandle_start") && named(overlay,"maskHandle_endWidth") &&
               !named(overlay,"maskHandle_startWidth"), "triangle handles")) return 1;
    auto *widthHandle = named(overlay, "maskHandle_endWidth");
    const auto widthAt = centreOf(widthHandle);
    const int beforeWidthEdit = listening->property("edits").toInt();
    mouse(window,QEvent::MouseButtonPress,widthAt,timestamp);
    mouse(window,QEvent::MouseMove,widthAt+QPointF(-60,0),timestamp);
    if (!check(listening->property("edits").toInt() == beforeWidthEdit, "width is previewed until release")) return 1;
    mouse(window,QEvent::MouseButtonRelease,widthAt+QPointF(-60,0),timestamp);
    if (!check(listening->property("edits").toInt() == beforeWidthEdit+1 &&
               std::abs(listening->property("lastValues").toMap().value("endWidth").toDouble()-.6)<.02,
               "width drag sends one normalized edit")) return 1;
    click(newTrapezoid); settle();
    const int trapezoidIndex = panel->property("selectedMask").toInt();
    overlay->setProperty("shape", shapeAt(editor,trapezoidIndex,0)); settle();
    if (!check(panel->property("tool").toString() == "trapezoid" &&
               named(panel,"maskStartWidth")->isVisible() && named(overlay,"maskHandle_startWidth"),
               "trapezoid exposes both widths")) return 1;
    if (!check(waitFor([&] { return !editor.busy(); }), "cone preview settles")) return 1;

    // Reordering commits on release, preserves the selected mask and supports both directions.
    const QVariantList beforeOrder = editor.localMasks();
    const int last = int(beforeOrder.size()) - 1;
    const qsizetype historyBefore = editor.historySteps().size();
    auto *drag = named(panel, "maskDrag_" + QString::number(last));
    auto *firstDrag = named(panel, "maskDrag_0");
    if (!check(drag && firstDrag, "reorder handles exist")) return 1;
    const QPointF dragFrom = centreOf(drag), dragTo = centreOf(firstDrag);
    mouse(window, QEvent::MouseButtonPress, dragFrom, timestamp);
    mouse(window, QEvent::MouseMove, dragTo, timestamp);
    if (!check(editor.localMasks() == beforeOrder, "drag does not edit before release")) return 1;
    mouse(window, QEvent::MouseButtonRelease, dragTo, timestamp);
    settle();
    if (!check(waitFor([&] { return !editor.busy(); }) && editor.localMasks()[0] == beforeOrder[last] &&
               panel->property("selectedMask").toInt() == 0 && editor.historySteps().size() == historyBefore + 1,
               "drop moves the selected mask in one step")) return 1;
    // Right-clicking the name opens the actual context menu, respecting list boundaries.
    const auto rightAt = centreOf(named(panel, "maskSelect_0"));
    mouse(window, QEvent::MouseButtonPress, rightAt, timestamp, Qt::RightButton);
    mouse(window, QEvent::MouseButtonRelease, rightAt, timestamp, Qt::RightButton);
    settle();
    auto *menu = panelObject->findChild<QObject *>(QStringLiteral("maskOrderMenu"));
    auto *forward = panelObject->findChild<QObject *>(QStringLiteral("maskMoveForward"));
    auto *backward = panelObject->findChild<QObject *>(QStringLiteral("maskMoveBackward"));
    if (!check(menu && forward && backward && menu->property("visible").toBool() &&
               !forward->property("enabled").toBool() && backward->property("enabled").toBool(),
               "context menu opens and disables movement above the first row")) return 1;
    QMetaObject::invokeMethod(backward, "triggered");
    QMetaObject::invokeMethod(menu, "close");
    settle();
    if (!check(waitFor([&] { return !editor.busy(); }) && editor.localMasks()[1] == beforeOrder[last] &&
               panel->property("selectedMask").toInt() == 1, "context action moves behind and follows selection")) return 1;
    editor.undo();
    if (!check(waitFor([&] { return !editor.busy(); }) && editor.localMasks()[0] == beforeOrder[last],
               "context movement can be undone")) return 1;

    std::cout << "masks panel: ok\n";
    return 0;
}
