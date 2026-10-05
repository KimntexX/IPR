#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QUrl>
#include <QVariant>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
ColorMixerModule {
    width: 300
    editor: QtObject {
        objectName: "mockEditor"
        property bool hasImage: true
        property int commits: 0
        property var values: [[0,0,0], [0,0,0], [0,0,0], [0,0,0],
                              [0,0,0], [0,0,0], [0,0,0], [0,0,0]]
        function colorMixerValue(band, component) { return values[band][component] }
        function setColorMixer(band, hue, saturation, luminance) {
            values[band] = [hue, saturation, luminance]
            commits++
        }
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/mixer-test.qml")));
    QScopedPointer<QObject> root(component.create());
    if (!root) {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    QObject *mock = root->findChild<QObject *>(QStringLiteral("mockEditor"));
    QObject *hue = nullptr;
    QObject *saturation = nullptr;
    QObject *luminance = nullptr;
    for (QObject *child : root->findChildren<QObject *>()) {
        if (!child->property("minimumValue").isValid())
            continue;
        const QString label = child->property("label").toString();
        if (label == QStringLiteral("Teinte")) hue = child;
        if (label.startsWith(QStringLiteral("Saturation"))) saturation = child;
        if (label.startsWith(QStringLiteral("Luminance"))) luminance = child;
    }
    if (!check(mock && hue && saturation && luminance, "mixer should create its controls"))
        return 1;
    const auto select = [&](int band) {
        return QMetaObject::invokeMethod(root.data(), "selectBand", Q_ARG(QVariant, band));
    };
    const auto value = [&](int band, int channel) {
        QVariant result;
        QMetaObject::invokeMethod(mock, "colorMixerValue", Q_RETURN_ARG(QVariant, result),
                                 Q_ARG(QVariant, band), Q_ARG(QVariant, channel));
        return result.toInt();
    };
    hue->setProperty("value", 32);
    saturation->setProperty("value", -45);
    luminance->setProperty("value", 18);
    if (!check(value(0, 0) == 32 && value(0, 1) == -45 && value(0, 2) == 18,
               "edited controls should update the selected band"))
        return 1;
    int commits = mock->property("commits").toInt();
    if (!check(select(1) && hue->property("value").toInt() == 0 &&
               saturation->property("value").toInt() == 0 &&
               luminance->property("value").toInt() == 0 &&
               mock->property("commits").toInt() == commits,
               "switching to orange should reload without copying red adjustments"))
        return 1;
    hue->setProperty("value", -17);
    commits = mock->property("commits").toInt();
    if (!check(value(1, 0) == -17 && value(1, 1) == 0 && value(1, 2) == 0 &&
               select(0) && hue->property("value").toInt() == 32 &&
               saturation->property("value").toInt() == -45 &&
               luminance->property("value").toInt() == 18 &&
               mock->property("commits").toInt() == commits,
               "switching back should preserve both bands without a spurious commit"))
        return 1;
    for (int band = 2; band < 8; ++band) {
        if (!check(select(band) && value(band, 0) == 0 && value(band, 1) == 0 &&
                   value(band, 2) == 0 && mock->property("commits").toInt() == commits,
                   "switching through all eight bands should not edit them"))
            return 1;
    }
    select(6);
    hue->setProperty("value", 55);
    select(7);
    hue->setProperty("value", -28);
    if (!check(value(6, 0) == 55 && value(7, 0) == -28 && select(6) &&
               hue->property("value").toInt() == 55,
               "violet and magenta should expose independent editable controls")) return 1;
    select(8);
    if (!check(root->property("band").toInt() == 6, "an invalid band must not break selection")) return 1;
    return 0;
}
