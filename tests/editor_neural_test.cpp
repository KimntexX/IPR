#include "imageeditor.h"
#include "imageprovider.h"
#include "Sidecar.h"
#include "RecipeCodec.h"
#include "GradingWheelProvider.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace {
bool settle(ImageEditor &editor)
{
    QElapsedTimer timer; timer.start();
    QElapsedTimer idle;
    while (timer.elapsed() < 180000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (editor.hasImage() && !editor.busy()) {
            if (!idle.isValid()) idle.start();
            if (idle.elapsed() >= 150) return true;
        } else idle.invalidate();
        QThread::msleep(10);
    }
    return false;
}
}
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3 && argc != 4) return 2;
    QTemporaryDir directory;
    const auto path = directory.filePath(QFileInfo(argv[1]).fileName());
    if (!QFile::copy(QString::fromLocal8Bit(argv[1]), path)) return 3;
    ImageProvider provider;
    ImageEditor editor(&provider);
    editor.loadImage(QUrl::fromLocalFile(path));
    if (!settle(editor)) return 4;
    const QImage baseline = provider.originalImage().copy();
    QQmlEngine qml;
    qml.addImageProvider(QStringLiteral("gradingwheel"), new GradingWheelProvider);
    QQmlComponent component(&qml, QUrl::fromLocalFile(QStringLiteral(IPR_QML_DIRECTORY "/Setting.qml")));
    std::unique_ptr<QObject> panel(component.createWithInitialProperties({{"editor",QVariant::fromValue(&editor)}}));
    if (!panel) { std::cerr << component.errorString().toStdString(); return 14; }
    auto *check = panel->findChild<QObject *>("photoNeuralDenoiseCheckBox");
    auto *intensity = panel->findChild<QObject *>("photoNeuralDenoiseIntensity");
    if (!check || !intensity) return 15;
    editor.setNeuralDenoisePreferences(true, QString::fromLocal8Bit(argv[2]));
    if (editor.busy() || editor.neuralDenoiseActive() || provider.originalImage() != baseline) return 11;
    editor.setNeuralDenoiseEnabled(true);
    if (!editor.statusMessage().contains(QStringLiteral("Débruitage IA en cours"))) return 12;
    if (!settle(editor)) return 5;
    if (!editor.neuralDenoiseActive() || editor.neuralDenoiseStrength() != 50) return 13;
    const auto classical = [&](const QString &label) -> QObject* {
        for (auto *child : panel->findChildren<QObject*>())
            if (child->property("label").toString() == label) return child;
        return nullptr;
    };
    auto *luminance = classical(QStringLiteral("Bruit de luminance"));
    auto *amount = classical(QStringLiteral("Réduction du bruit"));
    auto *colour = classical(QStringLiteral("Bruit de couleur"));
    if (!check->property("checked").toBool() || !luminance || !amount || !colour ||
        luminance->property("adjustmentEnabled").toBool() || amount->property("adjustmentEnabled").toBool() ||
        !colour->property("adjustmentEnabled").toBool()) return 16;
    editor.copyAdjustments();
    const auto engine = editor.neuralDenoiseEngine();
    if (engine != "CPU" && engine != "DirectML") {
        std::cerr << "Engine: " << engine.toStdString() << "; status: "
                  << editor.statusMessage().toStdString() << "; RAW: "
                  << editor.rawDevelopmentStatus().toStdString() << '\n';
        return 6;
    }
    if (provider.originalImage() == baseline) return 7;
    const QImage strongest = provider.originalImage().copy();
    editor.setNeuralDenoiseStrength(100);
    if (!settle(editor) || provider.originalImage() == strongest) return 10;
    editor.setNeuralDenoiseEnabled(false);
    if (!settle(editor)) return 8;
    if (provider.originalImage() != baseline) return 9;
    editor.pasteAdjustments();
    if (!settle(editor) || !editor.neuralDenoiseActive() || editor.neuralDenoiseStrength() != 50) return 17;
    editor.saveCurrentAdjustments();
    const auto saved = Sidecar::read(path);
    if (!saved || saved->raw.neuralStrength != 50 || saved->raw.neuralModelId.isEmpty()) return 18;
    editor.undo();
    if (!settle(editor) || editor.neuralDenoiseActive()) return 19;
    editor.redo();
    if (!settle(editor) || !editor.neuralDenoiseActive() || editor.neuralDenoiseStrength() != 50) return 20;
    const QImage alreadyApplied = provider.originalImage().copy();
    editor.setNeuralDenoisePreferences(false, QString::fromLocal8Bit(argv[2]));
    if (editor.busy() || !editor.neuralDenoiseActive() || provider.originalImage() != alreadyApplied) return 27;
    editor.setNeuralDenoisePreferences(true, QString::fromLocal8Bit(argv[2]));
    if (editor.busy() || provider.originalImage() != alreadyApplied) return 28;
    // The 100-strength image is already cached: it must not hide a missing model.
    editor.setNeuralDenoisePreferences(true, directory.filePath(QStringLiteral("missing.onnx")));
    editor.setNeuralDenoiseStrength(100);
    if (!settle(editor) || editor.neuralDenoiseStrength() != 50 || provider.originalImage() != alreadyApplied) return 30;
    editor.setNeuralDenoisePreferences(true, QString::fromLocal8Bit(argv[2]));
    editor.resetAdjustments();
    if (!settle(editor) || editor.neuralDenoiseActive() || provider.originalImage() != baseline) return 29;
    if (argc == 4) {
        const auto other = directory.filePath(QFileInfo(argv[3]).fileName());
        if (!QFile::copy(QString::fromLocal8Bit(argv[3]), other)) return 21;
        Sidecar::Document claimed;
        claimed.raw.neuralStrength = 50;
        claimed.raw.neuralModelId = saved->raw.neuralModelId;
        if (!Sidecar::write(other, claimed)) return 22;
        editor.loadImage(QUrl::fromLocalFile(other));
        if (!settle(editor) || editor.neuralDenoiseActive() || editor.neuralDenoiseUnavailableReason().isEmpty()) return 23;
        if (check->property("enabled").toBool()) return 24;
        editor.pasteAdjustments();
        if (!settle(editor) || editor.neuralDenoiseActive()) return 25;
        editor.saveCurrentAdjustments();
        const auto cleaned = Sidecar::read(other);
        if (!cleaned || cleaned->raw.neuralStrength != 0 ||
            RecipeCodec::encodeRaw(cleaned->raw).contains(QStringLiteral("neuralStrength"))) return 26;
        std::cout << "Unsupported camera: disabled with reason and no neural recipe\n";
    }
    std::cout << "Activation changes decoded pixels; disabling restores the exact original; engine "
              << engine.toStdString() << '\n';
    return 0;
}
