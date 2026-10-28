#include "imageeditor.h"
#include "RenderStageCache.h"
#include "ToneRecovery.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <opencv2/imgproc.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}

// Smooth gradients with texture and noise, so every stage changes pixels.
ColorPipeline::WorkingImage scene(int width, int height)
{
    cv::Mat rgba(height, width, CV_32FC4);
    cv::RNG rng(7);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float u = float(x) / float(width), v = float(y) / float(height);
            const float detail = 0.08f * float(std::sin(x * 0.21) * std::cos(y * 0.17));
            rgba.at<cv::Vec4f>(y, x) = {
                std::clamp(0.05f + 0.7f * u * v + detail + float(rng.gaussian(0.01)), 0.f, 1.f),
                std::clamp(0.10f + 0.5f * u + detail + float(rng.gaussian(0.01)), 0.f, 1.f),
                std::clamp(0.30f * (1.f - v) + 0.1f + detail + float(rng.gaussian(0.01)), 0.f, 1.f), 1.f};
        }
    ColorPipeline::WorkingImage image;
    image.linearRgba = rgba;
    image.referenceWhiteBalanceKelvin = 5200.0;
    image.toneStatistics = ToneRecovery::measure(rgba);
    return image;
}

void policy()
{
    const cv::Mat source(8, 8, CV_32FC4, cv::Scalar::all(0.5));
    const cv::Mat rgb(8, 8, CV_32FC3, cv::Scalar::all(0.25));
    RenderStageCache::Keys keys{"a", "ab", "abc", "abcd", "abcde"};
    RenderStageCache cache;
    auto start = cache.begin(source, 0.5, keys);
    check(start.stage == -1 && start.keep[0] && !start.keep[1] && !start.keep[4], "first render keeps the tones");
    for (int stage = 0; stage < RenderStageCache::StageCount; ++stage) cache.store(start, stage, rgb);
    check(cache.count() == 1, "only the requested output is stored");
    // peek() reads a kept output without touching the keep policy (the point colour eyedropper).
    const cv::Mat peeked = cache.peek(source, 0.5, 0, keys[0]);
    check(!peeked.empty() && peeked.data != rgb.data && cv::norm(peeked, rgb, cv::NORM_INF) == 0,
          "peek returns a private copy of a kept output");
    check(cache.peek(source, 0.5, 0, "other").empty() && cache.peek(source, 0.25, 0, keys[0]).empty() &&
          cache.peek(source, 0.5, 1, keys[1]).empty() && cache.peek(source, 0.5, 7, keys[0]).empty(),
          "peek needs the same source, scale, stage and key");

    auto dehaze = keys;
    dehaze[4] = "abcdX";
    start = cache.begin(source, 0.5, dehaze);
    check(start.stage == 0 && start.keep[3] && !start.keep[0], "a dehaze edit resumes after the tones and keeps local contrast");
    check(cv::norm(start.rgb, rgb, cv::NORM_INF) == 0 && start.rgb.data != rgb.data, "resumed output is a private copy");
    cache.store(start, 3, rgb);
    dehaze[4] = "abcdY";
    start = cache.begin(source, 0.5, dehaze);
    check(start.stage == 3 && cache.count() == 2, "the next dehaze render resumes after local contrast");

    auto tones = dehaze;
    for (auto &key : tones) key.prepend('Z');
    start = cache.begin(source, 0.5, tones);
    check(start.stage == -1 && cache.count() == 0, "a tone edit invalidates every stage");

    start = cache.begin(source, 0.5, tones);
    cache.store(start, 0, rgb);
    check(cache.begin(source, 0.25, tones).stage == -1, "another pixel scale starts over");
    start = cache.begin(source.clone(), 0.25, tones);
    check(start.stage == -1, "another source starts over");
    cache.clear();
    cache.store(start, 0, rgb);
    check(cache.count() == 0, "a store from before clear() is ignored");
}
}

