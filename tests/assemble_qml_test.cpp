#include "StackSession.h"
#include "stack_fixture.h"
#include "synthetic_dcp.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>
#include <memory>

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

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "a temporary folder")) return 1;
    const QDir folder(directory.path());
    const QString dcp = folder.filePath(QStringLiteral("R6.dcp"));
    QFile file(dcp);
    if (!check(file.open(QIODevice::WriteOnly) && file.write(SyntheticDcp::make(true)) > 0, "a synthetic profile")) return 1;
    file.close();
    auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>();
    context->profilePath = dcp;
    context->sourceKelvin = 5000.0;
    context->cameraNeutral = cv::Vec3d(0.6, 1.0, 0.8);
    StackFixture::Source source;
    source.context = context;
    StackSession session(source);

    // What the panel asks of the editor.
    QQmlEngine engine;
    QQmlComponent editorComponent(&engine);
    editorComponent.setData("import QtQuick\nQtObject { property var stacking; property var selectedImageUrls: []; property url loaded\n"
                            "function loadImage(url) { loaded = url } }", QUrl());
    QScopedPointer<QObject> editor(editorComponent.createWithInitialProperties({{QStringLiteral("stacking"), QVariant::fromValue(&session)}}));
    if (!check(editor != nullptr, "the editor stand-in loads")) return 1;
    QQmlComponent panelComponent(&engine, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/AssemblePanel.qml")));
    QScopedPointer<QObject> panelObject(panelComponent.createWithInitialProperties({{QStringLiteral("editor"), QVariant::fromValue(editor.data())}}));
    if (!panelObject) {
        std::cerr << panelComponent.errorString().toStdString();
        return 1;
    }
    auto *panel = qobject_cast<QQuickItem *>(panelObject.data());
    QQuickWindow window;
    window.resize(360, 1200);
    panel->setParentItem(window.contentItem());
    panel->setWidth(360);
    panel->setHeight(1200);
    window.show();
    settle();

    auto *start = named(panel, QStringLiteral("assembleStartButton"));
    auto *exporter = named(panel, QStringLiteral("assembleExportButton"));
    auto *format = named(panel, QStringLiteral("assembleFormatSelector"));
    auto *opener = named(panel, QStringLiteral("assembleOpenButton"));
    auto *reference = named(panel, QStringLiteral("assembleReferenceSelector"));
    auto *preview = named(panel, QStringLiteral("assemblePreview"));
    if (!check(start && exporter && format && opener && reference && preview, "the panel has its controls")) return 1;

    // Without a selection, it says what to do, and nothing can start.
    if (!check(!start->isEnabled(), "without photos it cannot start")) return 1;
    if (!check(!exporter->isVisible() && !opener->isVisible(), "there is nothing to export")) return 1;
    const auto count = named(panel, QStringLiteral("assembleSelectionCount"))->property("text").toString();
    if (!check(count.contains(QStringLiteral("deux")), "it asks for two photos")) return 1;
    editor->setProperty("selectedImageUrls", StackFixture::frames(folder, 1));
    settle();
    if (!check(!start->isEnabled(), "one photo is not enough")) return 1;

    // A selection: the middle photo is the reference, and any other can be.
    const QStringList urls = StackFixture::frames(folder, 7);
    editor->setProperty("selectedImageUrls", urls);
    settle();
    if (!check(start->isEnabled(), "a selection can start")) return 1;
    if (!check(reference->property("currentIndex").toInt() == 3, "the middle photo is the reference")) return 1;
    if (!check(reference->property("count").toInt() == 7, "every photo can be it")) return 1;
    if (!check(reference->property("currentText").toString() == QStringLiteral("frame3.CR3"), "named by its file")) return 1;
    reference->setProperty("currentIndex", 5);

    // The work: the button gives way to the progress, and the export comes at its end.
    click(start);
    settle();
    if (!check(session.running() || session.hasResult(), "the button starts the work")) return 1;
    if (!check(waitFor([&] { return !session.running(); }), "the work ends")) return 1;
    settle();
    if (!check(session.hasResult(), "with a result")) return 1;
    if (!check(session.suggestedExport(QStringLiteral("DNG")).toLocalFile().contains(QStringLiteral("frame5-")), "from the chosen reference")) return 1;
    if (!check(waitFor([&] { return preview->property("status").toInt() == 1; }), "the preview is shown")) return 1;
    if (!check(preview->isVisible() && preview->height() > 50, "with room for it")) return 1;
    if (!check(exporter->isVisible() && exporter->isEnabled(), "the export button is ready at the end")) return 1;
    if (!check(exporter->mapToScene(QPointF(0, 0)).y() < preview->mapToScene(QPointF(0, 0)).y(), "above the preview, in view")) return 1;
    if (!check(!opener->isVisible(), "nothing to open yet")) return 1;
    if (!check(panel->property("format").toString() == QStringLiteral("DNG"), "the DNG comes first")) return 1;

    // Both formats, from the panel's own call; the dialog only gives it a destination.
    const QUrl dng = QUrl::fromLocalFile(folder.filePath(QStringLiteral("out.dng")));
    QMetaObject::invokeMethod(panel, "exportTo", Q_ARG(QVariant, QVariant(dng)));
    if (!check(session.exporting(), "the export starts")) return 1;
    settle();
    if (!check(!exporter->isEnabled() || !session.exporting(), "the button waits for it")) return 1;
    if (!check(waitFor([&] { return !session.exporting(); }), "the export ends")) return 1;
    settle();
    if (!check(QFile::exists(dng.toLocalFile()), "the DNG is written")) return 1;
    if (!check(opener->isVisible() && opener->isEnabled(), "and can be opened")) return 1;
    click(opener);
    if (!check(editor->property("loaded").toUrl() == dng, "in the editor")) return 1;

    format->setProperty("currentIndex", 1);
    settle();
    if (!check(panel->property("format").toString() == QStringLiteral("TIFF"), "TIFF can be chosen")) return 1;
    const QUrl tiff = QUrl::fromLocalFile(folder.filePath(QStringLiteral("out.tiff")));
    QMetaObject::invokeMethod(panel, "exportTo", Q_ARG(QVariant, QVariant(tiff)));
    if (!check(waitFor([&] { return !session.exporting(); }), "the TIFF export ends")) return 1;
    if (!check(QFile::exists(tiff.toLocalFile()), "the TIFF is written")) return 1;

    std::cout << "assemble panel: ok\n";
    return 0;
}
