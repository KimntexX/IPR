// Test-only extraction of the actual signed, balanced camera buffer of IPR.
// No changes to the engine, display rendering, source files or sidecars.
#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

QString hash(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open file for hashing");
    QCryptographicHash digest(QCryptographicHash::Sha256);
    require(digest.addData(&file), "Hash failed");
    return QString::fromLatin1(digest.result().toHex());
}

QJsonObject writeMatrix(QFile &file, const cv::Mat &matrix)
{
    require(matrix.depth() == CV_32F && !matrix.empty() && cv::checkRange(matrix),
            "Invalid camera data");
    const auto offset = file.pos();
    const qint64 rowBytes = qint64(matrix.cols) * qint64(matrix.elemSize());
    for (int y = 0; y < matrix.rows; ++y)
        require(file.write(reinterpret_cast<const char *>(matrix.ptr(y)), rowBytes) == rowBytes,
                "Incomplete float capture");
    return QJsonObject{{"offset_bytes", double(offset)}, {"dtype", "<f4"},
        {"shape", QJsonArray{matrix.rows, matrix.cols, matrix.channels()}}};
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    for (auto key : {"input", "output", "profile", "manifest"})
        parser.addOption(QCommandLineOption(key, key, "path"));
    parser.process(app);
    try {
        for (auto key : {"input", "output", "profile", "manifest"})
            require(parser.isSet(key), "Need input/output/profile/manifest");
        const QString source = QFileInfo(parser.value("input")).absoluteFilePath();
        const QString output = QFileInfo(parser.value("output")).absoluteFilePath();
        require(!QFile::exists(output) && !QFile::exists(output + ".bin"), "Output exists");
        const quint16 endian = 1;
        require(*reinterpret_cast<const unsigned char *>(&endian) == 1, "Little-endian capture required");
        const auto sourceHash = hash(source);
        const auto profileHash = hash(parser.value("profile"));
        const auto manifestHash = hash(parser.value("manifest"));
        cv::setNumThreads(1);
        RawDecoder::Options options;
        options.automaticWhiteBalance = false;
        options.dcpProfileOverride = parser.value("profile");
        options.profileDirectories = QStringList{QFileInfo(parser.value("profile")).absolutePath()};
        options.normalizationManifest = parser.value("manifest");
        options.useNormalizationEnvironment = false;
        options.automaticLcpSelection = false;
        options.lensCorrectionEnabled = false;
        options.chromaticAberrationEnabled = false;
        options.signedDevelopment = true;
        QString error;
        ImageMetadata metadata;
        QElapsedTimer timer;
        timer.start();
        auto image = RawDecoder::decode(source, options, &metadata, &error);
        if (image.isNull()) throw std::runtime_error(error.toStdString());
        require(image.dcpContext && !image.dcpContext->cameraRgb.empty(), "No retained camera signal");
        require(metadata.rawNormalizationApplied, "Production calibration did not apply; refusing training extraction");
        require(!metadata.lensGeometryApplied && !metadata.lensVignettingApplied &&
                !metadata.chromaticAberrationApplied, "Unexpected optics");
        require(hash(image.dcpContext->profilePath) == profileHash, "Wrong profile");
        const auto &rgb = image.dcpContext->cameraRgb;
        require(rgb.type() == CV_32FC3 && cv::checkRange(rgb), "Invalid signed camera buffer");
        require(rgb.rows >= 1200 && rgb.cols >= 1200, "Insufficient image extent");
        const double decodeMs = double(timer.nsecsElapsed())/1e6;
        QFile binary(output + ".bin");
        require(binary.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create camera capture");
        QJsonArray tiles;
        for (double fy : {.2, .5, .8}) for (double fx : {.2, .5, .8}) {
            const int x = int(rgb.cols * fx) - 192, y = int(rgb.rows * fy) - 192;
            auto descriptor = writeMatrix(binary, rgb(cv::Rect(x, y, 384, 384)));
            descriptor.insert("origin_xy", QJsonArray{x, y});
            tiles.append(descriptor);
        }
        cv::Mat green, proxy;
        cv::extractChannel(rgb, green, 1);
        cv::resize(green, proxy, cv::Size(800, int(std::lround(double(rgb.rows)*800/rgb.cols))),
                   0, 0, cv::INTER_AREA);
        const auto proxyDescription = writeMatrix(binary, proxy);
        binary.close();
        require(hash(source) == sourceHash && hash(parser.value("profile")) == profileHash &&
                hash(parser.value("manifest")) == manifestHash, "Input changed during extraction");
        QJsonObject result{{"schema_version", 1}, {"engine_revision", IPR_NEURAL_REVISION},
            {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source", source}, {"source_sha256", sourceHash},
            {"profile_sha256", profileHash}, {"manifest_sha256", manifestHash},
            {"pixels_sha256", hash(binary.fileName())},
            {"pixels", QFileInfo(binary.fileName()).fileName()}, {"tiles", tiles},
            {"green_proxy", proxyDescription}, {"full_shape", QJsonArray{rgb.rows, rgb.cols, 3}},
            {"decode_ms", decodeMs}, {"iso", metadata.iso}, {"model", metadata.cameraModel},
            {"calibration_applied", metadata.rawNormalizationApplied},
            {"calibration_white_dn", int(metadata.normalizationWhiteLevel)},
            {"black_estimated", metadata.opticalBlackLevelEstimated},
            {"camera_neutral", QJsonArray{image.dcpContext->cameraNeutral[0],
                image.dcpContext->cameraNeutral[1], image.dcpContext->cameraNeutral[2]}},
            {"profile_path", image.dcpContext->profilePath},
            {"source_kelvin", image.dcpContext->sourceKelvin},
            {"dcp_options", QJsonObject{
                {"camera_baseline_exposure_ev", image.dcpContext->options.cameraBaselineExposureEv},
                {"preserve_tone_curve_hue", image.dcpContext->options.preserveToneCurveHue},
                {"profile_exposure_compensation_ev", image.dcpContext->options.profileExposureCompensationEv}}},
            {"domain", "signed balanced camera RGB, post-RCD/median, before LCP/DCP; no denoise"}};
        QFile json(output);
        require(json.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create capture descriptor");
        const auto bytes = QJsonDocument(result).toJson();
        require(json.write(bytes) == bytes.size(), "Incomplete capture descriptor");
        std::cout << "decode_ms=" << decodeMs << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
