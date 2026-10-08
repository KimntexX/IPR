// Isolated diagnostic: reuse the exact private black/WB helpers without
// exposing them in the production API or linking duplicate RawDecoder symbols.
#define RawDecoder DiagnosticRawDecoder
#include "../src/raw/RawDecoder.cpp"
#undef RawDecoder

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace {
class WhiteTraceRaw : public LibRaw {
public:
    QJsonObject beforeScale;
    WhiteTraceRaw()
    {
        callbacks.pre_scalecolors_cb = [](void *context) {
            auto &raw = *static_cast<WhiteTraceRaw *>(static_cast<LibRaw *>(context));
            const auto &c = raw.imgdata.color;
            raw.beforeScale = {{"maximum", double(c.maximum)},
                {"data_maximum", double(c.data_maximum)}, {"black", double(c.black)}};
        };
    }
};
void require(bool ok, const QString &message)
{
    if (!ok) throw std::runtime_error(message.toStdString());
}

template<typename T> QJsonArray four(const T *values)
{
    QJsonArray result;
    for (int i = 0; i < 4; ++i) result.append(double(values[i]));
    return result;
}

QJsonObject metadata(const LibRaw &raw)
{
    const auto &c = raw.imgdata.color;
    const auto &canon = raw.imgdata.makernotes.canon;
    const auto area = [](const libraw_area_t &a) {
        return QJsonArray{a.l, a.t, a.r, a.b};
    };
    return {{"cam_mul", four(c.cam_mul)}, {"pre_mul", four(c.pre_mul)},
            {"black", double(c.black)}, {"cblack", four(c.cblack)},
            {"maximum", double(c.maximum)},
            {"libraw_dng_baseline_exposure", double(c.dng_levels.baseline_exposure)},
            {"canon", QJsonObject{{"color_data_family", canon.ColorDataVer},
                {"color_data_subversion", canon.ColorDataSubVer},
                {"channel_black_rgbg", four(canon.ChannelBlackLevel)},
                {"normal_white", canon.NormalWhiteLevel}, {"specular_white", canon.SpecularWhiteLevel},
                {"left_optical_black_ltrb", area(canon.LeftOpticalBlack)},
                {"upper_optical_black_ltrb", area(canon.UpperOpticalBlack)},
                {"active_area_ltrb", area(canon.ActiveArea)},
                {"highlight_tone_priority", canon.HighlightTonePriority},
                {"auto_lighting_optimizer", canon.AutoLightingOptimizer}}}};
}

// Diagnostics only: never feed these estimates back into the decoder.
QJsonObject borderAudit(LibRaw &raw)
{
    const auto &s = raw.imgdata.sizes;
    const auto *mosaic = raw.imgdata.rawdata.raw_image;
    require(mosaic && s.raw_pitch >= s.raw_width * sizeof(ushort), "Invalid raw mosaic stride");
    const auto region = [&](int left, int top, int right, int bottom) {
        QJsonObject result{{"rectangle_xyxy_exclusive", QJsonArray{left, top, right, bottom}}};
        QJsonArray channels;
        for (int c = 0; c < 4; ++c) {
            std::vector<unsigned> hist(65536);
            quint64 count = 0;
            double sum = 0;
            for (int y = top; y < bottom; ++y) {
                const auto *row = reinterpret_cast<const ushort *>(
                    reinterpret_cast<const uchar *>(mosaic) + size_t(y) * s.raw_pitch);
                for (int x = left; x < right; ++x) {
                    if (raw.COLOR(y, x) != c) continue;
                    ++hist[row[x]]; ++count; sum += row[x];
                }
            }
            if (!count) { channels.append(QJsonObject{{"count", 0}}); continue; }
            const auto quantile = [&](double p) {
                const quint64 target = quint64(p * double(count - 1));
                quint64 accumulated = 0;
                for (unsigned v = 0; v < hist.size(); ++v) {
                    accumulated += hist[v];
                    if (accumulated > target) return v;
                }
                return 65535u;
            };
            const auto median = quantile(.5);
            std::vector<quint64> deviations(65536);
            for (unsigned v = 0; v < hist.size(); ++v)
                deviations[unsigned(std::abs(int(v) - int(median)))] += hist[v];
            quint64 accumulated = 0;
            unsigned mad = 0;
            for (; mad < deviations.size(); ++mad) {
                accumulated += deviations[mad];
                if (accumulated > (count - 1) / 2) break;
            }
            channels.append(QJsonObject{{"count", double(count)}, {"mean", sum / double(count)},
                {"p01", int(quantile(.01))}, {"median", int(median)},
                {"p99", int(quantile(.99))}, {"mad", int(mad)}});
        }
        result["channels_rgbg"] = channels;
        return result;
    };
    QJsonObject result{{"raw_width", s.raw_width}, {"raw_height", s.raw_height},
        {"raw_pitch_bytes", int(s.raw_pitch)},
        {"decoder_active_xywh", QJsonArray{s.left_margin, s.top_margin, s.width, s.height}}};
    result["left"] = region(0, 0, s.left_margin, s.raw_height);
    result["right"] = region(s.left_margin + s.width, 0, s.raw_width, s.raw_height);
    result["top"] = region(s.left_margin, 0, s.left_margin + s.width, s.top_margin);
    result["bottom"] = region(s.left_margin, s.top_margin + s.height, s.left_margin + s.width, s.raw_height);
    for (const auto &named : {std::make_pair("canon_left", raw.imgdata.makernotes.canon.LeftOpticalBlack),
                             std::make_pair("canon_upper", raw.imgdata.makernotes.canon.UpperOpticalBlack)}) {
        const auto &a = named.second; // Canon bounds are inclusive; reject invalid/absent areas.
        if (a.r > a.l && a.b > a.t && a.l >= 0 && a.t >= 0 &&
            a.r < s.raw_width && a.b < s.raw_height) {
            result[named.first] = region(a.l, a.t, a.r + 1, a.b + 1);
            if (a.r - a.l > 4 && a.b - a.t > 4)
                result[QString::fromLatin1(named.first) + "_inset2"] = region(a.l + 2, a.t + 2, a.r - 1, a.b - 1);
        }
    }
    return result;
}

void writeNew(const QString &path, const QByteArray &data)
{
    QFile out(path);
    require(out.open(QIODevice::WriteOnly | QIODevice::NewOnly), out.errorString() + ": " + path);
    require(out.write(data) == data.size(), "Incomplete write: " + path);
}

cv::Mat sample(const cv::Mat &rgb)
{
    cv::Mat result((rgb.rows + 7) / 8, (rgb.cols + 7) / 8, CV_32FC3);
    for (int y = 0; y < result.rows; ++y)
        for (int x = 0; x < result.cols; ++x)
            result.at<cv::Vec3f>(y, x) = rgb.at<cv::Vec3f>(8*y, 8*x);
    return result;
}

QJsonObject saveSample(const QDir &out, const QString &name, const cv::Mat &rgb)
{
    const cv::Mat contiguous = rgb.isContinuous() ? rgb : rgb.clone();
    const auto bytes = QByteArray(reinterpret_cast<const char *>(contiguous.data),
                                 qsizetype(contiguous.total() * contiguous.elemSize()));
    writeNew(out.filePath(name + ".f32"), bytes);
    const cv::Scalar average = cv::mean(rgb);
    return {{"file", name + ".f32"}, {"rows", rgb.rows}, {"cols", rgb.cols},
            {"dtype", "little-endian float32 RGB"},
            {"mean_rgb", QJsonArray{average[0], average[1], average[2]}},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
}

QJsonObject saveEncodedSample(const QDir &out, const QString &name, const cv::Mat &rgb)
{
    std::vector<cv::Mat> channels;
    cv::split(rgb, channels);
    channels.push_back(cv::Mat(rgb.rows, rgb.cols, CV_32FC1, cv::Scalar(1.f)));
    ColorPipeline::WorkingImage working;
    cv::merge(channels, working.linearRgba);
    working.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
    QString error;
    working = ColorPipeline::mapToSrgbGamut(working, &error);
    const QImage encoded = ColorPipeline::toSrgb(working, ColorPipeline::OutputFormat::HighBitDepth, &error);
    require(!encoded.isNull(), error);
    cv::Mat encodedRgb(encoded.height(), encoded.width(), CV_32FC3);
    for (int y = 0; y < encoded.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgba64 *>(encoded.constScanLine(y));
        for (int x = 0; x < encoded.width(); ++x)
            encodedRgb.at<cv::Vec3f>(y, x) = cv::Vec3f(row[x].red(), row[x].green(), row[x].blue()) / 65535.f;
    }
    return saveSample(out, name, encodedRgb);
}

void auditSampleExport(const QString &manifestPath, const QString &outputPath)
{
    QFile manifestFile(manifestPath);
    require(manifestFile.open(QIODevice::ReadOnly), "Cannot read probe manifest");
    const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QJsonArray images;
    for (const auto &entry : manifest["images"].toArray()) {
        const auto item = entry.toObject();
        const auto stage = item["stages"].toObject()["dcp_with_lcp"].toObject();
        QFile data(QFileInfo(manifestPath).dir().filePath(stage["file"].toString()));
        require(data.open(QIODevice::ReadOnly), "Cannot read float stage");
        const auto bytes = data.readAll();
        require(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) ==
                stage["sha256"].toString(), "Float stage hash mismatch");
        const int rows = stage["rows"].toInt(), cols = stage["cols"].toInt();
        require(rows > 0 && cols > 0 && bytes.size() == qint64(rows) * cols * 12, "Invalid float sample dimensions");
        cv::Mat rgb(rows, cols, CV_32FC3);
        std::memcpy(rgb.data, bytes.constData(), size_t(bytes.size()));
        std::vector<cv::Mat> channels;
        cv::split(rgb, channels);
        channels.push_back(cv::Mat(rows, cols, CV_32FC1, cv::Scalar(1.f)));
        ColorPipeline::WorkingImage working;
        cv::merge(channels, working.linearRgba);
        working.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
        QString error;
        // The editor maps the loaded working image before the output function.
        working = ColorPipeline::mapToSrgbGamut(working, &error);
        const QImage output = ColorPipeline::toSrgb(working, ColorPipeline::OutputFormat::HighBitDepth, &error);
        require(!output.isNull(), error);
        quint64 low = 0, black = 0, high = 0;
        for (int y = 0; y < rows; ++y) {
            const auto *row = reinterpret_cast<const QRgba64 *>(output.constScanLine(y));
            for (int x = 0; x < cols; ++x) {
                const auto &p = row[x];
                low += std::min({p.red(), p.green(), p.blue()}) <= 1;
                high += std::max({p.red(), p.green(), p.blue()}) >= 65534;
                black += p.red() == 0 && p.green() == 0 && p.blue() == 0;
            }
        }
        images.append(QJsonObject{{"raw", item["raw"]}, {"source_float_sha256", stage["sha256"]},
            {"samples", double(rows) * cols}, {"any_channel_low_fraction", double(low) / (double(rows) * cols)},
            {"any_channel_high_fraction", double(high) / (double(rows) * cols)},
            {"all_black_fraction", double(black) / (double(rows) * cols)}});
    }
    QImage ramp(64, 1, QImage::Format_RGBA64);
    auto *rampPixels = reinterpret_cast<QRgba64 *>(ramp.scanLine(0));
    for (int code = 0; code < 64; ++code)
        rampPixels[code] = QRgba64::fromRgba64(ushort(code), ushort(code), ushort(code), 65535);
    ramp.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    const QImage encodedRamp = ramp.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    require(!encodedRamp.isNull(), "Cannot encode diagnostic ramp");
    const auto *encodedPixels = reinterpret_cast<const QRgba64 *>(encodedRamp.constScanLine(0));
    QJsonArray transfer;
    for (int code = 0; code < 64; ++code)
        transfer.append(QJsonObject{{"linear16_code", code},
            {"qt_srgb16_code", encodedPixels[code].red()},
            {"analytic_srgb16_code", int(std::lround(12.92 * code))}});
    writeNew(outputPath, QJsonDocument(QJsonObject{{"images", images}, {"neutral_ramp", transfer},
        {"note", "Existing native float grids, real ColorPipeline mapping and Qt sRGB output; no TIFF or RAW modified"}}).toJson());
}

QJsonObject process(const QString &path, const QDir &out, bool calibrationOnly = false,
                    unsigned whiteCandidate = 0, bool disableAutoMaximum = false)
{
    WhiteTraceRaw raw;
    require(raw.open_file(QFile::encodeName(path).constData()) == LIBRAW_SUCCESS, "open RAW: " + path);
    auto &d = raw.imgdata;
    const auto profiles = CameraProfileEngine::findProfiles(
        QString::fromLocal8Bit(d.idata.make), QString::fromLocal8Bit(d.idata.model),
        QString::fromLocal8Bit(d.lens.LensMake), QString::fromLocal8Bit(d.lens.Lens));
    require(!profiles.dcpPath.isEmpty(), "No DCP found; this diagnostic requires one");
    QJsonObject result{{"raw", path}, {"dcp", profiles.dcpPath}, {"lcp", profiles.lcpPath},
                       {"camera", QString::fromLocal8Bit(d.idata.model)},
                       {"lens", QString::fromLocal8Bit(d.lens.Lens)},
                       {"iso", d.other.iso_speed}, {"before_unpack", metadata(raw)}};
    d.params.use_camera_wb = 1;
    d.params.use_auto_wb = 0;
    d.params.use_camera_matrix = 0;
    d.params.no_auto_bright = 1;
    d.params.output_bps = 16;
    d.params.output_color = 0;
    d.params.gamm[0] = d.params.gamm[1] = 1.0;
    require(raw.unpack() == LIBRAW_SUCCESS, "unpack: " + path);
    result["border_audit"] = borderAudit(raw);
    result["before_black_estimation"] = metadata(raw);
    result["black_estimated"] = DiagnosticRawDecoder::estimateMissingCanonCr3BlackLevel(raw, path);
    result["after_black_estimation"] = metadata(raw);
    if (whiteCandidate) {
        require(whiteCandidate > d.color.cblack[0] && whiteCandidate <= 65535,
                "Invalid diagnostic white candidate");
        // raw2image restores rawdata.color; set both copies BEFORE black subtraction.
        d.color.maximum = d.rawdata.color.maximum = whiteCandidate;
    }
    if (disableAutoMaximum) d.params.adjust_maximum_thr = 0;
    result["white_experiment"] = QJsonObject{{"candidate_raw_white", int(whiteCandidate)},
        {"disable_auto_maximum", disableAutoMaximum},
        {"adjust_maximum_thr", d.params.adjust_maximum_thr},
        {"before_process", metadata(raw)}};
    const auto kelvin = DiagnosticRawDecoder::estimateWhiteBalanceKelvin(raw);
    result["wb_kelvin"] = kelvin ? QJsonValue(*kelvin) : QJsonValue(QJsonValue::Null);
    if (calibrationOnly) {
        writeNew(out.filePath(QFileInfo(path).completeBaseName() + ".json"), QJsonDocument(result).toJson());
        return result;
    }
    require(raw.dcraw_process() == LIBRAW_SUCCESS, "process: " + path);
    result["before_scale_colors"] = raw.beforeScale;
    result["after_process"] = metadata(raw);
    int status = 0;
    auto *memory = raw.dcraw_make_mem_image(&status);
    require(memory && !status && memory->bits == 16 && memory->colors == 3, "memory image: " + path);
    cv::Mat rgb;
    cv::Mat(memory->height, memory->width, CV_16UC3, memory->data).convertTo(rgb, CV_32FC3, 1.0/65535.0);
    result["width"] = rgb.cols;
    result["height"] = rgb.rows;
    LibRaw::dcraw_clear_mem(memory);
    const QString stem = QFileInfo(path).completeBaseName();
    QJsonObject stages;
    auto noLens = sample(rgb);
    stages["camera_rgb"] = saveSample(out, stem + "-camera", noLens);
    QString error;
    require(CameraProfileEngine::applyDcp(profiles.dcpPath, noLens, kelvin.value_or(5500), &error), error);
    stages["dcp_without_lcp"] = saveSample(out, stem + "-dcp-no-lcp", noLens);
    if (!profiles.lcpPath.isEmpty()) {
        CameraProfileEngine::CaptureSettings capture;
        capture.focalLengthMm = d.other.focal_len;
        capture.aperture = d.other.aperture;
        require(CameraProfileEngine::applyLcp(profiles.lcpPath, rgb, capture, &error), error);
    }
    auto withLens = sample(rgb);
    rgb.release();
    stages["camera_rgb_after_lcp"] = saveSample(out, stem + "-camera-lcp", withLens);
    require(CameraProfileEngine::applyDcp(profiles.dcpPath, withLens, kelvin.value_or(5500), &error), error);
    stages["dcp_with_lcp"] = saveSample(out, stem + "-dcp-lcp", withLens);
    stages["encoded_srgb"] = saveEncodedSample(out, stem + "-encoded-srgb", withLens);
    result["stages"] = stages;
    writeNew(out.filePath(stem + ".json"), QJsonDocument(result).toJson());
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("IPR");
    app.setApplicationName("IPR");
    try {
        if (app.arguments().value(1) == "--profile-samples") {
            require(argc >= 5, "Usage: tonalBaseProbe --profile-samples WHITE_MANIFEST NEW_DIRECTORY DCP...");
            QFile input(app.arguments()[2]);
            require(input.open(QIODevice::ReadOnly), "Cannot read white experiment manifest");
            const QByteArray inputBytes = input.readAll();
            const auto manifest = QJsonDocument::fromJson(inputBytes).object();
            const QDir sourceRoot = QFileInfo(input).dir();
            const QDir out(app.arguments()[3]);
            require(!out.exists(), "Output directory already exists; refuse overwrite");
            require(QDir().mkpath(out.absolutePath()), "Cannot create profile output folder");
            QJsonArray images;
            for (const auto &entry : manifest["images"].toArray()) {
                const auto source = entry.toObject();
                if (source["variant"].toString() != "baseline") continue;
                const QString stem = QFileInfo(source["raw"].toString()).completeBaseName();
                for (int profileIndex = 4; profileIndex < argc; ++profileIndex) {
                    const QString profile = app.arguments()[profileIndex];
                    QFile dcp(profile);
                    require(dcp.open(QIODevice::ReadOnly), "Cannot read profile");
                    const QString profileHash = QString::fromLatin1(QCryptographicHash::hash(dcp.readAll(), QCryptographicHash::Sha256).toHex());
                    for (const QString &geometry : {QString("with_lcp"), QString("without_lcp")}) {
                        const QString cameraStage = geometry == "with_lcp" ? "camera_rgb_after_lcp" : "camera_rgb";
                        const auto stage = source["stages"].toObject()[cameraStage].toObject();
                        QFile data(sourceRoot.filePath(source["directory"].toString() + "/" + stage["file"].toString()));
                        require(data.open(QIODevice::ReadOnly), "Cannot read camera grid");
                        const auto bytes = data.readAll();
                        require(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) == stage["sha256"].toString(), "Camera grid hash mismatch");
                        const int rows = stage["rows"].toInt(), cols = stage["cols"].toInt();
                        require(rows > 0 && cols > 0 && bytes.size() == qint64(rows) * cols * 12, "Invalid camera grid size");
                        cv::Mat rgb(rows, cols, CV_32FC3);
                        std::memcpy(rgb.data, bytes.constData(), size_t(bytes.size()));
                        QString error;
                        require(CameraProfileEngine::applyDcp(profile, rgb, source["wb_kelvin"].toDouble(), &error), error);
                        const QString directory = stem + "/profile-" + QString::number(profileIndex - 4) + "/" + geometry;
                        QDir folder(out.filePath(directory));
                        require(QDir().mkpath(folder.absolutePath()), "Cannot create profile variant directory");
                        auto item = QJsonObject{{"raw", source["raw"]}, {"raw_sha256", source["raw_sha256"]},
                            {"profile", profile}, {"profile_sha256", profileHash}, {"geometry", geometry},
                            {"directory", directory}, {"source_camera_sha256", stage["sha256"]},
                            {"wb_kelvin", source["wb_kelvin"]},
                            {"stages", QJsonObject{{"dcp_linear", saveSample(folder, stem + "-linear", rgb)},
                                {"encoded_srgb", saveEncodedSample(folder, stem + "-encoded", rgb)}}}};
                        images.append(item);
                    }
                    QTextStream(stdout) << stem << " / " << QFileInfo(profile).completeBaseName() << Qt::endl;
                }
            }
            require(!images.isEmpty(), "No baseline entries found");
            writeNew(out.filePath("manifest.json"), QJsonDocument(QJsonObject{
                {"sample_step", manifest["sample_step"]}, {"libraw_version", LibRaw::version()},
                {"source_manifest_sha256", QString::fromLatin1(QCryptographicHash::hash(inputBytes, QCryptographicHash::Sha256).toHex())},
                {"images", images}}).toJson());
            return 0;
        }
        if (app.arguments().value(1) == "--white-levels") {
            require(argc == 4, "Usage: tonalBaseProbe --white-levels METADATA_JSON NEW_OUTPUT_DIRECTORY");
            QFile input(app.arguments()[2]);
            require(input.open(QIODevice::ReadOnly), "Cannot read independent Canon audit");
            const QByteArray auditBytes = input.readAll();
            const auto audit = QJsonDocument::fromJson(auditBytes).object();
            const QDir out(app.arguments()[3]);
            require(!out.exists(), "Output directory already exists; refuse overwrite");
            require(QDir().mkpath(out.absolutePath()), "Cannot create output directory");
            QJsonArray images;
            for (const auto &entry : audit["images"].toArray()) {
                const auto item = entry.toObject();
                const QString path = item["raw"].toString();
                QFile source(path);
                require(source.open(QIODevice::ReadOnly), "Cannot read RAW for hash");
                QCryptographicHash hash(QCryptographicHash::Sha256);
                require(hash.addData(&source), "Cannot hash RAW");
                require(QString::fromLatin1(hash.result().toHex()) == item["raw_sha256"].toString(), "RAW hash mismatch");
                const auto records = item["color_data_records"].toArray();
                require(records.size() == 1, "Ambiguous independent ColorData");
                const auto record = records[0].toObject();
                require(record["version_word"].toInt() == 66, "Experiment restricted to audited ColorData 66");
                const auto words = record["candidate_words_not_certified"].toObject()["0x290"].toArray();
                require(words.size() >= 6, "Missing white candidates");
                const unsigned normal = words[4].toInt(), specular = words[5].toInt();
                require(normal > 2048 && specular >= normal && specular < 16383, "Implausible candidates");
                const QString stem = QFileInfo(path).completeBaseName();
                for (const QString &variant : {QString("baseline"), QString("full_fixed"),
                     QString("normal_auto"), QString("normal_fixed"), QString("specular_fixed")}) {
                    const QDir folder(out.filePath(stem + "/" + variant));
                    require(QDir().mkpath(folder.absolutePath()), "Cannot create variant folder");
                    const unsigned white = variant == "baseline" ? 0 : variant == "full_fixed" ? 16383 :
                        variant == "specular_fixed" ? specular : normal;
                    const bool fixed = variant.endsWith("fixed");
                    QTextStream(stdout) << stem << " / " << variant << Qt::endl;
                    auto measurement = process(path, folder, false, white, fixed);
                    measurement["variant"] = variant;
                    measurement["directory"] = stem + "/" + variant;
                    measurement["raw_sha256"] = item["raw_sha256"];
                    measurement["independent_record"] = record;
                    images.append(measurement);
                }
            }
            writeNew(out.filePath("manifest.json"), QJsonDocument(QJsonObject{
                {"schema", 1}, {"libraw_version", LibRaw::version()}, {"sample_step", 8},
                {"audit_sha256", QString::fromLatin1(QCryptographicHash::hash(auditBytes, QCryptographicHash::Sha256).toHex())},
                {"note", "Diagnostic only; five freshly decoded variants per RAW, fixed WB/DCP/LCP and sRGB; white candidates not certified"},
                {"images", images}}).toJson());
            return 0;
        }
        if (app.arguments().value(1) == "--export-samples") {
            require(argc == 4, "Usage: tonalBaseProbe --export-samples MANIFEST OUTPUT_JSON");
            auditSampleExport(app.arguments()[2], app.arguments()[3]);
            return 0;
        }
        const bool calibrationOnly = app.arguments().value(1) == "--calibration-only";
        const int directoryIndex = calibrationOnly ? 2 : 1;
        require(argc >= directoryIndex + 2, "Usage: tonalBaseProbe [--calibration-only] OUTPUT_DIRECTORY RAW...");
        const QDir out(app.arguments()[directoryIndex]);
        require(!out.exists(), "Output directory already exists; refuse overwrite");
        require(QDir().mkpath(out.absolutePath()), "Cannot create output directory");
        QJsonArray images;
        for (int i = directoryIndex + 1; i < argc; ++i) {
            QTextStream(stdout) << "Investigating " << app.arguments()[i] << Qt::endl;
            images.append(process(app.arguments()[i], out, calibrationOnly));
        }
        writeNew(out.filePath("manifest.json"), QJsonDocument(QJsonObject{
            {"libraw_version", LibRaw::version()}, {"sample_step", 8},
            {"note", "Native-grid samples; full-resolution LCP before sampling. Camera RGB is not sRGB luminance."},
            {"images", images}}).toJson());
        return 0;
    } catch (const std::exception &e) {
        QTextStream(stderr) << e.what() << Qt::endl;
        return 1;
    }
}
