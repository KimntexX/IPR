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
#include "OpenCvBridge.h"
#include "ToneCurve.h"
#include "ToneRecovery.h"

#include <QDataStream>
#include <QDebug>
#include <QIODevice>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace RenderChain {

namespace {

// What a luminance range reads: the image as it is before any edit, on the canvas. Empty when
// no mask has a range.
cv::Mat rangeLuminanceOf(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale)
{
    if (!LocalAdjustments::usesLuminance(settings.localMasks))
        return {};
    cv::Mat rgb;
    rgb = ipr::withoutAlpha(source.linearRgba);
    return LocalAdjustments::rangeLuminance(Geometry::warpRgb(rgb, settings.geometry, geometryQuality(pixelScale)));
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
    const ipr::Size canvas = Geometry::canvasSize(settings.geometry, ipr::fromCv(source.linearRgba.size()));
    const LocalValues::Maps maps = LocalAdjustments::valueMaps(settings.localMasks, canvas, rangeLuminanceOf(source, settings, pixelScale));
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
        stream << qint32(s.denoiseLuminance);
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
    stream << qint32(s.denoiseLuminance) << qint32(s.denoiseChrominance);
    keys[2] = key;
    stream << qint32(s.texture) << qint32(s.clarity);
    keys[3] = key;
    stream << qint32(s.dehaze);
    keys[4] = key;
    return keys;
}

// Camera-signal development, grayscale, exposure, contrast, white balance and
// the four tone controls, in place on the source's RGB. local: what the masks
// add, on the canvas (LocalAdjustments::valueMaps).
bool renderTones(const ColorPipeline::WorkingImage &source, const Settings &settings, int referenceTemperature,
                 double pixelScale, cv::Mat &adjusted, const LocalValues::Maps &local)
{
    const bool whiteBalanceEdit = settings.temperature != referenceTemperature || settings.tint != 0 ||
                                  !local[LocalValues::Temperature].empty() || !local[LocalValues::Tint].empty();
    // RAW with a DCP: exposure and white balance act on the scene-linear camera
    // signal before the profile tone curve, so highlights keep its shoulder.
    const bool sceneExposure = source.dcpContext && (settings.brightness != 0.0f || !local[LocalValues::Exposure].empty());
    // Experimental: the luminance denoise acts on the camera signal, before the
    // profile; colour noise is removed after the tones (reduceNoise).
    const bool denoiseCamera = source.dcpContext && settings.denoiseBeforeProfile &&
                               settings.denoiseLuminance != 0;
    if (source.dcpContext && (whiteBalanceEdit || sceneExposure || denoiseCamera)) {
        QString error;
        const CameraProfileEngine::DcpWorkingContext *camera = source.dcpContext.get();
        CameraProfileEngine::DcpWorkingContext denoised;
        if (denoiseCamera) {
            // From the retained source each time: never denoised twice.
            // Before the lens corrections when the decode kept that signal, then them again.
            // CameraDenoise publishes a new buffer and never writes the retained one.
            denoised = *source.dcpContext;
            const bool preLens = !source.dcpContext->preLensCameraRgb.empty();
            denoised.cameraRgb = preLens ? source.dcpContext->preLensCameraRgb : source.dcpContext->cameraRgb;
            CameraDenoise::Context denoiseContext;
            denoiseContext.pixelScale = pixelScale;
            if (!CameraDenoise::apply(denoised.cameraRgb, settings.denoiseLuminance, denoiseContext) ||
                (preLens && !CameraProfileEngine::applyLcp(denoised.lcpPath, denoised.cameraRgb, denoised.capture,
                                                           &error, denoised.lensOptions))) {
                qWarning() << "Échec du débruitage du signal caméra :" << error;
                return false;
            }
            camera = &denoised;
        }
        // The camera signal comes before the geometry: there, the maps follow the source's pixels.
        LocalValues::Maps onSource;
        for (const LocalValues::Parameter parameter : {LocalValues::Exposure, LocalValues::Temperature, LocalValues::Tint})
            onSource[parameter] = LocalAdjustments::toSource(local[parameter], settings.geometry, ipr::fromCv(adjusted.size()));
        if (camera->cameraRgb.size() != adjusted.size() ||
            !CameraProfileEngine::renderFromCameraSignal(*camera,
                settings.temperature, settings.tint, settings.brightness, adjusted, &error, onSource)) {
            qWarning() << "Échec du rendu capteur avant DCP :" << error;
            return false;
        }
    }

    if (settings.grayscale) ColorAdjustments::toGrayscale(adjusted);

    // Sources without a camera signal (rendered rasters, RAW without DCP) have
    // no profile curve to precede.
    // The geometry's single resampling, before anything looks at neighbours; the
    // camera-signal luminance denoise above runs on the sensor's own pixel grid.
    adjusted = Geometry::warpRgb(adjusted, settings.geometry, geometryQuality(pixelScale));

    if (!sceneExposure) Exposure::apply(adjusted, settings.brightness, local[LocalValues::Exposure]);

    Contrast::apply(adjusted, settings.contrast, local[LocalValues::Contrast]);

    QString colorError;
    if (!source.dcpContext && whiteBalanceEdit &&
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
        ToneRecovery::apply(adjusted, tones, source.toneStatistics ? *source.toneStatistics
                                                                   : ToneRecovery::measure(source.linearRgba), local);
    return true;
}

} // namespace

Geometry::Quality geometryQuality(double pixelScale)
{
    return pixelScale < 1 ? Geometry::Quality::Preview : Geometry::Quality::Export;
}

ColorPipeline::WorkingImage cropped(ColorPipeline::WorkingImage image, const Geometry::Settings &geometry)
{
    if (!image.isNull() && Geometry::hasCrop(geometry))
        image.linearRgba = Geometry::crop(image.linearRgba, geometry).clone();
    return image;
}

PerceptualColour::Parameters perceptualParameters(const Settings &settings)
{
    return {settings.saturation, settings.vibrance,
            {settings.mixerHue, settings.mixerSaturation, settings.mixerLuminance},
            settings.pointColor, settings.gradingHue, settings.gradingSaturation,
            settings.gradingLuminance, settings.gradingBlending, settings.gradingBalance};
}

cv::Mat tonesOutput(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale,
                    RenderStageCache *cache)
{
    cv::Mat tones = cache ? cache->peek(source.linearRgba, pixelScale, 0, stageKeys(settings)[0]) : cv::Mat();
    if (!tones.empty())
        return tones;
    tones = ipr::withoutAlpha(source.linearRgba);
    const int reference = static_cast<int>(std::lround(source.referenceWhiteBalanceKelvin.value_or(5200.0)));
    if (!renderTones(source, settings, reference, pixelScale, tones, maskValues(source, settings, pixelScale, cache)))
        tones.release();
    return tones;
}

ColorPipeline::WorkingImage workingImage(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                         double pixelScale, RenderStageCache *cache, int *retouchesNotDone)
{
    if (source.isNull() || source.colorSpace != ColorPipeline::workingColorSpace() ||
        source.linearRgba.type() != CV_32FC4 || !ipr::allFinite(source.linearRgba))
        return {};

    const int referenceTemperature = static_cast<int>(std::lround(
        source.referenceWhiteBalanceKelvin.value_or(5200.0)));
    const bool hasToneCurves = !ToneCurve::isIdentity(settings.toneCurves);
    // A neutral pipeline returns its shallow source and therefore avoids all
    // RGB split/merge buffers and per-pixel passes.
    if (!settings.grayscale && settings.brightness == 0.0f &&
        settings.contrast == 0 && settings.blur == 0 && settings.shadows == 0 &&
        settings.blacks == 0 && settings.highlights == 0 && settings.whites == 0 &&
        settings.temperature == referenceTemperature && settings.tint == 0 &&
        settings.vibrance == 0 && settings.saturation == 0 &&
        settings.texture == 0 && settings.clarity == 0 && settings.dehaze == 0 &&
        settings.grain == 0 && settings.denoiseLuminance == 0 &&
        settings.denoiseChrominance == 0 && settings.sharpeningAmount == 0 &&
        std::all_of(settings.mixerHue.cbegin(), settings.mixerHue.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.mixerSaturation.cbegin(), settings.mixerSaturation.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.mixerLuminance.cbegin(), settings.mixerLuminance.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.gradingSaturation.cbegin(), settings.gradingSaturation.cend(),
                    [](int value) { return value == 0; }) &&
        std::all_of(settings.gradingLuminance.cbegin(), settings.gradingLuminance.cend(),
                    [](int value) { return value == 0; }) && !PointColor::active(settings.pointColor) &&
        !hasToneCurves && !Geometry::transformsPixels(settings.geometry) &&
        !LocalAdjustments::active(settings.localMasks) && !Retouch::active(settings.retouches))
        return source;

    // What the masks add per setting; every map stays empty without any.
    const LocalValues::Maps local = maskValues(source, settings, pixelScale, cache);

    // Each costly stage runs only past the cached output it resumes from, then
    // hands its output to the cache when asked to keep it.
    RenderStageCache::Start start;
    if (cache) start = cache->begin(source.linearRgba, pixelScale, stageKeys(settings));
    const auto runs = [&start](int stage) { return start.stage < stage; };
    const auto reached = [&](int stage, const cv::Mat &rgb) { if (cache) cache->store(start, stage, rgb); };
    cv::Mat adjusted = start.rgb;
    if (runs(0)) {
        adjusted = ipr::withoutAlpha(source.linearRgba);
        if (!renderTones(source, settings, referenceTemperature, pixelScale, adjusted, local)) return {};
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
        const bool cameraLuminance = settings.denoiseBeforeProfile && source.dcpContext;
        DetailProcessing::reduceNoise(adjusted, {cameraLuminance ? 0 : settings.denoiseLuminance,
                                                 settings.denoiseChrominance}, pixelScale, local);
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

    if (settings.blur > 0) {
        const double sigma = static_cast<double>(settings.blur) / 2.0 * pixelScale;
        adjusted = ipr::gaussianBlurred(adjusted, sigma);
    }

    // Capture/output sharpening follows denoise and all contrast operations.
    // Its adaptive mask suppresses low-contrast noise and the local bound
    // limits the bright/dark halos produced by a naive unsharp mask.
    if (settings.sharpeningAmount > 0 || !local[LocalValues::Sharpening].empty()) {
        DetailProcessing::sharpen(adjusted, {settings.sharpeningAmount,
                                              settings.sharpeningRadius,
                                              settings.sharpeningMasking}, pixelScale, local);
    }

    Grain::apply(adjusted, settings.grain, pixelScale);

    std::vector<cv::Mat> channels;
    channels = ipr::splitChannels(adjusted);
    cv::Mat alpha;
    alpha = ipr::channel(source.linearRgba, 3);
    channels.push_back(Geometry::warpCoverage(alpha, settings.geometry, geometryQuality(pixelScale)));
    cv::Mat rgbaOutput;
    rgbaOutput = ipr::merged(channels);
    if (Geometry::transformsPixels(settings.geometry))
        Geometry::clearTransparent(rgbaOutput);

    return {rgbaOutput, source.colorSpace, source.sourceProfile,
            source.referenceWhiteBalanceKelvin};
}

Preview preview(const ColorPipeline::WorkingImage &source, const Settings &settings, bool includeOriginal,
                double pixelScale, RenderStageCache *cache, int overlayPoint, int overlayMask, bool wholeCanvas,
                int overlayRetouch)
{
    Preview result;
    if (includeOriginal) {
        // The decoded source includes camera/profile transforms and its decode
        // white balance, but none of the editor settings (including sidecars),
        // except the framing, so that it stays comparable with the preview.
        QString error;
        ColorPipeline::WorkingImage framed = source;
        framed.linearRgba = Geometry::warpRgba(source.linearRgba, settings.geometry, geometryQuality(pixelScale));
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
        const cv::Mat tones = tonesOutput(source, settings, pixelScale, cache);
        const cv::Mat weights = PerceptualColour::pointWeights(tones, perceptualParameters(settings), overlayPoint);
        if (!weights.empty() && weights.size() == rendered.linearRgba.size()) {
            // A neutral pipeline hands back the source buffer itself: overlay a copy.
            rendered.linearRgba = rendered.linearRgba.clone();
            ColorAdjustments::showWeights(rendered.linearRgba, weights);
        }
    }
    if (overlayMask >= 0 && overlayMask < int(settings.localMasks.size())) {
        const cv::Mat weights = LocalAdjustments::coverage(settings.localMasks[size_t(overlayMask)], ipr::fromCv(rendered.linearRgba.size()),
                                                           rangeLuminanceOf(source, settings, pixelScale));
        // A neutral pipeline hands back the source buffer itself: overlay a copy.
        rendered.linearRgba = rendered.linearRgba.clone();
        ColorAdjustments::showWeights(rendered.linearRgba, weights);
    }

    if (overlayRetouch >= 0 && overlayRetouch < int(settings.retouches.size())) {
        const cv::Mat weights = Retouch::coverage(settings.retouches[size_t(overlayRetouch)], ipr::fromCv(rendered.linearRgba.size()));
        rendered.linearRgba = rendered.linearRgba.clone();
        ColorAdjustments::showWeights(rendered.linearRgba, weights);
    }

    QString error;
    result.preview = ColorPipeline::toSrgb(
        wholeCanvas ? rendered : cropped(std::move(rendered), settings.geometry),
        ColorPipeline::OutputFormat::Preview8Bit, &error);
    if (result.preview.isNull()) {
        qWarning() << "Échec du rendu de l'aperçu :" << error;
        return result;
    }

    result.histogram = Histogram::compute(result.preview);
    return result;
}

} // namespace RenderChain
