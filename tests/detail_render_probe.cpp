// Manual measurement only (adaptive preview). Without option: time of the full-resolution
// render the zoom shows today, per stage group, against the preview proxy's (step 1).
// With --regions: for the recipes that render by region, the measures of the whole image,
// a screen and a 512-pixel tile rendered alone, and whether they equal the same parts of the
// whole render bit for bit (step 3). No sidecar is read or written; one JSON on stdout.
#include "imageeditor.h"
#include "ImageImporter.h"
#include "RenderChain.h"
#include "ToneRecovery.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

// ImageEditor's friend for its test tools: the editor's own proxy.
class IprTestImageGenerator
{
public:
    static ColorPipeline::WorkingImage proxy(const ColorPipeline::WorkingImage &image)
    {
        return RenderChain::editProxy(image);
    }
};

namespace {

double workingSetMiB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c), sizeof(c)))
        return double(c.PeakWorkingSetSize) / (1024.0 * 1024.0);
#endif
    return -1;
}

using Settings = RenderChain::Settings;

// The states of the zoom's question: which stages make the full-resolution render slow.
std::vector<std::pair<QString, std::function<void(Settings &)>>> states()
{
    const auto base = [](Settings &s) {
        s.brightness = 0.3f; s.contrast = 20; s.shadows = 30; s.highlights = -30; s.vibrance = 15;
    };
    return {
        {"neutral", [](Settings &) {}},
        // One control at a time: which part of the tones and colour costs.
        {"brightness_only", [](Settings &s) { s.brightness = 0.3f; }},
        {"contrast_only", [](Settings &s) { s.contrast = 20; }},
        {"shadows_highlights_only", [](Settings &s) { s.shadows = 30; s.highlights = -30; }},
        {"vibrance_only", [](Settings &s) { s.vibrance = 15; }},
        {"tones_colour", base},
        {"tones_colour_denoise", [base](Settings &s) { base(s); s.denoiseLuminance = 40; s.denoiseChrominance = 30; }},
        {"tones_colour_sharpening", [base](Settings &s) { base(s); s.sharpeningAmount = 40; }},
        {"tones_colour_texture_clarity", [base](Settings &s) { base(s); s.texture = 20; s.clarity = 30; }},
        {"tones_colour_dehaze", [base](Settings &s) { base(s); s.dehaze = 25; }},
        {"everything", [base](Settings &s) {
            base(s); s.denoiseLuminance = 40; s.denoiseChrominance = 30; s.sharpeningAmount = 40;
            s.texture = 20; s.clarity = 30; s.dehaze = 25; s.grain = 20; }},
    };
}

double renderMs(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale, int runs)
{
    double best = 1e30;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        const RenderChain::Preview rendered = RenderChain::preview(source, settings, false, pixelScale);
        const double ms = double(t.nsecsElapsed()) / 1e6;
        if (rendered.preview.isNull()) return -1;
        best = std::min(best, ms);
    }
    return best;
}

QJsonObject measure(const QString &path, int runs)
{
    RawDecoder::Options options;
    options.preferCameraNeutral = true;
    options.listDcpProfiles = true;
    options.useNormalizationEnvironment = false;
    ImageImport::Result loaded = ImageImport::load(path, options);
    if (loaded.image.isNull()) return {{"file", QFileInfo(path).fileName()}, {"error", loaded.error}};
    ColorPipeline::WorkingImage proxy = IprTestImageGenerator::proxy(loaded.image);
    // As the editor: the statistics of the neutral proxy weigh the tones of both.
    if (!proxy.isNull()) {
        const auto statistics = ToneRecovery::measure(proxy.linearRgba);
        proxy.toneStatistics = statistics;
        loaded.image.toneStatistics = statistics;
    }
    const ColorPipeline::WorkingImage &full = loaded.image;
    const ColorPipeline::WorkingImage &preview = proxy.isNull() ? full : proxy;
    const double pixelScale = double(std::max(preview.linearRgba.width(), preview.linearRgba.height())) /
                              double(std::max(full.linearRgba.width(), full.linearRgba.height()));
    QJsonArray rows;
    for (const auto &[name, edit] : states()) {
        Settings settings;
        settings.temperature = int(std::lround(full.referenceWhiteBalanceKelvin.value_or(5200.0)));
        edit(settings);
        const double fullMs = renderMs(full, settings, 1.0, runs);
        const double previewMs = renderMs(preview, settings, pixelScale, runs);
        rows.append(QJsonObject{{"state", name}, {"full_resolution_ms", fullMs}, {"preview_ms", previewMs},
                                {"ratio", previewMs > 0 ? fullMs / previewMs : -1},
                                {"peak_working_set_mib", workingSetMiB()}});
        std::cerr << QFileInfo(path).fileName().toStdString() << ' ' << name.toStdString() << ' '
                  << fullMs << " ms\n";
    }
    return {{"file", QFileInfo(path).fileName()}, {"camera", loaded.metadata.cameraModel},
            {"width", full.linearRgba.width()}, {"height", full.linearRgba.height()},
            {"preview_width", preview.linearRgba.width()}, {"preview_height", preview.linearRgba.height()},
            {"states", rows}};
}

