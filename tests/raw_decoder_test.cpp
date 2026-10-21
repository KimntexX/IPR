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
    bool receivedThumbnailSize = false;
    RawDecoder::FormatOptions futureFormat;
    futureFormat.decoderId = QStringLiteral("test-future-backend");
    futureFormat.decoderVersion = QStringLiteral("1.0");
    futureFormat.dimensions = [](const QString &) {
        return RawDecoder::supportedExtensions().contains("testraw") ? QSize(40, 30) : QSize();
    };
    futureFormat.thumbnail = [&](const QString &, const QSize &size, QString *) {
        receivedThumbnailSize = size == QSize(12, 8) &&
            RawDecoder::supportedExtensions().contains("testraw"); // Reentrant, outside lock.
        QImage thumbnail(100, 50, QImage::Format_RGB32);
        thumbnail.fill(Qt::green);
        return thumbnail;
    };

    QString error;
    if (!check(RawDecoder::registerFormatHandler(
                   {QStringLiteral("testraw")},
                   [&receivedAutomaticWhiteBalance](
                       const QString &, const RawDecoder::Options &options,
                       ImageMetadata *metadata, QString *) {
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
                   &error, futureFormat),
               "a new format decoder should be registerable"))
        return 1;

    const QStringList extensions = RawDecoder::supportedExtensions();
    if (!check(RawDecoder::sourceDimensions("future.testraw") == QSize(40, 30),
               "format-specific dimension probe runs outside registry lock")) return 1;
    if (!check(extensions.contains(QStringLiteral("testraw")),
               "registered format should be discoverable by the image model"))
        return 1;

    ImageMetadata metadata;
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
    if (!check(metadata.decoderId == futureFormat.decoderId &&
               metadata.decoderVersion == futureFormat.decoderVersion,
               "backend identity is propagated separately from camera identity")) return 1;
    const auto thumbnail = RawDecoder::decodeEmbeddedThumbnail("sample.TESTRAW", QSize(12, 8), &error);
    if (!check(!thumbnail.isNull() && receivedThumbnailSize && thumbnail.width() <= 12 &&
               thumbnail.height() <= 8 && error.isEmpty(),
               "future format uses its own bounded thumbnail callback, never LibRaw")) return 1;

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
                      ImageMetadata *, QString *) {
                       return ColorPipeline::WorkingImage{};
                   },
                   &error) &&
                   !error.isEmpty(),
               "duplicate extensions should be rejected with an error"))
        return 1;

    if (!RawDecoder::registerFormatHandler({QStringLiteral("wrongmaster")},
        [](const QString &, const RawDecoder::Options &, ImageMetadata *, QString *) {
            ColorPipeline::WorkingImage result;
            result.linearRgba = cv::Mat(1,1,CV_32FC4,cv::Scalar(.2,.3,.4,1));
            result.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
            return result;
        }, &error)) return 1;
    error.clear();
    if (!check(RawDecoder::decode("sample.wrongmaster", {}, nullptr, &error).isNull() && !error.isEmpty(),
               "RAW registration cannot bypass the Rec.2020 master contract")) return 1;
    error.clear();
    if (!check(RawDecoder::decodeEmbeddedThumbnail("sample.wrongmaster", QSize(12,8), &error).isNull()
               && !error.isEmpty(), "missing thumbnail reader fails without falling through to LibRaw")) return 1;
    const RawDecoder::DecodeFunction empty = [](const QString &, const RawDecoder::Options &,
        ImageMetadata *, QString *) { return ColorPipeline::WorkingImage{}; };
    for (const auto &suffix : QStringList{"*", "../raw", ".raw", "raw image", "raw/other", ""}) {
        if (!check(!RawDecoder::registerFormatHandler({suffix}, empty, &error),
                   "malformed or wildcard extension rejected")) return 1;
    }
    if (!check(!RawDecoder::registerFormatHandler({"unused", "testraw"}, empty, &error) &&
               !RawDecoder::supportedExtensions().contains("unused"),
               "failed multi-extension registration is atomic")) return 1;
    return 0;
}
