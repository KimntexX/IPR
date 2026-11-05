#include "RenderChain.h"
#include "ToneRecovery.h"

#include <QCoreApplication>

#include <QColorSpace>

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using ColorPipeline::WorkingImage;
using RenderChain::Settings;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

Settings neutral()
{
    Settings settings;
    settings.temperature = 5200;
    return settings;
}

// Gradients with texture and noise, so that every stage changes pixels.
WorkingImage scene(int width = 200, int height = 150)
{
    cv::Mat rgba(height, width, CV_32FC4);
    cv::RNG random(7);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float u = float(x) / float(width), v = float(y) / float(height);
            const float detail = 0.05f * float(std::sin(x * 0.7) * std::cos(y * 0.5));
            rgba.at<cv::Vec4f>(y, x) = {0.1f + 0.6f * u + detail + float(random.gaussian(0.005)),
                                        0.15f + 0.5f * v + detail + float(random.gaussian(0.005)),
                                        0.5f - 0.3f * u * v + detail + float(random.gaussian(0.005)), 1.0f};
        }
    WorkingImage image;
    image.linearRgba = rgba;
    image.colorSpace = ColorPipeline::workingColorSpace();
    image.referenceWhiteBalanceKelvin = 5200.0;
    image.toneStatistics = ToneRecovery::measure(rgba);
    return image;
}

bool same(const cv::Mat &a, const cv::Mat &b)
{
    return a.size() == b.size() && a.type() == b.type() && cv::norm(a, b, cv::NORM_INF) == 0;
}

LocalAdjustments::BrushStroke stroke()
{
    LocalAdjustments::BrushStroke result;
    result.points = {{0.4f, 0.5f}, {0.5f, 0.5f}};
    result.size = 0.1f;
    return result;
}

Settings withRemoval()
{
    Settings settings = neutral();
    Retouch::Operation operation;
    operation.strokes = {stroke()};
    settings.retouches = {operation};
    return settings;
}

Settings withMask()
{
    Settings settings = neutral();
    LocalAdjustments::Mask mask;
    LocalAdjustments::Shape shape;
    shape.kind = LocalAdjustments::Kind::Brush;
    shape.strokes = {stroke()};
    mask.shapes = {shape};
    mask.values[LocalValues::Exposure] = 40;
    settings.localMasks = {mask};
    return settings;
}

Settings withCrop()
{
    Settings settings = neutral();
    settings.geometry.cropX = 0.25;
    settings.geometry.cropY = 0.25;
    settings.geometry.cropWidth = 0.5;
    settings.geometry.cropHeight = 0.5;
    settings.contrast = 10;
    return settings;
}

void aNeutralRecipeHandsBackTheSourceBuffer()
{
    const WorkingImage source = scene();
    const WorkingImage rendered = RenderChain::workingImage(source, neutral());
    check(rendered.linearRgba.data == source.linearRgba.data, "rendered.linearRgba.data == source.linearRgba.data");
}

void invalidSourcesRenderNothing()
{
    check(RenderChain::workingImage({}, neutral()).isNull(), "RenderChain::workingImage({}, neutral()).isNull()");

    WorkingImage notFinite = scene();
    notFinite.linearRgba.at<cv::Vec4f>(3, 3)[1] = std::numeric_limits<float>::quiet_NaN();
    int notDone = -9;
    check(RenderChain::workingImage(notFinite, neutral(), 1.0, nullptr, &notDone).isNull(), "RenderChain::workingImage(notFinite, neutral(), 1.0, nullptr, &notDone).isNull()");
    check(notDone == -9, "notDone == -9");

    WorkingImage wrongType = scene();
    wrongType.linearRgba.convertTo(wrongType.linearRgba, CV_64FC4);
    check(RenderChain::workingImage(wrongType, neutral()).isNull(), "RenderChain::workingImage(wrongType, neutral()).isNull()");

    WorkingImage wrongSpace = scene();
    wrongSpace.colorSpace = QColorSpace(QColorSpace::SRgb);
    check(RenderChain::workingImage(wrongSpace, neutral()).isNull(), "RenderChain::workingImage(wrongSpace, neutral()).isNull()");

    const RenderChain::Preview preview = RenderChain::preview({}, neutral(), false);
    check(preview.preview.isNull(), "preview.preview.isNull()");
    check(preview.histogram.isEmpty(), "preview.histogram.isEmpty()");
    check(preview.retouchesNotDone == -1, "preview.retouchesNotDone == -1");
}

