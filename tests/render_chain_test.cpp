#include "RenderChain.h"
#include <random>
#include "image_testing.h"
#include "ToneRecovery.h"
#include "Filters.h"
#include "Pixels.h"


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
    ipr::Image rgba = ipr::Image::create({width, height}, ipr::Depth::F32, 4);
    std::mt19937 random(7);
    std::normal_distribution<float> gaussian(0.f, 0.005f);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float u = float(x) / float(width), v = float(y) / float(height);
            const float detail = 0.05f * float(std::sin(x * 0.7) * std::cos(y * 0.5));
            const float r = gaussian(random), g = gaussian(random), b = gaussian(random);
            imagetest::at<ipr::Vec4f>(rgba, y, x) = {0.1f + 0.6f * u + detail + r, 0.15f + 0.5f * v + detail + g,
                                                     0.5f - 0.3f * u * v + detail + b, 1.0f};
        }
    WorkingImage image;
    image.toneStatistics = ToneRecovery::measure(rgba);
    image.linearRgba = std::move(rgba);
    image.colorSpace = ColorPipeline::workingColorSpace();
    image.referenceWhiteBalanceKelvin = 5200.0;
    return image;
}

bool same(const ipr::Image &a, const ipr::Image &b)
{
    return imagetest::same(a, b);
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
    check(rendered.linearRgba.block() == source.linearRgba.block(), "the same pixels");
}

void neuralDenoisePreservesClassicalSettings()
{
    const auto source = scene(40, 32);
    auto settings = neutral();
    settings.neuralDenoiseApplied = true;
    settings.denoiseLuminance = 100;
    settings.denoiseChrominance = 25;
    const auto suppressed = RenderChain::workingImage(source, settings);
    check(settings.denoiseLuminance == 100, "stored classical luminance retained");
    settings.neuralDenoiseApplied = false;
    settings.denoiseLuminance = 0;
    const auto expected = RenderChain::workingImage(source, settings);
    check(same(suppressed.linearRgba, expected.linearRgba), "neural bypasses luminance but keeps colour");
}

void invalidSourcesRenderNothing()
{
    check(RenderChain::workingImage({}, neutral()).isNull(), "RenderChain::workingImage({}, neutral()).isNull()");

    WorkingImage notFinite = scene();
    imagetest::at<ipr::Vec4f>(notFinite.linearRgba, 3, 3)[1] = std::numeric_limits<float>::quiet_NaN();
    int notDone = -9;
    check(RenderChain::workingImage(notFinite, neutral(), 1.0, nullptr, &notDone).isNull(), "RenderChain::workingImage(notFinite, neutral(), 1.0, nullptr, &notDone).isNull()");
    check(notDone == -9, "notDone == -9");

    WorkingImage wrongType = scene();
    wrongType.linearRgba = ipr::Image::zeros(wrongType.linearRgba.size(), ipr::Depth::F64, 4);
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
    const ipr::Image before = source.linearRgba.clone();
    for (const auto &[name, edit] : stages) {
        Settings settings = neutral();
        edit(settings);
        const WorkingImage rendered = RenderChain::workingImage(source, settings);
        check(!rendered.isNull(), name);
        check(ipr::allFinite(rendered.linearRgba), name);
        check(!same(rendered.linearRgba, before), name);
        check(same(source.linearRgba, before), name);
    }
}

