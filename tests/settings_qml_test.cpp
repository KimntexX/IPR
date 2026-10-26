#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "GradingWheelProvider.h"
#include "Sidecar.h"

#include <QEventLoop>
#include <QDir>
#include <QFileInfo>
#include <QFile>
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
    editor.copyAdjustments();
    editor.pasteAdjustments();
    if (!check(!editor.hasCopiedAdjustments(), "copy without an image must leave the clipboard empty")) return 1;
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("gradingwheel"), new GradingWheelProvider);
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
               root->findChild<QObject *>("rawDevelopmentStatusLabel") &&
               root->findChild<QObject *>("lensCorrectionToggle") &&
               root->findChild<QObject *>("chromaticAberrationToggle") &&
               root->findChild<QObject *>("opticalDevelopmentStatusLabel") &&
               !root->findChild<QObject *>("rawNormalizationToggle") &&
               !root->findChild<QObject *>("rawNormalizationManifestButton") &&
               !root->findChild<QObject *>("resetRawDevelopmentButton"),
               "normal settings must expose profiles and optics but no advanced calibration controls")) return 1;
    auto *optics = root->findChild<QObject *>("opticsSection");
    auto *rawSection = root->findChild<QObject *>("rawDevelopmentSection");
    int automaticWhiteBalanceControls = 0;
    for (QObject *child : root->findChildren<QObject *>())
        if (child->property("checked").isValid() &&
            child->property("text").toString() == QStringLiteral("Balance des blancs automatique"))
            ++automaticWhiteBalanceControls;
    if (!check(automaticWhiteBalanceControls == 1 &&
               rawSection->findChild<QObject *>("rawAutomaticWhiteBalanceToggle"),
               "automatic white balance must exist once, exclusively under RAW development")) return 1;
    auto *opticsItem = qobject_cast<QQuickItem *>(optics);
    auto *exportItem = root->findChild<QQuickItem *>("exportSection");
    const auto sections = opticsItem->parentItem()->childItems();
    if (!check(sections.indexOf(exportItem) == sections.indexOf(opticsItem) + 1,
               "optical corrections must be placed immediately before export")) return 1;
    if (!check(optics->findChild<QObject *>("lensCorrectionToggle") &&
               optics->findChild<QObject *>("chromaticAberrationToggle") &&
               !rawSection->findChild<QObject *>("lensCorrectionToggle"),
               "optical corrections must belong exclusively to their independent section")) return 1;
    // A click replaces a plain QML binding: the box must still show the engine
    // state afterwards (here the engine refuses: no RAW is open).
    const std::pair<const char *, const char *> toggles[] = {
        {"lensCorrectionToggle", "lensCorrectionEnabled"},
        {"chromaticAberrationToggle", "chromaticAberrationEnabled"},
        {"rawAutomaticWhiteBalanceToggle", "automaticWhiteBalance"}};
    for (const auto &[name, state] : toggles) {
        QObject *toggle = root->findChild<QObject *>(QString::fromLatin1(name));
        if (!check(toggle, "RAW development checkbox exists")) return 1;
        QMetaObject::invokeMethod(toggle, "toggle");
        QMetaObject::invokeMethod(toggle, "clicked");
        settleLayout();
        if (!check(toggle->property("checked").toBool() == editor.property(state).toBool(),
                   "a RAW development checkbox must show the engine state after a click")) return 1;
    }
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

    // Geometry regression: signed values fill from neutral, not from the left.
    root->findChild<QObject *>("lightSection")->setProperty("expanded", true);
    settleLayout();
    QObject *contrast = controlWithLabel(root.data(), QStringLiteral("Contraste"));
    auto *track = contrast->findChild<QQuickItem *>("adjustmentTrack");
    auto *fill = contrast->findChild<QQuickItem *>("adjustmentFill");
    auto *valueField = contrast->findChild<QQuickItem *>("adjustmentValueEditor");
    if (!check(track->width() > 0 && valueField && valueField->height() - valueField->property("topPadding").toDouble() -
               valueField->property("bottomPadding").toDouble() >= valueField->property("contentHeight").toDouble(),
               "compact value fields must leave enough height for their editable text")) return 1;
    for (int value : {-100, -50, 0, 50, 100}) {
        contrast->setProperty("value", value);
        settleLayout();
        const qreal origin = track->width() / 2;
        const qreal endpoint = (value + 100) / 200.0 * track->width();
        if (!check(std::abs(fill->x() - std::min(origin, endpoint)) < 0.5 &&
                   std::abs(fill->width() - std::abs(endpoint - origin)) < 0.5,
                   "bipolar slider fill must extend from zero towards either signed value")) return 1;
    }
    contrast->setProperty("value", 0);
    for (const QString &label : {QStringLiteral("Température (K)"),
                                QStringLiteral("Nuance vert ↔ magenta"),
                                QStringLiteral("Vibrance"), QStringLiteral("Saturation")}) {
        QObject *control = controlWithLabel(root.data(), label);
        if (!check(control && control->property("hasColorTrack").toBool(),
                   "all global colour sliders must expose a coloured track")) return 1;
    }
    QObject *mixer = root->findChild<QObject *>("mixerSection");
    if (!check(controlWithLabel(mixer, QStringLiteral("Teinte"))->property("hasColorTrack").toBool() &&
               !contrast->property("hasColorTrack").toBool(),
               "mixer hue must have a coloured track without colouring light sliders")) return 1;
    if (!check(quality->property("originPosition").toDouble() == 0 &&
               noise->property("originPosition").toDouble() == 0,
               "unipolar sliders must retain a minimum origin")) return 1;

    QObject *histogramView = root->findChild<QObject *>("histogramModule");
    QVariantList histogramData;
    for (int channel = 0; channel < 4; ++channel) {
        QVariantList bins;
        for (int bin = 0; bin < 256; ++bin)
            bins.append(bin == 128 ? 100 : (bin == 255 ? 10 : 0));
        histogramData.append(QVariant::fromValue(bins));
    }
    histogramView->setProperty("histogram", histogramData);
    QObject *curveView = root->findChild<QObject *>("toneCurveModule");
    QVariantList curveData;
    for (int channel = 0; channel < 4; ++channel) {
        QVariantList bins;
        for (int bin = 0; bin < 256; ++bin)
            bins.append(bin == 128 ? (channel == 0 ? 100 : 10000) : 0);
        curveData.append(QVariant::fromValue(bins));
    }
    curveView->setProperty("histogram", curveData);
    QVariant curveHeight;
    QMetaObject::invokeMethod(curveView, "histogramHeight", Q_RETURN_ARG(QVariant, curveHeight), Q_ARG(QVariant, 50));
    if (!check(curveView->property("histogramPeak").toDouble() == 100 &&
               std::abs(curveHeight.toDouble() - 0.5) < 0.001,
               "curve backdrop must use luminance alone, ignoring stronger RGB channel peaks")) return 1;
    curveView->setProperty("histogram", QVariantList{});
    if (!check(curveView->property("histogramPeak").toDouble() == 1,
               "curve backdrop must clear safely when changing to an empty image")) return 1;
    QVariant heightRatio;
    QMetaObject::invokeMethod(histogramView, "relativeHeight", Q_RETURN_ARG(QVariant, heightRatio), Q_ARG(QVariant, 10));
    if (!check(std::abs(heightRatio.toDouble() - 0.1) < 0.001 &&
               !histogramView->property("shadowsClipped").toBool() &&
               histogramView->property("highlightsClipped").toBool(),
               "histogram must use proportional counts and report only preview-channel endpoint presence")) return 1;
    const QList<double> zoneBoundaries{0, 0.10, 0.33, 0.66, 0.90, 1};
    for (int index = 0; index < zoneBoundaries.size(); ++index) {
        QVariant zone;
        QMetaObject::invokeMethod(histogramView, "zoneAt", Q_RETURN_ARG(QVariant, zone),
                                  Q_ARG(QVariant, zoneBoundaries[index]));
        if (!check(zone.toInt() == std::min(index, 4),
                   "histogram zones must follow the supplied 10/33/66/90 percent boundaries")) return 1;
    }
    contrast->setProperty("value", 0);

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
    rawSection->setProperty("expanded", true);
    const QUrl firstUrl = QUrl::fromLocalFile(first);
    const QUrl aliasUrl = QUrl::fromLocalFile(directory.path() + "/./first.png");
    editor.setImageSelected(aliasUrl, true);
    if (!check(editor.selectedImageCount() == 1 && editor.isImageSelected(firstUrl) &&
               editor.isImageSelected(aliasUrl), "selection queries must resolve canonical path aliases")) return 1;
    editor.setImageSelected(firstUrl, false);
    if (!check(!editor.isImageSelected(aliasUrl), "canonical deselection must clear aliases too")) return 1;
