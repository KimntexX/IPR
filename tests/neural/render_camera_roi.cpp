// Bench-only shared Camera Neutral -> Rec.2020 -> encoded sRGB display of camera ROI.
#include "CameraProfileEngine.h"
#include "ColorPipeline.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
QString hash(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot hash display input");
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Hash failed");
    return QString::fromLatin1(value.result().toHex());
}
}
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    for (const char *key : {"input", "context", "output"})
        parser.addOption(QCommandLineOption(key, key, "path"));
    parser.addOption(QCommandLineOption("halo", "Additional display context in pixels", "pixels", "0"));
    parser.addOption(QCommandLineOption("tiles", "Input contains nine contiguous RGB ROI tiles, diagnostic only"));
    parser.process(application);
    try {
        for (const char *key : {"input", "context", "output"}) require(parser.isSet(key), "Missing input/context/output");
        const QString prefix = parser.value("output");
        require(!QFile::exists(prefix + ".json"), "Display output exists");
        QFile contextFile(parser.value("context"));
        require(contextFile.open(QIODevice::ReadOnly), "Missing camera context");
        const auto context = QJsonDocument::fromJson(contextFile.readAll()).object();
        const auto shape = context.value("full_shape").toArray();
        require(shape.size() == 3 && shape[2].toInt() == 3, "Invalid RGB shape");
        const int height = shape[0].toInt(), width = shape[1].toInt();
        bool haloOk = false;
        const int halo = parser.value("halo").toInt(&haloOk);
        require(haloOk && halo >= 0 && halo <= 384, "Invalid display halo");
        const int side = 384 + 2 * halo;
        require(height > 0 && width > 0, "Invalid image extent");
        QFile input(parser.value("input"));
        const bool tileInput = parser.isSet("tiles");
        require(!tileInput || halo == 0, "Tile diagnostic cannot request extra context");
        const qint64 expectedBytes = tileInput ? qint64(9) * 384 * 384 * 12 : qint64(height) * width * 12;
        require(input.open(QIODevice::ReadOnly) && input.size() == expectedBytes, "Invalid camera buffer");
        const auto profile = context.value("profile_path").toString();
        require(!profile.isEmpty() && hash(profile) == context.value("profile_sha256").toString(), "Display profile changed");
        const double kelvin = context.value("source_kelvin").toDouble();
        require(kelvin > 0, "Missing actual illuminant");
        const auto declared = context.value("dcp_options").toObject();
        CameraProfileEngine::DcpRenderOptions options;
        options.cameraBaselineExposureEv = declared.value("camera_baseline_exposure_ev").toDouble();
        options.preserveToneCurveHue = declared.value("preserve_tone_curve_hue").toBool();
        options.profileExposureCompensationEv = declared.value("profile_exposure_compensation_ev").toDouble();
        QJsonArray records;
        const auto tiles = context.value("tiles").toArray();
        require(tiles.size() == 9, "Nine fixed display regions required");
        for (int index = 0; index < tiles.size(); ++index) {
            const auto xy = tiles[index].toObject().value("origin_xy").toArray();
            require(xy.size() == 2, "Missing ROI origin");
            const int x = xy[0].toInt() - halo, y = xy[1].toInt() - halo;
            require(x >= 0 && y >= 0 && x + side <= width && y + side <= height, "ROI outside image");
            cv::Mat rgb(side, side, CV_32FC3);
            for (int row = 0; row < side; ++row) {
                const qint64 offset = tileInput ? (qint64(index) * 384 * 384 + row * 384) * 12
                                                : (qint64(y + row) * width + x) * 12;
                require(input.seek(offset), "Cannot seek camera row");
                require(input.read(reinterpret_cast<char *>(rgb.ptr(row)), side * 12) == side * 12, "Truncated camera row");
            }
            require(cv::checkRange(rgb), "Nonfinite camera pixels");
            QString error;
            require(CameraProfileEngine::applyDcp(profile, rgb, kelvin, &error, options), "Profile render failed");
            ColorPipeline::WorkingImage working;
            cv::cvtColor(rgb, working.linearRgba, cv::COLOR_RGB2RGBA);
            const auto display = ColorPipeline::toSrgb(working, ColorPipeline::OutputFormat::Preview8Bit, &error);
            require(!display.isNull(), "Display encoding failed");
            const auto path = prefix + QStringLiteral("-roi%1.png").arg(index);
            require(!QFile::exists(path) && display.save(path, "PNG"), "Cannot create PNG");
            records.append(QJsonObject{{"roi", index}, {"origin_xy", xy}, {"halo", halo}, {"path", path}, {"sha256", hash(path)}});
        }
        const QJsonObject proof{{"schema_version", 1}, {"engine_revision", IPR_NEURAL_REVISION},
            {"tile_diagnostic_only", tileInput},
            {"input", parser.value("input")}, {"input_sha256", hash(parser.value("input"))},
            {"context_sha256", hash(parser.value("context"))}, {"profile_sha256", hash(profile)},
            {"source_kelvin", kelvin}, {"dcp_options", declared}, {"rois", records},
            {"scope", "Shared frozen DCP/Rec2020-to-sRGB display; no independent stretching, optics or additional edits"}};
        QFile output(prefix + ".json");
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Display proof exists");
        const auto bytes = QJsonDocument(proof).toJson();
        require(output.write(bytes) == bytes.size(), "Incomplete display proof");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
