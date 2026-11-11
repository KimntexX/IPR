#include "imageeditor.h"
#include "imageprovider.h"
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
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QTemporaryDir directory;
    const auto path = directory.filePath(QFileInfo(argv[1]).fileName());
    if (!QFile::copy(QString::fromLocal8Bit(argv[1]), path)) return 3;
    ImageProvider provider;
    ImageEditor editor(&provider);
    editor.loadImage(QUrl::fromLocalFile(path));
    if (!settle(editor)) return 4;
    const QImage baseline = provider.originalImage().copy();
    editor.setNeuralDenoisePreferences(true, 100, QString::fromLocal8Bit(argv[2]));
    if (!settle(editor)) return 5;
    const auto engine = editor.neuralDenoiseEngine();
    if (engine != "CPU" && engine != "DirectML") {
        std::cerr << "Engine: " << engine.toStdString() << "; status: "
                  << editor.statusMessage().toStdString() << "; RAW: "
                  << editor.rawDevelopmentStatus().toStdString() << '\n';
        return 6;
    }
    if (provider.originalImage() == baseline) return 7;
    const QImage strongest = provider.originalImage().copy();
    editor.setNeuralDenoisePreferences(true, 50, QString::fromLocal8Bit(argv[2]));
    if (!settle(editor) || provider.originalImage() == strongest) return 10;
    editor.setNeuralDenoisePreferences(false, 100, QString::fromLocal8Bit(argv[2]));
    if (!settle(editor)) return 8;
    if (provider.originalImage() != baseline) return 9;
    std::cout << "Activation changes decoded pixels; disabling restores the exact original; engine "
              << engine.toStdString() << '\n';
    return 0;
}
