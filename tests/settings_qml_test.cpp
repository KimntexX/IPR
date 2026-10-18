#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QEventLoop>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cmath>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

void settleLayout()
{
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
}

QObject *controlWithLabel(QObject *root, const QString &label)
{
    for (QObject *child : root->findChildren<QObject *>()) {
        if (child->property("minimumValue").isValid() &&
            child->property("label").toString() == label)
            return child;
    }
    return nullptr;
}

QQuickItem *visualItemNamed(QQuickItem *root, const QString &name)
{
    // Repeater delegates are not necessarily QObject children of their visual
    // parent; inspect the actual scene graph for visibility/geometry tests.
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = visualItemNamed(child, name))
            return found;
    return nullptr;
}

bool awaitLoad(ImageEditor &editor, const QUrl &url)
{
    QEventLoop loop;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::busyChanged, &loop, [&] {
            if (!editor.busy() && editor.hasImage())
                loop.quit();
        });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    editor.loadImage(url);
    if (editor.busy() || !editor.hasImage())
        loop.exec();
    QObject::disconnect(connection);
    return !editor.busy() && editor.hasImage();
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QQmlEngine engine;
    QQmlComponent component(&engine,
        QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/Setting.qml")));
    QScopedPointer<QObject> root(component.createWithInitialProperties(
        {{QStringLiteral("editor"), QVariant::fromValue(&editor)}}));
    if (!root) {
        std::cerr << component.errorString().toStdString();
        return 1;
    }
    auto *panel = qobject_cast<QQuickItem *>(root.data());
    if (!check(panel != nullptr, "settings should instantiate as a visual item"))
        return 1;
    QQuickWindow window;
    window.setColor(QColor(QStringLiteral("#2a3039")));
    window.resize(320, 720);
    panel->setParentItem(window.contentItem());
    panel->setWidth(320);
    panel->setHeight(720);
    window.show();
    settleLayout();

    const QStringList names{
        "rawDevelopmentSection", "opticsSection", "lightSection", "curveSection", "colorSection", "mixerSection",
        "gradingSection", "effectsSection", "denoiseSection", "sharpeningSection",
        "exportSection"};
    for (const QString &name : names) {
        auto *section = root->findChild<QQuickItem *>(name);
        if (!check(section != nullptr, "all eleven independent settings sections should exist"))
            return 1;
        auto *body = section->property("body").value<QQuickItem *>();
        auto *header = section->findChild<QQuickItem *>(QStringLiteral("sectionToggle"));
        if (!check(body && header && header->parentItem() == section &&
                   body->parentItem() == section,
                   "the section header must not be inside its collapsible body"))
            return 1;
        const bool initiallyOpen = name == QStringLiteral("lightSection") || name == QStringLiteral("rawDevelopmentSection");
        if (!check(section->property("expanded").toBool() == initiallyOpen &&
                   body->isVisible() == initiallyOpen,
                   "RAW development and Light should start expanded"))
            return 1;
        if (!initiallyOpen)
            QMetaObject::invokeMethod(header, "clicked");
        settleLayout();
        const qreal expandedHeight = section->height();
        if (!check(body->isVisible() && expandedHeight > header->height(),
                   "clicking a section header should expose its controls"))
            return 1;
        QMetaObject::invokeMethod(header, "clicked");
        settleLayout();
        if (!check(!body->isVisible() && section->height() < expandedHeight,
                   "collapsing a section should reclaim vertical space"))
            return 1;
    }

    if (!check(root->findChild<QObject *>("cameraProfileSelector") &&
               root->findChild<QObject *>("rawNormalizationToggle") &&
               root->findChild<QObject *>("rawDevelopmentStatusLabel") &&
               root->findChild<QObject *>("rawNormalizationManifestButton") &&
               root->findChild<QObject *>("lensCorrectionToggle") &&
               root->findChild<QObject *>("chromaticAberrationToggle") &&
               root->findChild<QObject *>("opticalDevelopmentStatusLabel"),
               "RAW module must expose profile, opt-in, manifest and actual status")) return 1;
    auto *optics = root->findChild<QObject *>("opticsSection");
    auto *rawSection = root->findChild<QObject *>("rawDevelopmentSection");
    if (!check(optics->findChild<QObject *>("lensCorrectionToggle") &&
               optics->findChild<QObject *>("chromaticAberrationToggle") &&
               !rawSection->findChild<QObject *>("lensCorrectionToggle"),
               "optical corrections must belong exclusively to their independent section")) return 1;
    auto *calibrationToggle=root->findChild<QObject *>("rawNormalizationToggle");
    editor.setRawNormalizationEnabled(true);settleLayout();
    if(!check(editor.rawNormalizationEnabled()&&calibrationToggle->property("checked").toBool(),
              "calibration checkbox follows enabled backend state"))return 1;
    editor.setRawNormalizationEnabled(false);settleLayout();
    if(!check(!editor.rawNormalizationEnabled()&&!calibrationToggle->property("checked").toBool(),
              "calibration checkbox follows disabled/restored backend state"))return 1;
    QObject *noiseSection = root->findChild<QObject *>(QStringLiteral("denoiseSection"));
    QObject *sharpSection = root->findChild<QObject *>(QStringLiteral("sharpeningSection"));
    QObject *exportSection = root->findChild<QObject *>(QStringLiteral("exportSection"));
    auto *exportHeader = exportSection->findChild<QQuickItem *>(QStringLiteral("sectionToggle"));
    exportHeader->forceActiveFocus();
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &spacePress);
    QCoreApplication::sendEvent(&window, &spaceRelease);
    settleLayout();
    if (!check(exportSection->property("expanded").toBool(),
               "a focused section header should expand using the Space key"))
        return 1;
    exportSection->setProperty("expanded", false);
    QObject *noise = controlWithLabel(noiseSection, QStringLiteral("Bruit de luminance"));
    QObject *sharp = controlWithLabel(sharpSection, QStringLiteral("Gain de netteté"));
    QObject *radius = controlWithLabel(sharpSection, QStringLiteral("Rayon de netteté"));
    QObject *quality = controlWithLabel(exportSection, QStringLiteral("Qualité JPEG"));
    QObject *format = exportSection->findChild<QObject *>(QStringLiteral("exportFormat"));
    if (!check(noise && sharp && radius && quality && format &&
               !controlWithLabel(exportSection, QStringLiteral("Bruit de luminance")) &&
               !controlWithLabel(exportSection, QStringLiteral("Gain de netteté")),
               "export should contain only export controls, not noise or sharpening"))
        return 1;
    if (!check(radius->property("value").toDouble() == 1.0,
               "the initial sharpening radius should match the engine default"))
        return 1;

    QTemporaryDir directory;
    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(QColor(100, 140, 180));
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString first = directory.filePath(QStringLiteral("first.png"));
    const QString second = directory.filePath(QStringLiteral("second.png"));
    if (!check(directory.isValid() && source.save(first) && source.save(second) &&
               awaitLoad(editor, QUrl::fromLocalFile(first)),
               "test source should load in the editor"))
        return 1;
    noiseSection->setProperty("expanded", true);
    sharpSection->setProperty("expanded", true);
    exportSection->setProperty("expanded", true);
    noise->setProperty("value", 43);
    sharp->setProperty("value", 51);
    radius->setProperty("value", 1.7);
    quality->setProperty("value", 84);
    format->setProperty("currentIndex", 1); // PNG
    for (QObject *section : {noiseSection, sharpSection, exportSection}) {
        section->setProperty("expanded", false);
        section->setProperty("expanded", true);
    }
    if (!check(controlWithLabel(noiseSection, QStringLiteral("Bruit de luminance")) == noise &&
               noise->property("value").toInt() == 43 && sharp->property("value").toInt() == 51 &&
               std::abs(radius->property("value").toDouble() - 1.7) < 0.001 &&
               quality->property("value").toInt() == 84 &&
               format->property("currentIndex").toInt() == 1 &&
               !quality->property("visible").toBool() &&
               editor.adjustmentValue(QStringLiteral("denoiseLuminance")) == 43,
               "folding sections should retain controls, adjustments and export options"))
        return 1;
    format->setProperty("currentIndex", 0); // JPEG
    if (!check(quality->property("visible").toBool(),
               "JPEG quality should reappear for JPEG"))
        return 1;
    noiseSection->setProperty("expanded", false);
    sharpSection->setProperty("expanded", false);
    if (!check(awaitLoad(editor, QUrl::fromLocalFile(second)) &&
               noise->property("value").toInt() == 0 && sharp->property("value").toInt() == 0 &&
               radius->property("value").toDouble() == 1.0 &&
               !noiseSection->property("expanded").toBool(),
               "image changes should refresh hidden controls without expanding sections"))
        return 1;

    // Main.qml permits a 260 px sidebar with 14 px margins on each side.
    QObject *dialog = root->findChild<QObject *>(QStringLiteral("exportDialog"));
    QObject *filter = dialog ? dialog->property("selectedNameFilter").value<QObject *>() : nullptr;
    if (!check(dialog && filter, "export dialog must expose its selected format filter")) return 1;
    const QStringList suffixes{"jpg", "png", "tiff", "dng"};
    for (int index : {2, 0, 3, 1, 2}) {
        format->setProperty("currentIndex", index);
        if (!check(QMetaObject::invokeMethod(root.data(), "prepareExportDialog") &&
                   filter->property("index").toInt() == index &&
                   dialog->property("defaultSuffix").toString() == suffixes[index] &&
                   dialog->property("currentFile").toUrl().fileName() == QStringLiteral("second-IPR.") + suffixes[index],
                   "opening export must select the panel format even after remembering another dialog filter")) return 1;
    }
    filter->setProperty("index", 1);
    if (!check(dialog->property("defaultSuffix").toString() == QStringLiteral("png"),
               "changing the dialog Type must update the extension for names without suffixes")) return 1;

    const QString dialogTarget = directory.filePath(QStringLiteral("dialog-choice.tiff"));
    dialog->setProperty("selectedFile", QUrl::fromLocalFile(dialogTarget));
    QEventLoop exportLoop;
    bool completed = false;
    const auto exportConnection = QObject::connect(&editor, &ImageEditor::statusMessageChanged, &exportLoop, [&] {
        if (editor.statusMessage().startsWith(QStringLiteral("Image exportée"))) {
            completed = true;
            exportLoop.quit();
        } else if (editor.statusMessage().startsWith(QStringLiteral("Échec"))) {
            exportLoop.quit();
        }
    });
    QTimer::singleShot(5000, &exportLoop, &QEventLoop::quit);
    QMetaObject::invokeMethod(dialog, "accepted");
    if (!completed) exportLoop.exec();
    QObject::disconnect(exportConnection);
    const QString actualPath = directory.filePath(QStringLiteral("dialog-choice.png"));
    if (!check(completed && format->property("currentText").toString() == QStringLiteral("PNG") &&
               !QImage(actualPath).isNull() && !QFileInfo::exists(dialogTarget),
               "accepting a changed dialog Type must write that format and synchronize the panel")) return 1;

    const QString exportDirectory = directory.filePath(QStringLiteral("selected-destination"));
    if (!QDir().mkpath(exportDirectory)) return 1;
    // Reproduce exports into a folder different from the source: the next
    // dialog should propose the next actual filename, not keep showing -2.
    for (int number = 1; number <= 4; ++number) {
        const QString expected = exportDirectory + QStringLiteral("/second-IPR") +
            (number == 1 ? QString() : QStringLiteral("-%1").arg(number)) + QStringLiteral(".png");
        if (number == 1)
            dialog->setProperty("selectedFile", QUrl::fromLocalFile(expected));
        else if (!check(QMetaObject::invokeMethod(root.data(), "prepareExportDialog") &&
                       dialog->property("currentFile").toUrl().toLocalFile() == expected,
                       "reopening the dialog must suggest -2, -3 and -4 in the selected folder")) return 1;
        QEventLoop numberedLoop;
        bool numberedComplete = false;
        const auto numberedConnection = QObject::connect(&editor, &ImageEditor::statusMessageChanged, &numberedLoop, [&] {
            if (editor.statusMessage().startsWith(QStringLiteral("Image exportée"))) {
                numberedComplete = true;
                numberedLoop.quit();
            } else if (editor.statusMessage().startsWith(QStringLiteral("Échec"))) {
                numberedLoop.quit();
            }
        });
        QTimer::singleShot(5000, &numberedLoop, &QEventLoop::quit);
        QMetaObject::invokeMethod(dialog, "accepted");
        if (!numberedComplete) numberedLoop.exec();
        QObject::disconnect(numberedConnection);
        if (!check(numberedComplete && editor.statusMessage().endsWith(expected) && !QImage(expected).isNull(),
                   "accepted sequential exports must agree with the visible filename")) return 1;
    }

    for (int width : {232, 280, 320}) {
        panel->setWidth(width);
        for (const QString &name : names)
            root->findChild<QObject *>(name)->setProperty("expanded", true);
        settleLayout();
        for (const QString &name : names) {
            auto *section = root->findChild<QQuickItem *>(name);
            if (!check(section->width() <= width + 0.5,
                       "expanded sections should fit the settings panel width"))
                return 1;
            auto *body = section->property("body").value<QQuickItem *>();
            for (QQuickItem *control : body->childItems()) {
                if (!control->isVisible())
                    continue;
                if (control->width() > body->width() + 0.5)
                    std::cerr << "wide control in " << name.toStdString() << ": "
                              << control->metaObject()->className() << " width="
                              << control->width() << '\n';
                if (!check(control->width() <= body->width() + 0.5,
                           "section controls should fit rather than being clipped"))
                    return 1;
            }
        }
        QObject *scrollView = root->findChild<QObject *>(QStringLiteral("settingsScrollView"));
        if (!check(scrollView && scrollView->property("contentWidth").toDouble() <= width + 0.5,
                   "sections must not introduce horizontal scrolling"))
            return 1;
        auto *mixerSection = root->findChild<QQuickItem *>(QStringLiteral("mixerSection"));
        // Regression: a collapsed selector could pass width-only assertions
        // while every button had zero height and remained invisible.
        for (int band = 0; band < 8; ++band) {
            auto *swatch = visualItemNamed(mixerSection, QStringLiteral("mixerBand%1").arg(band));
            if (!check(swatch && swatch->isVisible() && swatch->isEnabled() &&
                       swatch->width() >= 20 && swatch->height() >= 20 &&
                       std::abs(swatch->width() - swatch->height()) < 0.5,
                       "all eight selectable color circles must remain visible and round after expanding"))
                return 1;
            const QPointF origin = swatch->mapToItem(mixerSection, QPointF(0, 0));
            if (!check(origin.x() >= -0.5 &&
                       origin.x() + swatch->width() <= mixerSection->width() + 0.5,
                       "all color circles must fit the minimum sidebar width")) return 1;
            QMetaObject::invokeMethod(swatch, "clicked");
            if (!check(swatch->parentItem()->parentItem()->parentItem()->property("band").toInt() == band,
                       "clicking a color circle must select its own band")) return 1;
        }
        mixerSection->setProperty("expanded", false);
        settleLayout();
        mixerSection->setProperty("expanded", true);
        settleLayout();
        auto *lastSwatch = visualItemNamed(mixerSection, QStringLiteral("mixerBand7"));
        if (!check(lastSwatch && lastSwatch->isVisible() && lastSwatch->height() >= 20,
                   "collapsing and reopening must not hide the selector")) return 1;
    }
    // Optional offscreen screenshot for reviewing the three separated groups.
    if (argc == 2) {
        window.resize(320, 1080);
        panel->setHeight(1080);
        for (const QString &name : names)
            root->findChild<QObject *>(name)->setProperty("expanded",
                name == QStringLiteral("denoiseSection") ||
                name == QStringLiteral("sharpeningSection") ||
                name == QStringLiteral("exportSection"));
        settleLayout();
        if (!check(window.grabWindow().save(QString::fromLocal8Bit(argv[1])),
                   "optional settings screenshot should save"))
            return 1;
    }
    return 0;
}
