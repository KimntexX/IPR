#include "UiPreferences.h"
#include "imageeditor.h"
#include "imageprovider.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QUrl>
#include <QWindow>
#include <QtQml>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
QObject *withProperty(QObject *root, const char *property)
{
    for (QObject *child : root->findChildren<QObject *>())
        if (child->property(property).isValid()) return child;
    return nullptr;
}
QObject *qualityControl(QObject *root)
{
    for (QObject *child : root->findChildren<QObject *>())
        if (child->property("label").toString() == QStringLiteral("Qualité JPEG")) return child;
    return nullptr;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    qmlRegisterModule("IPR", 1, 0); // Local QML files provide the same component types.
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("ipr-ui.ini"));
    ImageProvider provider;
    ImageEditor editor(&provider);
    QQmlEngine engine;
    QQmlComponent component(&engine,
        QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/Setting.qml")));
    {
        UiPreferences preferences(path);
        preferences.setSectionExpanded("light", false);
        preferences.setSectionExpanded("denoise", true);
        preferences.setSectionExpanded("export", true);
        preferences.setValue("selection/toneCurveChannel", 3);
        preferences.setValue("selection/mixerBand", 7);
        preferences.setValue("selection/gradingZone", 2);
        preferences.setValue("export/format", "TIFF");
        preferences.setValue("export/quality", 83);
        QScopedPointer<QObject> root(component.createWithInitialProperties({
            {QStringLiteral("editor"), QVariant::fromValue(&editor)},
            {QStringLiteral("preferences"), QVariant::fromValue(&preferences)}}));
        if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
        QObject *light = root->findChild<QObject *>(QStringLiteral("lightSection"));
        QObject *noise = root->findChild<QObject *>(QStringLiteral("denoiseSection"));
        QObject *exportGroup = root->findChild<QObject *>(QStringLiteral("exportSection"));
        QObject *format = root->findChild<QObject *>(QStringLiteral("exportFormat"));
        QObject *quality = qualityControl(root.data());
        QObject *mixer = withProperty(root.data(), "band");
        QObject *grading = withProperty(root.data(), "zone");
        if (!check(light && noise && exportGroup && format && quality && mixer && grading,
                   "settings should expose all persisted controls")) return 1;
        if (!check(!light->property("expanded").toBool() && noise->property("expanded").toBool() &&
                   exportGroup->property("expanded").toBool() &&
                   root->property("activeCurve").toInt() == 3 &&
                   mixer->property("band").toInt() == 7 && grading->property("zone").toInt() == 2 &&
                   format->property("currentText").toString() == "TIFF" &&
                   quality->property("value").toInt() == 83,
                   "stored state should override QML defaults without startup writes")) return 1;
        light->setProperty("expanded", true);
        noise->setProperty("expanded", false);
        format->setProperty("currentIndex", 1); // PNG
        quality->setProperty("value", 76);
        root->setProperty("activeCurve", 2);
        QMetaObject::invokeMethod(mixer, "selectBand", Q_ARG(QVariant, 1));
        QMetaObject::invokeMethod(grading, "selectZone", Q_ARG(QVariant, 1));
        preferences.setValue("theme/text", "#abcdef");
        if (!check(preferences.sectionExpanded("light") && !preferences.sectionExpanded("denoise") &&
                   preferences.value("export/format").toString() == "PNG" &&
                   preferences.value("export/quality").toInt() == 76 &&
                   preferences.value("selection/mixerBand").toInt() == 1 &&
                   preferences.value("selection/gradingZone").toInt() == 1 &&
                   root->property("textColor").value<QColor>() == QColor("#abcdef") &&
                   editor.colorMixerValue(1, 0) == 0 && editor.colorMixerValue(1, 1) == 0,
                   "UI interactions should update global preferences without changing photo adjustments")) return 1;
        preferences.sync();
    }
    {
        UiPreferences restored(path);
        QScopedPointer<QObject> root(component.createWithInitialProperties({
            {QStringLiteral("editor"), QVariant::fromValue(&editor)},
            {QStringLiteral("preferences"), QVariant::fromValue(&restored)}}));
        if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
        if (!check(root->findChild<QObject *>(QStringLiteral("lightSection"))->property("expanded").toBool() &&
                   !root->findChild<QObject *>(QStringLiteral("denoiseSection"))->property("expanded").toBool() &&
                   root->property("activeCurve").toInt() == 2 &&
                   root->findChild<QObject *>(QStringLiteral("exportFormat"))->property("currentText").toString() == "PNG" &&
                   qualityControl(root.data())->property("value").toInt() == 76 &&
                   withProperty(root.data(), "band")->property("band").toInt() == 1,
                   "a recreated settings panel should restore the last UI state")) return 1;
    }
    {
        // Exercise the actual window/context wiring without touching user preferences.
        UiPreferences preferences(path);
        preferences.setValue("layout/showImageInfo", false);
        preferences.setValue("layout/showHistogram", false);
        preferences.setValue("layout/showFilmstrip", false);
        preferences.setValue("theme/background", "#112233");
        engine.rootContext()->setContextProperty("imageEditor", &editor);
        engine.rootContext()->setContextProperty("uiPreferences", &preferences);
        QQmlComponent mainComponent(&engine, QUrl(QStringLiteral("qrc:/IPR/Main.qml")));
        QScopedPointer<QObject> root(mainComponent.create());
        if (!root) { std::cerr << mainComponent.errorString().toStdString(); return 1; }
        auto *window = qobject_cast<QWindow *>(root.data());
        QObject *info = root->findChild<QObject *>(QStringLiteral("imageInfoPanel"));
        QObject *ribbon = root->findChild<QObject *>(QStringLiteral("thumbnailRibbon"));
        QObject *histogram = root->findChild<QObject *>(QStringLiteral("histogramModule"));
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("preferencesInfoDialog"));
        if (!check(window && window->isVisible() && info && ribbon && histogram && dialog &&
                   !info->property("visible").toBool() && !ribbon->property("visible").toBool() &&
                   !histogram->property("visible").toBool() &&
                   root->property("color").value<QColor>() == QColor("#112233"),
                   "Main should restore the window, theme and hidden panels")) return 1;
        preferences.setValue("layout/showImageInfo", true);
        preferences.setValue("layout/showHistogram", true);
        preferences.setValue("layout/showFilmstrip", true);
        QMetaObject::invokeMethod(dialog, "open");
        QCoreApplication::processEvents();
        if (!check(info->property("visible").toBool() && ribbon->property("visible").toBool() &&
                   histogram->property("visible").toBool() && dialog->property("visible").toBool(),
                   "panel visibility and preferences dialog should react at runtime")) return 1;
        QMetaObject::invokeMethod(dialog, "close");
        window->close();
    }
    return 0;
}