#ifndef Q_OS_WIN
    const QUrl symbolicUrl = QUrl::fromLocalFile(directory.filePath("first-link.png"));
    if (!check(QFile::link(first, symbolicUrl.toLocalFile()), "symbolic selection fixture must be created")) return 1;
    editor.setImageSelected(symbolicUrl, true);
    if (!check(editor.isImageSelected(firstUrl) && editor.isImageSelected(symbolicUrl),
               "symbolic paths and original paths must share a selection identity")) return 1;
    editor.clearImageSelection();
#endif
    auto *profileStatus = root->findChild<QQuickItem *>("rawDevelopmentStatusLabel");
    auto *lightSection = root->findChild<QQuickItem *>("lightSection");
    for (int width : {232, 292, 332}) {
        panel->setWidth(width);
        settleLayout();
        const QString stableText = profileStatus->property("text").toString();
        const qreal stableHeight = profileStatus->height();
        const qreal stableLightY = lightSection->y();
        editor.setBrightness(width / 1000.0);
        if (!check(editor.busy() && profileStatus->property("text").toString() == stableText,
                   "processing must not replace the RAW profile description")) return 1;
        settleLayout();
        if (!check(std::abs(profileStatus->height() - stableHeight) < .1 &&
                   std::abs(lightSection->y() - stableLightY) < .1,
                   "profile status and downstream controls must not move during rendering")) return 1;
        for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();
        if (!check(!editor.busy() && profileStatus->property("text").toString() == stableText &&
                   std::abs(lightSection->y() - stableLightY) < .1,
                   "finishing a render must preserve profile text and control positions")) return 1;
    }
    panel->setWidth(320);
    editor.setBrightness(0);
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
    for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();
    editor.setColorMixer(2, 12, 23, -34);
    editor.setColorGrading(1, 150, 20);
    editor.setColorGradingBlending(80);
    editor.setColorGradingBalance(-30);
    const QVariantList copiedCurve{QVariantMap{{"x", 0.0}, {"y", 0.0}},
                                  QVariantMap{{"x", .5}, {"y", .6}},
                                  QVariantMap{{"x", 1.0}, {"y", 1.0}}};
    editor.setToneCurve(ImageEditor::LuminanceCurve, copiedCurve);
    const QVariantList normalizedCurve = editor.toneCurve(ImageEditor::LuminanceCurve);
    for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();
    editor.copyAdjustments();
    if (!check(editor.hasCopiedAdjustments(), "copy must retain a complete settings snapshot")) return 1;
    editor.setDenoiseLuminance(7); // Editing the source must not change the snapshot.
    for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();
    if (!check(awaitLoad(editor, QUrl::fromLocalFile(second)) &&
               noise->property("value").toInt() == 0 && sharp->property("value").toInt() == 0 &&
               radius->property("value").toDouble() == 1.0 &&
               !noiseSection->property("expanded").toBool(),
               "image changes should refresh hidden controls without expanding sections"))
        return 1;

    editor.setImageRating(2);
    editor.setImageColorLabel("blue");
    editor.setImageFlagged(true);
    const QString targetProfile = editor.selectedCameraProfile();
    const bool targetCalibration = editor.rawNormalizationEnabled();
    const bool targetOptics = editor.lensCorrectionEnabled();
    const bool targetWhiteBalance = editor.automaticWhiteBalance();
    editor.pasteAdjustments();
    for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();
    if (!check(!editor.busy() && noise->property("value").toInt() == 43 &&
               sharp->property("value").toInt() == 51 &&
               std::abs(radius->property("value").toDouble() - 1.7) < .001 &&
               editor.colorMixerValue(2, 2) == -34 && editor.colorGradingValue(1, 0) == 150 &&
               editor.colorGradingBlending() == 80 && editor.colorGradingBalance() == -30 &&
               editor.toneCurve(ImageEditor::LuminanceCurve) == normalizedCurve &&
               editor.imageRating() == 2 && editor.imageColorLabel() == "blue" && editor.imageFlagged() &&
               editor.selectedCameraProfile() == targetProfile &&
               editor.rawNormalizationEnabled() == targetCalibration &&
               editor.lensCorrectionEnabled() == targetOptics &&
               editor.automaticWhiteBalance() == targetWhiteBalance,
               "paste must restore the snapshot and UI while preserving target metadata and development")) return 1;
    editor.saveCurrentAdjustments();
    EditRecipe::Settings saved;
    EditRecipe::UserImageMetadata savedMetadata;
    if (!check(Sidecar::read(second, &saved, &savedMetadata) && saved.denoiseLuminance == 43 &&
               saved.gradingBlending == 80 && saved.gradingBalance == -30 &&
               savedMetadata.rating == 2 && savedMetadata.colorLabel == "blue" &&
               editor.statusMessage().startsWith(QStringLiteral("Réglages enregistrés")),
               "manual save must persist the latest recipe and metadata and report success")) return 1;
    editor.resetAdjustments();
    for (int attempt = 0; attempt < 100 && editor.busy(); ++attempt) settleLayout();

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
    QObject *review = root->findChild<QObject *>("exportReviewDialog");
    if (!check(review && review->property("visible").toBool() &&
               review->property("format").toString() == "PNG" &&
               review->property("encoding").toString().contains("16") &&
               review->property("dimensions").toString() == editor.imageDetails().value("dimensions").toString() &&
               !QFileInfo::exists(directory.filePath("dialog-choice.png")),
               "native destination acceptance must show accurate review without writing a file")) return 1;
    settleLayout();
    auto *confirmButton = review->findChild<QQuickItem *>("confirmExportButton");
    auto *cancelButton = review->findChild<QQuickItem *>("cancelConfirmationButton");
    if (!check(confirmButton && cancelButton &&
               std::abs(confirmButton->y() - cancelButton->y()) < 0.5 &&
               confirmButton->height() == cancelButton->height() &&
               confirmButton->height() >= 36,
               "confirmation buttons must share alignment and comfortable height")) return 1;
    for (QObject *child : review->findChildren<QObject *>()) {
        if (child->property("wrapMode").toInt() > 0 && child->property("lineHeight").isValid() &&
            !check(child->property("lineHeight").toDouble() == 1.5,
                   "wrapped confirmation text must use 1.5 line spacing")) return 1;
    }
    QMetaObject::invokeMethod(review, "confirm");
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
        QMetaObject::invokeMethod(review, "confirm");
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
        auto *histogram = root->findChild<QQuickItem *>("histogramModule");
        auto *header = root->findChild<QQuickItem *>("fixedSettingsHeader");
        auto *flickable = scrollView->property("contentItem").value<QQuickItem *>();
        const QPointF histogramPosition = histogram->mapToItem(panel, QPointF(0, 0));
        if (!check(header && flickable && histogram->parentItem() == header &&
                   flickable->property("contentHeight").toDouble() > flickable->height(),
                   "histogram must live outside the scrolling controls")) return 1;
        flickable->setProperty("contentY", 400);
        settleLayout();
        if (!check(flickable->property("contentY").toDouble() > 0 &&
                   histogram->mapToItem(panel, QPointF(0, 0)) == histogramPosition,
                   "histogram must stay fixed while expanded settings scroll")) return 1;
        flickable->setProperty("contentY", 0);
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
    const QString cancelledTarget = directory.filePath(QStringLiteral("cancelled-export.png"));
    dialog->setProperty("selectedFile", QUrl::fromLocalFile(cancelledTarget));
    QMetaObject::invokeMethod(dialog, "accepted");
    QMetaObject::invokeMethod(review, "close");
    if (!check(!QFileInfo::exists(cancelledTarget), "cancelling the export review must not write a file")) return 1;
    QMetaObject::invokeMethod(dialog, "accepted");
    root->setProperty("reviewedImageKey", "another-photo");
    QMetaObject::invokeMethod(review, "confirm");
    if (!check(!review->property("exportEnabled").toBool() &&
               review->property("visible").toBool() && !QFileInfo::exists(cancelledTarget),
               "a stale photo identity must prevent export confirmation")) return 1;
    QMetaObject::invokeMethod(review, "close");

    for (int i = 0; i < 5; ++i) settleLayout();
    auto *exposure = controlWithLabel(root.data(), QStringLiteral("Exposition (EV)"));
    if (!check(exposure, "exposure control must exist")) return 1;
    exposure->setProperty("value", 1.25);
    if (!check(exposure->property("modified").toBool() &&
               root->findChild<QObject *>("lightSection")->property("modified").toBool(),
               "numeric modifications must be marked on the control and section")) return 1;
    QMetaObject::invokeMethod(exposure, "resetToDefault");
    if (!check(exposure->property("value").toDouble() == 0 &&
               !exposure->property("modified").toBool() && editor.adjustmentValue("brightness") == 0,
               "reset gesture must update both slider and engine")) return 1;
    auto *slider = exposure->findChild<QQuickItem *>("adjustmentSlider");
    slider->forceActiveFocus();
    QKeyEvent stepPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QKeyEvent stepRelease(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &stepPress);
    QCoreApplication::sendEvent(&window, &stepRelease);
    if (!check(std::abs(exposure->property("value").toDouble() - .01) < 1e-6 &&
               std::abs(editor.adjustmentValue("brightness") - .01) < 1e-6,
               "keyboard arrows must apply the declared numeric increment")) return 1;
    QMetaObject::invokeMethod(exposure, "resetToDefault");

    QQuickWindow gestureWindow;
    gestureWindow.resize(280, 100);
    QQmlComponent gestureComponent(&engine,
        QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/AdjustmentControl.qml")));
    QScopedPointer<QObject> gesture(gestureComponent.createWithInitialProperties({
        {"minimumValue", -100}, {"maximumValue", 100}, {"value", 25}, {"adjustmentEnabled", true}}));
    if (!gesture) { std::cerr << gestureComponent.errorString().toStdString(); return 1; }
    auto *gestureItem = qobject_cast<QQuickItem *>(gesture.data());
    gestureItem->setParentItem(gestureWindow.contentItem());
    gestureItem->setX(10); gestureItem->setY(10); gestureItem->setWidth(240); gestureItem->setHeight(60);
    gestureWindow.show();
    settleLayout();
    auto *gestureSlider = gesture->findChild<QQuickItem *>("adjustmentSlider");
    auto *gestureHandle = gestureSlider->property("handle").value<QQuickItem *>();
    const QPointF gesturePoint = gestureHandle->mapToItem(nullptr, QPointF(5, 5));
    unsigned long timestamp = 1000;
    // QWindow pointer delivery uses two press/release pairs (as QTest's
    // QWindow mouseDClick does), not the QWidget-only double-click shortcut.
    for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
                             QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, gesturePoint, QPointF(gestureWindow.mapToGlobal(gesturePoint.toPoint())),
                          Qt::LeftButton, type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        event.setTimestamp(timestamp);
        timestamp += 50;
        QCoreApplication::sendEvent(&gestureWindow, &event);
        QCoreApplication::processEvents();
    }
    if (!check(gesture->property("value").toDouble() == 0,
               "a real double click on the slider must restore its reset value")) return 1;
    const QPointF dragStart = gestureHandle->mapToItem(nullptr, QPointF(5, 5));
    for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
        const QPointF point = type == QEvent::MouseButtonPress ? dragStart : dragStart + QPointF(40, 0);
        QMouseEvent event(type, point, QPointF(gestureWindow.mapToGlobal(point.toPoint())),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        event.setTimestamp(timestamp + 1000);
        timestamp += 50;
        QCoreApplication::sendEvent(&gestureWindow, &event);
        QCoreApplication::processEvents();
    }
    if (!check(gesture->property("value").toDouble() > 10,
               "the reset tap recognizer must not steal normal slider dragging")) return 1;
    const double contextValue = gesture->property("value").toDouble();
    const QPointF contextPoint = gestureHandle->mapToItem(nullptr, QPointF(5, 5));
    for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, contextPoint, QPointF(gestureWindow.mapToGlobal(contextPoint.toPoint())), Qt::RightButton,
            type == QEvent::MouseButtonPress ? Qt::RightButton : Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&gestureWindow, &event);
    }
    settleLayout();
    QObject *parameterMenu = gesture->findChild<QObject *>("adjustmentContextMenu");
    if (!check(parameterMenu && parameterMenu->property("visible").toBool() &&
               gesture->property("value").toDouble() == contextValue,
               "right-click must open a parameter menu without moving the slider")) return 1;
    QMetaObject::invokeMethod(parameterMenu->findChild<QObject *>("contextResetParameter"), "triggered");
    if (!check(gesture->property("value").toDouble() == 0, "parameter context reset must use its declared reference")) return 1;
    QMetaObject::invokeMethod(parameterMenu, "close");

    QQmlComponent statusComponent(&engine,
        QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/ProcessingStatus.qml")));
    QScopedPointer<QObject> status(statusComponent.create());
    if (!status) { std::cerr << statusComponent.errorString().toStdString(); return 1; }
    qobject_cast<QQuickItem *>(status.data())->setParentItem(window.contentItem());
    status->setProperty("busy", true);
    if (!check(!status->property("visible").toBool(), "short operations must not flash a badge")) return 1;
    for (int i = 0; i < 10 && !status->property("visible").toBool(); ++i) settleLayout();
    if (!check(status->property("visible").toBool(), "long operations must show a badge")) return 1;
    status->setProperty("busy", false);
    if (!check(!status->property("visible").toBool(), "badge must clear when idle without claiming success")) return 1;
    status->setProperty("embeddedPreview", true);
    if (!check(status->property("visible").toBool(), "interim camera preview must be labelled immediately")) return 1;
    return 0;
}
