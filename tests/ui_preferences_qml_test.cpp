#include "UiPreferences.h"
#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QGuiApplication>
#include <QFont>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
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
QQuickItem *visualItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}
void settleEditor(ImageEditor &editor)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    } while (editor.busy() && timer.elapsed() < 5000);
}
bool captureReview(QWindow *window, const QString &name)
{
    const QString directory = qEnvironmentVariable("IPR_UI_CAPTURE_DIRECTORY");
    if (directory.isEmpty()) return true;
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    if (!quickWindow || !QDir().mkpath(directory)) return false;
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
    return quickWindow->grabWindow().save(QDir(directory).filePath(name + ".png"));
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
    if (!qEnvironmentVariableIsEmpty("IPR_UI_CAPTURE_DIRECTORY"))
        app.setFont(QFont(QStringLiteral("Segoe UI")));
    qmlRegisterModule("IPR", 1, 0); // Local QML files provide the same component types.
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("ipr-ui.ini"));
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
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
        QObject *dialog = root->findChild<QObject *>(QStringLiteral("preferencesDialog"));
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
        QObject *tabs = dialog->findChild<QObject *>("preferencesTabs");
        const QStringList pages{"interfacePreferencesPage", "appearancePreferencesPage", "advancedRawPreferencesPage"};
        if (!check(tabs && tabs->property("count").toInt() == 3,
                   "preferences must provide three independent navigation tabs")) return 1;
        for (int index = 0; index < pages.size(); ++index) {
            tabs->setProperty("currentIndex", index);
            QCoreApplication::processEvents();
            auto *page = dialog->findChild<QQuickItem *>(pages[index]);
            if (!check(page && page->isVisible() && page->height() > 0 &&
                       page->property("contentWidth").toDouble() <= page->width() &&
                       page->findChild<QObject *>("preferenceScrollBar")->property("width").toDouble() == 6,
                       "each preferences page must fit and use a slim scrollbar")) return 1;
        }
        tabs->setProperty("currentIndex", 0);
        QObject *background = dialog->findChild<QObject *>("preference_canvasBackground");
        QObject *infoSize = dialog->findChild<QObject *>("preference_imageInfoFontSize");
        if (!check(infoSize, "preferences must expose information text sizing")) return 1;
        infoSize->setProperty("value", 16);
        QMetaObject::invokeMethod(infoSize, "valueModified");
        if (!check(info->property("informationFontSize").toInt() == 16 &&
                   preferences.layout().value("imageInfoFontSize").toInt() == 16,
                   "changing information text size must immediately update the panel and INI state")) return 1;
        qobject_cast<QQuickItem *>(infoSize)->forceActiveFocus();
        QKeyEvent increasePress(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QKeyEvent increaseRelease(QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &increasePress);
        QCoreApplication::sendEvent(window, &increaseRelease);
        if (!check(preferences.layout().value("imageInfoFontSize").toInt() == 17,
                   "the styled spinbox must keep real keyboard stepping connected to preferences")) return 1;
        QObject *sizeInput = infoSize->property("contentItem").value<QObject *>();
        qobject_cast<QQuickItem *>(sizeInput)->forceActiveFocus();
        sizeInput->setProperty("text", "18");
        QKeyEvent acceptPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QKeyEvent acceptRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &acceptPress);
        QCoreApplication::sendEvent(window, &acceptRelease);
        if (!check(preferences.layout().value("imageInfoFontSize").toInt() == 18,
                   "typed values must remain connected after restyling the spinbox")) return 1;
        preferences.setValue("layout/imageInfoFontSize", 16);
        if (!check(captureReview(window, "preferences-interface"), "interface capture should save")) return 1;
        tabs->setProperty("currentIndex", 1);
        QObject *calibration = dialog->findChild<QObject *>("rawNormalizationToggle");
        if (!check(background && calibration && !calibration->property("enabled").toBool(),
                   "preferences must expose canvas colours and disable calibration without a RAW")) return 1;
        QMetaObject::invokeMethod(background, "activated", Q_ARG(int, 1));
        if (!check(preferences.theme().value("canvas").value<QColor>() == QColor("#777777"),
                   "canvas preference must update the actual persisted theme")) return 1;
        QObject *paletteSelector = dialog->findChild<QObject *>("interfacePaletteSelector");
        if (!check(paletteSelector, "preferences must expose all five interface palettes")) return 1;
        QObject *canvasSurface = root->findChild<QObject *>("imageCanvasBackground");
        if (!check(canvasSurface && background->property("count").toInt() == 6,
                   "photo background choices must include blue and red")) return 1;
        const QStringList starts{"#1b2d45", "#42252e"};
        const QStringList ends{"#111c2b", "#291a20"};
        const QColor originalPanel = preferences.theme().value("panel").value<QColor>();
        for (int index = 0; index < 2; ++index) {
            QMetaObject::invokeMethod(background, "activated", Q_ARG(int, 4 + index));
            if (!check(preferences.theme().value("canvas").value<QColor>() == QColor(starts[index]) &&
                       canvasSurface->property("baseColor").value<QColor>() == QColor(starts[index]) &&
                       canvasSurface->property("endColor").value<QColor>() == QColor(ends[index]) &&
                       preferences.theme().value("panel").value<QColor>() == originalPanel,
                       "coloured photo backgrounds must use the matching interface shades without changing its palette")) return 1;
            preferences.sync();
            UiPreferences readback(path);
            if (!check(readback.theme().value("canvas").value<QColor>() == QColor(starts[index]),
                       "coloured photo backgrounds must persist in the existing INI")) return 1;
        }
        preferences.setValue("theme/canvas", "#777777");
        if (!check(canvasSurface->property("endColor").value<QColor>() == QColor("#777777"),
                   "neutral backgrounds must remain uniform after leaving a coloured gradient")) return 1;
        const QStringList panelColours{"#42252e", "#f4f0e6", "#1c1e21", "#1b2d45", "#2a3039"};
        for (int index = 0; index < panelColours.size(); ++index) {
            QMetaObject::invokeMethod(paletteSelector, "applyPalette", Q_ARG(QVariant, index));
            QCoreApplication::processEvents();
            const QColor expected(panelColours[index]);
            QObject *windowPalette = root->property("palette").value<QObject *>();
            QObject *popupPalette = dialog->property("palette").value<QObject *>();
            QObject *comboSurface = background->property("background").value<QObject *>();
            QObject *spinSurface = infoSize->property("background").value<QObject *>();
            QObject *closeButton = dialog->findChild<QObject *>("closePreferencesButton");
            QObject *closeText = closeButton->property("contentItem").value<QObject *>();
            if (!check(preferences.theme().value("panel").value<QColor>() == expected &&
                       paletteSelector->property("selectedIndex").toInt() == index &&
                       info->property("color").value<QColor>() == expected &&
                       windowPalette && windowPalette->property("windowText").value<QColor>() ==
                           preferences.theme().value("text").value<QColor>() &&
                       preferences.theme().value("canvas").value<QColor>() == QColor("#777777"),
                       "each palette must update panels and text without changing the independent photo canvas")) return 1;
            if (!check(popupPalette && popupPalette->property("window").value<QColor>() == expected &&
                       comboSurface->property("color").value<QColor>() == expected.darker(110) &&
                       spinSurface->property("color").value<QColor>() == expected.darker(110) &&
                       closeText->property("color").value<QColor>() == preferences.theme().value("text").value<QColor>(),
                       "preferences controls and close button must follow every palette, not platform white defaults")) return 1;
            QObject *choices = background->property("popup").value<QObject *>();
            QMetaObject::invokeMethod(choices, "open");
            QCoreApplication::processEvents();
            QObject *choicesSurface = choices->property("background").value<QObject *>();
            if (!check(choices->property("visible").toBool() && choicesSurface &&
                       choicesSurface->property("color").value<QColor>() == expected,
                       "the opened dropdown must follow the selected palette too")) return 1;
            QMetaObject::invokeMethod(choices, "close");
            for (const QString &name : {QStringLiteral("resetAllAdjustmentsButton"),
                                       QStringLiteral("exportDestinationButton"),
                                       QStringLiteral("openImageButton")}) {
                QObject *button = root->findChild<QObject *>(name);
                QObject *buttonSurface = button ? button->property("background").value<QObject *>() : nullptr;
                QObject *buttonText = button ? button->property("contentItem").value<QObject *>() : nullptr;
                if (!check(buttonSurface && buttonText &&
                           buttonSurface->property("color").value<QColor>() == expected.darker(110) &&
                           buttonText->property("color").value<QColor>() == preferences.theme().value("text").value<QColor>(),
                           "reset, export and opening buttons must follow every interface palette")) return 1;
            }
            preferences.sync();
            UiPreferences readback(path);
            if (!check(readback.theme().value("panel").value<QColor>() == expected &&
                       readback.layout().value("imageInfoFontSize").toInt() == 16,
                       "palette and information font size must survive an INI reload")) return 1;
            if ((index == 1 || index == 3) &&
                !check(captureReview(window, QStringLiteral("palette-%1").arg(index)),
                       "optional palette captures should save")) return 1;
        }
        tabs->setProperty("currentIndex", 2);
        editor.setRawNormalizationEnabled(true);
        if (!check(calibration->property("checked").toBool(),
                   "preferences calibration must reflect the current editor state")) return 1;
        editor.setRawNormalizationEnabled(false);
        if (!check(!calibration->property("checked").toBool(),
                   "preferences calibration must follow restored disabled state")) return 1;
        if (!check(captureReview(window, "preferences"), "optional preferences capture should save")) return 1;
        QMetaObject::invokeMethod(dialog, "close");

        QTemporaryDir photos;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(QColor("#6688aa"));
        const QString first = photos.filePath("01.png");
        const QString second = photos.filePath("02.png");
        if (!check(image.save(first) && image.save(second), "navigation fixtures should save")) return 1;
        editor.openImage(QUrl::fromLocalFile(first));
        settleEditor(editor);
        auto *firstStar = visualItem(qobject_cast<QQuickWindow *>(window)->contentItem(), "imageRatingStar0");
        auto *starGlyph = visualItem(qobject_cast<QQuickWindow *>(window)->contentItem(), "imageRatingGlyph0");
        if (!check(firstStar && starGlyph && firstStar->width() >= 23 &&
                   starGlyph->property("font").value<QFont>().pixelSize() == 18,
                   "rating stars must render at 18 px with a slightly enlarged hit area")) return 1;
        QMetaObject::invokeMethod(firstStar, "clicked");
        if (!check(editor.imageRating() == 1, "enlarged stars must still edit the actual image rating")) return 1;
        QMetaObject::invokeMethod(firstStar, "clicked");
        if (!check(editor.imageRating() == 0, "clicking the current rating must clear it")) return 1;
        QObject *navigation = root->findChild<QObject *>("collapsedFilmstripNavigation");
        if (!check(navigation && navigation->property("currentIndex").toInt() == 0 &&
                   navigation->property("previousName").toString().isEmpty() &&
                   navigation->property("nextName").toString() == "02.png",
                   "collapsed navigation must identify the next actual photo and respect the first boundary")) return 1;
        auto *toggle = root->findChild<QQuickItem *>("filmstripToggleButton");
        if (!check(toggle, "a single filmstrip toggle must exist")) return 1;
        const QPointF expandedTogglePosition = toggle->mapToItem(nullptr, QPointF(0, 0));
        if (!check(toggle->mapToItem(qobject_cast<QQuickItem *>(ribbon), QPointF(0, toggle->height())).y() <= 0,
                   "the filmstrip toggle must sit above the expanded ribbon")) return 1;
        QMetaObject::invokeMethod(toggle, "clicked");
        QCoreApplication::processEvents();
        if (!check(!preferences.layout().value("showFilmstrip").toBool() &&
                   navigation->property("visible").toBool(),
                   "collapsing the ribbon must persist its visibility and expose navigation")) return 1;
        root->findChild<QObject *>("rawDevelopmentSection")->setProperty("expanded", false);
        root->findChild<QObject *>("lightSection")->setProperty("expanded", false);
        root->findChild<QObject *>("colorSection")->setProperty("expanded", true);
        root->findChild<QObject *>("mixerSection")->setProperty("expanded", true);
        if (!check(captureReview(window, "collapsed-filmstrip"), "optional main window capture should save")) return 1;
        auto *overlay = root->findChild<QQuickItem *>("imageStatusOverlay");
        auto *navigationItem = qobject_cast<QQuickItem *>(navigation);
        const QPointF overlayEnd = overlay->mapToItem(nullptr, QPointF(overlay->width(), 0));
        const QPointF navigationStart = navigationItem->mapToItem(nullptr, QPointF(0, 0));
        if (!check(overlayEnd.x() <= navigationStart.x(),
                   "image information must not overlap collapsed navigation arrows")) return 1;
        QMetaObject::invokeMethod(navigation, "navigate", Q_ARG(QVariant, 1));
        settleEditor(editor);
        if (!check(editor.imageDetails().value("name").toString() == "02.png" &&
                   navigation->property("currentIndex").toInt() == 1 &&
                   navigation->property("nextName").toString().isEmpty() &&
                   navigation->property("previousName").toString() == "01.png",
                   "next arrow must open its announced image and stop at the final boundary")) return 1;
        QMetaObject::invokeMethod(navigation, "navigate", Q_ARG(QVariant, -1));
        settleEditor(editor);
        if (!check(editor.imageDetails().value("name").toString() == "01.png",
                   "previous arrow must return to the announced photo")) return 1;
        const QPointF collapsedTogglePosition = toggle->mapToItem(nullptr, QPointF(0, 0));
        if (!check(collapsedTogglePosition.x() == expandedTogglePosition.x() &&
                   collapsedTogglePosition.y() > expandedTogglePosition.y(),
                   "the toggle must remain centred and return to the bottom when the ribbon is hidden")) return 1;
        QMetaObject::invokeMethod(toggle, "clicked");
        if (!check(preferences.layout().value("showFilmstrip").toBool(),
                   "the collapsed tab must restore the ribbon")) return 1;
        window->close();
    }
    return 0;
}
