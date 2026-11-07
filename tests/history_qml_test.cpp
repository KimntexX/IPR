#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "GradingWheelProvider.h"

#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <initializer_list>
#include <iostream>
#include <utility>

// The interface side of the edit history: a real pointer drag makes one step,
// and the panel and shortcuts drive the engine's history.
namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

void settleLayout(int milliseconds = 50)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 5000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(5);
    if (!done()) loop.exec();
    return done();
}

QObject *controlWithLabel(QObject *root, const QString &label)
{
    for (QObject *child : root->findChildren<QObject *>())
        if (child->property("minimumValue").isValid() && child->property("label").toString() == label)
            return child;
    return nullptr;
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

void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &point, unsigned long &timestamp,
           Qt::MouseButton button = Qt::LeftButton)
{
    QMouseEvent event(type, point, QPointF(window.mapToGlobal(point.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button), Qt::NoModifier);
    event.setTimestamp(timestamp);
    timestamp += 50;
    QCoreApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();
}

// A drag of several moves, as a hand makes it.
void drag(QQuickWindow &window, const QPointF &from, const QPointF &by, unsigned long &timestamp)
{
    mouse(window, QEvent::MouseButtonPress, from, timestamp);
    for (int step = 1; step <= 4; ++step)
        mouse(window, QEvent::MouseMove, from + by * (step / 4.0), timestamp);
    mouse(window, QEvent::MouseButtonRelease, from + by, timestamp);
}

void key(QQuickWindow &window, int code, const QString &text = {})
{
    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, text);
    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier, text);
    QCoreApplication::sendEvent(&window, &press);
    QCoreApplication::sendEvent(&window, &release);
    QCoreApplication::processEvents();
}

