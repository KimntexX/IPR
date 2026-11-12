// The curve graph under the mouse: points are grabbed where they are drawn, end
// points included, and a drag inside a scrolling panel stays with the curve.
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QEventLoop>
#include <QTimer>

#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

void settle()
{
    QEventLoop loop;
    QTimer::singleShot(30, &loop, &QEventLoop::quit);
    loop.exec();
}

void mouse(QQuickWindow &window, QEvent::Type type, QPointF pos, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, pos, QPointF(window.mapToGlobal(pos.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
    QCoreApplication::processEvents();
}

QPointF pointOnScreen(QQuickItem *canvas, double x, double y)
{
    QVariant px, py;
    const QVariantMap point{{"x", x}, {"y", y}};
    QMetaObject::invokeMethod(canvas, "curveX", Q_RETURN_ARG(QVariant, px), Q_ARG(QVariant, point));
    QMetaObject::invokeMethod(canvas, "curveY", Q_RETURN_ARG(QVariant, py), Q_ARG(QVariant, point));
    return canvas->mapToScene(QPointF(px.toDouble(), py.toDouble()));
}

// Drags from one place to another in small steps, as a hand would.
void drag(QQuickWindow &window, QPointF from, QPointF to)
{
    mouse(window, QEvent::MouseButtonPress, from, Qt::LeftButton);
    for (int step = 1; step <= 12; ++step)
        mouse(window, QEvent::MouseMove, from + (to - from) * (step / 12.0), Qt::LeftButton);
    mouse(window, QEvent::MouseButtonRelease, to, Qt::NoButton);
    settle();
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArray(R"(
import QtQuick
import "file:///)" IPR_QML_DIRECTORY R"("
Flickable {
    width: 300; height: 500
    contentHeight: 1500
    property var commits: 0
    QtObject { id: fakeEditor; property bool hasImage: true }
    ToneCurveModule {
        id: module
        objectName: "module"
        width: 300
        editor: fakeEditor
        activeCurve: 0
        curvePoints: [{"x": 0, "y": 0}, {"x": 1, "y": 1}]
        curveName: function(channel) { return "RVB" }
        curveColor: function(channel) { return "#ffffff" }
        evaluateCurve: function(points, sample) { return sample }
        onCurvePointsEdited: function(points) { curvePoints = points }
        onCommitRequested: parent.parent.commits++
    }
}
)"), QUrl());
    QScopedPointer<QObject> object(component.create());
    if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
    QQuickWindow window;
    window.resize(300, 500);
    auto *root = qobject_cast<QQuickItem *>(object.data());
    root->setParentItem(window.contentItem());
    window.show();
    settle();
    auto *module = root->findChild<QQuickItem *>("module");
    auto *canvas = root->findChild<QQuickItem *>("toneCurveCanvas");
    if (!check(module && canvas && canvas->height() > canvas->width(), "the graph is taller than wide")) return 1;

    // The black point is grabbed where it is drawn and raised, without scrolling the panel.
    drag(window, pointOnScreen(canvas, 0, 0), pointOnScreen(canvas, 0, 0.3));
    QVariantList points = module->property("curvePoints").toList();
    bool ok = check(points.size() == 2 && std::abs(points.at(0).toMap().value("y").toDouble() - 0.3) < 0.02 &&
                    points.at(0).toMap().value("x").toDouble() < 0.02 && root->property("contentY").toDouble() == 0.0,
                    "the black point is raised by a vertical drag, which the scrolling panel does not steal");
    // The white point moves left: clipped whites.
    drag(window, pointOnScreen(canvas, 1, 1), pointOnScreen(canvas, 0.8, 1));
    points = module->property("curvePoints").toList();
    ok &= check(std::abs(points.at(1).toMap().value("x").toDouble() - 0.8) < 0.02,
                "the white point moves left");
    ok &= check(root->property("commits").toInt() == 2, "each drag commits once");
    if (ok) std::cout << "toneCurveQmlTests passed\n";
    return ok ? 0 : 1;
}
