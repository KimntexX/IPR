// Manual measurement only (adaptive preview, step 1): time of the full-resolution
// render the zoom shows today, per stage group, against the preview proxy's.
// No sidecar is read or written; the result is one JSON on stdout.
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
        return ImageEditor::buildEditProxy(image);
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: detailRenderProbe <raw>...\n";
        return 2;
    }
    constexpr int runs = 2;
    QJsonArray images;
    for (int i = 1; i < argc; ++i) images.append(measure(QString::fromLocal8Bit(argv[i]), runs));
    const QJsonObject result{{"schema_version", 1},
        {"status", "adaptive_preview_step1_full_resolution_cost"},
        {"method", "RenderChain::preview at pixelScale 1 (the zoom's full-resolution render) and on the 2048-pixel "
                   "proxy, best of 2 runs, no stage cache, no neural stage"},
        {"threads", QThread::idealThreadCount()}, {"images", images}};
    std::cout << QJsonDocument(result).toJson().toStdString();
    return 0;
}
