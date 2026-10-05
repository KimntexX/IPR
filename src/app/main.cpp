#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "imageeditor.h"
#include "imageprovider.h"
#include "ThumbnailProvider.h"
#include "UiPreferences.h"
// #include "Cr3ImageProvider.h"
// void readCr2Cr3(const QString &filename) {
//     cv::Mat img = loadRawImage(filename);
//     if (!img.empty()) {
//         QImage qimg(img);
//         QImage display(qimg.convertToFormat(QImage::Format_RGB888));
//         label->setPixmap(QPixmap::fromPixmap(QPixmap::fromImage(display)));
//     }
// }

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IPR"));
    QCoreApplication::setApplicationName(QStringLiteral("IPR"));
    UiPreferences uiPreferences;
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &uiPreferences, &UiPreferences::sync);

    QQmlApplicationEngine engine;
    auto *imageProvider = new ImageProvider;
    auto *thumbnailProvider = new ThumbnailProvider;
    engine.addImageProvider(QLatin1String("editor"), imageProvider);
    engine.addImageProvider(QLatin1String("thumbnail"), thumbnailProvider);

    ImageEditor imageEditor(imageProvider, thumbnailProvider);
    engine.rootContext()->setContextProperty("imageEditor", &imageEditor);
    engine.rootContext()->setContextProperty("uiPreferences", &uiPreferences);

    const QUrl url(QStringLiteral("qrc:/IPR/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
