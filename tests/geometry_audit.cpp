// Numeric qualification of the geometry (B1) on synthetic charts held in memory:
// iprGeometryAudit OUTPUT.json. Tolerances are fixed here, before any campaign.
#include "Geometry.h"
#include "image_testing.h"
#include "Parallel.h"
#include "Pixels.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>


#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr double RigidLandmarkTolerance = 0.10;       // output pixels
constexpr double PerspectiveLandmarkTolerance = 0.25; // output pixels: a dot's centroid shifts under perspective
constexpr double PreviewExportTolerance = 0.50;       // preview pixels
constexpr double InverseTolerance = 1e-9;             // normalized
constexpr double RiseTolerance = 2.0;                 // output pixels, 10-90 %, at unit scale
constexpr double OvershootTolerance = 0.15;           // of the step
constexpr double AliasTolerance = 0.35;               // of the source's amplitude, at 50 %

struct State
{
    QString name;
    Geometry::Settings settings;
    bool rigid = true;
};

std::vector<State> states()
{
    std::vector<State> list;
    const auto add = [&](const QString &name, bool rigid, const std::function<void(Geometry::Settings &)> &set) {
        Geometry::Settings settings;
        set(settings);
        list.push_back({name, settings, rigid});
    };
    for (float angle : {-45.f, -8.5f, -1.f, 0.1f, 1.f, 8.5f, 45.f})
        add(QStringLiteral("rotation=%1").arg(angle), true, [=](auto &s) { s.rotation = angle; });
    for (int value : {-100, -50, 50, 100}) {
        add(QStringLiteral("vertical=%1").arg(value), false, [=](auto &s) { s.vertical = value; });
        add(QStringLiteral("horizontal=%1").arg(value), false, [=](auto &s) { s.horizontal = value; });
        add(QStringLiteral("aspect=%1").arg(value), true, [=](auto &s) { s.aspect = value; });
        add(QStringLiteral("offsetX=%1").arg(value), true, [=](auto &s) { s.offsetX = value; });
        add(QStringLiteral("offsetY=%1").arg(value), true, [=](auto &s) { s.offsetY = value; });
    }
    for (int value : {50, 75, 125, 150})
        add(QStringLiteral("scale=%1").arg(value), true, [=](auto &s) { s.scale = value; });
    for (int turns = 0; turns < 4; ++turns)
        for (int flips = 0; flips < 4; ++flips)
            if (turns || flips)
                add(QStringLiteral("turns=%1,flips=%2").arg(turns).arg(flips), true, [=](auto &s) {
                    s.quarterTurns = turns;
                    s.flipHorizontal = flips & 1;
                    s.flipVertical = flips & 2;
                });
    add(QStringLiteral("combined"), false, [](auto &s) {
        s.rotation = 7.5f; s.vertical = 30; s.horizontal = -20; s.aspect = 15; s.scale = 110; s.offsetX = 8; s.offsetY = -6;
    });
    add(QStringLiteral("combined,turned,mirrored"), false, [](auto &s) {
        s.quarterTurns = 3; s.flipHorizontal = true; s.rotation = -12.f; s.vertical = -40; s.horizontal = 25; s.scale = 90;
    });
    return list;
}

std::vector<ipr::Point2d> landmarkPositions()
{
    std::vector<ipr::Point2d> positions;
    for (double v : {0.2, 0.35, 0.5, 0.65, 0.8})
        for (double u : {0.2, 0.35, 0.5, 0.65, 0.8})
            positions.emplace_back(u, v);
    return positions;
}

ipr::Image landmarks(const ipr::Size &size, double sigma)
{
    ipr::Image image = ipr::filled(size, ipr::Depth::F32, 3, 0);
    const int radius = int(std::ceil(4 * sigma));
    for (const ipr::Point2d &position : landmarkPositions()) {
        const double cx = position.x * size.width, cy = position.y * size.height;
        for (int y = std::max(0, int(cy) - radius); y <= std::min(size.height - 1, int(cy) + radius); ++y)
            for (int x = std::max(0, int(cx) - radius); x <= std::min(size.width - 1, int(cx) + radius); ++x) {
                const double dx = x + .5 - cx, dy = y + .5 - cy;
                imagetest::at<ipr::Vec3f>(image, y, x) = ipr::Vec3f::all(float(std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma))));
            }
    }
    return image;
}