// The Effects slider gives half its value as a sigma, in pixels of the full image.
void theBlurSliderFollowsItsSigma()
{
    const WorkingImage source = scene();
    const WorkingImage plain = RenderChain::workingImage(source, neutral());
    Settings settings = neutral();
    settings.blur = 6;
    const WorkingImage blurred = RenderChain::workingImage(source, settings);
    ipr::Image expected = ipr::gaussianBlurred(plain.linearRgba, 3.0);
    check(ipr::largestDifference(blurred.linearRgba, expected) < 1e-5, "the whole image, borders included");

    const WorkingImage half = RenderChain::workingImage(source, settings, 0.5);
    expected = ipr::gaussianBlurred(plain.linearRgba, 1.5);
    check(ipr::largestDifference(half.linearRgba, expected) < 1e-5, "the sigma follows the scale");
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
    const WorkingImage same = RenderChain::cropped(ColorPipeline::sharedCopy(whole), neutral().geometry);
    check(same.linearRgba.block() == whole.linearRgba.block(), "without a crop, the same pixels");
    check(RenderChain::cropped({}, withCrop().geometry).isNull(), "RenderChain::cropped({}, withCrop().geometry).isNull()");

    const WorkingImage part = RenderChain::cropped(ColorPipeline::sharedCopy(whole), withCrop().geometry);
    check(part.linearRgba.width() == 100, "part.linearRgba.width() == 100");
    check(part.linearRgba.height() == 75, "part.linearRgba.height() == 75");
    // Its own pixels: the source is not reachable through them.
    check(part.linearRgba.block() != whole.linearRgba.block(), "the part has pixels of its own");
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
    const ipr::Image cold = RenderChain::tonesOutput(source, settings, 1.0, nullptr);
    check((cold.depth() == ipr::Depth::F32 && cold.channels() == 3), "(cold.depth() == ipr::Depth::F32 && cold.channels() == 3)");
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
    const ipr::Image before = source.linearRgba.clone();
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
    const ipr::Image before = source.linearRgba.clone();
    Settings settings = neutral();
    settings.pointColor.count = 1;
    settings.pointColor.points[0].source = PointColor::toHsv({0.4, 0.3, 0.2});
    check(!RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview.isNull(), "!RenderChain::preview(source, settings, false, 1.0, nullptr, 0).preview.isNull()");
    check(same(source.linearRgba, before), "same(source.linearRgba, before)");
}

} // namespace

