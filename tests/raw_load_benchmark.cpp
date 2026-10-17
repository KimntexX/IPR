#include "RawDecoder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include <QStandardPaths>

#include <algorithm>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("appIPR"));
    if (qEnvironmentVariableIsSet("IPR_LOG_RAW_TIMING")) {
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &,
                                  const QString &message) {
            std::cerr << message.toStdString() << '\n';
        });
        std::cerr << "IPR cache paths | generic="
                  << QStandardPaths::writableLocation(
                         QStandardPaths::GenericCacheLocation).toStdString()
                  << " | app="
                  << QStandardPaths::writableLocation(
                         QStandardPaths::AppLocalDataLocation).toStdString()
                  << '\n';
    }
    if (argc < 2) {
        std::cerr << "Usage: rawLoadBenchmark <image.raw> [runs]\n";
        return 2;
    }

    bool runsOk = false;
    const int runs = argc >= 3
        ? std::clamp(QString::fromLocal8Bit(argv[2]).toInt(&runsOk), 1, 20)
        : 2;
    if (argc >= 3 && !runsOk) {
        std::cerr << "runs must be an integer between 1 and 20\n";
        return 2;
    }

    const QString fileName = QString::fromLocal8Bit(argv[1]);
    for (int run = 1; run <= runs; ++run) {
        QElapsedTimer timer;
        timer.start();
        ImageMetadata metadata;
        QString error;
        const ColorPipeline::WorkingImage image = RawDecoder::decode(
            fileName, RawDecoder::Options{false}, &metadata, &error);
        const qint64 elapsed = timer.elapsed();
        if (image.isNull()) {
            std::cerr << "run=" << run << " failed: "
                      << error.toStdString() << '\n';
            return 1;
        }
        std::cout << "run=" << run << " total_ms=" << elapsed
                  << " size=" << image.linearRgba.cols << 'x'
                  << image.linearRgba.rows
                  << " dcp=" << metadata.dcpProfile.toStdString()
                  << " lcp=" << metadata.lcpProfile.toStdString() << '\n';
    }
    return 0;
}
