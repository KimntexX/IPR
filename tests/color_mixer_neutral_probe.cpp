#include "imageeditor.h"
#include "controlled_memory.h"
#include "imageprovider.h"

#include <QCoreApplication>
#include <QColorSpace>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace {
bool load(ImageEditor &editor, const QString &path)
{
    QEventLoop loop;
    const auto connection = QObject::connect(&editor, &ImageEditor::busyChanged, &loop, [&] {
        if (!editor.busy()) loop.quit();
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    editor.loadImage(QUrl::fromLocalFile(path));
    if (editor.busy()) loop.exec();
    QObject::disconnect(connection);
    return editor.hasImage() && !editor.busy();
}

QImage exportPng(ImageEditor &editor, const QString &path)
{
    QEventLoop loop;
    bool succeeded = false;
    const auto connection = QObject::connect(&editor, &ImageEditor::statusMessageChanged, &loop, [&] {
        const QString status = editor.statusMessage();
        if (status.startsWith(QStringLiteral("Image exportée"))) {
            succeeded = true;
            loop.quit();
        } else if (status.startsWith(QStringLiteral("Échec"))) {
            loop.quit();
        }
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    editor.exportImage(QUrl::fromLocalFile(path), QStringLiteral("PNG"), 100);
    loop.exec();
    QObject::disconnect(connection);
    return succeeded ? QImage(path) : QImage{};
}

QJsonArray grayValues(const QImage &image)
{
    QJsonArray result;
    for (int x = 0; x < image.width(); ++x) result.append(image.pixelColor(x, 0).red());
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    QImage input(5, 1, QImage::Format_RGBA8888);
    const int values[] = {0, 64, 128, 192, 255};
    for (int x = 0; x < 5; ++x) input.setPixelColor(x, 0, QColor(values[x], values[x], values[x], 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString source = temporary.filePath(QStringLiteral("gray.bmp"));
    if (!temporary.isValid() || !input.save(source, "BMP") || !load(editor, source)) return 1;
    editor.resetAdjustments();
    const QImage baseline = exportPng(editor, temporary.filePath(QStringLiteral("baseline.png")));
    if (baseline.isNull()) return 1;
    QJsonObject report{{QStringLiteral("input_gray_u8"), QJsonArray{0, 64, 128, 192, 255}},
                       {QStringLiteral("baseline_gray_u8"), grayValues(baseline)}};
    QJsonArray results;
    bool allUnchanged = true;
    for (int band = 0; band < 8; ++band) {
      for (int component = 0; component < 3; ++component) {
        for (int value : {-100, 100}) {
            editor.resetAdjustments();
            editor.setColorMixer(band, component == 0 ? value : 0,
                component == 1 ? value : 0, component == 2 ? value : 0);
            const QImage output = exportPng(editor, temporary.filePath(
                QStringLiteral("band-%1-%2-%3.png").arg(band).arg(component).arg(value)));
            if (output.isNull()) return 1;
            bool unchanged = true;
            for (int x = 0; x < 5; ++x)
                unchanged = unchanged && output.pixelColor(x, 0) == baseline.pixelColor(x, 0);
            results.append(QJsonObject{{QStringLiteral("band"), band},
                {QStringLiteral("component"), component}, {QStringLiteral("value"), value},
                {QStringLiteral("gray_u8"), grayValues(output)},
                {QStringLiteral("neutrals_preserved"), unchanged}});
            allUnchanged = allUnchanged && unchanged;
        }
      }
    }
    report.insert(QStringLiteral("results"), results);
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
    // This diagnostic is now also a regression: a neutral alteration fails it.
    return allUnchanged ? 0 : 1;
}
