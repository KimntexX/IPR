// Test-only reuse of IPR's exact private input preparation, following the
// existing tonal_base_probe pattern. No production API or LibRaw code copy.
#define RawDecoder NeuralMosaicDecoder
#include "../../src/raw/RawDecoder.cpp"
#undef RawDecoder

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

QString hash(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot hash input");
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Incomplete hash");
    return QString::fromLatin1(value.result().toHex());
}

cv::Point sensorPoint(int x, int y, int width, int height, int orientation)
{
    if (orientation & 4) std::swap(x, y);
    if (orientation & 1) x = width - 1 - x;
    if (orientation & 2) y = height - 1 - y;
    return {x, y};
}

cv::Mat orientedCrop(const cv::Mat &sensor, const cv::Point &origin, int orientation)
{
    const auto a = sensorPoint(origin.x, origin.y, sensor.cols, sensor.rows, orientation);
    const auto b = sensorPoint(origin.x + 383, origin.y + 383, sensor.cols, sensor.rows, orientation);
    cv::Rect rect(std::min(a.x, b.x), std::min(a.y, b.y), 384, 384);
    require(rect.x >= 0 && rect.y >= 0 && rect.br().x <= sensor.cols && rect.br().y <= sensor.rows,
            "Context outside sensor image");
    return RawDevelopment::orient(sensor(rect).clone(), orientation);
}

QJsonObject writeMatrix(QFile &file, const cv::Mat &matrix)
{
    require(matrix.depth() == CV_32F && !matrix.empty() && cv::checkRange(matrix), "Invalid mosaic data");
    const auto offset = file.pos();
    const qint64 rowBytes = qint64(matrix.cols) * qint64(matrix.elemSize());
    for (int y = 0; y < matrix.rows; ++y)
        require(file.write(reinterpret_cast<const char *>(matrix.ptr(y)), rowBytes) == rowBytes,
                "Incomplete float write");
    return {{"dtype", "<f4"}, {"offset_bytes", double(offset)},
            {"shape", QJsonArray{matrix.rows, matrix.cols, matrix.channels()}}};
}

QJsonArray four(const std::array<double, 4> &values)
{
    QJsonArray result;
    for (double value : values) result.append(value);
    return result;
}