void everyStageActsAndLeavesTheSourceAlone()
{
    const std::vector<std::pair<const char *, std::function<void(Settings &)>>> stages = {
        {"grayscale", [](Settings &s) { s.grayscale = true; }},
        {"exposure", [](Settings &s) { s.brightness = 0.5f; }},
        {"contrast", [](Settings &s) { s.contrast = 30; }},
        {"temperature", [](Settings &s) { s.temperature = 7000; }},
        {"shadows", [](Settings &s) { s.shadows = 50; }},
        {"saturation", [](Settings &s) { s.saturation = 40; }},
        {"denoise", [](Settings &s) { s.denoiseLuminance = 60; }},
        {"texture", [](Settings &s) { s.texture = 50; }},
        {"dehaze", [](Settings &s) { s.dehaze = 40; }},
        {"curve", [](Settings &s) { s.toneCurves[0] = {{0.0f, 0.1f}, {1.0f, 0.9f}}; }},
        {"blur", [](Settings &s) { s.blur = 6; }},
        {"sharpening", [](Settings &s) { s.sharpeningAmount = 100; }},
        {"grain", [](Settings &s) { s.grain = 50; }},
        {"rotation", [](Settings &s) { s.geometry.rotation = 5.0; }},
        {"mask", [](Settings &s) { s = withMask(); }},
        {"retouch", [](Settings &s) { s = withRemoval(); }},
    };
    const WorkingImage source = scene();
    const cv::Mat before = source.linearRgba.clone();
    for (const auto &[name, edit] : stages) {
        Settings settings = neutral();
        edit(settings);
        const WorkingImage rendered = RenderChain::workingImage(source, settings);
        check(!rendered.isNull(), name);
        check(cv::checkRange(rendered.linearRgba, true), name);
        check(!same(rendered.linearRgba, before), name);
        check(same(source.linearRgba, before), name);
    }
}

void theCacheNeverChangesThePixels()
{
    const WorkingImage source = scene();
    RenderStageCache cache;
    Settings settings = withMask();
    settings.denoiseChrominance = 30;
    for (const int texture : {0, 20, 40}) {
        settings.texture = texture;
        const WorkingImage plain = RenderChain::workingImage(source, settings, 0.5);
        const WorkingImage cached = RenderChain::workingImage(source, settings, 0.5, &cache);
        check(same(plain.linearRgba, cached.linearRgba), "same(plain.linearRgba, cached.linearRgba)");
    }
}

void retouchesWithoutEffectAreReported()
{
    const WorkingImage source = scene();
    Settings settings = withRemoval();
    int notDone = -1;
    check(!RenderChain::workingImage(source, settings, 1.0, nullptr, &notDone).isNull(), "!RenderChain::workingImage(source, settings, 1.0, nullptr, &notDone).isNull()");
    check(notDone == 0, "notDone == 0");

    // A whole canvas to fill has nothing to fill it from.
    settings.retouches[0].strokes[0].size = 3.0f;
    notDone = -1;
    check(!RenderChain::workingImage(source, settings, 1.0, nullptr, &notDone).isNull(), "!RenderChain::workingImage(source, settings, 1.0, nullptr, &notDone).isNull()");
    check(notDone == 1, "notDone == 1");
}

void cropKeepsWhatItShould()
{
    const WorkingImage whole = scene();
    const WorkingImage same = RenderChain::cropped(whole, neutral().geometry);
    check(same.linearRgba.data == whole.linearRgba.data, "same.linearRgba.data == whole.linearRgba.data");
    check(RenderChain::cropped({}, withCrop().geometry).isNull(), "RenderChain::cropped({}, withCrop().geometry).isNull()");

    const WorkingImage part = RenderChain::cropped(whole, withCrop().geometry);
    check(part.linearRgba.cols == 100, "part.linearRgba.cols == 100");
    check(part.linearRgba.rows == 75, "part.linearRgba.rows == 75");
    // Its own pixels: the source is not reachable through them.
    check(part.linearRgba.datastart != whole.linearRgba.datastart, "part.linearRgba.datastart != whole.linearRgba.datastart");
}

void theQualityFollowsTheScale()
{
    check(RenderChain::geometryQuality(1.0) == Geometry::Quality::Export, "RenderChain::geometryQuality(1.0) == Geometry::Quality::Export");
    check(RenderChain::geometryQuality(0.25) == Geometry::Quality::Preview, "RenderChain::geometryQuality(0.25) == Geometry::Quality::Preview");
}

