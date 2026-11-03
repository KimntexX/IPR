// Numeric audit of the masks: their place at every resolution, and what they cost per
// render. Works on charts in memory, on no photo. Tolerances are fixed here, before the run.
#include "LocalAdjustments.h"

#include <opencv2/imgproc.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace LocalAdjustments;

namespace {

constexpr double placeTolerance = 0.002;     // of the canvas's side
constexpr double crossTolerance = 0.02;      // between resolutions, in coverage
constexpr double previewLatencyMs = 40.0;    // one mask on the 2048 px proxy, per render

const cv::Size preview(2048, 1365);
const cv::Size full(6000, 4000);

Shape linear(cv::Point2f start, cv::Point2f end)
{
    Shape shape;
    shape.start = start;
    shape.end = end;
    return shape;
}

Shape radial(cv::Point2f centre, float radiusX, float radiusY, float feather)
{
    Shape shape;
    shape.kind = Kind::Radial;
    shape.center = centre;
    shape.radiusX = radiusX;
    shape.radiusY = radiusY;
    shape.feather = feather;
    return shape;
}

Shape stroke(int points, float size)
{
    Shape shape;
    shape.kind = Kind::Brush;
    BrushStroke line;
    line.size = size;
    line.feather = 0.5f;
    for (int i = 0; i < points; ++i) {
        const float t = float(i) / float(std::max(1, points - 1));
        line.points.emplace_back(0.1f + 0.8f * t, 0.5f + 0.3f * std::sin(t * 12.0f));
    }
    shape.strokes.push_back(std::move(line));
    return shape;
}

Mask maskOf(const Shape &shape)
{
    Mask mask;
    mask.shapes.push_back(shape);
    mask.values[size_t(LocalValues::Exposure)] = 1.0f;
    return mask;
}

// Where a falling profile crosses a level, as a share of its length, with interpolation.
double crossing(const std::vector<float> &profile, float level)
{
    for (size_t i = 1; i < profile.size(); ++i)
        if (profile[i - 1] >= level && profile[i] < level) {
            const double t = double(profile[i - 1] - level) / std::max(1e-9, double(profile[i - 1] - profile[i]));
            return (double(i - 1) + t + 0.5) / double(profile.size());
        }
    return -1.0;
}

std::vector<float> column(const cv::Mat &map, int x)
{
    std::vector<float> values(size_t(map.rows));
    for (int y = 0; y < map.rows; ++y)
        values[size_t(y)] = map.at<float>(y, x);
    return values;
}

std::vector<float> rowOf(const cv::Mat &map, int y)
{
    const float *row = map.ptr<float>(y);
    return std::vector<float>(row, row + map.cols);
}

double worstAcross(const Mask &mask)
{
    cv::Mat reduced, difference;
    cv::resize(coverage(mask, full), reduced, preview, 0, 0, cv::INTER_AREA);
    cv::absdiff(reduced, coverage(mask, preview), difference);
    double worst = 0.0;
    cv::minMaxLoc(difference, nullptr, &worst);
    return worst;
}

double millisecondsOf(const Masks &masks, const cv::Size &size)
{
    double best = std::numeric_limits<double>::infinity();
    for (int run = 0; run < 3; ++run) {
        QElapsedTimer watch;
        watch.start();
        const LocalValues::Maps maps = valueMaps(masks, size);
        best = std::min(best, double(watch.nsecsElapsed()) / 1e6);
        if (maps[LocalValues::Exposure].empty())
            return -1.0;
    }
    return best;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString outputFolder = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    bool passed = true;
    QJsonArray records;
    const auto record = [&](const QString &name, double measured, double tolerance) {
        const bool fine = measured >= 0.0 && measured <= tolerance;
        passed = passed && fine;
        records.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("measured"), measured},
                                   {QStringLiteral("tolerance"), tolerance}, {QStringLiteral("passed"), fine}});
        std::cout << (fine ? "ok   " : "FAIL ") << name.toStdString() << ' ' << measured << " / " << tolerance << '\n';
    };

    // A gradient's middle is where its two ends say, at both resolutions.
    for (const cv::Size &size : {preview, full}) {
        const cv::Mat map = coverage(maskOf(linear({0.5f, 0.3f}, {0.5f, 0.7f})), size);
        record(QStringLiteral("linear middle at %1 px").arg(size.width), std::abs(crossing(column(map, size.width / 2), 0.5f) - 0.5), placeTolerance);
        const cv::Mat edge = coverage(maskOf(radial({0.5f, 0.5f}, 0.25f, 0.2f, 0.0f)), size);
        // From the centre outwards; the share is then of the half that was read.
        const std::vector<float> across = rowOf(edge, size.height / 2), down = column(edge, size.width / 2);
        const std::vector<float> right(across.begin() + size.width / 2, across.end()), below(down.begin() + size.height / 2, down.end());
        record(QStringLiteral("radial edge across at %1 px").arg(size.width),
               std::abs(crossing(right, 0.5f) * (double(right.size()) / size.width) - 0.25), placeTolerance);
        record(QStringLiteral("radial edge down at %1 px").arg(size.width),
               std::abs(crossing(below, 0.5f) * (double(below.size()) / size.height) - 0.2), placeTolerance);
    }

    // The same zone at every resolution.
    record(QStringLiteral("linear across resolutions"), worstAcross(maskOf(linear({0.2f, 0.1f}, {0.8f, 0.9f}))), crossTolerance);
    record(QStringLiteral("radial across resolutions"), worstAcross(maskOf(radial({0.4f, 0.6f}, 0.3f, 0.3f, 0.7f))), crossTolerance);
    record(QStringLiteral("brush across resolutions"), worstAcross(maskOf(stroke(40, 0.05f))), crossTolerance);

    // What a render pays.
    QJsonArray costs;
    const auto cost = [&](const QString &name, const Masks &masks) {
        for (const cv::Size &size : {preview, full}) {
            const double ms = millisecondsOf(masks, size);
            costs.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("width"), size.width},
                                     {QStringLiteral("masks"), int(masks.size())}, {QStringLiteral("milliseconds"), ms}});
            std::cout << name.toStdString() << " at " << size.width << ": " << ms << " ms\n";
        }
    };
    cost(QStringLiteral("linear"), {maskOf(linear({0.5f, 0.3f}, {0.5f, 0.7f}))});
    cost(QStringLiteral("radial"), {maskOf(radial({0.5f, 0.5f}, 0.3f, 0.3f, 0.5f))});
    cost(QStringLiteral("brush, 200 points"), {maskOf(stroke(200, 0.03f))});
    cost(QStringLiteral("brush, 2000 points"), {maskOf(stroke(2000, 0.03f))});
    Masks four, sixteen;
    for (int i = 0; i < 16; ++i) {
        Mask mask = maskOf(radial({0.1f + 0.05f * float(i), 0.5f}, 0.2f, 0.2f, 0.5f));
        mask.values[size_t(LocalValues::Contrast)] = 10.0f;
        if (i < 4)
            four.push_back(mask);
        sixteen.push_back(std::move(mask));
    }
    cost(QStringLiteral("four radial masks"), four);
    cost(QStringLiteral("sixteen radial masks"), sixteen);
    record(QStringLiteral("one mask per render on the proxy, ms"), millisecondsOf({maskOf(radial({0.5f, 0.5f}, 0.3f, 0.3f, 0.5f))}, preview), previewLatencyMs);

    const LocalValues::Maps maps = valueMaps(sixteen, preview);
    quint64 bytes = 0;
    for (const cv::Mat &map : maps.values)
        bytes += quint64(map.total() * map.elemSize());

    const auto gitOutput = [](const QStringList &arguments) {
        QProcess git;
        git.start(QStringLiteral("git"), arguments);
        git.waitForFinished(30000);
        return QString::fromUtf8(git.readAllStandardOutput()).trimmed();
    };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString id = QStringLiteral("b2-masks-audit-v001-") + now.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    const QJsonObject root{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("data_version"), 1},
        {QStringLiteral("campaign_id"), id},
        {QStringLiteral("measured_at_utc"), now.toString(Qt::ISODate)},
        {QStringLiteral("group"), QStringLiteral("b2-masks")},
        {QStringLiteral("status"), passed ? QStringLiteral("passed") : QStringLiteral("failed")},
        {QStringLiteral("candidate_revision"), gitOutput({QStringLiteral("rev-parse"), QStringLiteral("HEAD")})},
        {QStringLiteral("candidate_dirty"), !gitOutput({QStringLiteral("status"), QStringLiteral("--porcelain")}).isEmpty()},
        {QStringLiteral("method"), QStringLiteral("Coverage of each kind of shape on charts in memory; timings are the best of three runs of the rasterizer alone")},
        {QStringLiteral("limitations"), QStringLiteral("No photo: the effect of the values is checked by editorMasksTests; a render adds the modules' own cost per map")},
        {QStringLiteral("units"), QJsonObject{{QStringLiteral("place"), QStringLiteral("share of the canvas's side")}, {QStringLiteral("cost"), QStringLiteral("milliseconds")}}},
        {QStringLiteral("records"), records},
        {QStringLiteral("costs"), costs},
        {QStringLiteral("maps_bytes_sixteen_masks_proxy"), double(bytes)},
    };
    QFile file(QDir(outputFolder).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) <= 0) {
        std::cerr << "evidence not written\n";
        return 1;
    }
    std::cout << file.fileName().toStdString() << '\n';
    return passed ? 0 : 1;
}
