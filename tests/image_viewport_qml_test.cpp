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
#include <QEventLoop>
#include <QWheelEvent>
#include <cmath>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
void settle()
{
    QEventLoop loop;
    QTimer::singleShot(60, &loop, &QEventLoop::quit);
    loop.exec();
}
bool waitForImage(QObject *viewport)
{
    for (int i = 0; i < 50; ++i) {
        settle();
        if (viewport->property("imageReady").toBool()) return true;
    }
    return false;
}
bool waitForComparison(QObject *viewport)
{
    for (int i = 0; i < 50; ++i) {
        settle();
        if (viewport->property("comparisonReady").toBool()) return true;
    }
    return false;
}
double number(QObject *object, const char *name) { return object->property(name).toDouble(); }
bool near(double a, double b) { return std::abs(a-b) < 1e-5; }
QQuickItem *visualItem(QQuickItem *root, const QString &name)
{
    // Repeater delegates follow visual ownership, not necessarily QObject ownership.
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
void mouse(QQuickWindow &window, QEvent::Type type, QPointF pos, Qt::MouseButton button,
           Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, QPointF(window.mapToGlobal(pos.toPoint())), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();
}
void key(QQuickWindow &window, int code, Qt::KeyboardModifiers modifiers, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
    QCoreApplication::sendEvent(&window, &release);
    settle();
}
void click(QQuickWindow &window, QPointF pos)
{
    mouse(window, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
    mouse(window, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    QImage image(1600, 1000, QImage::Format_RGBA8888);
    image.fill(QColor("#58718a"));
    const QString first = directory.filePath("first.png");
    const QString updated = directory.filePath("updated.png");
    if (!image.save(first)) return 1;
    image.fill(QColor("#bd7642"));
    if (!image.save(updated)) return 1;
    const QString alphaOriginal = directory.filePath("alpha-original.png");
    const QString alphaCurrent = directory.filePath("alpha-current.png");
    image.fill(QColor(40, 80, 170, 128));
    if (!image.save(alphaOriginal)) return 1;
    image.fill(QColor(180, 90, 30, 128));
    if (!image.save(alphaCurrent)) return 1;
    QQmlEngine engine;
    // Destroy the scoped QML root before its window/rendering surface, including
    // early assertion returns. Visual parenting alone is not QObject ownership.
    QQuickWindow window;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/ImageViewport.qml")));
    QScopedPointer<QObject> object(component.createWithInitialProperties({
        {"width", 800}, {"height", 500}, {"hasImage", true}, {"imageKey", "photo-A"},
        {"source", QUrl::fromLocalFile(first)}, {"originalSource", QUrl::fromLocalFile(first)}}));
    if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
    auto *viewport = qobject_cast<QQuickItem *>(object.data());
    window.setColor(QColor("#20252b"));
    window.resize(800, 500);
    viewport->setParentItem(window.contentItem());
    window.show();
    window.requestActivate();
    if (!check(waitForImage(viewport), "viewport should load the synthetic preview")) return 1;
    auto *canvas = viewport->findChild<QQuickItem *>("viewportImage");
    if (!check(canvas && near(number(viewport, "zoomScale"), .5) && viewport->property("fitMode").toBool()
            && near(canvas->width(), 800) && near(canvas->height(), 500), "initial image should fit")) return 1;
    click(window, QPointF(400, 250));
    if (!check(near(number(viewport, "zoomScale"), 1), "click should zoom to actual preview size")) return 1;
    mouse(window, QEvent::MouseButtonPress, QPointF(400, 250), Qt::LeftButton, Qt::LeftButton);
    mouse(window, QEvent::MouseMove, QPointF(490, 310), Qt::NoButton, Qt::LeftButton);
    mouse(window, QEvent::MouseButtonRelease, QPointF(490, 310), Qt::LeftButton, Qt::NoButton);
    if (!check(near(number(viewport, "offsetX"), 90) && near(number(viewport, "offsetY"), 60)
            && near(number(viewport, "zoomScale"), 1), "drag should pan without toggling zoom on release")) return 1;
    const QPointF anchor(300, 200);
    const QPointF imagePoint((anchor.x()-canvas->x())/number(viewport, "zoomScale"),
                             (anchor.y()-canvas->y())/number(viewport, "zoomScale"));
    QWheelEvent wheel(anchor, QPointF(window.mapToGlobal(anchor.toPoint())), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &wheel);
    settle();
    if (!check(near(number(viewport, "zoomScale"), 1.25)
            && near((anchor.x()-canvas->x())/1.25, imagePoint.x())
            && near((anchor.y()-canvas->y())/1.25, imagePoint.y()), "Ctrl wheel should zoom around the mouse")) return 1;
    const double previousOffset = number(viewport, "offsetY");
    QWheelEvent plainWheel(anchor, QPointF(window.mapToGlobal(anchor.toPoint())), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &plainWheel);
    settle();
    if (!check(near(number(viewport, "zoomScale"), 1.25) && number(viewport, "offsetY") < previousOffset,
               "plain wheel should pan, not zoom")) return 1;
    const double preservedX = number(viewport, "offsetX");
    const double preservedY = number(viewport, "offsetY");
    viewport->setProperty("source", QUrl::fromLocalFile(updated));
    if (!check(waitForImage(viewport) && near(number(viewport, "zoomScale"), 1.25)
            && near(number(viewport, "offsetX"), preservedX) && near(number(viewport, "offsetY"), preservedY),
               "an adjustment preview refresh should preserve zoom and pan")) return 1;
    viewport->setProperty("imageKey", "photo-B");
    if (!check(viewport->property("fitMode").toBool() && near(number(viewport, "zoomScale"), .5)
            && near(number(viewport, "offsetX"), 0) && near(number(viewport, "offsetY"), 0),
               "changing photo should reset to fit")) return 1;
    viewport->forceActiveFocus();
    key(window, Qt::Key_Equal, Qt::ControlModifier, "=");
    if (!check(near(number(viewport, "zoomScale"), .625), "Ctrl = should zoom in")) return 1;
    key(window, Qt::Key_Minus, Qt::ControlModifier, "-");
    if (!check(near(number(viewport, "zoomScale"), .5), "Ctrl - should zoom out")) return 1;
    key(window, Qt::Key_Plus, Qt::ControlModifier | Qt::ShiftModifier, "+");
    if (!check(near(number(viewport, "zoomScale"), .625), "Ctrl Shift + should zoom in without ambiguous shortcuts")) return 1;
    key(window, Qt::Key_1, Qt::ControlModifier, "1");
    if (!check(near(number(viewport, "zoomScale"), 1), "Ctrl 1 should select actual preview size")) return 1;
    key(window, Qt::Key_0, Qt::ControlModifier, "0");
    if (!check(viewport->property("fitMode").toBool(), "Ctrl 0 should return to fit")) return 1;
    QObject *plus = visualItem(viewport, "zoomInButton");
    QObject *fit = visualItem(viewport, "fitImageButton");
    if (!check(plus && fit, "zoom toolbar should exist")) return 1;
    QMetaObject::invokeMethod(plus, "clicked");
    if (!check(near(number(viewport, "zoomScale"), .625), "zoom button should act on the same scale")) return 1;
    QObject *compare = visualItem(viewport, "compareImageButton");
    auto *originalCanvas = visualItem(viewport, "originalViewportImage");
    if (!check(compare && originalCanvas && waitForComparison(viewport)
            && compare->property("enabled").toBool(), "comparison should wait for a matching origin preview")) return 1;
    QMetaObject::invokeMethod(compare, "clicked");
    if (!check(viewport->property("showingComparison").toBool()
            && compare->property("checked").toBool(), "comparison button should show the split view")) return 1;
    const double beforeDragX = number(viewport, "offsetX");
    mouse(window, QEvent::MouseButtonPress, QPointF(400, 250), Qt::LeftButton, Qt::LeftButton);
    mouse(window, QEvent::MouseMove, QPointF(560, 250), Qt::NoButton, Qt::LeftButton);
    mouse(window, QEvent::MouseButtonRelease, QPointF(560, 250), Qt::LeftButton, Qt::NoButton);
    if (!check(near(number(viewport, "comparisonPosition"), .7)
            && near(number(viewport, "offsetX"), beforeDragX)
            && near(number(viewport, "zoomScale"), .625), "divider drag should change only the split")) return 1;
    const QPointF currentOrigin = canvas->mapToItem(viewport, QPointF());
    const QPointF beforeOrigin = originalCanvas->mapToItem(viewport, QPointF());
    if (!check(near(currentOrigin.x(), beforeOrigin.x()) && near(currentOrigin.y(), beforeOrigin.y())
            && near(canvas->width(), originalCanvas->width()) && near(canvas->height(), originalCanvas->height()),
               "comparison images should share zoom and pan geometry")) return 1;
    QWheelEvent dividerWheel(QPointF(560, 250), QPointF(window.mapToGlobal(QPoint(560, 250))),
        QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &dividerWheel);
    settle();
    if (!check(near(number(viewport, "zoomScale"), .78125), "Ctrl wheel should also zoom above the divider")) return 1;
    key(window, Qt::Key_0, Qt::ControlModifier, "0");
    settle();
    const QImage split = window.grabWindow();
    if (!check(!split.isNull() && split.pixelColor(200, 250) == QColor("#58718a")
            && split.pixelColor(700, 250) == QColor("#bd7642"), "split should paint origin left and current right")) return 1;
    viewport->forceActiveFocus();
    const double savedScale = number(viewport, "zoomScale");
    const double savedOffset = number(viewport, "offsetX");
    const double savedSplit = number(viewport, "comparisonPosition");
    QKeyEvent holdPress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
    QKeyEvent holdRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ");
    QCoreApplication::sendEvent(&window, &holdPress);
    settle();
    if (!check(viewport->property("showingOriginal").toBool() &&
               !viewport->property("showingComparison").toBool() &&
               window.grabWindow().pixelColor(700, 250) == QColor("#58718a"),
               "holding Space must show the original across the entire image")) return 1;
    QCoreApplication::sendEvent(&window, &holdRelease);
    settle();
    if (!check(!viewport->property("showingOriginal").toBool() &&
               viewport->property("showingComparison").toBool() &&
               near(savedScale, number(viewport, "zoomScale")) &&
               near(savedOffset, number(viewport, "offsetX")) &&
               near(savedSplit, number(viewport, "comparisonPosition")),
               "release must restore the split and preserve the image transform")) return 1;
    QCoreApplication::sendEvent(&window, &holdPress);
    auto *otherFocus = new QQuickItem(window.contentItem());
    otherFocus->forceActiveFocus();
    if (!check(!viewport->property("originalHeld").toBool(), "losing focus must release the original")) return 1;
    viewport->forceActiveFocus();
    viewport->setProperty("source", QUrl::fromLocalFile(alphaCurrent));
    viewport->setProperty("originalSource", QUrl::fromLocalFile(alphaOriginal));
    if (!check(waitForImage(viewport) && waitForComparison(viewport), "transparent comparison fixtures should load")) return 1;
    const QImage alphaSplit = window.grabWindow();
    viewport->setProperty("comparisonEnabled", false);
    settle();
    const QImage currentOnly = window.grabWindow();
    viewport->setProperty("comparisonEnabled", true);
    viewport->setProperty("comparisonPosition", 1);
    settle();
    const QImage originalOnly = window.grabWindow();
    if (!check(!alphaSplit.isNull() && !currentOnly.isNull() && !originalOnly.isNull()
            && alphaSplit.pixelColor(200, 250) == originalOnly.pixelColor(200, 250)
            && alphaSplit.pixelColor(700, 250) == currentOnly.pixelColor(700, 250)
            && originalOnly.pixelColor(200, 250) != currentOnly.pixelColor(200, 250),
               "transparent origin must not be blended over the current preview")) return 1;
    viewport->setProperty("comparisonPosition", .7);
    key(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier, "C");
    if (!check(!viewport->property("comparisonEnabled").toBool(), "Ctrl Shift C should toggle the comparison")) return 1;
    key(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier, "C");
    viewport->setProperty("source", QUrl::fromLocalFile(first));
    if (!check(waitForImage(viewport) && waitForComparison(viewport)
            && viewport->property("showingComparison").toBool()
            && near(number(viewport, "comparisonPosition"), .7), "preview updates should preserve the comparison")) return 1;
    QMetaObject::invokeMethod(viewport, "moveComparison", Q_ARG(QVariant, -500));
    if (!check(near(number(viewport, "comparisonPosition"), 0), "divider should not move left of the viewport")) return 1;
    QMetaObject::invokeMethod(viewport, "moveComparison", Q_ARG(QVariant, 9000));
    if (!check(near(number(viewport, "comparisonPosition"), 1), "divider should not move right of the viewport")) return 1;
    viewport->setProperty("imageKey", "photo-C");
    if (!check(!viewport->property("comparisonEnabled").toBool()
            && near(number(viewport, "comparisonPosition"), .5), "changing photo should reset the comparison")) return 1;
    QMetaObject::invokeMethod(viewport, "setZoom", Q_ARG(QVariant, 1000), Q_ARG(QVariant, 400), Q_ARG(QVariant, 250));
    if (!check(near(number(viewport, "zoomScale"), 8) && !plus->property("enabled").toBool(), "zoom should have an upper bound")) return 1;
    viewport->setProperty("offsetX", 1e9);
    viewport->setProperty("offsetY", -1e9);
    QMetaObject::invokeMethod(viewport, "clampPan");
    if (!check(near(number(viewport, "offsetX"), number(viewport, "maxOffsetX"))
            && near(number(viewport, "offsetY"), -number(viewport, "maxOffsetY")), "pan should stay within the image")) return 1;
    QMetaObject::invokeMethod(fit, "clicked");
    viewport->setWidth(400);
    viewport->setHeight(500);
    if (!check(near(number(viewport, "zoomScale"), .25) && near(canvas->y(), 125), "fit should react to window resizing and center letterboxes")) return 1;
    QMetaObject::invokeMethod(viewport, "toggleZoom", Q_ARG(QVariant, 100), Q_ARG(QVariant, 50));
    if (!check(viewport->property("fitMode").toBool(), "clicking a letterbox should not zoom")) return 1;
    QMetaObject::invokeMethod(viewport, "setZoom", Q_ARG(QVariant, 0.0001), Q_ARG(QVariant, 200), Q_ARG(QVariant, 250));
    if (!check(near(number(viewport, "zoomScale"), .05), "zoom should have a lower bound")) return 1;
    for (int narrowWidth : {280, 350, 500, 800}) {
        viewport->setWidth(narrowWidth);
        settle();
        auto *toolbar = viewport->findChild<QQuickItem *>("viewportToolbar");
        if (!check(toolbar && toolbar->x() >= 0 && toolbar->x() + toolbar->width() <= narrowWidth,
                   "zoom and comparison toolbar must stay inside narrow viewports")) return 1;
        for (const QString &name : {QStringLiteral("compareImageButton"), QStringLiteral("holdOriginalButton"),
                                   QStringLiteral("actualSizeButton")}) {
            auto *button = visualItem(viewport, name);
            const QPointF position = button->mapToItem(toolbar, QPointF());
            if (!check(position.x() >= 0 && position.y() >= 0 &&
                       position.x() + button->width() <= toolbar->width() + .1 &&
                       position.y() + button->height() <= toolbar->height() + .1,
                       "every toolbar action must remain visible when its grid wraps")) return 1;
        }
    }
    viewport->setProperty("hasImage", false);
    if (!check(!viewport->property("imageReady").toBool() && !plus->property("enabled").toBool()
            && viewport->property("fitMode").toBool(), "without an image, interactions should be disabled")) return 1;
    if (!check(!compare->property("enabled").toBool() && !viewport->property("showingComparison").toBool(),
               "comparison should be disabled without an image")) return 1;
    std::cout << "Viewport zoom, comparison, click, drag, wheel, shortcuts, bounds and refresh checks passed" << std::endl;
    return 0;
}
