#include "ImageImporter.h"
#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const auto check=[](bool ok, const char *why) {
        if (!ok) std::cerr << "FAILED: " << why << '\n';
        return ok;
    };
    for (const auto &format : {QByteArray("png"), QByteArray("jpeg"), QByteArray("tiff")}) {
        for (bool tagged : {false, true}) {
            const auto path=dir.filePath(QString::fromLatin1(format)+(tagged?"-icc.":"-untagged.")+QString::fromLatin1(format));
            QImage source(7, 3, QImage::Format_RGBA64);
            source.fill(QColor::fromRgbF(.26, .46, .69, 1.));
            if (tagged) source.setColorSpace(QColorSpace(QColorSpace::AdobeRgb));
            QImageWriter writer(path, format);
            if (format=="png") {
                writer.setText("CameraMake", "Synthetic maker");
                writer.setText("ExposureTime", "1/125");
                writer.setText("WhiteBalanceTemperature", "5200 K");
                writer.setText("Rating", "4");
            }
            if (!check(writer.write(source), "required raster writer failed")) return 1;
            QImageReader reader(path);reader.setAutoTransform(true);
            const auto decoded=reader.read();
            QString error;
            const auto expected=ColorPipeline::fromSourceImage(decoded, &error);
            const auto result=ImageImport::load(path);
            if (!check(!result.image.isNull() && result.error.isEmpty() &&
                result.image.linearRgba.cols==7 && result.image.linearRgba.rows==3 &&
                cv::norm(result.image.linearRgba, expected.linearRgba, cv::NORM_INF)==0 &&
                result.image.colorSpace==ColorPipeline::workingColorSpace(),
                "raster adapter reaches common full-resolution master without extra transform")) return 1;
            if (!check(!result.capabilities.cameraDevelopment &&
                !result.capabilities.cameraWhiteBalanceContext && !result.capabilities.profileWithoutRedecode &&
                result.provenance.decoderId=="qt-image-reader" && !result.metadata.cameraRaw,
                "raster never advertises RAW development capabilities")) return 1;
            const bool hasIcc=decoded.colorSpace().isValid();
            if (!check(result.metadata.hasEmbeddedColorProfile==hasIcc &&
                result.provenance.colorOrigin==(hasIcc?ImageImport::ColorOrigin::EmbeddedIcc:ImageImport::ColorOrigin::AssumedSrgb) &&
                (hasIcc || !result.warnings.isEmpty()), "ICC provenance and assumed-sRGB warning follow actual decode")) return 1;
            if (tagged && !check(hasIcc, "embedded ICC must survive each tested container")) return 1;
            if (format=="png" && !check(result.metadata.cameraMake=="Synthetic maker" &&
                result.metadata.rating==4 && std::abs(result.metadata.exposureSeconds-.008)<1e-12 &&
                result.image.referenceWhiteBalanceKelvin==5200.,
                "raster metadata extraction moved without losing numeric or WB fields")) return 1;
        }
    }
    {
        QFile original(dir.filePath("jpeg-untagged.jpeg"));
        if (!original.open(QIODevice::ReadOnly)) return 1;
        auto bytes=original.readAll();
        // Minimal EXIF IFD: orientation 6 (90 degrees clockwise), no pixel rotation baked in.
        bytes.insert(2,QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
        const auto path=dir.filePath("orientation.jpg");
        QFile rotated(path);
        if (!rotated.open(QIODevice::WriteOnly) || rotated.write(bytes)!=bytes.size()) return 1;
        rotated.close();
        const auto result=ImageImport::load(path);
        if (!check(!result.image.isNull() && result.image.linearRgba.cols==3 && result.image.linearRgba.rows==7,
                   "JPEG EXIF orientation is applied exactly at import")) return 1;
    }
    QImage alpha(2,1,QImage::Format_RGBA64);
    alpha.setColorSpace(QColorSpace(QColorSpace::SRgb));
    auto *pixels=reinterpret_cast<QRgba64 *>(alpha.bits());
    pixels[0]=QRgba64::fromRgba64(10000,20000,30000,17000);
    pixels[1]=QRgba64::fromRgba64(10001,20000,30000,65535);
    const auto alphaPath=dir.filePath("alpha16.png");
    if (!alpha.save(alphaPath)) return 1;
    const auto importedAlpha=ImageImport::load(alphaPath);
    if (!check(!importedAlpha.image.isNull() &&
        std::abs(importedAlpha.image.linearRgba.at<cv::Vec4f>(0,0)[3]-17000.f/65535.f)<1e-6 &&
        importedAlpha.image.linearRgba.at<cv::Vec4f>(0,0)[0]!=importedAlpha.image.linearRgba.at<cv::Vec4f>(0,1)[0],
        "PNG alpha and adjacent 16bit values survive import")) return 1;

    QString registrationError;
    const RawDecoder::FormatOptions format{{}, "synthetic-decoder", "1"};
    if (!RawDecoder::registerFormatHandler({"importmock"}, [](const QString &,const RawDecoder::Options &,
        ImageMetadata *metadata,QString *) {
        metadata->cameraMake="Synthetic";metadata->cameraModel="Synthetic camera";
        ColorPipeline::WorkingImage master;
        master.linearRgba=cv::Mat(3,7,CV_32FC4,cv::Scalar(-.1f,.2f,1.4f,1.f)).clone();
        master.sourceProfile="Synthetic matrix, not a real camera characterization";
        return master;
    }, &registrationError, format)) return 1;
    const auto raw=ImageImport::load("not-a-real-file.importmock");
    if (!check(!raw.image.isNull() && raw.capabilities.cameraDevelopment &&
        raw.provenance.colorOrigin==ImageImport::ColorOrigin::DecoderMatrix &&
        raw.provenance.decoderId=="synthetic-decoder" && raw.provenance.decoderVersion=="1" &&
        raw.image.linearRgba.at<cv::Vec4f>(0,0)[0]<0 && raw.image.linearRgba.at<cv::Vec4f>(0,0)[2]>1,
        "future RAW adapter shares contract without clipping or fake DCP")) return 1;
    auto cached=raw;
    if (!check(ImageImport::complete(cached) && cached.image.linearRgba.data==raw.image.linearRgba.data,
               "cache descriptors are rebuilt without pixel copying")) return 1;
    for (int failure=0; failure<5; ++failure) {
        auto invalid=raw;invalid.image.linearRgba=raw.image.linearRgba.clone();
        if (failure==0) invalid.image.colorSpace=QColorSpace(QColorSpace::SRgbLinear);
        if (failure==1) invalid.image.linearRgba.at<cv::Vec4f>(0,0)[0]=std::numeric_limits<float>::quiet_NaN();
        if (failure==2) invalid.image.linearRgba.at<cv::Vec4f>(0,0)[3]=1.01f;
        if (failure==3) invalid.image.linearRgba=cv::Mat::zeros(2,2,CV_8UC4);
        if (failure==4) invalid.error="Decoder reported a failure";
        if (!check(!ImageImport::complete(invalid) && invalid.image.isNull() && !invalid.dcpSource &&
                   !invalid.capabilities.cameraDevelopment && !invalid.error.isEmpty(),
                   "invalid imports cannot leak a usable master or stale capabilities")) return 1;
    }
    const auto missing=ImageImport::load(dir.filePath("missing.png"));
    if (!check(missing.image.isNull() && !missing.error.isEmpty(), "missing files fail explicitly")) return 1;
    std::cout << "Common import: JPEG/PNG/TIFF, ICC, alpha, 16bit, synthetic RAW, cache and invalid results passed\n";
    return 0;
}
