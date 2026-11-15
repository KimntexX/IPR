#include "RenderChain.h"

#include "CameraDenoise.h"
#include "CameraProfileEngine.h"
#include "ColorAdjustments.h"
#include "Contrast.h"
#include "Dehaze.h"
#include "DetailProcessing.h"
#include "Exposure.h"
#include "Grain.h"
#include "Histogram.h"
#include "LocalContrast.h"
#include "Filters.h"
#include "Pixels.h"
#include "Resize.h"
#include "ToneCurve.h"
#include "ToneRecovery.h"

#include <QDataStream>
#include <QDebug>
#include <QIODevice>

#include <algorithm>
#include <memory>
#include <cmath>
#include <vector>

namespace RenderChain {

namespace {

int classicalLuminance(const Settings &settings)
{
    return settings.neuralDenoiseApplied ? 0 : settings.denoiseLuminance;
}

// What a luminance range reads: the image as it is before any edit, on the canvas. Empty when
// no mask has a range.
ipr::Image rangeLuminanceOf(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale)
{
    if (!LocalAdjustments::usesLuminance(settings.localMasks))
        return {};
    return LocalAdjustments::rangeLuminance(
        Geometry::warpRgb(ipr::withoutAlpha(source.linearRgba), settings.geometry, geometryQuality(pixelScale)));
}

// What the masks add on the canvas; a range reads the image as it is before any edit.
LocalValues::Maps maskValues(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale,
                             RenderStageCache *cache)
{
    if (!LocalAdjustments::active(settings.localMasks))
        return {};
    QByteArray key;
    if (cache) {
        QDataStream stream(&key, QIODevice::WriteOnly);
        LocalAdjustments::appendKey(stream, settings.localMasks);
        const Geometry::Settings &g = settings.geometry;
        stream << qint32(g.quarterTurns) << g.flipHorizontal << g.flipVertical << g.rotation << qint32(g.vertical)
               << qint32(g.horizontal) << qint32(g.aspect) << qint32(g.scale) << qint32(g.offsetX) << qint32(g.offsetY);
        LocalValues::Maps kept;
        if (cache->maskMaps(source.linearRgba, pixelScale, key, kept))
            return kept;
    }
    LocalValues::Maps maps = LocalAdjustments::canvasValueMaps(settings.localMasks, settings.geometry, source.linearRgba.size(), rangeLuminanceOf(source, settings, pixelScale));
    if (cache)
        cache->storeMaskMaps(source.linearRgba, pixelScale, key, maps);
    return maps;
}

// Keys of the cached stages, in pipeline order: each one appends its stage's
// settings to the previous key, so equal keys mean equal input to the next stage.
RenderStageCache::Keys stageKeys(const Settings &s)
{
    RenderStageCache::Keys keys;
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    const auto ints = [&stream](const auto &values) { for (int value : values) stream << qint32(value); };
    LocalAdjustments::appendKey(stream, s.localMasks);   // adds nothing without a mask
    Retouch::appendKey(stream, s.retouches);             // nor without a retouch
    stream << s.grayscale << s.brightness << qint32(s.contrast) << qint32(s.temperature) << qint32(s.tint)
           << qint32(s.shadows) << qint32(s.blacks) << qint32(s.highlights) << qint32(s.whites);
    const Geometry::Settings &g = s.geometry;   // the crop comes after every stage
    stream << qint32(g.quarterTurns) << g.flipHorizontal << g.flipVertical << g.rotation << qint32(g.vertical)
           << qint32(g.horizontal) << qint32(g.aspect) << qint32(g.scale) << qint32(g.offsetX) << qint32(g.offsetY);
    if (s.denoiseBeforeProfile)   // the camera-signal luminance denoise belongs to the first stage then
        stream << qint32(classicalLuminance(s));
    keys[0] = key;
    stream << qint32(s.saturation) << qint32(s.vibrance) << qint32(s.gradingBlending) << qint32(s.gradingBalance);
    ints(s.mixerHue); ints(s.mixerSaturation); ints(s.mixerLuminance); ints(s.gradingHue); ints(s.gradingSaturation);
    ints(s.gradingLuminance);
    const size_t points = std::min(s.pointColor.count, PointColor::MaxPoints);
    stream << qint32(points);
    for (size_t i = 0; i < points; ++i) {
        const PointColor::Point &p = s.pointColor.points[i];
        stream << p.source[0] << p.source[1] << p.source[2] << qint32(p.hueShift) << qint32(p.saturationShift)
               << qint32(p.luminanceShift) << qint32(p.variance) << qint32(p.range);
    }
    keys[1] = key;
    stream << qint32(classicalLuminance(s)) << qint32(s.denoiseChrominance);
    keys[2] = key;
    stream << qint32(s.texture) << qint32(s.clarity);
    keys[3] = key;
    stream << qint32(s.dehaze);
    keys[4] = key;
    return keys;
}

bool whiteBalanceEdit(const Settings &settings, int referenceTemperature, const LocalValues::Maps &local)
{
    return settings.temperature != referenceTemperature || settings.tint != 0 ||
           !local[LocalValues::Temperature].empty() || !local[LocalValues::Tint].empty();
}

// RAW with a DCP: exposure and white balance act on the scene-linear camera
// signal before the profile tone curve, so highlights keep its shoulder.
bool sceneExposure(const ColorPipeline::WorkingImage &source, const Settings &settings, const LocalValues::Maps &local)
{
    return source.dcpContext && (settings.brightness != 0.0f || !local[LocalValues::Exposure].empty());
}

// Camera-signal development and grayscale, in place on the source's RGB, on the source's
// pixel grid: what the geometry then resamples. local: what the masks add, on the canvas.
bool renderCamera(const ColorPipeline::WorkingImage &source, const Settings &settings, int referenceTemperature,
                  double pixelScale, ipr::Image &adjusted, const LocalValues::Maps &local,
                  RenderStageCache *cache = nullptr)
{
    const bool whiteBalance = whiteBalanceEdit(settings, referenceTemperature, local);
    const bool exposeScene = sceneExposure(source, settings, local);
    // Experimental: the luminance denoise acts on the camera signal, before the
    // profile; colour noise is removed after the tones (reduceNoise).
    const bool denoiseCamera = source.dcpContext && settings.denoiseBeforeProfile &&
                               classicalLuminance(settings) != 0;
    if (source.dcpContext && (whiteBalance || exposeScene || denoiseCamera)) {
        QByteArray cameraKey;
        if (cache) {
            QDataStream key(&cameraKey, QIODevice::WriteOnly);
            key << settings.brightness << qint32(settings.temperature) << qint32(settings.tint)
                << denoiseCamera << qint32(denoiseCamera ? classicalLuminance(settings) : 0);
            LocalAdjustments::appendKey(key, settings.localMasks);
        }
        ipr::Image cachedCamera = cache ? cache->cameraOutput(source.linearRgba, pixelScale, cameraKey) : ipr::Image{};
        if (!cachedCamera.empty()) adjusted = std::move(cachedCamera);
        else {

            QString error;
            const CameraProfileEngine::DcpWorkingContext *camera = source.dcpContext.get();
            CameraProfileEngine::DcpWorkingContext denoised;
            if (denoiseCamera) {
                // From the retained source each time: never denoised twice.
                // Before the lens corrections when the decode kept that signal, then them again.
                // CameraDenoise publishes a new buffer and never writes the retained one.
                denoised = CameraProfileEngine::sharedCopy(*source.dcpContext);
                const bool preLens = !source.dcpContext->preLensCameraRgb.empty();
                denoised.cameraRgb = preLens ? source.dcpContext->preLensCameraRgb.share() : source.dcpContext->cameraRgb.share();
                CameraDenoise::Context denoiseContext;
                denoiseContext.pixelScale = pixelScale;
                if (!CameraDenoise::apply(denoised.cameraRgb, classicalLuminance(settings), denoiseContext) ||
                    (preLens && !CameraProfileEngine::applyLcp(denoised.lcpPath, denoised.cameraRgb, denoised.capture,
                                                               &error, denoised.lensOptions))) {
                    qWarning() << "Échec du débruitage du signal caméra :" << error;
                    return false;
                }
                camera = &denoised;
            }
            // The camera signal comes before the geometry: there, the maps follow the source's pixels.
            LocalValues::Maps onSource;
            LocalAdjustments::Masks cameraMasks = settings.localMasks;
            for (auto &mask : cameraMasks)
                for (size_t i = 0; i < mask.values.size(); ++i)
                    if (i != LocalValues::Exposure && i != LocalValues::Temperature && i != LocalValues::Tint)
                        mask.values[i] = 0;
            onSource = LocalAdjustments::canvasValueMaps(cameraMasks, {}, adjusted.size(),
                LocalAdjustments::usesLuminance(cameraMasks)
                    ? LocalAdjustments::rangeLuminance(ipr::withoutAlpha(source.linearRgba)) : ipr::Image{});
            if (camera->cameraRgb.size() != adjusted.size() ||
                !CameraProfileEngine::renderFromCameraSignal(*camera, settings.temperature, settings.tint,
                                                             settings.brightness, adjusted, &error, onSource)) {
                qWarning() << "Échec du rendu capteur avant DCP :" << error;
                return false;
            }
            if (cache) cache->storeCameraOutput(source.linearRgba, pixelScale, cameraKey, adjusted);
        }
    }

    if (settings.grayscale) ColorAdjustments::toGrayscale(adjusted);
    return true;
}

// Exposure, contrast, white balance without a camera signal and the four tone controls, in
// place on the canvas. statistics: the source's, measured at load.
bool renderCanvasTones(const ColorPipeline::WorkingImage &source, const Settings &settings, int referenceTemperature,
                       const ToneRecovery::Statistics &statistics, ipr::Image &adjusted, const LocalValues::Maps &local)
{
    if (!sceneExposure(source, settings, local)) Exposure::apply(adjusted, settings.brightness, local[LocalValues::Exposure]);

    Contrast::apply(adjusted, settings.contrast, local[LocalValues::Contrast]);

    QString colorError;
    if (!source.dcpContext && whiteBalanceEdit(settings, referenceTemperature, local) &&
        !ColorPipeline::applyTemperatureTint(
            adjusted, settings.temperature,
            source.referenceWhiteBalanceKelvin.value_or(5200.0),
            settings.tint, &colorError, local)) {
        qWarning() << "Échec de la balance température/nuance :" << colorError;
        return false;
    }

    // Shadows, Blacks, Highlights and Whites, weighted by the statistics
    // measured at load (ToneRecovery).
    const ToneRecovery::Amounts tones{static_cast<float>(settings.shadows) / 100.0f,
        static_cast<float>(settings.blacks) / 100.0f,
        static_cast<float>(settings.highlights) / 100.0f,
        static_cast<float>(settings.whites) / 100.0f};
    const bool localTones = !local[LocalValues::Shadows].empty() || !local[LocalValues::Blacks].empty() ||
                            !local[LocalValues::Highlights].empty() || !local[LocalValues::Whites].empty();
    if (!ToneRecovery::identity(tones) || localTones)
        ToneRecovery::apply(adjusted, tones, statistics, local);
    return true;
}

ToneRecovery::Statistics toneStatisticsOf(const ColorPipeline::WorkingImage &source)
{
    return source.toneStatistics ? *source.toneStatistics : ToneRecovery::measure(source.linearRgba);
}

// Camera-signal development, grayscale, exposure, contrast, white balance and
// the four tone controls, in place on the source's RGB. local: what the masks
// add, on the canvas (LocalAdjustments::valueMaps).
bool renderTones(const ColorPipeline::WorkingImage &source, const Settings &settings, int referenceTemperature,
                 double pixelScale, ipr::Image &adjusted, const LocalValues::Maps &local, RenderStageCache *cache)
{
    if (!renderCamera(source, settings, referenceTemperature, pixelScale, adjusted, local, cache))
        return false;
    // Sources without a camera signal (rendered rasters, RAW without DCP) have
    // no profile curve to precede.
    // The geometry's single resampling, before anything looks at neighbours; the
    // camera-signal luminance denoise above runs on the sensor's own pixel grid.
    adjusted = Geometry::warpRgb(std::move(adjusted), settings.geometry, geometryQuality(pixelScale));
    if (adjusted.empty())
        return false;
    return renderCanvasTones(source, settings, referenceTemperature, toneStatisticsOf(source), adjusted, local);
}

bool isNeutral(const Settings &settings, int referenceTemperature)
{
    const auto zero = [](const auto &values) {
        return std::all_of(values.cbegin(), values.cend(), [](int value) { return value == 0; });
    };
    return !settings.grayscale && settings.brightness == 0.0f &&
           settings.contrast == 0 && settings.blur == 0 && settings.shadows == 0 &&
           settings.blacks == 0 && settings.highlights == 0 && settings.whites == 0 &&
           settings.temperature == referenceTemperature && settings.tint == 0 &&
           settings.vibrance == 0 && settings.saturation == 0 &&
           settings.texture == 0 && settings.clarity == 0 && settings.dehaze == 0 &&
           settings.grain == 0 && classicalLuminance(settings) == 0 &&
           settings.denoiseChrominance == 0 && settings.sharpeningAmount == 0 &&
           zero(settings.mixerHue) && zero(settings.mixerSaturation) && zero(settings.mixerLuminance) &&
           zero(settings.gradingSaturation) && zero(settings.gradingLuminance) && !PointColor::active(settings.pointColor) &&
           ToneCurve::isIdentity(settings.toneCurves) && !Geometry::transformsPixels(settings.geometry) &&
           !LocalAdjustments::active(settings.localMasks) && !Retouch::active(settings.retouches);
}

bool validFormat(const ColorPipeline::WorkingImage &source)
{
    return !source.isNull() && source.colorSpace == ColorPipeline::workingColorSpace() &&
           source.linearRgba.depth() == ipr::Depth::F32 && source.linearRgba.channels() == 4;
}

bool validSource(const ColorPipeline::WorkingImage &source)
{
    return validFormat(source) && ipr::allFinite(source.linearRgba);
}

int referenceTemperatureOf(const ColorPipeline::WorkingImage &source)
{
    return static_cast<int>(std::lround(source.referenceWhiteBalanceKelvin.value_or(5200.0)));
}

// The canvas's alpha from the source's, then fully transparent pixels black: the end of every render.
ipr::Image withCoverage(const ipr::Image &rgb, ipr::Image coverage, const Geometry::Settings &geometry)
{
    std::vector<ipr::Image> channels = ipr::splitChannels(rgb);
    channels.push_back(std::move(coverage));
    ipr::Image rgbaOutput = ipr::merged(channels);
    if (Geometry::transformsPixels(geometry))
        Geometry::clearTransparent(rgbaOutput);
    return rgbaOutput;
}

// The noise reduction after the tones: its luminance part moves before the profile when the
// camera-signal denoise is asked for and the source has a camera signal.
DetailProcessing::DenoiseSettings denoiseSettingsOf(const ColorPipeline::WorkingImage &source, const Settings &settings)
{
    const bool cameraLuminance = settings.denoiseBeforeProfile && source.dcpContext;
    return {cameraLuminance ? 0 : classicalLuminance(settings), settings.denoiseChrominance};
}

double blurSigma(const Settings &settings, double pixelScale)
{
    return static_cast<double>(settings.blur) / 2.0 * pixelScale;
}

DetailProcessing::SharpenSettings sharpenSettings(const Settings &settings)
{
    return {settings.sharpeningAmount, settings.sharpeningRadius, settings.sharpeningMasking};
}

} // namespace

ColorPipeline::WorkingImage editProxy(
    const ColorPipeline::WorkingImage &source, int maximumExtent)
{
    // Interactive preview rendering (every slider drag) re-applies the
    // pipeline from the last cached stage before the edited one
    // (RenderStageCache), so its cost scales directly with pixel count. Camera RAW files routinely
    // exceed 24-50 Mpx while the on-screen preview (Main.qml's "canvas"
    // Image, fitted inside the editor pane) is at most a few Mpx, so editing
    // on a capped-resolution proxy removes most of that redundant work
    // without any visible quality loss, while export keeps using the full
    // resolution m_original untouched.
    if (source.isNull())
        return {};

    const int kEditProxyMaxDimension = std::max(1, maximumExtent);
    const int longestSide =
        std::max(source.linearRgba.width(), source.linearRgba.height());
    if (longestSide <= kEditProxyMaxDimension)
        return ColorPipeline::sharedCopy(source); // The same pixels, shared: nothing is allocated.

    const double scale =
        static_cast<double>(kEditProxyMaxDimension) / static_cast<double>(longestSide);
    const ipr::Size proxySize(
        std::max(1, static_cast<int>(std::lround(source.linearRgba.width() * scale))),
        std::max(1, static_cast<int>(std::lround(source.linearRgba.height() * scale))));

    ColorPipeline::WorkingImage proxy;
    proxy.linearRgba = ipr::resized(source.linearRgba, proxySize, ipr::Interpolation::Area);
    // Area averaging rounds alpha slightly above 1; a proxy restored from the
    // cache is revalidated as a master, which requires alpha within [0, 1].
    for (int y = 0; y < proxy.linearRgba.height(); ++y) {
        auto *row = proxy.linearRgba.mutableRow<ipr::Vec4f>(y);
        for (int x = 0; x < proxy.linearRgba.width(); ++x)
            row[x][3] = std::clamp(row[x][3], 0.f, 1.f);
    }
    proxy.toneStatistics = source.toneStatistics;
    proxy.colorSpace = source.colorSpace;
    proxy.sourceProfile = source.sourceProfile;
    proxy.referenceWhiteBalanceKelvin = source.referenceWhiteBalanceKelvin;
    if (source.dcpContext) {
        auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>(CameraProfileEngine::sharedCopy(*source.dcpContext));
        context->cameraRgb = ipr::resized(source.dcpContext->cameraRgb, proxySize, ipr::Interpolation::Area);
        if (!source.dcpContext->preLensCameraRgb.empty())
            context->preLensCameraRgb = ipr::resized(source.dcpContext->preLensCameraRgb, proxySize, ipr::Interpolation::Area);
        proxy.dcpContext = context;
    }
    return proxy;
}

Preview displaySized(Preview result, const ipr::Size &size)
{
    if (!result.preview.isNull())
        result.preview = result.preview.scaled(size.width, size.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!result.originalPreview.isNull())
        result.originalPreview = result.originalPreview.scaled(size.width, size.height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return result;
}

Geometry::Quality geometryQuality(double pixelScale)
{
    return pixelScale < 1 ? Geometry::Quality::Preview : Geometry::Quality::Export;
}

ColorPipeline::WorkingImage cropped(ColorPipeline::WorkingImage image, const Geometry::Settings &geometry)
{
    if (!image.isNull() && Geometry::hasCrop(geometry))
        image.linearRgba = Geometry::crop(std::move(image.linearRgba), geometry);
    return image;
}

PerceptualColour::Parameters perceptualParameters(const Settings &settings)
{
    return {settings.saturation, settings.vibrance,
            {settings.mixerHue, settings.mixerSaturation, settings.mixerLuminance},
            settings.pointColor, settings.gradingHue, settings.gradingSaturation,
            settings.gradingLuminance, settings.gradingBlending, settings.gradingBalance};
}

ipr::Image tonesOutput(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale,
                    RenderStageCache *cache)
{
    ipr::Image tones = cache ? cache->peek(source.linearRgba, pixelScale, 0, stageKeys(settings)[0]) : ipr::Image();
    if (!tones.empty())
        return tones;
    tones = ipr::withoutAlpha(source.linearRgba);
    const int reference = static_cast<int>(std::lround(source.referenceWhiteBalanceKelvin.value_or(5200.0)));
    if (!renderTones(source, settings, reference, pixelScale, tones, maskValues(source, settings, pixelScale, cache), cache))
        tones = {};
    return tones;
}

ColorPipeline::WorkingImage workingImage(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                         double pixelScale, RenderStageCache *cache, int *retouchesNotDone)
{
    if (!validSource(source))
        return {};

    const int referenceTemperature = referenceTemperatureOf(source);
    // A neutral pipeline returns its source, shared, and therefore avoids all
    // RGB split/merge buffers and per-pixel passes.
    if (isNeutral(settings, referenceTemperature))
        return ColorPipeline::sharedCopy(source);

    // What the masks add per setting; every map stays empty without any.
    const LocalValues::Maps local = maskValues(source, settings, pixelScale, cache);

    // Each costly stage runs only past the cached output it resumes from, then
    // hands its output to the cache when asked to keep it.
    RenderStageCache::Start start;
    if (cache) start = cache->begin(source.linearRgba, pixelScale, stageKeys(settings));
    const auto runs = [&start](int stage) { return start.stage < stage; };
    const auto reached = [&](int stage, const ipr::Image &rgb) { if (cache) cache->store(start, stage, rgb); };
    ipr::Image adjusted = std::move(start.rgb);
    if (runs(0)) {
        adjusted = ipr::withoutAlpha(source.linearRgba);
        if (!renderTones(source, settings, referenceTemperature, pixelScale, adjusted, local, cache)) return {};
        // On the canvas, once the tones are set; what follows treats the zone as the rest.
        const int notDone = Retouch::apply(adjusted, settings.retouches);
        if (retouchesNotDone)
            *retouchesNotDone = notDone;
        reached(0, adjusted);
    }
    if (runs(1)) {
        PerceptualColour::apply(adjusted, perceptualParameters(settings), local);
        reached(1, adjusted);
    }
    // Denoise before local-detail enhancement and sharpening. The operation
    // runs in linear RGB and keeps luminance and chrominance independent so
    // coloured speckles can be removed without flattening edge detail.
    // With the camera-signal denoise, luminance was denoised before the profile;
    // colour noise is removed here on both paths.
    if (runs(2)) {
        DetailProcessing::reduceNoise(adjusted, denoiseSettingsOf(source, settings), pixelScale, local);
        reached(2, adjusted);
    }
    if (runs(3)) {
        LocalContrast::apply(adjusted, settings.texture, settings.clarity, pixelScale, local);
        reached(3, adjusted);
    }
    if (runs(4)) {
        Dehaze::apply(adjusted, settings.dehaze, pixelScale, local[LocalValues::Dehaze]);
        reached(4, adjusted);
    }
    ToneCurve::apply(adjusted, settings.toneCurves);

    if (settings.blur > 0)
        adjusted = ipr::gaussianBlurred(adjusted, blurSigma(settings, pixelScale));

    // Capture/output sharpening follows denoise and all contrast operations.
    // Its adaptive mask suppresses low-contrast noise and the local bound
    // limits the bright/dark halos produced by a naive unsharp mask.
    if (settings.sharpeningAmount > 0 || !local[LocalValues::Sharpening].empty()) {
        DetailProcessing::sharpen(adjusted, sharpenSettings(settings), pixelScale, local);
    }

    Grain::apply(adjusted, settings.grain, pixelScale);

    ipr::Image rgbaOutput = withCoverage(
        adjusted, Geometry::warpCoverage(ipr::channel(source.linearRgba, 3), settings.geometry, geometryQuality(pixelScale)),
        settings.geometry);
    return {std::move(rgbaOutput), source.colorSpace, source.sourceProfile,
            source.referenceWhiteBalanceKelvin};
}

namespace {

int blurReach(const Settings &settings)
{
    return settings.blur > 0 ? int(ipr::gaussianHalfKernel(blurSigma(settings, 1.0)).size()) - 1 : 0;
}

// How far the stages after the curves read around a pixel, at full resolution: a region renders
// what comes before them on itself grown by this much.
int localReach(const ColorPipeline::WorkingImage &source, const Settings &settings)
{
    return DetailProcessing::reduceNoiseReach(denoiseSettingsOf(source, settings), 1.0) +
           LocalContrast::regionReach(settings.texture, settings.clarity) + blurReach(settings) +
           (settings.sharpeningAmount > 0 ? DetailProcessing::sharpenReach(sharpenSettings(settings), 1.0) : 0) +
           Grain::reach(settings.grain, 1.0);
}

ipr::Recti grownWithin(const ipr::Recti &area, int by, const ipr::Size &canvas)
{
    return ipr::Recti(area.x - by, area.y - by, area.width + 2 * by, area.height + 2 * by) &
           ipr::Recti(0, 0, canvas.width, canvas.height);
}

// The pixel-by-pixel stages, camera signal to perceptual colour, on one area of the canvas, from the window
// of the source it reads. camera: the camera stage of the whole source already rendered
// (renderCamera), read instead of rendering the window's. Empty on a failed stage.
ipr::Image regionBeforeLocal(const ColorPipeline::WorkingImage &source, const Settings &settings,
                             const ipr::Recti &area, const WholeMeasures &measures, const ipr::Image *camera = nullptr)
{
    const ipr::Size sourceSize = source.linearRgba.size();
    const Geometry::Quality quality = geometryQuality(1.0);
    const ipr::Recti window = Geometry::sourceWindow(settings.geometry, sourceSize, area, quality);
    const int referenceTemperature = referenceTemperatureOf(source);
    if (camera) {
        ipr::Image adjusted = Geometry::warpRgbRegion(camera->view(window), window.tl(), sourceSize, settings.geometry,
                                                      quality, area, measures.warpFloor);
        if (adjusted.empty() ||
            !renderCanvasTones(source, settings, referenceTemperature, measures.toneStatistics, adjusted, {}))
            return {};
        PerceptualColour::apply(adjusted, perceptualParameters(settings), {});
        return adjusted;
    }
    ColorPipeline::WorkingImage windowSource = ColorPipeline::sharedCopy(source);
    windowSource.linearRgba = source.linearRgba.view(window);
    if (source.dcpContext) {
        auto camera = std::make_shared<CameraProfileEngine::DcpWorkingContext>(
            CameraProfileEngine::sharedCopy(*source.dcpContext));
        if (camera->cameraRgb.size() != sourceSize)
            return {};
        camera->cameraRgb = camera->cameraRgb.view(window);
        camera->preLensCameraRgb = {};
        windowSource.dcpContext = std::move(camera);
    }
    if (windowSource.linearRgba.empty())
        return {};
    ipr::Image adjusted = ipr::withoutAlpha(windowSource.linearRgba);
    if (!renderCamera(windowSource, settings, referenceTemperature, 1.0, adjusted, {}))
        return {};
    adjusted = Geometry::warpRgbRegion(adjusted, window.tl(), sourceSize, settings.geometry, quality, area,
                                       measures.warpFloor);
    if (adjusted.empty() ||
        !renderCanvasTones(source, settings, referenceTemperature, measures.toneStatistics, adjusted, {}))
        return {};
    PerceptualColour::apply(adjusted, perceptualParameters(settings), {});
    return adjusted;
}

// The noise the sharpening measures on its whole input, from bands of rows around the rows it
// samples: each band rendered alone (with the blur's reach above and below), the same samples
// as the whole canvas. When the bands would cover most of it, the whole canvas at once. camera:
// the camera stage of the whole source, rendered once for every band.
bool measureSharpenNoise(const ColorPipeline::WorkingImage &source, const Settings &settings,
                         WholeMeasures &measures, const ipr::Size &canvas, const ipr::Image &camera)
{
    const int step = DetailProcessing::sharpenNoiseStep(canvas);
    const int reach = DetailProcessing::sharpenNoiseReach(), blur = blurReach(settings);
    const double sigma = blurSigma(settings, 1.0);
    std::vector<float> samples;
    const auto add = [&](const ipr::Recti &rows) {
        const ipr::Recti read = grownWithin(rows, blur, canvas) & ipr::Recti(0, rows.y - blur, canvas.width,
                                                                             rows.height + 2 * blur);
        ipr::Image rgb = regionBeforeLocal(source, settings, read, measures, &camera);
        if (rgb.empty())
            return false;
        ToneCurve::apply(rgb, settings.toneCurves);
        if (settings.blur > 0)
            rgb = ipr::gaussianBlurred(rgb, sigma);
        DetailProcessing::appendSharpenNoiseSamples(rgb.view(rows - read.tl()), rows.y, canvas.height, step, samples);
        return true;
    };
    if (2 * (reach + blur) + 1 >= step) {
        if (!add(ipr::Recti(0, 0, canvas.width, canvas.height)))
            return false;
    } else {
        for (int y = 0; y < canvas.height; y += step) {
            const int top = std::max(0, y - reach), bottom = std::min(canvas.height, y + reach + 1);
            if (!add(ipr::Recti(0, top, canvas.width, bottom - top)))
                return false;
        }
    }
    measures.sharpenNoiseSigma = DetailProcessing::sharpenNoiseSigma(std::move(samples));
    return true;
}

bool contrastActive(const Settings &settings)
{
    return settings.texture != 0 || settings.clarity != 0;
}

bool denoiseActive(const ColorPipeline::WorkingImage &source, const Settings &settings)
{
    const DetailProcessing::DenoiseSettings denoise = denoiseSettingsOf(source, settings);
    return denoise.luminance > 0 || denoise.chrominance > 0;
}

// Stages whose measures need the whole canvas rendered up to them.
bool wholePassNeeded(const ColorPipeline::WorkingImage &source, const Settings &settings)
{
    return denoiseActive(source, settings) || contrastActive(settings) || settings.dehaze != 0;
}

} // namespace

bool rendersByRegion(const Settings &settings)
{
    // The stages that read far around a pixel or measure the whole image come one by one; until
    // then, their recipes render whole.
    // The camera-signal luminance denoise measures its own model on the whole source: whole.
    return !LocalAdjustments::active(settings.localMasks) && !Retouch::active(settings.retouches) &&
           !(settings.denoiseBeforeProfile && classicalLuminance(settings) != 0);
}

QByteArray wholeMeasuresKey(const Settings &s)
{
    // What the stages before the geometry read (renderCamera), and the geometry itself; with
    // the sharpening, every setting before it (its noise measure reads its whole input).
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << qint32(s.temperature) << qint32(s.tint) << s.brightness << s.grayscale << s.denoiseBeforeProfile
           << qint32(classicalLuminance(s));
    const Geometry::Settings &g = s.geometry;
    stream << qint32(g.quarterTurns) << g.flipHorizontal << g.flipVertical << g.rotation << qint32(g.vertical)
           << qint32(g.horizontal) << qint32(g.aspect) << qint32(g.scale) << qint32(g.offsetX) << qint32(g.offsetY);
    // The noise reduction's measures read every setting before it and its own amounts.
    if (classicalLuminance(s) != 0 || s.denoiseChrominance != 0)
        stream << stageKeys(s)[2] << s.denoiseBeforeProfile;
    // Texture and clarity: their layers read every setting before them, and the clarity's the
    // texture's output; not their own slider values.
    if (contrastActive(s))
        stream << stageKeys(s)[2] << (s.texture != 0) << (s.clarity != 0) << qint32(s.clarity != 0 ? s.texture : 0);
    // The dehaze's reduced maps read every setting before it, not its own slider.
    if (s.dehaze != 0)
        stream << stageKeys(s)[3] << true;
    if (s.sharpeningAmount > 0) {
        stream << stageKeys(s)[4] << qint32(s.blur);
        for (const auto &curve : s.toneCurves) {
            stream << qint32(curve.size());
            for (const auto &point : curve)
                stream << point.x << point.y;
        }
    }
    return key;
}

bool measureWhole(const ColorPipeline::WorkingImage &source, const Settings &settings, WholeMeasures &measures)
{
    measures = {};
    if (!validSource(source) || !rendersByRegion(settings))
        return false;
    const bool neutral = isNeutral(settings, referenceTemperatureOf(source));
    const bool sharpens = settings.sharpeningAmount > 0 && !neutral;
    const bool contrast = wholePassNeeded(source, settings) && !neutral;
    // The camera stage of the whole source, when a measure reads it: once for all of them.
    ipr::Image camera;
    if (Geometry::resamples(settings.geometry) || sharpens || contrast) {
        camera = ipr::withoutAlpha(source.linearRgba);
        if (!renderCamera(source, settings, referenceTemperatureOf(source), 1.0, camera, {}))
            return false;
    }
    if (Geometry::resamples(settings.geometry))
        measures.warpFloor = Geometry::warpFloor(camera);
    measures.toneStatistics = toneStatisticsOf(source);
    measures.sourceSize = source.linearRgba.size();
    const ipr::Size canvas = Geometry::canvasSize(settings.geometry, measures.sourceSize);
    if (contrast) {
        // Texture and clarity read up to 256 pixels around: their coarse layers come from the whole
        // canvas once, and the sharpening's noise from that same render.
        ipr::Image whole = regionBeforeLocal(source, settings, ipr::Recti(0, 0, canvas.width, canvas.height), measures,
                                             &camera);
        if (whole.empty()) {
            measures = {};
            return false;
        }
        DetailProcessing::reduceNoise(whole, denoiseSettingsOf(source, settings), 1.0, {}, &measures.noise);
        auto layers = std::make_shared<LocalContrast::Layers>();
        LocalContrast::apply(whole, settings.texture, settings.clarity, 1.0, {}, layers.get());
        measures.contrastLayers = std::move(layers);
        Dehaze::apply(whole, settings.dehaze, 1.0, {}, &measures.dehazeLayers);
        if (sharpens) {
            ToneCurve::apply(whole, settings.toneCurves);
            if (settings.blur > 0)
                whole = ipr::gaussianBlurred(whole, blurSigma(settings, 1.0));
            std::vector<float> samples;
            DetailProcessing::appendSharpenNoiseSamples(whole, 0, canvas.height, DetailProcessing::sharpenNoiseStep(canvas),
                                                        samples);
            measures.sharpenNoiseSigma = DetailProcessing::sharpenNoiseSigma(std::move(samples));
        }
    } else if (sharpens && !measureSharpenNoise(source, settings, measures, canvas, camera)) {
        measures = {};
        return false;
    }
    measures.valid = true;
    return true;
}

ColorPipeline::WorkingImage workingRegion(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                          const ipr::Recti &region, const WholeMeasures &measures)
{
    // The whole source was checked when measured: each region skips that pass over every pixel.
    if (!measures.valid || !validFormat(source) || measures.sourceSize != source.linearRgba.size() ||
        !rendersByRegion(settings) || region.empty())
        return {};
    const ipr::Size sourceSize = source.linearRgba.size();
    const ipr::Size canvas = Geometry::canvasSize(settings.geometry, sourceSize);
    if ((region & ipr::Recti(0, 0, canvas.width, canvas.height)) != region ||
        (Geometry::resamples(settings.geometry) && measures.warpFloor.size() != 3) ||
        (settings.sharpeningAmount > 0 && !measures.sharpenNoiseSigma) ||
        (contrastActive(settings) && (!measures.contrastLayers || measures.contrastLayers->canvas != canvas)) ||
        (settings.dehaze != 0 && (!measures.dehazeLayers.state || measures.dehazeLayers.canvas != canvas)) ||
        (denoiseActive(source, settings) && !measures.noise.valid))
        return {};
    if (isNeutral(settings, referenceTemperatureOf(source))) {
        ColorPipeline::WorkingImage part = ColorPipeline::sharedCopy(source);
        part.linearRgba = source.linearRgba.view(region);
        return part;
    }

    // The pixel-by-pixel stages on the region grown by what the local stages read, those stages,
    // then the region: a pixel near the edge of the grown area is wrong only within the margin.
    const ipr::Recti area = grownWithin(region, localReach(source, settings), canvas);
    ipr::Image adjusted = regionBeforeLocal(source, settings, area, measures);
    if (adjusted.empty())
        return {};
    if (denoiseActive(source, settings))
        DetailProcessing::reduceNoise(adjusted, denoiseSettingsOf(source, settings), 1.0, {}, nullptr, &measures.noise);
    if (contrastActive(settings) &&
        !LocalContrast::applyRegion(adjusted, settings.texture, settings.clarity, area.tl(), *measures.contrastLayers))
        return {};
    if (settings.dehaze != 0 && !Dehaze::applyRegion(adjusted, settings.dehaze, area.tl(), measures.dehazeLayers))
        return {};
    ToneCurve::apply(adjusted, settings.toneCurves);
    if (settings.blur > 0)
        adjusted = ipr::gaussianBlurred(adjusted, blurSigma(settings, 1.0));
    if (settings.sharpeningAmount > 0)
        DetailProcessing::sharpen(adjusted, sharpenSettings(settings), 1.0, {}, &*measures.sharpenNoiseSigma);
    Grain::apply(adjusted, settings.grain, 1.0, area.tl());
    adjusted = adjusted.copy(region - area.tl());

    const Geometry::Quality quality = geometryQuality(1.0);
    const ipr::Recti window = Geometry::sourceWindow(settings.geometry, sourceSize, region, quality);
    ipr::Image rgbaOutput = withCoverage(
        adjusted, Geometry::warpCoverageRegion(ipr::channel(source.linearRgba.view(window), 3), window.tl(),
                                               sourceSize, settings.geometry, quality, region),
        settings.geometry);
    return {std::move(rgbaOutput), source.colorSpace, source.sourceProfile, source.referenceWhiteBalanceKelvin};
}

Preview preview(const ColorPipeline::WorkingImage &source, const Settings &settings, bool includeOriginal,
                double pixelScale, RenderStageCache *cache, int overlayPoint, int overlayMask, bool wholeCanvas,
                int overlayRetouch, const ColorPipeline::WorkingImage *originalSource)
{
    Preview result;
    if (includeOriginal) {
        // The decoded source includes camera/profile transforms and its decode
        // white balance, but none of the editor settings (including sidecars),
        // except the framing, so that it stays comparable with the preview.
        QString error;
        ColorPipeline::WorkingImage framed = ColorPipeline::sharedCopy(
            originalSource && !originalSource->isNull() ? *originalSource : source);
        framed.linearRgba = Geometry::warpRgba(std::move(framed.linearRgba), settings.geometry, geometryQuality(pixelScale));
        if (!wholeCanvas)
            framed = cropped(std::move(framed), settings.geometry);
        result.originalPreview = ColorPipeline::toSrgb(
            framed, ColorPipeline::OutputFormat::Preview8Bit, &error);
        result.originalGeometry = settings.geometry;
        result.originalWholeCanvas = wholeCanvas;
        if (result.originalPreview.isNull())
            qWarning() << "Échec du rendu de l'origine :" << error;
    }
    ColorPipeline::WorkingImage rendered =
        workingImage(source, settings, pixelScale, cache, &result.retouchesNotDone);
    if (rendered.isNull())
        return result;
    if (overlayPoint >= 0) {
        // Weights on the colour entering the point colour: the tones' output the
        // render just kept, or rendered here.
        const ipr::Image tones = tonesOutput(source, settings, pixelScale, cache);
        const ipr::Image weights = PerceptualColour::pointWeights(tones, perceptualParameters(settings), overlayPoint);
        if (!weights.empty() && weights.size() == rendered.linearRgba.size()) {
            // A neutral pipeline hands back the source buffer itself: overlay a copy.
            rendered.linearRgba = rendered.linearRgba.clone();
            ColorAdjustments::showWeights(rendered.linearRgba, weights);
        }
    }
    if (overlayMask >= 0 && overlayMask < int(settings.localMasks.size())) {
        const ipr::Image weights = LocalAdjustments::canvasCoverage(settings.localMasks[size_t(overlayMask)], settings.geometry, source.linearRgba.size(),
                                                           rangeLuminanceOf(source, settings, pixelScale));
        // A neutral pipeline hands back the source buffer itself: overlay a copy.
        rendered.linearRgba = rendered.linearRgba.clone();
        ColorAdjustments::showWeights(rendered.linearRgba, weights);
    }

    if (overlayRetouch >= 0 && overlayRetouch < int(settings.retouches.size())) {
        const ipr::Image weights = Retouch::coverage(settings.retouches[size_t(overlayRetouch)], rendered.linearRgba.size());
        rendered.linearRgba = rendered.linearRgba.clone();
        ColorAdjustments::showWeights(rendered.linearRgba, weights);
    }

    QString error;
    if (!wholeCanvas)
        rendered = cropped(std::move(rendered), settings.geometry);
    result.preview = ColorPipeline::toSrgb(rendered, ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (result.preview.isNull()) {
        qWarning() << "Échec du rendu de l'aperçu :" << error;
        return result;
    }

    result.histogram = Histogram::compute(result.preview);
    return result;
}

} // namespace RenderChain