// A region renders alone as the same region of the full-resolution render, bit for bit, for each
// stage that renders by region and for them together; the others say so and render nothing.
void aRegionIsThatPartOfTheWholeRender()
{
    const std::vector<std::pair<const char *, std::function<void(Settings &)>>> recipes = {
        {"neutral", [](Settings &) {}},
        {"grayscale", [](Settings &s) { s.grayscale = true; }},
        {"exposure", [](Settings &s) { s.brightness = 0.5f; }},
        {"contrast", [](Settings &s) { s.contrast = 30; }},
        {"temperature", [](Settings &s) { s.temperature = 7000; s.tint = 12; }},
        {"tones", [](Settings &s) { s.shadows = 50; s.blacks = -20; s.highlights = -60; s.whites = 15; }},
        {"colour", [](Settings &s) { s.saturation = 40; s.vibrance = -25; s.mixerHue[2] = 30; s.gradingSaturation[0] = 40; }},
        {"curve", [](Settings &s) { s.toneCurves[0] = {{0.0f, 0.1f}, {1.0f, 0.9f}}; }},
        {"turn", [](Settings &s) { s.geometry.quarterTurns = 1; s.geometry.flipVertical = true; s.contrast = 20; }},
        {"rotation", [](Settings &s) { s.geometry.rotation = 5.0f; s.brightness = 0.3f; }},
        {"perspective", [](Settings &s) { s.geometry.vertical = 35; s.geometry.scale = 70; s.geometry.aspect = 20; }},
        {"crop", [](Settings &s) { s = withCrop(); s.contrast = 25; }},
        {"sharpening", [](Settings &s) { s.sharpeningAmount = 60; s.sharpeningRadius = 1.5f; s.sharpeningMasking = 20; }},
        {"blur", [](Settings &s) { s.blur = 6; }},
        {"grain", [](Settings &s) { s.grain = 40; }},
        {"local stages", [](Settings &s) {
             s.contrast = 20; s.blur = 3; s.sharpeningAmount = 120; s.sharpeningRadius = 3.0f; s.grain = 30;
             s.geometry.rotation = 3.0f; }},
        {"texture", [](Settings &s) { s.texture = 50; }},
        {"clarity", [](Settings &s) { s.clarity = 40; }},
        {"negative clarity", [](Settings &s) { s.clarity = -60; s.contrast = 10; }},
        {"texture and clarity", [](Settings &s) { s.texture = -30; s.clarity = 50; s.brightness = 0.2f; }},
        {"clarity, sharpening, grain, rotation", [](Settings &s) {
             s.clarity = 35; s.texture = 20; s.sharpeningAmount = 60; s.grain = 20; s.blur = 2; s.geometry.rotation = -4.0f; }},
        {"curve after clarity", [](Settings &s) { s.clarity = 40; s.toneCurves[0] = {{0.0f, 0.1f}, {1.0f, 0.9f}}; }},
        {"dehaze", [](Settings &s) { s.dehaze = 40; }},
        {"adding haze", [](Settings &s) { s.dehaze = -50; s.contrast = 10; }},
        {"dehaze with everything after", [](Settings &s) {
             s.dehaze = 30; s.clarity = 25; s.texture = 10; s.toneCurves[0] = {{0.0f, 0.05f}, {0.5f, 0.55f}, {1.0f, 0.95f}};
             s.sharpeningAmount = 50; s.grain = 15; s.geometry.rotation = 2.0f; }},
        {"luminance noise", [](Settings &s) { s.denoiseLuminance = 60; }},
        {"colour noise", [](Settings &s) { s.denoiseChrominance = 40; }},
        {"strongest noise reduction", [](Settings &s) { s.denoiseLuminance = 100; s.denoiseChrominance = 100; s.contrast = 15; }},
        {"noise reduction and everything after", [](Settings &s) {
             s.denoiseLuminance = 40; s.denoiseChrominance = 25; s.clarity = 20; s.dehaze = 15; s.sharpeningAmount = 40;
             s.geometry.rotation = 3.0f; }},
        {"together", [](Settings &s) {
             s.brightness = -0.4f; s.contrast = -15; s.temperature = 4300; s.shadows = 40; s.saturation = 20;
             s.toneCurves[0] = {{0.0f, 0.05f}, {0.5f, 0.55f}, {1.0f, 0.95f}};
             s.geometry.rotation = -8.0f; s.geometry.horizontal = 20; s.geometry.quarterTurns = 3; }},
    };
    const WorkingImage source = scene(211, 143);
    for (const auto &[name, edit] : recipes) {
        Settings settings = neutral();
        edit(settings);
        check(RenderChain::rendersByRegion(settings), name);
        const WorkingImage whole = RenderChain::workingImage(source, settings);
        check(!whole.isNull(), name);
        RenderChain::WholeMeasures measures;
        check(RenderChain::measureWhole(source, settings, measures), name);
        const ipr::Size canvas = whole.linearRgba.size();
        for (const ipr::Recti &region : {ipr::Recti(0, 0, 40, 40), ipr::Recti(canvas.width - 33, canvas.height - 21, 33, 21),
                                         ipr::Recti(canvas.width / 2 - 25, canvas.height / 2 - 10, 50, 20),
                                         ipr::Recti(17, 0, 1, canvas.height), ipr::Recti(0, 0, canvas.width, canvas.height)}) {
            const WorkingImage part = RenderChain::workingRegion(source, settings, region, measures);
            check(!part.isNull(), name);
            check(same(part.linearRgba, whole.linearRgba.view(region)), name);
        }
        check(RenderChain::workingRegion(source, settings, ipr::Recti(canvas.width - 5, 0, 10, 10), measures).isNull(),
              "a region beyond the canvas renders nothing");
    }

    for (const auto &[name, edit] : std::vector<std::pair<const char *, std::function<void(Settings &)>>>{
             {"camera-signal denoise", [](Settings &s) { s.denoiseLuminance = 60; s.denoiseBeforeProfile = true; }},
             {"mask", [](Settings &s) { s = withMask(); }}, {"retouch", [](Settings &s) { s = withRemoval(); }}}) {
        Settings settings = neutral();
        edit(settings);
        RenderChain::WholeMeasures measures;
        check(!RenderChain::rendersByRegion(settings) && !RenderChain::measureWhole(source, settings, measures) &&
                  RenderChain::workingRegion(source, settings, ipr::Recti(0, 0, 10, 10), measures).isNull(), name);
    }
    // Large enough for the sharpening's noise measure to sample one row in eight: it reads
    // bands of rows instead of the whole canvas, and gives the same pixels.
    const WorkingImage large = scene(1100, 960);
    for (const auto &[name, edit] : std::vector<std::pair<const char *, std::function<void(Settings &)>>>{
             {"sharpening by bands", [](Settings &s) { s.sharpeningAmount = 80; s.contrast = 15; }},
             {"sharpening after a blur, rotated", [](Settings &s) {
                  s.sharpeningAmount = 50; s.blur = 2; s.geometry.rotation = -2.0f; }},
             {"clarity and texture on a larger canvas", [](Settings &s) {
                  s.clarity = 45; s.texture = 25; s.sharpeningAmount = 40; }},
             {"dehaze and a curve on a larger canvas", [](Settings &s) {
                  s.dehaze = 35; s.sharpeningAmount = 30; s.toneCurves[0] = {{0.0f, 0.08f}, {1.0f, 0.92f}}; }},
             {"a curve before the sharpening's bands", [](Settings &s) {
                  s.sharpeningAmount = 60; s.toneCurves[0] = {{0.0f, 0.2f}, {1.0f, 0.8f}}; }},
             {"noise reduction on a larger canvas", [](Settings &s) {
                  s.denoiseLuminance = 50; s.denoiseChrominance = 60; s.sharpeningAmount = 30; }}}) {
        Settings settings = neutral();
        edit(settings);
        const WorkingImage whole = RenderChain::workingImage(large, settings);
        RenderChain::WholeMeasures measures;
        check(RenderChain::measureWhole(large, settings, measures) && measures.sharpenNoiseSigma.has_value(), name);
        const ipr::Size canvas = whole.linearRgba.size();
        for (const ipr::Recti &region : {ipr::Recti(0, 0, 64, 64), ipr::Recti(canvas.width / 2 - 40, canvas.height / 2 - 40, 80, 80),
                                         ipr::Recti(canvas.width - 50, canvas.height - 70, 50, 70)}) {
            const WorkingImage part = RenderChain::workingRegion(large, settings, region, measures);
            check(!part.isNull() && same(part.linearRgba, whole.linearRgba.view(region)), name);
        }
    }

    // The coarse layers do not depend on the sliders: measures taken at one clarity serve another.
    {
        Settings first = neutral(), second = neutral();
        first.clarity = 30; first.texture = 15; first.contrast = 10;
        second = first; second.clarity = 70;
        check(RenderChain::wholeMeasuresKey(first) == RenderChain::wholeMeasuresKey(second), "the clarity's value keeps the key");
        second.texture = 40;
        check(RenderChain::wholeMeasuresKey(first) != RenderChain::wholeMeasuresKey(second),
              "the texture under a clarity changes the key");
        second.texture = first.texture;
        RenderChain::WholeMeasures measures;
        check(RenderChain::measureWhole(source, first, measures), "measured at one clarity");
        const WorkingImage whole = RenderChain::workingImage(source, second);
        const ipr::Recti region(30, 20, 90, 70);
        const WorkingImage part = RenderChain::workingRegion(source, second, region, measures);
        check(!part.isNull() && same(part.linearRgba, whole.linearRgba.view(region)), "rendered at another");
    }
    // Nor do the dehaze's: measures taken at one dehaze value serve another.
    {
        Settings first = neutral(), second = neutral();
        first.dehaze = 20; first.saturation = 10;
        second = first; second.dehaze = -60;
        check(RenderChain::wholeMeasuresKey(first) == RenderChain::wholeMeasuresKey(second), "the dehaze's value keeps the key");
        RenderChain::WholeMeasures measures;
        check(RenderChain::measureWhole(source, first, measures), "measured at one dehaze value");
        for (const Settings &settings : {first, second}) {
            const WorkingImage whole = RenderChain::workingImage(source, settings);
            const ipr::Recti region(10, 40, 120, 60);
            const WorkingImage part = RenderChain::workingRegion(source, settings, region, measures);
            check(!part.isNull() && same(part.linearRgba, whole.linearRgba.view(region)), "rendered at both values");
        }
    }

    Settings rotated = neutral();
    rotated.geometry.rotation = 4.0f;
    check(RenderChain::workingRegion(source, rotated, ipr::Recti(0, 0, 10, 10), RenderChain::WholeMeasures{}).isNull(),
          "a region needs the measures of the whole image");
}