void selfTest()
{
    cv::Mat sensor(800, 900, CV_32FC1);
    for (int y = 0; y < sensor.rows; ++y)
        for (int x = 0; x < sensor.cols; ++x) sensor.at<float>(y, x) = float(y * 1000 + x) - 400000.f;
    for (int orientation = 0; orientation < 8; ++orientation) {
        const auto full = RawDevelopment::orient(sensor.clone(), orientation);
        for (const cv::Point origin : {cv::Point{24, 24}, cv::Point{25, 27}}) {
            const auto crop = orientedCrop(sensor, origin, orientation);
            require(cv::norm(crop, full(cv::Rect(origin.x, origin.y, 384, 384)), cv::NORM_INF) == 0.,
                    "Crop/orientation self-test failed");
            const auto point = sensorPoint(origin.x, origin.y, sensor.cols, sensor.rows, orientation);
            require(crop.at<float>(0, 0) == sensor.at<float>(point.y, point.x), "CFA origin self-test failed");
        }
    }
    std::cout << "16 signed orientation/crop checks passed\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(QCommandLineOption("self-test", "Check all orientation and CFA-origin mappings"));
    parser.addOption(QCommandLineOption("full", "Also write the sensor-frame full balanced CFA for whole-image comparison"));
    for (const char *key : {"camera-proof", "output"}) parser.addOption(QCommandLineOption(key, key, "path"));
    parser.process(app);
    try {
        if (parser.isSet("self-test")) {
            selfTest();
            return 0;
        }
        require(parser.isSet("camera-proof") && parser.isSet("output"), "Need camera-proof and output");
        const auto proofPath = QFileInfo(parser.value("camera-proof")).absoluteFilePath();
        QFile proofFile(proofPath);
        require(proofFile.open(QIODevice::ReadOnly), "Missing camera proof");
        const auto proof = QJsonDocument::fromJson(proofFile.readAll()).object();
        const auto source = proof.value("source").toString();
        require(!source.isEmpty() && proof.value("calibration_applied").toBool(), "Calibrated camera proof required");
        require(hash(source) == proof.value("source_sha256").toString(), "Changed RAW");
        const auto oldPixels = QFileInfo(proofPath).dir().filePath(proof.value("pixels").toString());
        require(hash(oldPixels) == proof.value("pixels_sha256").toString(), "Changed camera pixels");
        const auto output = QFileInfo(parser.value("output")).absoluteFilePath();
        require(!QFile::exists(output) && !QFile::exists(output + ".bin") &&
                (!parser.isSet("full") || !QFile::exists(output + ".full.f32")), "Output exists");
        const quint16 endian = 1;
        require(*reinterpret_cast<const unsigned char *>(&endian) == 1, "Little endian required");
        cv::setNumThreads(1);
        LibRaw raw;
        require(NeuralMosaicDecoder::openRaw(raw, source) == LIBRAW_SUCCESS, "RAW open failed");
        require(raw.unpack() == LIBRAW_SUCCESS, "RAW unpack failed");
        const bool blackEstimated = NeuralMosaicDecoder::readBlackLevel(raw, source);
        const int white = proof.value("calibration_white_dn").toInt();
        require(white > 0 && white <= 65535, "Invalid frozen calibration white");
        raw.imgdata.color.maximum = raw.imgdata.rawdata.color.maximum = unsigned(white);
        raw.imgdata.params.adjust_maximum_thr = 0.f;
        const auto input = LibRawInput::read(raw);
        require(RawInput::valid(input), "Unsupported Bayer input");
        require(int(std::lround(input.iso)) == proof.value("iso").toInt(), "ISO mismatch");
        require(input.model == proof.value("model").toString(), "Camera mismatch");
        const auto originalTiles = proof.value("tiles").toArray();
        require(originalTiles.size() == 9, "Need nine frozen camera regions");
        QFile binary(output + ".bin");
        require(binary.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create output");
        QJsonArray stages;
        QJsonObject fullDescriptor;
        RawDevelopment::Context applied;
        RawDevelopment::Options options;
        options.policy = RawDevelopment::Policy::Signed;
        options.adjustWhiteThreshold = 0.;
        options.median = Demosaic::OpeningMedian;
        options.observer = [&](const RawDevelopment::Capture &capture) {
            if (capture.stage != RawDevelopment::Stage::NormalizedCfa &&
                capture.stage != RawDevelopment::Stage::PreDemosaicCfa) return;
            applied = capture.context;
            if (parser.isSet("full") && capture.stage == RawDevelopment::Stage::PreDemosaicCfa) {
                require(fullDescriptor.isEmpty(), "Full CFA captured more than once");
                QFile full(output + ".full.f32");
                require(full.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create full CFA");
                fullDescriptor = writeMatrix(full, capture.pixels);
                full.close();
                QJsonArray pattern;
                for (int y = 0; y < 2; ++y) {
                    QJsonArray row;
                    for (int x = 0; x < 2; ++x) {
                        row.append(input.pattern[size_t(y)][size_t(x)]);
                    }
                    pattern.append(row);
                }
                fullDescriptor.insert("pattern", pattern);
                fullDescriptor.insert("pixels", QFileInfo(full.fileName()).fileName());
                fullDescriptor.insert("pixels_sha256", hash(full.fileName()));
                fullDescriptor.insert("stage", "balanced_cfa_pre_rcd_white_clipped");
                fullDescriptor.insert("orientation_applied", false);
            }
            for (int roi = 0; roi < originalTiles.size(); ++roi) {
                const auto xy = originalTiles[roi].toObject().value("origin_xy").toArray();
                require(xy.size() == 2, "Invalid camera origin");
                const cv::Point origin(xy[0].toInt(), xy[1].toInt());
                auto descriptor = writeMatrix(binary, orientedCrop(capture.pixels, origin, input.orientation));
                QJsonArray pattern;
                for (int y = 0; y < 2; ++y) {
                    QJsonArray row;
                    for (int x = 0; x < 2; ++x) {
                        const auto point = sensorPoint(origin.x + x, origin.y + y,
                            input.mosaic.cols, input.mosaic.rows, input.orientation);
                        row.append(input.pattern[size_t(point.y & 1)][size_t(point.x & 1)]);
                    }
                    pattern.append(row);
                }
                descriptor.insert("roi", roi);
                descriptor.insert("stage", capture.stage == RawDevelopment::Stage::NormalizedCfa
                    ? "normalized_cfa_before_wb_unclipped" : "balanced_cfa_pre_rcd_white_clipped");
                descriptor.insert("origin_xy", xy);
                descriptor.insert("pattern", pattern);
                stages.append(descriptor);
            }
        };
        auto developed = RawDevelopment::develop(input, options);
        require(!developed.cameraRgb.empty(), "Development failed");
        auto rgb = RawDevelopment::orient(std::move(developed.cameraRgb), input.orientation);
        const auto fullShape = proof.value("full_shape").toArray();
        require(fullShape.size() == 3 && rgb.rows == fullShape[0].toInt() && rgb.cols == fullShape[1].toInt(),
                "Orientation or visible dimensions differ");
        if (parser.isSet("full"))
            require(fullDescriptor.value("shape").toArray() == QJsonArray{input.mosaic.rows, input.mosaic.cols, 1}, "Full CFA dimensions differ");
        QFile reference(oldPixels);
        require(reference.open(QIODevice::ReadOnly), "Missing camera pixels");
        for (int roi = 0; roi < originalTiles.size(); ++roi) {
            const auto previous = originalTiles[roi].toObject();
            require(previous.value("dtype").toString() == "<f4" &&
                previous.value("shape").toArray() == QJsonArray{384, 384, 3}, "Unexpected camera region type");
            const auto xy = previous.value("origin_xy").toArray();
            const cv::Mat tile = rgb(cv::Rect(xy[0].toInt(), xy[1].toInt(), 384, 384)).clone();
            require(reference.seek(qint64(previous.value("offset_bytes").toDouble())), "Invalid reference offset");
            const auto bytes = reference.read(384 * 384 * 3 * 4);
            require(bytes.size() == 384 * 384 * 3 * 4 &&
                std::memcmp(bytes.constData(), tile.data, size_t(bytes.size())) == 0, "RGB replay differs from production capture");
        }
        binary.close();
        require(hash(source) == proof.value("source_sha256").toString(), "RAW changed during capture");
        QJsonObject result{{"schema_version", 1}, {"tool_revision", IPR_NEURAL_REVISION},
            {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source", source}, {"source_sha256", proof.value("source_sha256")},
            {"camera_proof", proofPath}, {"camera_proof_sha256", hash(proofPath)},
            {"iso", input.iso}, {"model", input.model}, {"orientation_flip", input.orientation},
            {"sensor_origin_xy", QJsonArray{input.origin.x, input.origin.y}},
            {"black", four(applied.black)}, {"white", applied.white}, {"divisor", applied.divisor},
            {"gains", four(applied.gains)}, {"black_estimated", blackEstimated},
            {"rgb_replay_bit_exact", true}, {"rgb_regions_compared", 9}, {"stages", stages},
            {"pixels", QFileInfo(binary.fileName()).fileName()}, {"pixels_sha256", hash(binary.fileName())},
            {"scope", "Bench only; frozen production calibration; signed mosaics; no new training or clean-target claim"}};
        QFile destination(output);
        if (parser.isSet("full")) result.insert("full_balanced_cfa", fullDescriptor);
        require(destination.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Cannot create descriptor");
        const auto bytes = QJsonDocument(result).toJson();
        require(destination.write(bytes) == bytes.size(), "Incomplete descriptor");
        std::cout << "RGB replay: nine regions bit exact; mosaics captured\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
