#include "RawDecoder.h"

#include <QCoreApplication>

#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool receivedAutomaticWhiteBalance = false;
    bool receivedMetadata = false;

    QString error;
    if (!check(RawDecoder::registerFormatHandler(
                   {QStringLiteral("testraw")},
                   [&receivedAutomaticWhiteBalance](
                       const QString &, const RawDecoder::Options &options,
                       RawDecoder::SourceMetadata *metadata, QString *) {
                       receivedAutomaticWhiteBalance =
                           options.automaticWhiteBalance;
                       if (metadata) {
                           metadata->cameraModel = QStringLiteral("Test Camera");
                           metadata->lensModel = QStringLiteral("Test Lens");
                       }
                       ColorPipeline::WorkingImage result;
                       result.linearRgba = cv::Mat::zeros(1, 1, CV_32FC4);
                       result.linearRgba.at<cv::Vec4f>(0, 0)[3] = 1.0f;
                       return result;
                   },
                   &error),
               "a new format decoder should be registerable"))
        return 1;

    const QStringList extensions = RawDecoder::supportedExtensions();
    if (!check(extensions.contains(QStringLiteral("testraw")),
               "registered format should be discoverable by the image model"))
        return 1;

    RawDecoder::SourceMetadata metadata;
    const ColorPipeline::WorkingImage image = RawDecoder::decode(
        QStringLiteral("sample.testraw"), RawDecoder::Options{true}, &metadata,
        &error);
    receivedMetadata = metadata.format == QStringLiteral("testraw") &&
                       metadata.cameraModel == QStringLiteral("Test Camera") &&
                       metadata.lensModel == QStringLiteral("Test Lens");
    if (!check(!image.isNull() && receivedAutomaticWhiteBalance &&
                   receivedMetadata,
               "decoder should receive options, populate metadata and return its image"))
        return 1;

    error.clear();
    const ColorPipeline::WorkingImage unsupported =
        RawDecoder::decode(QStringLiteral("sample.unknownraw"), {}, nullptr, &error);
    if (!check(unsupported.isNull() && !error.isEmpty(),
               "unsupported extensions should fail explicitly"))
        return 1;

    error.clear();
    if (!check(!RawDecoder::registerFormatHandler(
                   {QStringLiteral("testraw")},
                   [](const QString &, const RawDecoder::Options &,
                      RawDecoder::SourceMetadata *, QString *) {
                       return ColorPipeline::WorkingImage{};
                   },
                   &error) &&
                   !error.isEmpty(),
               "duplicate extensions should be rejected with an error"))
        return 1;

    return 0;
}