class IprTestImageGenerator {
public:
    static void exercise()
    {
        const auto source = scene(1200, 800);
        constexpr double pixelScale = 0.3;
        ImageEditor::Settings settings;
        settings.temperature = 5200;
        settings.contrast = 20; settings.shadows = 30; settings.highlights = -40;
        settings.saturation = 10; settings.vibrance = 15;
        settings.gradingHue = {210, 0, 40}; settings.gradingSaturation = {30, 0, 20};
        settings.denoiseLuminance = 20; settings.clarity = 20; settings.texture = 10; settings.dehaze = 15;
        settings.sharpeningAmount = 30; settings.grain = 10;

        // Drags on each stage in turn, then back to an earlier one.
        std::vector<std::function<void(ImageEditor::Settings &)>> edits;
        for (int v : {25, 35, 45}) edits.push_back([v](auto &s) { s.dehaze = v; });
        for (int v : {30, 40}) edits.push_back([v](auto &s) { s.clarity = v; });
        for (int v : {60, 90}) edits.push_back([v](auto &s) { s.gradingHue[0] = v; });
        for (int v : {40, 60}) edits.push_back([v](auto &s) { s.sharpeningAmount = v; });
        for (int v : {30, 40}) edits.push_back([v](auto &s) { s.gradingBalance = v; });
        for (int v : {10, 0}) edits.push_back([v](auto &s) { s.contrast = v; });
        for (int v : {30, 40}) edits.push_back([v](auto &s) { s.denoiseLuminance = v; });
        edits.push_back([](auto &s) { s.dehaze = 0; });
        // A point colour on a colour of the scene, then another shift: the cache key holds the points.
        const cv::Vec4f picked = source.linearRgba.at<cv::Vec4f>(400, 600);
        const PointColor::Hsv hsv = PointColor::toHsv({picked[0], picked[1], picked[2]});
        edits.push_back([hsv](auto &s) {
            s.pointColor.count = 1;
            s.pointColor.points[0].source = hsv;
            s.pointColor.points[0].saturationShift = -60;
        });
        edits.push_back([](auto &s) { s.pointColor.points[0].saturationShift = -40; });
        edits.push_back([](auto &s) { s.pointColor.points[0].range = 80; });

        RenderStageCache cache;
        double plain = 0, cached = 0;
        for (size_t step = 0; step <= edits.size(); ++step) {
            if (step) edits[step - 1](settings);
            QElapsedTimer clock;
            clock.start();
            const auto expected = ImageEditor::renderWorkingImage(source, settings, pixelScale);
            const double plainMs = double(clock.nsecsElapsed()) / 1e6;
            clock.restart();
            const auto rendered = ImageEditor::renderWorkingImage(source, settings, pixelScale, &cache);
            const double cachedMs = double(clock.nsecsElapsed()) / 1e6;
            if (step > 0) { plain += plainMs; cached += cachedMs; }
            check(!expected.isNull() && !rendered.isNull(), "render");
            check(cv::checkRange(rendered.linearRgba, true), "finite output");
            check(cv::norm(rendered.linearRgba, expected.linearRgba, cv::NORM_INF) == 0, "cached render is bit-identical");
            check(cache.count() <= 2, "at most two stage outputs kept");
        }
        std::cout << "edits: " << plain << " ms without the cache, " << cached << " ms with it\n";

        // The export path never touches a cache, and a new source starts over.
        auto other = scene(1200, 800);
        other.linearRgba *= 0.8;
        const auto expected = ImageEditor::renderWorkingImage(other, settings, pixelScale);
        const auto rendered = ImageEditor::renderWorkingImage(other, settings, pixelScale, &cache);
        check(cv::norm(rendered.linearRgba, expected.linearRgba, cv::NORM_INF) == 0, "new source renders from scratch");
    }

    // The range overlay must never write into the source: a point without shift
    // leaves the pipeline neutral, which hands back the source buffer itself.
    static void overlayKeepsSource()
    {
        const auto source = scene(64, 48);
        const cv::Mat before = source.linearRgba.clone();
        ImageEditor::Settings settings;
        settings.temperature = 5200;
        settings.pointColor.count = 1;
        settings.pointColor.points[0].source = PointColor::toHsv({0.3, 0.2, 0.1});
        RenderStageCache cache;
        const auto overlaid = ImageEditor::renderPreview(source, settings, false, 1.0, &cache, 0);
        check(!overlaid.preview.isNull(), "overlay preview renders");
        check(cv::norm(source.linearRgba, before, cv::NORM_INF) == 0, "the overlay leaves the source untouched");
        const auto plain = ImageEditor::renderPreview(source, settings, false, 1.0, &cache, -1);
        check(plain.preview != overlaid.preview, "without the overlay the preview keeps its colours");
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        policy();
        IprTestImageGenerator::exercise();
        IprTestImageGenerator::overlayKeepsSource();
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "render stage cache: ok\n";
    return 0;
}