void theTonesOutputIsTheSameFromTheCache()
{
    const WorkingImage source = scene();
    Settings settings = neutral();
    settings.contrast = 25;
    settings.saturation = 30;
    const cv::Mat cold = RenderChain::tonesOutput(source, settings, 1.0, nullptr);
    check(cold.type() == CV_32FC3, "cold.type() == CV_32FC3");
    check(cold.size() == source.linearRgba.size(), "cold.size() == source.linearRgba.size()");

    RenderStageCache cache;
    check(!RenderChain::workingImage(source, settings, 1.0, &cache).isNull(), "!RenderChain::workingImage(source, settings, 1.0, &cache).isNull()");
    check(same(RenderChain::tonesOutput(source, settings, 1.0, &cache), cold), "same(RenderChain::tonesOutput(source, settings, 1.0, &cache), cold)");
}

void thePreviewShowsTheOriginalOnlyWhenAsked()
{
    const WorkingImage source = scene();
    const Settings settings = withCrop();
    const RenderChain::Preview without = RenderChain::preview(source, settings, false);
    check(!without.preview.isNull(), "!without.preview.isNull()");
    check(without.originalPreview.isNull(), "without.originalPreview.isNull()");
    check(!without.histogram.isEmpty(), "!without.histogram.isEmpty()");

    const RenderChain::Preview with = RenderChain::preview(source, settings, true);
    check(with.originalPreview.size() == QSize(100, 75), "with.originalPreview.size() == QSize(100, 75)");
    check(with.originalGeometry == settings.geometry, "with.originalGeometry == settings.geometry");
    check(!with.originalWholeCanvas, "!with.originalWholeCanvas");
    check(with.preview == without.preview, "with.preview == without.preview");
}

void theWholeCanvasIgnoresTheCrop()
{
    const RenderChain::Preview whole = RenderChain::preview(scene(), withCrop(), true, 1.0, nullptr, -1, -1, true);
    check(whole.preview.size() == QSize(200, 150), "whole.preview.size() == QSize(200, 150)");
    check(whole.originalPreview.size() == QSize(200, 150), "whole.originalPreview.size() == QSize(200, 150)");
    check(whole.originalWholeCanvas, "whole.originalWholeCanvas");
}

void overlaysShowCoverageWithoutTouchingTheSource()
{
    const WorkingImage source = scene();
    const cv::Mat before = source.linearRgba.clone();
    Settings settings = withMask();
    settings.retouches = withRemoval().retouches;
    settings.pointColor.count = 1;
    settings.pointColor.points[0].source = PointColor::toHsv({0.4, 0.3, 0.2});
    const QImage plain = RenderChain::preview(source, settings, false).preview;
    check(RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview != plain, "RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview != plain");
    check(RenderChain::preview(source, settings, false, 1.0, nullptr, -1, 0).preview != plain, "RenderChain::preview(source, settings, false, 1.0, nullptr, -1, 0).preview != plain");
    check(RenderChain::preview(source, settings, false, 1.0, nullptr, -1, -1, false, 0).preview != plain, "RenderChain::preview(source, settings, false, 1.0, nullptr, -1, -1, false, 0).preview != plain");
    // An element that does not exist any more shows nothing.
    check(RenderChain::preview(source, settings, false, 1.0, nullptr, -1, 5).preview == plain, "RenderChain::preview(source, settings, false, 1.0, nullptr, -1, 5).preview == plain");
    check(RenderChain::preview(source, settings, false, 1.0, nullptr, -1, -1, false, 5).preview == plain, "RenderChain::preview(source, settings, false, 1.0, nullptr, -1, -1, false, 5).preview == plain");
    check(same(source.linearRgba, before), "same(source.linearRgba, before)");
}

void anOverlayOnANeutralRecipeNeverWritesInTheSource()
{
    const WorkingImage source = scene();
    const cv::Mat before = source.linearRgba.clone();
    Settings settings = neutral();
    settings.pointColor.count = 1;
    settings.pointColor.points[0].source = PointColor::toHsv({0.4, 0.3, 0.2});
    check(!RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview.isNull(), "!RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview.isNull()");
    check(same(source.linearRgba, before), "same(source.linearRgba, before)");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        aNeutralRecipeHandsBackTheSourceBuffer();
        invalidSourcesRenderNothing();
        everyStageActsAndLeavesTheSourceAlone();
        theCacheNeverChangesThePixels();
        retouchesWithoutEffectAreReported();
        cropKeepsWhatItShould();
        theQualityFollowsTheScale();
        theTonesOutputIsTheSameFromTheCache();
        thePreviewShowsTheOriginalOnlyWhenAsked();
        theWholeCanvasIgnoresTheCrop();
        overlaysShowCoverageWithoutTouchingTheSource();
        anOverlayOnANeutralRecipeNeverWritesInTheSource();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "render chain: all passed\n";
    return 0;
}