// The recipes that render by region, on a real RAW: the camera signal through its DCP included.
std::vector<std::pair<QString, std::function<void(Settings &)>>> regionStates()
{
    const auto base = [](Settings &s) {
        s.brightness = 0.3f; s.contrast = 20; s.shadows = 30; s.highlights = -30; s.vibrance = 15;
    };
    return {
        {"neutral", [](Settings &) {}},
        {"tones_colour", base},
        {"tones_colour_white_balance", [base](Settings &s) { base(s); s.temperature += 800; s.tint = 10; }},
        {"tones_colour_curve", [base](Settings &s) { base(s); s.toneCurves[0] = {{0.0f, 0.04f}, {0.5f, 0.56f}, {1.0f, 0.97f}}; }},
        {"tones_colour_rotation", [base](Settings &s) { base(s); s.geometry.rotation = 2.5f; }},
        {"tones_colour_perspective", [base](Settings &s) { base(s); s.geometry.vertical = 25; s.geometry.scale = 90; }},
        {"tones_colour_sharpening_40", [base](Settings &s) { base(s); s.sharpeningAmount = 40; }},
        {"tones_colour_sharpening_rotation", [base](Settings &s) { base(s); s.sharpeningAmount = 40; s.geometry.rotation = 2.5f; }},
        {"tones_colour_grain_blur", [base](Settings &s) { base(s); s.grain = 25; s.blur = 2; }},
        {"tones_colour_texture_clarity", [base](Settings &s) { base(s); s.texture = 20; s.clarity = 30; }},
        {"tones_colour_clarity_sharpening", [base](Settings &s) { base(s); s.clarity = 30; s.sharpeningAmount = 40; }},
        {"tones_colour_dehaze", [base](Settings &s) { base(s); s.dehaze = 25; }},
        {"tones_colour_curve_clarity_dehaze_sharpening", [base](Settings &s) {
             base(s); s.toneCurves[0] = {{0.0f, 0.04f}, {0.5f, 0.56f}, {1.0f, 0.97f}}; s.clarity = 30; s.dehaze = 25;
             s.sharpeningAmount = 40; }},
    };
}

bool same(const ipr::Image &a, const ipr::Image &b)
{
    if (a.size() != b.size() || a.depth() != b.depth() || a.channels() != b.channels())
        return false;
    const size_t bytes = size_t(a.width()) * size_t(a.pixelBytes());
    for (int y = 0; y < a.height(); ++y)
        if (std::memcmp(a.row<unsigned char>(y), b.row<unsigned char>(y), bytes) != 0)
            return false;
    return true;
}