// The centroids of the landmarks that stay on the canvas, in normalized coordinates.
struct Measured { ipr::Point2d found, expected; };

std::vector<Measured> measureLandmarks(const ipr::Image &output, const State &state, const ipr::Size &source, double sigma)
{
    std::vector<Measured> result;
    const double zoom = std::max(0.3, state.settings.scale / 100.0);
    const int radius = int(std::ceil(6 * sigma * std::max(1.0, zoom * 1.5)));
    for (const ipr::Point2d &position : landmarkPositions()) {
        const auto expected = Geometry::toCanvas(state.settings, source, position);
        if (!expected)
            continue;
        const double px = expected->x * output.width(), py = expected->y * output.height();
        if (px < radius || py < radius || px >= output.width() - radius || py >= output.height() - radius)
            continue;
        double sum = 0, sumX = 0, sumY = 0;
        for (int y = int(py) - radius; y <= int(py) + radius; ++y)
            for (int x = int(px) - radius; x <= int(px) + radius; ++x) {
                const double value = std::max(0.f, imagetest::at<ipr::Vec3f>(output, y, x)[0]);
                sum += value;
                sumX += value * (x + .5);
                sumY += value * (y + .5);
            }
        if (sum > 0)
            result.push_back({{sumX / sum, sumY / sum}, {px, py}});
    }
    return result;
}

QJsonObject landmarkRecord(const State &state, const ipr::Size &small, const ipr::Size &large)
{
    // Small on both: a wide dot's centroid drifts under perspective, without any error.
    const double smallSigma = 3, largeSigma = 3;
    const ipr::Image preview = Geometry::warpRgb(landmarks(small, smallSigma), state.settings, Geometry::Quality::Preview);
    const ipr::Image exported = Geometry::warpRgb(landmarks(large, largeSigma), state.settings, Geometry::Quality::Export);
    const auto previewDots = measureLandmarks(preview, state, small, smallSigma);
    const auto exportDots = measureLandmarks(exported, state, large, largeSigma);
    double previewError = 0, exportError = 0, between = 0;
    for (const auto &dot : previewDots)
        previewError = std::max(previewError, std::hypot(dot.found.x - dot.expected.x, dot.found.y - dot.expected.y));
    for (const auto &dot : exportDots)
        exportError = std::max(exportError, std::hypot(dot.found.x - dot.expected.x, dot.found.y - dot.expected.y));
    // The same landmark, in preview pixels.
    const double ratioX = double(preview.width()) / std::max(1, exported.width()), ratioY = double(preview.height()) / std::max(1, exported.height());
    for (const auto &small : previewDots)
        for (const auto &big : exportDots)
            if (std::hypot(small.expected.x - big.expected.x * ratioX, small.expected.y - big.expected.y * ratioY) < 1)
                between = std::max(between, std::hypot(small.found.x - big.found.x * ratioX, small.found.y - big.found.y * ratioY));
    double inverse = 0;
    for (const ipr::Point2d &position : landmarkPositions()) {
        const auto canvas = Geometry::toCanvas(state.settings, large, position);
        const auto back = canvas ? Geometry::toSource(state.settings, large, *canvas) : std::nullopt;
        if (back)
            inverse = std::max(inverse, std::hypot(back->x - position.x, back->y - position.y));
    }
    const double landmarkTolerance = state.rigid ? RigidLandmarkTolerance : PerspectiveLandmarkTolerance;
    const bool passed = previewError <= landmarkTolerance && exportError <= landmarkTolerance &&
                        between <= PreviewExportTolerance && inverse <= InverseTolerance;
    return {{"state", state.name}, {"landmarks_preview", int(previewDots.size())}, {"landmarks_export", int(exportDots.size())},
            {"error_preview_px", previewError}, {"error_export_px", exportError},
            {"preview_vs_export_preview_px", between}, {"inverse_normalized", inverse},
            {"landmark_tolerance_px", landmarkTolerance}, {"passed", passed}};
}

