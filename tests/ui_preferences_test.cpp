#include "UiPreferences.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("nested/ipr-ui.ini"));
    {
        UiPreferences preferences(path);
        if (!check(preferences.writable() && QFile::exists(path) &&
                   preferences.value("window/normalWidth").toInt() == 1440 &&
                   preferences.layout().value("imageInfoFontSize").toInt() == 12 &&
                   preferences.sectionExpanded("light") && !preferences.sectionExpanded("denoise") &&
                   preferences.theme().value("background").value<QColor>() == QColor("#20242b") &&
                   preferences.value("future/aiEnabled").toString() == "false",
                   "embedded defaults should materialize a complete, readable INI file")) return 1;
        if (!check(preferences.setSectionExpanded("denoise", true) &&
                   preferences.setValue("theme/background", "#123456") &&
                   preferences.setValue("export/format", "tiff") &&
                   preferences.setValue("export/quality", 81) &&
                   preferences.setValue("selection/mixerBand", 1) &&
                   preferences.setValue("window/normalX", -1500),
                   "valid preferences including negative monitor coordinates should be accepted")) return 1;
        if (!check(preferences.setValue("layout/imageInfoFontSize", 16) &&
                   !preferences.setValue("layout/imageInfoFontSize", 9) &&
                   !preferences.setValue("layout/imageInfoFontSize", 21) &&
                   !preferences.setValue("layout/imageInfoFontSize", "huge"),
                   "information font size must be validated within 10 to 20 px")) return 1;
        if (!check(!preferences.setValue("window/normalWidth", -1) &&
                   !preferences.setValue("sections/light", "sometimes") &&
                   !preferences.setValue("theme/background", "#00000000") &&
                   !preferences.setValue("theme/panel", "not-a-color") &&
                   !preferences.setValue("export/quality", 101) &&
                   !preferences.setValue("selection/mixerBand", 8) &&
                   !preferences.setValue("unknown/code", "anything") &&
                   !preferences.setValue("meta/schemaVersion", 2),
                   "invalid or unrecognized keys should not mutate preferences")) return 1;
        // Destructor flushes pending changes even before the debounce fires.
    }
    {
        UiPreferences restored(path);
        if (!check(restored.sectionExpanded("denoise") &&
                   restored.layout().value("imageInfoFontSize").toInt() == 16 &&
                   restored.theme().value("background").value<QColor>() == QColor("#123456") &&
                   restored.value("export/format").toString() == "TIFF" &&
                   restored.value("export/quality").toInt() == 81 &&
                   restored.value("selection/mixerBand").toInt() == 1,
                   "preferences should roundtrip across service instances")) return 1;
    }
    {
        QSettings manuallyEdited(path, QSettings::IniFormat);
        manuallyEdited.setValue("custom/keepMe", "unknown extension");
        manuallyEdited.setValue("window/normalWidth", "invalid");
        manuallyEdited.setValue("theme/panel", "not-a-color");
        manuallyEdited.setValue("sections/light", "sometimes");
        manuallyEdited.sync();
    }
    {
        UiPreferences repaired(path);
        if (!check(repaired.value("window/normalWidth").toInt() == 1440 &&
                   repaired.theme().value("panel").value<QColor>() == QColor("#2a3039") &&
                   repaired.sectionExpanded("light"),
                   "bad known values should fall back to defaults")) return 1;
        repaired.setValue("export/quality", 80);
        repaired.sync();
        QSettings readback(path, QSettings::IniFormat);
        if (!check(readback.value("custom/keepMe").toString() == "unknown extension",
                   "unknown keys should survive subsequent saves")) return 1;
    }
    const QString newerPath = directory.filePath(QStringLiteral("newer.ini"));
    {
        QSettings newer(newerPath, QSettings::IniFormat);
        newer.setValue("meta/schemaVersion", 3);
        newer.setValue("theme/background", "#987654");
        newer.sync();
    }
    QFile before(newerPath);
    before.open(QIODevice::ReadOnly);
    const QByteArray originalBytes = before.readAll();
    before.close();
    {
        UiPreferences newer(newerPath);
        if (!check(!newer.writable() && !newer.setValue("export/quality", 50) &&
                   newer.theme().value("background").value<QColor>() == QColor("#987654"),
                   "a future schema should be readable but never rewritten")) return 1;
    }
    before.open(QIODevice::ReadOnly);
    if (!check(before.readAll() == originalBytes, "future schema file should remain byte-identical")) return 1;

    // Existing schema 1 selected the last (magenta) circle at index 6.
    for (bool explicitVersion : {false, true}) {
        const QString legacyPath = directory.filePath(explicitVersion ? "legacy.ini" : "unversioned.ini");
        {
            QSettings legacy(legacyPath, QSettings::IniFormat);
            if (explicitVersion) legacy.setValue("meta/schemaVersion", 1);
            legacy.setValue("selection/mixerBand", 6);
            legacy.setValue("custom/keepMe", "preserve");
            legacy.sync();
        }
        {
            UiPreferences migrated(legacyPath);
            if (!check(migrated.value("selection/mixerBand").toInt() == 7 &&
                       migrated.value("meta/schemaVersion").toInt() == 2 &&
                       migrated.setValue("selection/mixerBand", 6),
                       "schema 1 magenta must migrate to 7; schema 2 must allow violet at 6")) return 1;
        }
        UiPreferences again(legacyPath);
        QSettings stored(legacyPath, QSettings::IniFormat);
        if (!check(again.value("selection/mixerBand").toInt() == 6 &&
                   stored.value("custom/keepMe").toString() == "preserve",
                   "migration must be idempotent and retain unknown keys")) return 1;
    }

    const QRect screen(-1920, 40, 1920, 1040);
    const QRect visible = UiPreferences::fitGeometry(QRect(-1600, 100, 1200, 800), screen, QSize(1000, 650), true);
    const QRect disconnected = UiPreferences::fitGeometry(QRect(9000, -5000, 1440, 900), screen, QSize(1000, 650), true);
    const QRect oversized = UiPreferences::fitGeometry(QRect(-1920, 0, 7000, 4000), screen, QSize(1000, 650), true);
    if (!check(visible == QRect(-1600, 100, 1200, 800) &&
               screen.contains(disconnected) && screen.contains(oversized),
               "geometry should preserve valid placement and recover after monitor removal")) return 1;

    const QString windowPath = directory.filePath(QStringLiteral("window.ini"));
    UiPreferences windowPreferences(windowPath);
    QWindow window;
    windowPreferences.restoreWindow(&window);
    window.setGeometry(40, 60, 1200, 760);
    app.processEvents();
    windowPreferences.saveWindow(&window);
    const int normalWidth = windowPreferences.value("window/normalWidth").toInt();
    const int normalHeight = windowPreferences.value("window/normalHeight").toInt();
    if (!check(normalWidth == 1200 && normalHeight == 760 &&
               windowPreferences.value("window/positionSaved").toBool(),
               "window moves and resizes should persist normal geometry")) return 1;
    window.setWindowState(Qt::WindowMaximized);
    window.resize(1800, 1000);
    windowPreferences.saveWindow(&window);
    if (!check(windowPreferences.value("window/state").toString() == "maximized" &&
               windowPreferences.value("window/normalWidth").toInt() == normalWidth &&
               windowPreferences.value("window/normalHeight").toInt() == normalHeight,
               "maximizing should retain independent normal geometry")) return 1;
    window.setWindowState(Qt::WindowMinimized);
    windowPreferences.saveWindow(&window);
    if (!check(windowPreferences.value("window/state").toString() == "maximized",
               "minimizing should not become the next launch state")) return 1;
    return 0;
}