QJsonObject measureRegions(const QString &path)
{
    RawDecoder::Options options;
    options.preferCameraNeutral = true;
    options.listDcpProfiles = true;
    options.useNormalizationEnvironment = false;
    ImageImport::Result loaded = ImageImport::load(path, options);
    if (loaded.image.isNull()) return {{"file", QFileInfo(path).fileName()}, {"error", loaded.error}};
    ColorPipeline::WorkingImage proxy = IprTestImageGenerator::proxy(loaded.image);
    if (!proxy.isNull())
        loaded.image.toneStatistics = ToneRecovery::measure(proxy.linearRgba);
    const ColorPipeline::WorkingImage &full = loaded.image;
    const auto elapsedMs = [](const QElapsedTimer &t) { return double(t.nsecsElapsed()) / 1e6; };
    QJsonArray rows;
    for (const auto &[name, edit] : regionStates()) {
        Settings settings;
        settings.temperature = int(std::lround(full.referenceWhiteBalanceKelvin.value_or(5200.0)));
        edit(settings);
        QElapsedTimer t;
        t.start();
        const ColorPipeline::WorkingImage whole = RenderChain::workingImage(full, settings);
        const double wholeMs = elapsedMs(t);
        t.restart();
        RenderChain::WholeMeasures measures;
        const bool measured = RenderChain::measureWhole(full, settings, measures);
        const double measuresMs = elapsedMs(t);
        const ipr::Size canvas = whole.linearRgba.size();
        QJsonArray parts;
        for (const auto &[label, size] : std::vector<std::pair<QString, ipr::Size>>{
                 {"screen_2560x1440", ipr::Size(2560, 1440)}, {"tile_512", ipr::Size(512, 512)}}) {
            for (const auto &[where, origin] : std::vector<std::pair<QString, ipr::Point2i>>{
                     {"centre", ipr::Point2i((canvas.width - size.width) / 2, (canvas.height - size.height) / 2)},
                     {"corner", ipr::Point2i(canvas.width - size.width, 0)}}) {
                const ipr::Recti region(origin, size);
                t.restart();
                const ColorPipeline::WorkingImage part = RenderChain::workingRegion(full, settings, region, measures);
                const double partMs = elapsedMs(t);
                const bool equal = !part.isNull() && same(part.linearRgba, whole.linearRgba.view(region));
                parts.append(QJsonObject{{"region", label}, {"where", where}, {"x", region.x}, {"y", region.y},
                                         {"ms", partMs}, {"bit_identical", equal}});
                std::cerr << QFileInfo(path).fileName().toStdString() << ' ' << name.toStdString() << ' '
                          << label.toStdString() << ' ' << where.toStdString() << ' ' << partMs << " ms "
                          << (equal ? "identical" : "DIFFERENT") << std::endl;
            }
        }
        rows.append(QJsonObject{{"state", name}, {"whole_ms", wholeMs}, {"measures_ms", measuresMs},
                                {"measured", measured}, {"parts", parts}});
    }
    return {{"file", QFileInfo(path).fileName()}, {"camera", loaded.metadata.cameraModel},
            {"width", full.linearRgba.width()}, {"height", full.linearRgba.height()}, {"states", rows}};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: detailRenderProbe <raw>...\n";
        return 2;
    }
    const bool regions = QString::fromLocal8Bit(argv[1]) == QStringLiteral("--regions");
    constexpr int runs = 2;
    QJsonArray images;
    for (int i = regions ? 2 : 1; i < argc; ++i)
        images.append(regions ? measureRegions(QString::fromLocal8Bit(argv[i])) : measure(QString::fromLocal8Bit(argv[i]), runs));
    if (regions) {
        const QJsonObject result{{"schema_version", 1},
            {"status", "adaptive_preview_step3_region_render"},
            {"method", "RenderChain::workingImage at full resolution, then measureWhole and workingRegion on a "
                       "2560x1440 screen and a 512-pixel tile at the centre and at the top right corner, compared "
                       "with the same part of the whole render byte for byte; one run each, no stage cache"},
            {"threads", QThread::idealThreadCount()}, {"images", images}};
        std::cout << QJsonDocument(result).toJson().toStdString();
        return 0;
    }
    const QJsonObject result{{"schema_version", 1},
        {"status", "adaptive_preview_step1_full_resolution_cost"},
        {"method", "RenderChain::preview at pixelScale 1 (the zoom's full-resolution render) and on the 2048-pixel "
                   "proxy, best of 2 runs, no stage cache, no neural stage"},
        {"threads", QThread::idealThreadCount()}, {"images", images}};
    std::cout << QJsonDocument(result).toJson().toStdString();
    return 0;
}