void click(QQuickItem *button)
{
    if (button) QMetaObject::invokeMethod(button, "click");
    QCoreApplication::processEvents();
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary directory")) return 1;
    QImage image(48, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(60 + 3 * x, 80 + 2 * y, 120, 255));
    const QString photo = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    if (!check(image.save(photo, "PNG"), "input written")) return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("gradingwheel"), new GradingWheelProvider);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/Setting.qml")));
    QScopedPointer<QObject> root(component.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!root) {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    auto *panel = qobject_cast<QQuickItem *>(root.data());
    QQmlComponent infoComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/ImageInfoPanel.qml")));
    QScopedPointer<QObject> info(infoComponent.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!info) {
        std::cerr << infoComponent.errorString().toStdString();
        return 1;
    }
    auto *infoPanel = qobject_cast<QQuickItem *>(info.data());
    QQuickWindow window;
    window.resize(640, 1600);
    panel->setParentItem(window.contentItem());
    panel->setWidth(320);
    panel->setHeight(1600);
    infoPanel->setParentItem(window.contentItem());
    infoPanel->setX(320);
    infoPanel->setWidth(300);
    infoPanel->setHeight(1600);
    window.show();
    settleLayout();

    editor.loadImage(QUrl::fromLocalFile(QFileInfo(photo).canonicalFilePath()));
    if (!check(waitFor([&] { return editor.hasImage() && !editor.busy(); }), "photo loads")) return 1;
    settleLayout();

    // Only the sections under test are open, so that they fit in the window.
    for (QObject *section : root->findChildren<QObject *>())
        if (section->property("stateKey").isValid())
            section->setProperty("expanded", section->objectName() == "lightSection");
    settleLayout();
    auto *history = info->findChild<QQuickItem *>(QStringLiteral("historySection"));
    if (!check(history != nullptr && !root->findChild<QQuickItem *>(QStringLiteral("historySection")),
               "the history section is in the information panel, not in the settings")) return 1;
    settleLayout();
    auto *stepBar = history->findChild<QQuickItem *>(QStringLiteral("panelScrollBar"));
    if (!check(stepBar != nullptr && stepBar->width() == 6, "the history's scroll bar is the interface's")) return 1;

    // A real drag of a slider is one step.
    QObject *contrast = controlWithLabel(root.data(), QStringLiteral("Contraste"));
    auto *slider = contrast ? contrast->findChild<QQuickItem *>(QStringLiteral("adjustmentSlider")) : nullptr;
    if (!check(slider != nullptr, "the contrast slider exists")) return 1;
    auto *handle = slider->property("handle").value<QQuickItem *>();
    unsigned long timestamp = 1000;
    drag(window, handle->mapToItem(nullptr, QPointF(5, 5)), QPointF(40, 0), timestamp);
    const double dragged = editor.adjustmentValue(QStringLiteral("contrast"));
    if (!check(dragged > 0, "the drag reaches the engine")) return 1;
    if (!check(editor.historySteps().size() == 2 && editor.canUndo(), "a slider drag is one history step")) return 1;
    if (!check(contrast->property("value").toDouble() == dragged, "the release position is in that step")) return 1;

    // The panel names the steps and drives the history.
    auto *module = visualItemNamed(history, QStringLiteral("historyUndo"))
        ? history->findChild<QObject *>(QStringLiteral("historyStepList")) : nullptr;
    if (!check(module != nullptr, "the history panel shows its step list")) return 1;
    click(visualItemNamed(history, QStringLiteral("historyUndo")));
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == 0 && contrast->property("value").toDouble() == 0 &&
               editor.canRedo(), "the undo button restores the engine and the slider")) return 1;
    click(visualItemNamed(history, QStringLiteral("historyRedo")));
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == dragged &&
               contrast->property("value").toDouble() == dragged && editor.historySteps().size() == 2,
               "the redo button replays the step without adding one")) return 1;

    settleLayout();
    QQuickItem *firstRow = nullptr;
    auto *listContent = qobject_cast<QQuickItem *>(module)->property("contentItem").value<QQuickItem *>();
    for (QQuickItem *row : listContent->childItems())
        if (row->objectName() == "historyStepRow" && row->property("index").toInt() == 0)
            firstRow = row;
    if (!check(firstRow != nullptr, "the opening state has a row")) return 1;
    const QPointF rowPoint = firstRow->mapToItem(nullptr, QPointF(10, 10));
    mouse(window, QEvent::MouseButtonPress, rowPoint, timestamp);
    mouse(window, QEvent::MouseButtonRelease, rowPoint, timestamp);
    if (!check(editor.historyPosition() == 0 && editor.adjustmentValue(QStringLiteral("contrast")) == 0,
               "clicking a step goes to it")) return 1;
    editor.redo();

    QObject *historyModule = nullptr;
    for (QObject *child : info->findChildren<QObject *>())
        if (child->metaObject()->indexOfMethod("stepLabel(QVariant)") >= 0)
            historyModule = child;
    if (!check(historyModule != nullptr, "the history module exists")) return 1;
    const auto label = [&](int step) {
        QVariant result;
        QMetaObject::invokeMethod(historyModule, "stepLabel", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, step));
        return result.toString();
    };
    if (!check(label(0) == QStringLiteral("Ouverture") && label(1) == QStringLiteral("Contraste"),
               "steps carry the names the panels use")) return 1;
    editor.resetAdjustments();
    editor.setAdjustment(QStringLiteral("saturation"), 20);
    editor.undo();
    editor.undo();
    editor.setAdjustment(QStringLiteral("contrast"), 10);
    editor.setAdjustment(QStringLiteral("saturation"), 10);
    editor.setAdjustment(QStringLiteral("vibrance"), 10);
    editor.pasteAdjustments();   // nothing copied: no step
    if (!check(label(editor.historySteps().size() - 1) == QStringLiteral("Vibrance"), "a step names its one field"))
        return 1;

    // Removing a step: from its context menu or with the Delete key.
    {
        const auto rowOf = [&](int step) -> QQuickItem * {
            for (QQuickItem *row : listContent->childItems())
                if (row->objectName() == "historyStepRow" && row->property("index").toInt() == step)
                    return row;
            return nullptr;
        };
        settleLayout();
        const int steps = int(editor.historySteps().size());
        // Contrast 10, saturation 10, vibrance 10: the saturation's step goes.
        QQuickItem *saturationRow = rowOf(steps - 2);
        if (!check(saturationRow && label(steps - 2) == QStringLiteral("Saturation"), "the step to remove has a row"))
            return 1;
        const QPointF point = saturationRow->mapToItem(nullptr, QPointF(10, 10));
        mouse(window, QEvent::MouseButtonPress, point, timestamp, Qt::RightButton);
        mouse(window, QEvent::MouseButtonRelease, point, timestamp, Qt::RightButton);
        settleLayout();
        auto *menu = info->findChild<QObject *>(QStringLiteral("historyStepMenu"));
        if (!check(menu && menu->property("opened").toBool() && editor.historyPosition() == steps - 1,
                   "a right click opens the menu without moving in the history")) return 1;
        // Every menu takes the interface's colours: a popup does not by itself.
        auto *menuPalette = menu->property("palette").value<QObject *>();
        auto *panelPalette = infoPanel->property("palette").value<QObject *>();
        panelPalette->setProperty("window", QColor(QStringLiteral("#123456")));
        settleLayout();
        auto *menuBackground = menu->property("background").value<QObject *>();
        if (!check(menuPalette && menuPalette->property("window").value<QColor>() == QColor(QStringLiteral("#123456")) &&
                   menuBackground && menuBackground->property("color").value<QColor>() == QColor(QStringLiteral("#123456")),
                   "the menu follows the colours of the panel it opens from")) return 1;
        QQuickItem *removeItem = nullptr;
        for (QObject *child : menu->findChildren<QObject *>())
            if (child->objectName() == "historyStepRemove") removeItem = qobject_cast<QQuickItem *>(child);
        if (!removeItem) {
            const QVariant count = menu->property("count");
            for (int index = 0; index < count.toInt() && !removeItem; ++index) {
                QQuickItem *item = nullptr;
                QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
                if (item && item->objectName() == "historyStepRemove") removeItem = item;
            }
        }
        if (!check(removeItem != nullptr, "the menu offers to remove the step")) return 1;
        click(removeItem);
        settleLayout();
        if (!check(editor.historySteps().size() == steps - 1 && editor.adjustmentValue(QStringLiteral("saturation")) == 0 &&
                   editor.adjustmentValue(QStringLiteral("vibrance")) == 10,
                   "the menu takes the edit out and keeps the later one")) return 1;
        // The Delete key removes the current step of the focused list.
        qobject_cast<QQuickItem *>(module)->forceActiveFocus();
        settleLayout();
        key(window, Qt::Key_Delete);
        settleLayout();
        if (!check(editor.historySteps().size() == steps - 2 && editor.adjustmentValue(QStringLiteral("vibrance")) == 0,
                   "the Delete key removes the current step")) return 1;
        editor.setAdjustment(QStringLiteral("saturation"), 10);
        editor.setAdjustment(QStringLiteral("vibrance"), 10);
        settleLayout();
    }

    // Typing a value is one step, whatever the digits on the way.
    const int beforeTyping = int(editor.historySteps().size());
    auto *field = contrast->findChild<QQuickItem *>(QStringLiteral("adjustmentValueEditor"));
    if (!check(field != nullptr, "the contrast field exists")) return 1;
    field->forceActiveFocus();
    field->setProperty("text", QString());
    settleLayout();
    key(window, Qt::Key_3, QStringLiteral("3"));
    key(window, Qt::Key_7, QStringLiteral("7"));
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == 37, "the typed value reaches the engine")) return 1;
    key(window, Qt::Key_Return, QStringLiteral("\r"));
    if (!check(editor.historySteps().size() == beforeTyping + 1, "a typed value is one history step")) return 1;
    panel->forceActiveFocus();

    // A drag on a grading wheel is one step.
    for (QObject *section : root->findChildren<QObject *>())
        if (section->property("stateKey").isValid())
            section->setProperty("expanded", section->objectName() == "gradingSection");
    settleLayout();
    auto *wheel = visualItemNamed(panel, QStringLiteral("gradingWheelArea"));
    if (!check(wheel != nullptr && wheel->isVisible(), "the grading wheel is shown")) return 1;
    const int beforeWheel = int(editor.historySteps().size());
    drag(window, wheel->mapToItem(nullptr, QPointF(wheel->width() / 2 + 10, wheel->height() / 2)),
         QPointF(15, 15), timestamp);
    if (!check(editor.historySteps().size() == beforeWheel + 1, "a wheel drag is one history step")) return 1;

    // Named snapshots.
    auto *name = visualItemNamed(history, QStringLiteral("snapshotName"));
    if (!check(name != nullptr, "the snapshot name field exists")) return 1;
    if (!check(name->property("color").value<QColor>() == info->property("textColor").value<QColor>() &&
               name->property("placeholderTextColor").value<QColor>().alphaF() < 1.0,
               "the snapshot field uses the panel's text colours")) return 1;
    name->setProperty("text", QStringLiteral("Avant ciel"));
    click(visualItemNamed(history, QStringLiteral("snapshotCreate")));
    settleLayout();
    if (!check(editor.snapshots().size() == 1 && name->property("text").toString().isEmpty() &&
               editor.snapshots().at(0).toMap().value("name").toString() == QStringLiteral("Avant ciel"),
               "the create button takes a snapshot under its name")) return 1;
    const double saved = editor.adjustmentValue(QStringLiteral("contrast"));
    editor.setAdjustment(QStringLiteral("contrast"), int(saved) - 20);
    click(visualItemNamed(history, QStringLiteral("snapshotRestore")));
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == saved, "a snapshot's button restores it")) return 1;
    click(visualItemNamed(history, QStringLiteral("snapshotRemove")));
    settleLayout();
    if (!check(editor.snapshots().isEmpty(), "a snapshot's other button removes it")) return 1;

    // Optional capture for a visual review (IPR_UI_CAPTURE_DIRECTORY, as the other interface tests).
    const QString captureDirectory = qEnvironmentVariable("IPR_UI_CAPTURE_DIRECTORY");
    if (!captureDirectory.isEmpty()) {
        for (int step = 0; step < 12; ++step) editor.setAdjustment(QStringLiteral("clarity"), step + 1);
        editor.addSnapshot(QStringLiteral("Avant ciel"));
        editor.addSnapshot(QStringLiteral("Contraste fort"));
#ifdef Q_OS_WIN
        // The offscreen backend does not enumerate Windows fonts automatically.
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/segoeui.ttf");
#endif
        QGuiApplication::setFont(QFont(QStringLiteral("Segoe UI")));
        // The application's dark theme (Main.qml), which this panel inherits there.
        auto *colours = infoPanel->property("palette").value<QObject *>();
        for (const auto &[role, colour] : std::initializer_list<std::pair<const char *, const char *>>{
                 {"window", "#2a3039"}, {"windowText", "#edf2f8"}, {"text", "#edf2f8"}, {"buttonText", "#edf2f8"},
                 {"base", "#262b34"}, {"button", "#262b34"}, {"mid", "#4a5564"}})
            colours->setProperty(role, QColor(QString::fromLatin1(colour)));
        settleLayout(150);
        QQuickItem *currentRow = nullptr;
        for (QQuickItem *row : listContent->childItems())
            if (row->objectName() == "historyStepRow" && row->property("index").toInt() == editor.historyPosition())
                currentRow = row;
        const QPointF menuPoint = currentRow ? currentRow->mapToItem(nullptr, QPointF(30, 10)) : QPointF();
        mouse(window, QEvent::MouseButtonPress, menuPoint, timestamp, Qt::RightButton);
        mouse(window, QEvent::MouseButtonRelease, menuPoint, timestamp, Qt::RightButton);
        settleLayout(150);
        window.grabWindow().copy(320, 0, 300, 1100).save(QDir(captureDirectory).filePath(QStringLiteral("history-panel.png")));
        QMetaObject::invokeMethod(info->findChild<QObject *>(QStringLiteral("historyStepMenu")), "close");
        settleLayout(50);
    }

    // The menu actions and their shortcuts follow the engine.
    QQmlComponent actionsComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/ApplicationActions.qml")));
    QScopedPointer<QObject> actions(actionsComponent.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(&editor)}, {QStringLiteral("settings"), QVariant::fromValue(root.data())}}));
    if (!actions) {
        std::cerr << actionsComponent.errorString().toStdString();
        return 1;
    }
    QObject *undoAction = actions->findChild<QObject *>(QStringLiteral("undoAction"));
    QObject *redoAction = actions->findChild<QObject *>(QStringLiteral("redoAction"));
    if (!check(undoAction && redoAction, "the history actions exist")) return 1;
    const double current = editor.adjustmentValue(QStringLiteral("contrast"));
    editor.setAdjustment(QStringLiteral("contrast"), int(current) + 5);
    if (!check(undoAction->property("enabled").toBool(), "undo is offered when a step can be undone")) return 1;
    QMetaObject::invokeMethod(undoAction, "trigger");
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == current &&
               redoAction->property("enabled").toBool(), "the undo action undoes")) return 1;
    QMetaObject::invokeMethod(redoAction, "trigger");
    if (!check(editor.adjustmentValue(QStringLiteral("contrast")) == current + 5, "the redo action redoes")) return 1;

    std::cout << "history qml: ok\n";
    return 0;
}
