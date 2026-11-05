// Manual measurement of the straightening proposal on real photos: the angle a known rotation
// gives back, what each mode proposes, how often it refuses, and the time it takes. Reads the
// sources only; everything it writes goes to --json and --sheets.
#include "ColorPipeline.h"
#include "Geometry.h"
#include "ImageImporter.h"
#include "LineDetection.h"
#include "RawDecoder.h"
#include "Straightening.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr int proxySide = 2048;
constexpr double centralShare = 0.7;   // what is kept of a rotated image, away from its repeated borders

cv::Mat proxyOf(const cv::Mat &rgba)
{
    cv::Mat rgb;
    cv::cvtColor(rgba, rgb, cv::COLOR_RGBA2RGB);
    const double scale = double(proxySide) / double(std::max(rgb.cols, rgb.rows));
    if (scale < 1.0)
        cv::resize(rgb, rgb, cv::Size(), scale, scale, cv::INTER_AREA);
    return rgb;
}

// Square root of the luminance: a step weighs the same in shadows and in lights.
cv::Mat perceptualLuminance(const cv::Mat &rgb)
{
    cv::Mat out(rgb.size(), CV_32FC1);
    for (int y = 0; y < rgb.rows; ++y) {
        const auto *in = rgb.ptr<cv::Vec3f>(y);
        auto *row = out.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x)
            row[x] = std::sqrt(std::max(0.0f, 0.2627f * in[x][0] + 0.678f * in[x][1] + 0.0593f * in[x][2]));
    }
    return out;
}

std::vector<LineDetection::Segment> linesOf(const cv::Mat &luminance)
{
    const cv::Mat continuous = luminance.isContinuous() ? luminance : luminance.clone();
    return LineDetection::detect({continuous.ptr<float>(), continuous.cols, continuous.rows, std::ptrdiff_t(continuous.step1())});
}

const char *refusalName(Straightening::Refusal refusal)
{
    switch (refusal) {
    case Straightening::Refusal::None: return "none";
    case Straightening::Refusal::InvalidInput: return "invalid_input";
    case Straightening::Refusal::NotEnoughLines: return "not_enough_lines";
    case Straightening::Refusal::NoAgreement: return "no_agreement";
    case Straightening::Refusal::NotEnoughEvidence: return "not_enough_evidence";
    }
    return "unknown";
}

QJsonObject toJson(const Straightening::Proposal &proposal)
{
    return {{"refusal", refusalName(proposal.refusal)}, {"rotation", double(proposal.rotation)},
            {"vertical", proposal.vertical}, {"horizontal", proposal.horizontal},
            {"supporting_lines", proposal.supportingLines},
            {"misalignment_before", double(proposal.misalignmentBefore)},
            {"misalignment_after", double(proposal.misalignmentAfter)}};
}

cv::Mat central(const cv::Mat &image)
{
    const int width = int(image.cols * centralShare), height = int(image.rows * centralShare);
    return image(cv::Rect((image.cols - width) / 2, (image.rows - height) / 2, width, height));
}

QImage shown(const cv::Mat &rgb, int side)
{
    cv::Mat rgba;
    cv::cvtColor(rgb, rgba, cv::COLOR_RGB2RGBA);
    std::vector<cv::Mat> channels;
    cv::split(rgba, channels);
    channels[3].setTo(1.0f);
    cv::merge(channels, rgba);
    ColorPipeline::WorkingImage image;
    image.linearRgba = rgba;
    image.colorSpace = ColorPipeline::workingColorSpace();
    QString error;
    const QImage srgb = ColorPipeline::toSrgb(image, ColorPipeline::OutputFormat::Preview8Bit, &error);
    return srgb.isNull() ? srgb : srgb.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
}