// A straight step: the output's edge spread, from pixels projected on its normal.
QJsonObject edgeRecord(double angle, Geometry::Quality quality)
{
    const ipr::Size size(1536, 1024);
    ipr::Image image = ipr::Image::create(size, ipr::Depth::F32, 3);
    // Antialiased by area, a sensor's edge rather than an impossible perfect step.
    const double edgeX = size.width / 2.0 + 0.3;
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            imagetest::at<ipr::Vec3f>(image, y, x) = ipr::Vec3f::all(float(std::clamp(x + 1 - edgeX, 0.0, 1.0) * .6 + .2));
    Geometry::Settings settings;
    settings.rotation = float(angle);
    const ipr::Image output = Geometry::warpRgb(image.share(), settings, quality);
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double nx = std::cos(radians), ny = std::sin(radians);
    const auto edge = Geometry::toCanvas(settings, size, {edgeX / size.width, 0.5});
    const double ex = edge->x * size.width, ey = edge->y * size.height;
    constexpr int binsPerPixel = 8, reach = 12;
    std::vector<double> sums((2 * reach) * binsPerPixel, 0.0), counts(sums.size(), 0.0);
    for (int y = 200; y < output.height() - 200; ++y)
        for (int x = 400; x < output.width() - 400; ++x) {
            const double distance = (x + .5 - ex) * nx + (y + .5 - ey) * ny;
            if (std::abs(distance) >= reach)
                continue;
            const auto bin = size_t((distance + reach) * binsPerPixel);
            sums[bin] += imagetest::at<ipr::Vec3f>(output, y, x)[0];
            counts[bin] += 1;
        }
    const double low = .2, high = .8, step = high - low;
    double p10 = 0, p90 = 0, top = 0, bottom = 1;
    for (size_t bin = 0; bin < sums.size(); ++bin) {
        if (counts[bin] == 0)
            continue;
        const double value = (sums[bin] / counts[bin] - low) / step;
        top = std::max(top, value);
        bottom = std::min(bottom, value);
        if (p10 == 0 && value >= .1) p10 = double(bin) / binsPerPixel;
        if (p90 == 0 && value >= .9) p90 = double(bin) / binsPerPixel;
    }
    const double rise = p90 - p10, overshoot = std::max(top - 1, -bottom);
    return {{"angle_degrees", angle}, {"quality", quality == Geometry::Quality::Export ? "lanczos" : "bicubic"},
            {"rise_10_90_px", rise}, {"overshoot_of_step", overshoot},
            {"passed", rise <= RiseTolerance && overshoot <= OvershootTolerance}};
}

// A fine grating, beyond the output's Nyquist at this scale: what comes out is alias.
QJsonObject aliasRecord(int scale, Geometry::Quality quality)
{
    const ipr::Size size(1024, 768);
    const double period = 2.5, amplitude = .2;
    ipr::Image image = ipr::Image::create(size, ipr::Depth::F32, 3);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            imagetest::at<ipr::Vec3f>(image, y, x) = ipr::Vec3f::all(float(.5 + amplitude * std::sin(2 * 3.14159265358979323846 * (x + .5) / period)));
    Geometry::Settings settings;
    settings.scale = scale;
    const ipr::Image output = Geometry::warpRgb(image.share(), settings, quality);
    const double zoom = scale / 100.0;
    const int inset = int(size.width * (.5 - zoom / 2)) + 16;
    const ipr::Recti area(std::max(16, inset), size.height / 4, size.width - 2 * std::max(16, inset), size.height / 2);
    std::vector<double> mean, deviation;
    ipr::meanAndDeviationOf(ipr::channel(output.copy(area), 0), mean, deviation);
    const double ratio = deviation[0] * std::sqrt(2.0) / amplitude;
    return {{"scale", scale}, {"quality", quality == Geometry::Quality::Export ? "lanczos" : "bicubic"},
            {"grating_period_source_px", period}, {"beyond_output_nyquist", period * zoom < 2},
            {"output_amplitude_of_source", ratio},
            {"passed", period * zoom >= 2 || ratio <= AliasTolerance}};
}

