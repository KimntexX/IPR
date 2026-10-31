#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTimer>

#include "imageeditor.h"
#include "imageprovider.h"
#include "GradingWheelProvider.h"
#include "ThumbnailProvider.h"
#include "UiPreferences.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IPR"));
    QCoreApplication::setApplicationName(QStringLiteral("IPR"));
    QCoreApplication::setApplicationVersion(QStringLiteral(IPR_RELEASE_LABEL));
    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));
    QTemporaryDir smokePreferences;
    if (smokeTest && !smokePreferences.isValid()) return 2;
    UiPreferences uiPreferences(smokeTest ? smokePreferences.filePath("ipr-ui.ini") : QString{});
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &uiPreferences, &UiPreferences::sync);

    QQmlApplicationEngine engine;
    auto *imageProvider = new ImageProvider;
    auto *thumbnailProvider = new ThumbnailProvider;
    engine.addImageProvider(QLatin1String("editor"), imageProvider);
    engine.addImageProvider(QLatin1String("thumbnail"), thumbnailProvider);
    engine.addImageProvider(QLatin1String("gradingwheel"), new GradingWheelProvider);

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
    if (smokeTest) {
        if (engine.rootObjects().isEmpty()) return 3;
        QTimer::singleShot(1000, &app, [] { QCoreApplication::exit(0); });
    }

    const int result = app.exec();
    // Destroy QML bindings while their context objects and providers still live.
    // Otherwise Qt 6.4 evaluates bindings against a destroyed ImageEditor.
    qDeleteAll(engine.rootObjects());
    return result;
}