// The photo with the lines the proposal reads, then with the result of Full.
void writeSheet(const QString &path, const cv::Mat &rgb, const std::vector<LineDetection::Segment> &lines,
                const Geometry::Settings &settings)
{
    constexpr int side = 800;
    QImage before = shown(rgb, side);
    if (before.isNull())
        return;
    {
        QPainter painter(&before);
        const double scale = double(before.width()) / double(rgb.cols);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(255, 60, 40), 1.5));
        for (const LineDetection::Segment &s : lines)
            painter.drawLine(QPointF((s.x0 + 0.5) * scale, (s.y0 + 0.5) * scale), QPointF((s.x1 + 0.5) * scale, (s.y1 + 0.5) * scale));
    }
    const QImage after = shown(Geometry::warpRgb(rgb, settings, Geometry::Quality::Preview), side);
    if (after.isNull())
        return;
    QImage sheet(before.width() + after.width() + 8, std::max(before.height(), after.height()), QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter painter(&sheet);
    painter.drawImage(0, 0, before);
    painter.drawImage(before.width() + 8, 0, after);
    painter.end();
    sheet.save(path);
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"base", "Folder of photos, read only, without recursion", "folder"});
    parser.addOption({"json", "The proof; refused if it exists", "file"});
    parser.addOption({"sheets", "Folder of the side-by-side images", "folder"});
    parser.addOption({"revision", "The commit this build comes from", "sha"});
    parser.process(application);
    const QString jsonPath = parser.value("json");
    if (parser.value("base").isEmpty() || parser.value("revision").isEmpty() || jsonPath.isEmpty() || QFile::exists(jsonPath)) {
        std::cerr << "--base, --revision and a new --json are required\n";
        return 2;
    }
    QStringList patterns;
    for (const QString &extension : RawDecoder::supportedExtensions())
        patterns << QStringLiteral("*.") + extension;
    patterns << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg") << QStringLiteral("*.tif");
    const QFileInfoList files = QDir(parser.value("base")).entryInfoList(patterns, QDir::Files, QDir::Name);
    if (!parser.value("sheets").isEmpty() && !QDir().mkpath(parser.value("sheets"))) {
        std::cerr << "Cannot create the sheets folder\n";
        return 2;
    }

    QJsonArray entries;
    std::vector<double> errors, detectMs;
    int refused = 0, proposals = 0, skipped = 0;
    for (const QFileInfo &file : files) {
        QJsonObject entry{{"file", file.fileName()}};
        RawDecoder::Options options;
        const ImageImport::Result loaded = ImageImport::load(file.absoluteFilePath(), options);
        if (loaded.image.isNull()) {
            ++skipped;
            entry["error"] = loaded.error;
            entries.append(entry);
            continue;
        }
        const cv::Mat rgb = proxyOf(loaded.image.linearRgba);
        const cv::Size size = rgb.size();
        entry["proxy"] = QJsonArray{size.width, size.height};

        const cv::Mat luminance = perceptualLuminance(rgb);
        QElapsedTimer clock;
        clock.start();
        const std::vector<LineDetection::Segment> lines = linesOf(luminance);
        const double elapsed = double(clock.nsecsElapsed()) / 1e6;
        detectMs.push_back(elapsed);
        entry["lines"] = int(lines.size());
        entry["detect_ms"] = elapsed;

        clock.restart();
        const auto level = Straightening::propose(lines, size, {}, Straightening::Mode::Level);
        const auto vertical = Straightening::propose(lines, size, {}, Straightening::Mode::Vertical);
        const auto full = Straightening::propose(lines, size, {}, Straightening::Mode::Full);
        entry["propose_ms"] = double(clock.nsecsElapsed()) / 1e6 / 3.0;
        entry["level"] = toJson(level);
        entry["vertical"] = toJson(vertical);
        entry["full"] = toJson(full);
        ++proposals;
        refused += level.valid() ? 0 : 1;

        // A known rotation of the photo itself, read on the same central area.
        const cv::Mat straightCentre = central(luminance);
        const auto reference = Straightening::propose(linesOf(straightCentre), straightCentre.size(), {}, Straightening::Mode::Level);
        QJsonArray known;
        for (const float angle : {-5.0f, -2.0f, -0.5f, 0.5f, 2.0f, 5.0f}) {
            Geometry::Settings turned;
            turned.rotation = angle;
            const cv::Mat turnedCentre = central(perceptualLuminance(Geometry::warpRgb(rgb, turned, Geometry::Quality::Export)));
            const auto read = Straightening::propose(linesOf(turnedCentre), turnedCentre.size(), {}, Straightening::Mode::Level);
            QJsonObject item{{"angle", double(angle)}, {"refusal", refusalName(read.refusal)}};
            if (read.valid() && reference.valid()) {
                // The photo leans by the angle more than the reference did, and the proposal answers the opposite.
                const double error = double(read.rotation) + double(angle) - double(reference.rotation);
                item["error"] = error;
                errors.push_back(std::abs(error));
            }
            known.append(item);
        }
        entry["known_rotations"] = known;
        entries.append(entry);
        if (!parser.value("sheets").isEmpty())
            writeSheet(QDir(parser.value("sheets")).filePath(file.completeBaseName() + QStringLiteral(".png")), rgb, lines,
                       Straightening::applied({}, full));
        std::cout << file.fileName().toStdString() << ": " << lines.size() << " lines, " << elapsed << " ms, level "
                  << level.rotation << " (" << refusalName(level.refusal) << "), full " << full.rotation << ' '
                  << full.vertical << ' ' << full.horizontal << std::endl;
    }

    std::sort(errors.begin(), errors.end());
    const double worst = errors.empty() ? 0.0 : errors.back();
    const double p95 = errors.empty() ? 0.0 : errors[std::min(errors.size() - 1, size_t(double(errors.size()) * 0.95))];
    const QJsonObject proof{{"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                            {"revision", parser.value("revision")}, {"corpus", QDir(parser.value("base")).dirName()},
                            {"proxy_side", proxySide}, {"central_share", centralShare},
                            {"photos", proposals}, {"unreadable", skipped}, {"level_refused", refused},
                            {"known_rotation_measures", int(errors.size())},
                            {"known_rotation_error_median_degrees", median(errors)},
                            {"known_rotation_error_p95_degrees", p95},
                            {"known_rotation_error_worst_degrees", worst},
                            {"detect_ms_median", median(detectMs)},
                            {"truth", "the rotation we applied, relative to the same photo unrotated: repeatability of the "
                                      "reading, not whether the proposal is the right level for the scene"},
                            {"entries", entries}};
    QFile out(jsonPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly) || out.write(QJsonDocument(proof).toJson()) < 0) {
        std::cerr << "Cannot write the proof\n";
        return 1;
    }
    std::cout << proposals << " photos, " << refused << " refused to level, " << errors.size() << " known rotations: median error "
              << median(errors) << ", p95 " << p95 << ", worst " << worst << " degrees; detection median " << median(detectMs) << " ms\n";
    return 0;
}