QJsonObject timing(const ipr::Size &size, Geometry::Quality quality)
{
    ipr::Image rgb = ipr::Image::create(size, ipr::Depth::F32, 3);
    std::mt19937 random(1);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    ipr::mapInPlace(rgb, [&](float) { return uniform(random); });
    ipr::Image alpha = ipr::filled(size, ipr::Depth::F32, 1, 1);
    Geometry::Settings settings;
    settings.rotation = 8.5f;
    settings.vertical = 30;
    const auto measure = [&](const std::function<void()> &work) {
        double best = 1e30;
        for (int run = 0; run < 3; ++run) {
            const auto start = std::chrono::steady_clock::now();
            work();
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        return best;
    };
    return {{"width", size.width}, {"height", size.height},
            {"quality", quality == Geometry::Quality::Export ? "lanczos" : "bicubic"},
            {"threads", ipr::workerThreads()},
            {"rgb_ms", measure([&] { Geometry::warpRgb(rgb.share(), settings, quality); })},
            {"coverage_ms", measure([&] { Geometry::warpCoverage(alpha.share(), settings, quality); })}};
}

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: iprGeometryAudit OUTPUT.json\n";
        return 2;
    }
    const QString target = app.arguments().at(1);
    if (QFile::exists(target)) {
        std::cerr << "Refusing to overwrite evidence\n";
        return 2;
    }
    bool passed = true;
    const auto collect = [&](QJsonArray &records, const QJsonObject &record) {
        passed = record.value("passed").toBool() && passed;
        records.append(record);
    };
    QJsonArray landmarksRecords, edges, aliases, timings;
    // A preview proxy's rounded proportions against the full image's.
    const ipr::Size small(2048, 1365), large(6000, 4000);
    for (const State &state : states())
        collect(landmarksRecords, landmarkRecord(state, small, large));
    for (double angle : {0.5, 1.0, 8.5, 20.0, 45.0})
        for (const auto quality : {Geometry::Quality::Preview, Geometry::Quality::Export})
            collect(edges, edgeRecord(angle, quality));
    for (int scale : {50, 75, 90, 125})
        for (const auto quality : {Geometry::Quality::Preview, Geometry::Quality::Export})
            collect(aliases, aliasRecord(scale, quality));
    timings.append(timing(small, Geometry::Quality::Preview));
    timings.append(timing(large, Geometry::Quality::Export));

    const QJsonObject proof{
        {"schema_version", 1}, {"data_version", 1}, {"group", "b1-geometry"},
        {"campaign_id", QFileInfo(target).completeBaseName()},
        {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"tool_sha256", sha256Of(app.applicationFilePath())},
        {"method", "Synthetic charts in memory: Gaussian landmarks at known positions, a step edge and a fine grating, "
                   "through Geometry::warpRgb at preview and export sizes"},
        {"tolerances", QJsonObject{
            {"rigid_landmark_px", RigidLandmarkTolerance}, {"perspective_landmark_px", PerspectiveLandmarkTolerance},
            {"preview_vs_export_preview_px", PreviewExportTolerance}, {"inverse_normalized", InverseTolerance},
            {"rise_10_90_px", RiseTolerance}, {"overshoot_of_step", OvershootTolerance}, {"alias_of_source", AliasTolerance}}},
        {"limitations", QJsonArray{
            "Synthetic charts, not photographs; the application's whole chain is compared by the parity audit",
            "A homography is not a distortion correction: lens profiles stay in their own domain",
            "Timings on the machine of the campaign, opencv's own thread pool"}},
        {"landmarks", landmarksRecords}, {"edges", edges}, {"aliasing", aliases}, {"timings", timings},
        {"status", passed ? "passed" : "failed"}};
    QFile output(target);
    if (!output.open(QIODevice::NewOnly | QIODevice::WriteOnly) ||
        output.write(QJsonDocument(proof).toJson(QJsonDocument::Indented)) < 0) {
        std::cerr << "Cannot write the evidence\n";
        return 1;
    }
    std::cout << "status " << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 3;
}
