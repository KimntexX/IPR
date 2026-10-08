// Manual private-corpus diagnostic, excluded from CTest/default build.
#include "RawDecoder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstring>

int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    if (argc != 5) {
        QTextStream(stderr) << "Usage: rawNormalizationCorpusProbe RAW DCP MANIFEST NEW_OUTPUT_DIRECTORY\n";
        return 2;
    }
    const auto args = app.arguments();
    QTemporaryDir profiles;
    QDir out(args[4]);
    if (!profiles.isValid() || out.exists() || !QDir().mkpath(out.absolutePath()) ||
        !QFile::copy(args[2], profiles.filePath(QFileInfo(args[2]).fileName()))) return 3;
    RawDecoder::Options options{false, {profiles.path()}, args[3]};
    RawDecoder::SourceMetadata metadata;
    QString error;
    auto image = RawDecoder::decode(args[1],options,&metadata,&error);
    if (image.isNull() || !metadata.rawNormalizationApplied || !metadata.lcpProfile.isEmpty()) {
        QTextStream(stderr) << "Decode/contract failed: " << error << '\n';
        return 4;
    }
    // The diagnostic profile copy is confined to a temporary folder; no LUT
    // payload is stored in the repository or in the resulting measurements.
    const int rows=(image.linearRgba.rows+7)/8, cols=(image.linearRgba.cols+7)/8;
    cv::Mat grid(rows,cols,CV_32FC4);
    for (int y=0;y<rows;++y)
        for (int x=0;x<cols;++x)
            grid.at<cv::Vec4f>(y,x)=image.linearRgba.at<cv::Vec4f>(8*y,8*x);
    image.linearRgba=grid;
    image=ColorPipeline::mapToSrgbGamut(image,&error);
    const auto encoded=ColorPipeline::toSrgb(image,ColorPipeline::OutputFormat::HighBitDepth,&error);
    if (encoded.isNull()) return 5;
    cv::Mat rgb(rows,cols,CV_32FC3);
    for(int y=0;y<rows;++y) {
        const auto *line=reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
        for(int x=0;x<cols;++x)
            rgb.at<cv::Vec3f>(y,x)=cv::Vec3f(line[x].red(),line[x].green(),line[x].blue())/65535.f;
    }
    QFile pixels(out.filePath("encoded.f32"));
    const qint64 bytes=qint64(rgb.total()*rgb.elemSize());
    if (!pixels.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
        pixels.write(reinterpret_cast<const char *>(rgb.data),bytes)!=bytes) return 6;
    pixels.close();
    const QJsonObject proof{{"raw",args[1]}, {"profile",args[2]}, {"rows",rows},{"cols",cols},
        {"white_level",int(metadata.normalizationWhiteLevel)},
        {"baseline_exposure_ev",metadata.normalizationBaselineExposureEv},
        {"normalization_applied",metadata.rawNormalizationApplied},
        {"provenance",metadata.normalizationProvenance}, {"no_lcp",true}};
    QFile meta(out.filePath("manifest.json"));
    const auto json=QJsonDocument(proof).toJson();
    if (!meta.open(QIODevice::WriteOnly|QIODevice::NewOnly) || meta.write(json)!=json.size()) return 7;
    QTextStream(stdout) << json;
    return 0;
}