// The original's tiles: a region of the source warped by the framing alone, the same pixels as the
// whole original (Geometry::warpRgba), whatever the geometry.
void anOriginalRegionIsThatPartOfTheWholeOriginal()
{
    const WorkingImage source = scene();
    const std::vector<float> floor = Geometry::warpFloor(ipr::withoutAlpha(source.linearRgba));
    const std::vector<std::pair<const char *, std::function<void(Geometry::Settings &)>>> framings = {
        {"none", [](Geometry::Settings &) {}},
        {"quarter turn and mirror", [](Geometry::Settings &g) { g.quarterTurns = 1; g.flipHorizontal = true; }},
        {"rotation", [](Geometry::Settings &g) { g.rotation = 5.0f; }},
        {"perspective", [](Geometry::Settings &g) { g.rotation = -3.0f; g.vertical = 15; }},
    };
    for (const auto &[name, frame] : framings) {
        Geometry::Settings geometry;
        frame(geometry);
        const ipr::Image whole = Geometry::warpRgba(source.linearRgba.clone(), geometry, RenderChain::geometryQuality(1.0));
        const ipr::Size canvas = Geometry::canvasSize(geometry, source.linearRgba.size());
        for (const ipr::Recti &region : {ipr::Recti(0, 0, 64, 48), ipr::Recti(canvas.width - 70, canvas.height - 50, 70, 50),
                                         ipr::Recti(canvas.width / 3, canvas.height / 4, 50, 60)}) {
            const WorkingImage part = RenderChain::originalRegion(source, geometry, region, floor);
            check(!part.isNull() && same(part.linearRgba, whole.view(region)), name);
        }
        check(RenderChain::originalRegion(source, geometry, ipr::Recti(canvas.width - 5, 0, 10, 10), floor).isNull(),
              "a region off the canvas renders nothing");
    }
    Geometry::Settings rotated;
    rotated.rotation = 4.0f;
    check(RenderChain::originalRegion(source, rotated, ipr::Recti(0, 0, 10, 10), {}).isNull(),
          "a resampling framing needs the whole source's floor");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        aNeutralRecipeHandsBackTheSourceBuffer();
        neuralDenoisePreservesClassicalSettings();
        invalidSourcesRenderNothing();
        everyStageActsAndLeavesTheSourceAlone();
        theBlurSliderFollowsItsSigma();
        theCacheNeverChangesThePixels();
        retouchesWithoutEffectAreReported();
        cropKeepsWhatItShould();
        theQualityFollowsTheScale();
        theTonesOutputIsTheSameFromTheCache();
        thePreviewShowsTheOriginalOnlyWhenAsked();
        theWholeCanvasIgnoresTheCrop();
        overlaysShowCoverageWithoutTouchingTheSource();
        anOverlayOnANeutralRecipeNeverWritesInTheSource();
        aRegionIsThatPartOfTheWholeRender();
        anOriginalRegionIsThatPartOfTheWholeOriginal();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "render chain: all passed\n";
    return 0;
}
