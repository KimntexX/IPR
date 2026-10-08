#include "ColorPipeline.h"

#include <QColor>
#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QImageWriter>

#include <cmath>
#include <iostream>
#include <limits>

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

    QImage srgb(1, 1, QImage::Format_RGBA8888);
    srgb.setPixelColor(0, 0, QColor(128, 128, 128, 255));
    srgb.setColorSpace(QColorSpace(QColorSpace::SRgb));

    QString error;
    ColorPipeline::WorkingImage ramp;
    ramp.linearRgba = cv::Mat(1, 65536, CV_32FC4);
    for (int code = 0; code <= 65535; ++code) {
        const float value = float(code) / 65535.f;
        ramp.linearRgba.at<cv::Vec4f>(0, code) = {value, value, value, .37f};
    }
    ramp.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
    const QImage encodedRamp = ColorPipeline::toSrgb(ramp, ColorPipeline::OutputFormat::HighBitDepth, &error);
    if (!check(!encodedRamp.isNull(), "full-range sRGB ramp should encode")) return 1;
    const auto *codes = reinterpret_cast<const QRgba64 *>(encodedRamp.constScanLine(0));
    for (int code = 0; code <= 65535; ++code) {
        const double value = double(float(code) / 65535.f);
        const double encoded = value <= .0031308 ? value * 12.92
            : 1.055 * std::pow(value, 1. / 2.4) - .055;
        const int expected = int(std::lround(encoded * 65535.));
        if (!check(std::abs(int(codes[code].red()) - expected) <= 1 &&
                   codes[code].red() == codes[code].green() && codes[code].red() == codes[code].blue() &&
                   std::abs(int(codes[code].alpha()) - int(std::lround(.37f * 65535.f))) <= 1,
                   "analytic transfer must preserve ramp accuracy and straight alpha")) return 1;
        if (code && !check(codes[code].red() >= codes[code-1].red(), "ramp should be monotonic")) return 1;
    }
    if (!check(codes[1].red() == 13 && codes[15].red() == 194 &&
               codes[0].red() == 0 && codes[65535].red() == 65535,
               "low linear codes must survive; endpoints must remain exact")) return 1;
    const ColorPipeline::WorkingImage linear =
        ColorPipeline::fromSourceImage(srgb, &error);
    if (!check(!linear.isNull(), "sRGB input should convert to working space"))
        return 1;
    if (!check(linear.linearRgba.type() == CV_32FC4,
               "working image should be float RGBA"))
        return 1;
    if (!check(linear.sourceProfile.contains(QStringLiteral("sRGB"),
                                             Qt::CaseInsensitive),
               "working image should retain input profile provenance"))
        return 1;

    const cv::Vec4f gray = linear.linearRgba.at<cv::Vec4f>(0, 0);
    if (!check(std::abs(gray[0] - 0.21586f) < 0.002f,
               "sRGB gray should be linearized"))
        return 1;
    if (!check(std::abs(gray[3] - 1.0f) < 0.0001f,
               "alpha should be preserved"))
        return 1;

    const QImage preview = ColorPipeline::toSrgb(
        linear, ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (!check(!preview.isNull(), "linear image should encode to preview sRGB"))
        return 1;
    const QColor previewGray = preview.pixelColor(0, 0);
    if (!check(std::abs(previewGray.red() - 128) <= 1,
               "sRGB round trip should preserve an 8-bit gray sample"))
        return 1;

    QImage profiled(1, 1, QImage::Format_RGBA8888);
    profiled.setPixelColor(0, 0, QColor(128, 128, 128, 255));
    profiled.setColorSpace(QColorSpace(QColorSpace::AdobeRgb));
    const ColorPipeline::WorkingImage convertedProfile =
        ColorPipeline::fromSourceImage(profiled, &error);
    if (!check(!convertedProfile.isNull(),
               "an embedded non-sRGB profile should convert to working space"))
        return 1;
    if (!check(convertedProfile.sourceProfile ==
                   QColorSpace(QColorSpace::AdobeRgb).description(),
               "embedded profile provenance should be retained"))
        return 1;
    const QImage profiledRoundTrip = ColorPipeline::toSrgb(
        convertedProfile, ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (!check(!profiledRoundTrip.isNull() &&
                   std::abs(profiledRoundTrip.pixelColor(0, 0).red() - 128) <= 2,
               "profile conversion should preserve neutral gray"))
        return 1;

    const quint16 rawPixel[] = {32768, 16384, 8192};
    const ColorPipeline::WorkingImage raw =
        ColorPipeline::fromLinearRgb16(rawPixel, 1, 1, &error);
    if (!check(!raw.isNull(), "16-bit RAW RGB should convert to working space"))
        return 1;
    const cv::Vec4f rawLinear = raw.linearRgba.at<cv::Vec4f>(0, 0);
    if (!check(std::abs(rawLinear[0] - 0.5f) < 0.0001f &&
                   std::abs(rawLinear[1] - 0.25f) < 0.0001f &&
                   std::abs(rawLinear[2] - 0.125f) < 0.0001f,
               "16-bit RAW samples should be normalized as linear values"))
        return 1;
    if (!check(raw.sourceProfile.contains(QStringLiteral("LibRaw")),
               "RAW input should retain its decoder provenance"))
        return 1;

    cv::Mat neutralRgb(1, 1, CV_32FC3, cv::Scalar(0.25f, 0.5f, 0.75f));
    const cv::Mat originalNeutral = neutralRgb.clone();
    if (!check(ColorPipeline::applyTemperatureTint(neutralRgb, 6500, 6500.0, 0, &error),
               "neutral temperature and tint adjustments should succeed"))
        return 1;
    if (!check(cv::norm(neutralRgb, originalNeutral, cv::NORM_INF) == 0.0,
               "zero temperature and tint should leave pixels unchanged"))
        return 1;

    cv::Mat warmRgb = originalNeutral.clone();
    if (!check(ColorPipeline::applyTemperatureTint(warmRgb, 50000, 6500.0, 0, &error),
               "warm adjustment should succeed"))
        return 1;
    if (!check(warmRgb.at<cv::Vec3f>(0, 0)[0] > originalNeutral.at<cv::Vec3f>(0, 0)[0] &&
                   warmRgb.at<cv::Vec3f>(0, 0)[2] < originalNeutral.at<cv::Vec3f>(0, 0)[2],
               "positive temperature should warm the image"))
        return 1;

    cv::Mat coolRgb = originalNeutral.clone();
    if (!check(ColorPipeline::applyTemperatureTint(coolRgb, 2000, 6500.0, 0, &error),
               "cool adjustment should succeed"))
        return 1;
    if (!check(coolRgb.at<cv::Vec3f>(0, 0)[0] < originalNeutral.at<cv::Vec3f>(0, 0)[0] &&
                   coolRgb.at<cv::Vec3f>(0, 0)[2] > originalNeutral.at<cv::Vec3f>(0, 0)[2],
               "low color temperature should cool the image"))
        return 1;

    cv::Mat mediumCoolRgb = originalNeutral.clone();
    if (!check(ColorPipeline::applyTemperatureTint(mediumCoolRgb, 4000, 6500.0, 0, &error),
               "intermediate cool adjustment should succeed"))
        return 1;
    if (!check(coolRgb.at<cv::Vec3f>(0, 0)[0] < mediumCoolRgb.at<cv::Vec3f>(0, 0)[0] &&
                   coolRgb.at<cv::Vec3f>(0, 0)[2] > mediumCoolRgb.at<cv::Vec3f>(0, 0)[2],
               "cold calibration should strengthen progressively toward 2000 K"))
        return 1;

    cv::Mat magentaRgb = originalNeutral.clone();
    if (!check(ColorPipeline::applyTemperatureTint(magentaRgb, 6500, 6500.0, 150, &error),
               "magenta tint adjustment should succeed"))
        return 1;
    if (!check(magentaRgb.at<cv::Vec3f>(0, 0)[0] > originalNeutral.at<cv::Vec3f>(0, 0)[0] &&
                   magentaRgb.at<cv::Vec3f>(0, 0)[1] < originalNeutral.at<cv::Vec3f>(0, 0)[1] &&
                   magentaRgb.at<cv::Vec3f>(0, 0)[2] > originalNeutral.at<cv::Vec3f>(0, 0)[2],
               "positive tint should shift green toward magenta"))
        return 1;

    cv::Mat neutralGrey(1, 1, CV_32FC3, cv::Scalar(0.5f, 0.5f, 0.5f));
    if (!check(ColorPipeline::applyTemperatureTint(neutralGrey, 6500, 6500.0, -150, &error),
               "green tint adjustment should succeed"))
        return 1;
    const cv::Vec3f tintedGrey = neutralGrey.at<cv::Vec3f>(0, 0);
    const float tintedGreyLuminance = 0.2126f * tintedGrey[0] +
                                      0.7152f * tintedGrey[1] +
                                      0.0722f * tintedGrey[2];
    if (!check(std::abs(tintedGreyLuminance - 0.5f) < 0.0001f,
               "tint should preserve the luminance of a neutral grey"))
        return 1;

    if (!check(!ColorPipeline::applyTemperatureTint(neutralRgb, 1999, 6500.0, 0, &error),
               "temperature outside the supported range should be rejected"))
        return 1;
    if (!check(!ColorPipeline::applyTemperatureTint(neutralRgb, 6500, 6500.0, 151, &error),
               "tint outside the supported range should be rejected"))
        return 1;
    if (!check(!ColorPipeline::applyTemperatureTint(neutralRgb, 6500, 1999.0, 0, &error),
               "reference temperature outside the supported range should be rejected"))
        return 1;

    const QImage highBit = ColorPipeline::toSrgb(
        raw, ColorPipeline::OutputFormat::HighBitDepth, &error);
    if (!check(!highBit.isNull() && highBit.format() == QImage::Format_RGBA64,
               "high-bit output should remain RGBA64"))
        return 1;
    if (!check(highBit.colorSpace() == QColorSpace(QColorSpace::SRgb),
               "encoded output should be tagged sRGB"))
        return 1;
    const QImage rawPreview = ColorPipeline::toSrgb(
        raw, ColorPipeline::OutputFormat::Preview8Bit, &error);
    const QColor previewRawColor = rawPreview.pixelColor(0, 0);
    const QColor highBitRawColor = highBit.pixelColor(0, 0);
    if (!check(!rawPreview.isNull() &&
                   std::abs(previewRawColor.red() - highBitRawColor.red()) <= 1 &&
                   std::abs(previewRawColor.green() - highBitRawColor.green()) <= 1 &&
                   std::abs(previewRawColor.blue() - highBitRawColor.blue()) <= 1,
               "preview and high-bit export should use the same sRGB transform"))
        return 1;

    QByteArray pngData;
    QBuffer pngBuffer(&pngData);
    if (!check(pngBuffer.open(QIODevice::WriteOnly), "PNG buffer should open"))
        return 1;
    QImageWriter pngWriter(&pngBuffer, "png");
    if (!check(pngWriter.write(highBit), "16-bit PNG should encode"))
        return 1;
    pngBuffer.close();
    if (!check(pngBuffer.open(QIODevice::ReadOnly), "PNG buffer should reopen"))
        return 1;
    QImageReader pngReader(&pngBuffer, "png");
    const QImage decodedPng = pngReader.read();
    if (!check(!decodedPng.isNull(), "encoded PNG should decode"))
        return 1;
    if (!check(decodedPng.colorSpace().isValid() &&
                   decodedPng.colorSpace() == QColorSpace(QColorSpace::SRgb),
               "PNG should retain its sRGB profile"))
        return 1;

    const QImage jpeg = ColorPipeline::toSrgb(
        raw, ColorPipeline::OutputFormat::Jpeg8Bit, &error);
    if (!check(!jpeg.isNull() && jpeg.format() == QImage::Format_RGB888,
               "JPEG output should be 8-bit RGB without alpha"))
        return 1;

    QByteArray jpegData;
    QBuffer jpegBuffer(&jpegData);
    if (!check(jpegBuffer.open(QIODevice::WriteOnly), "JPEG buffer should open"))
        return 1;
    QImageWriter jpegWriter(&jpegBuffer, "jpeg");
    jpegWriter.setQuality(95);
    if (!check(jpegWriter.write(jpeg), "JPEG output should encode"))
        return 1;
    jpegBuffer.close();
    if (!check(jpegBuffer.open(QIODevice::ReadOnly), "JPEG buffer should reopen"))
        return 1;
    QImageReader jpegReader(&jpegBuffer, "jpeg");
    const QImage decodedJpeg = jpegReader.read();
    if (!check(!decodedJpeg.isNull(), "encoded JPEG should decode"))
        return 1;
    if (!check(decodedJpeg.colorSpace().isValid() &&
                   decodedJpeg.colorSpace() == QColorSpace(QColorSpace::SRgb),
               "JPEG should retain its sRGB profile"))
        return 1;

    const QList<QByteArray> supportedFormats = QImageWriter::supportedImageFormats();
    if (supportedFormats.contains("tiff") || supportedFormats.contains("tif")) {
        QByteArray tiffData;
        QBuffer tiffBuffer(&tiffData);
        if (!check(tiffBuffer.open(QIODevice::WriteOnly), "TIFF buffer should open"))
            return 1;
        QImageWriter tiffWriter(&tiffBuffer, "tiff");
        if (!check(tiffWriter.write(highBit), "16-bit TIFF should encode"))
            return 1;
        tiffBuffer.close();
        if (!check(tiffBuffer.open(QIODevice::ReadOnly), "TIFF buffer should reopen"))
            return 1;
        QImageReader tiffReader(&tiffBuffer, "tiff");
        const QImage decodedTiff = tiffReader.read();
        if (!check(!decodedTiff.isNull(), "encoded TIFF should decode"))
            return 1;
        if (!check(decodedTiff.colorSpace().isValid() &&
                       decodedTiff.colorSpace() == QColorSpace(QColorSpace::SRgb),
                   "TIFF should retain its sRGB profile"))
            return 1;
    }

    QImage untagged(1, 1, QImage::Format_RGBA8888);
    untagged.setPixelColor(0, 0, QColor(128, 128, 128, 255));
    const ColorPipeline::WorkingImage assumedSrgb =
        ColorPipeline::fromSourceImage(untagged, &error);
    if (!check(!assumedSrgb.isNull() &&
                   assumedSrgb.sourceProfile.contains(QStringLiteral("supposé")),
               "an untagged image should use and report the documented sRGB fallback"))
        return 1;
    if (!check(std::abs(assumedSrgb.linearRgba.at<cv::Vec4f>(0, 0)[0] -
                        gray[0]) < 0.002f,
               "untagged input should be interpreted as sRGB"))
        return 1;

    ColorPipeline::WorkingImage outOfGamut;
    outOfGamut.linearRgba = cv::Mat(1, 1, CV_32FC4);
    outOfGamut.linearRgba.at<cv::Vec4f>(0, 0) =
        cv::Vec4f(1.20f, 0.70f, 0.40f, 1.0f);
    const ColorPipeline::WorkingImage gamutMapped =
        ColorPipeline::mapToSrgbGamut(outOfGamut, &error);
    const cv::Vec4f mapped = gamutMapped.linearRgba.at<cv::Vec4f>(0, 0);
    const float mappedLuminance =
        0.2126f * mapped[0] + 0.7152f * mapped[1] + 0.0722f * mapped[2];
    if (!check(!gamutMapped.isNull() &&
                   mapped[0] <= 1.0f && mapped[1] >= 0.0f &&
                   mapped[2] >= 0.0f &&
                   std::abs(mappedLuminance - 0.78464f) < 0.001f,
               "gamut mapping should preserve luminance inside sRGB"))
        return 1;
    if (!check(mapped[1] > 0.70f && mapped[2] > 0.40f,
               "gamut mapping should desaturate toward neutral instead of clipping channels"))
        return 1;

    QImage translucent(1, 1, QImage::Format_RGBA8888);
    translucent.setPixelColor(0, 0, QColor(64, 100, 150, 96));
    translucent.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const ColorPipeline::WorkingImage alphaInput =
        ColorPipeline::fromSourceImage(translucent, &error);
    const QImage alphaOutput = ColorPipeline::toSrgb(
        alphaInput, ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (!check(!alphaOutput.isNull() &&
                   std::abs(alphaOutput.pixelColor(0, 0).alpha() - 96) <= 1,
               "straight alpha should be preserved by input and output conversion"))
        return 1;

    ColorPipeline::WorkingImage invalidType = linear;
    invalidType.linearRgba = cv::Mat(1, 1, CV_32FC3, cv::Scalar(0.5f));
    if (!check(ColorPipeline::toSrgb(
                   invalidType, ColorPipeline::OutputFormat::Preview8Bit,
                   &error).isNull(),
               "invalid working pixel layout should be rejected"))
        return 1;

    ColorPipeline::WorkingImage invalidValues = linear;
    invalidValues.linearRgba.at<cv::Vec4f>(0, 0)[0] =
        std::numeric_limits<float>::quiet_NaN();
    if (!check(ColorPipeline::toSrgb(
                   invalidValues, ColorPipeline::OutputFormat::Preview8Bit,
                   &error).isNull(),
               "non-finite working pixels should be rejected"))
        return 1;

    return 0;
}
