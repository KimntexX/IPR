#ifndef IPR_EDITOR_RENDERCHAIN_H
#define IPR_EDITOR_RENDERCHAIN_H

#include "ColorPipeline.h"
#include "Dehaze.h"
#include "DetailProcessing.h"
#include "EditRecipe.h"
#include "LocalContrast.h"
#include "PerceptualColour.h"
#include "RenderStageCache.h"
#include "ToneRecovery.h"

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVariantList>

#include <memory>
#include <optional>
#include <vector>
#include "Image.h"

// From a source image and a recipe to pixels: the order of the processing modules, and
// nothing else. No state: what is kept between two renders lives in the cache a caller gives.
namespace RenderChain {

using Settings = EditRecipe::Settings;

// The preview's proxy takes the fast filter; a full-resolution render, the fine one.
Geometry::Quality geometryQuality(double pixelScale);
// Area proxy retaining camera signal and tone statistics; export never uses it.
ColorPipeline::WorkingImage editProxy(const ColorPipeline::WorkingImage &source, int maximumExtent = 2048);

// The crop of the geometry, when it has one; a null image stays null.
ColorPipeline::WorkingImage cropped(ColorPipeline::WorkingImage image, const Geometry::Settings &geometry);

PerceptualColour::Parameters perceptualParameters(const Settings &settings);

// pixelScale: pixels of source per pixel of the full image (below 1 for the preview proxy),
// so effects keep their full-image radii. With a cache (the preview), the render resumes
// from the stages it kept; same pixels as without. Null on an invalid source or a failed
// stage. A neutral recipe hands back the source buffer itself, not a copy.
// retouchesNotDone, when given and the render reaches them: the retouches without effect.
ColorPipeline::WorkingImage workingImage(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                         double pixelScale = 1.0, RenderStageCache *cache = nullptr,
                                         int *retouchesNotDone = nullptr);

// A region of the full-resolution render (pixelScale 1, no cache), rendered alone.
//
// What the stages measure on the whole image, a region takes as measured there.
struct WholeMeasures
{
    bool valid = false;
    ipr::Size sourceSize;                     // of the source measured, checked whole there
    std::vector<float> warpFloor;            // before the geometry, when it resamples
    std::optional<float> sharpenNoiseSigma;  // the sharpening's input, when it sharpens
    // Texture and clarity's coarse bands, when active; shared, as every copy of the measures reads them.
    std::shared_ptr<const LocalContrast::Layers> contrastLayers;
    Dehaze::Layers dehazeLayers;             // the dehaze's reduced maps, when active
    DetailProcessing::NoiseMeasures noise;   // the classical noise reduction's, when active
    ToneRecovery::Statistics toneStatistics;  // the source's
};
// Whether this recipe renders a region by itself; otherwise a caller renders the whole image and
// takes the region from it. Grows as the stages that read neighbours learn to render a region.
bool rendersByRegion(const Settings &settings);
// The measures a region of this recipe needs, on the whole source at full resolution. False on an
// invalid source, a failed stage or a recipe that does not render by region.
bool measureWhole(const ColorPipeline::WorkingImage &source, const Settings &settings, WholeMeasures &measures);
// The settings those measures depend on: two recipes of one key share the measures of a source.
QByteArray wholeMeasuresKey(const Settings &settings);
// The region, in pixels of the full-resolution canvas (before the crop), of workingImage(source,
// settings): the same pixels, bit for bit. Null when the recipe does not render by region, when
// the region leaves the canvas, on measures of another source's size or recipe's shape, or on a
// failed stage. The measures must be this source's and recipe's: the region trusts them.
// The original of a region of the canvas at full resolution: the decoded source with no setting
// but the framing, as preview()'s originalPreview. floor: Geometry::warpFloor of the whole
// source's RGB, needed only when the geometry resamples. Empty on a region off the canvas.
ColorPipeline::WorkingImage originalRegion(const ColorPipeline::WorkingImage &source, const Geometry::Settings &geometry,
                                           const ipr::Recti &region, const std::vector<float> &floor);
ColorPipeline::WorkingImage workingRegion(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                          const ipr::Recti &region, const WholeMeasures &measures);

// The colour entering the point colour: the tones' output (F32 of three channels, whole canvas), from the
// cache when it kept it. Empty on failure.
ipr::Image tonesOutput(const ColorPipeline::WorkingImage &source, const Settings &settings, double pixelScale,
                    RenderStageCache *cache);

struct Preview
{
    QImage preview;
    QImage originalPreview;
    Geometry::Settings originalGeometry;   // the framing originalPreview shows
    bool originalWholeCanvas = false;
    QVariantList histogram;
    QString error;
    int retouchesNotDone = -1;             // -1: the render did not reach them, a cache had them
};

// Keep the displayed pixel grid stable during a lower-resolution geometry gesture.
Preview displaySized(Preview result, const ipr::Size &size);

// The render for the screen, in 8-bit sRGB. overlayPoint, overlayMask, overlayRetouch: an
// element whose coverage the preview shows (colour where it acts, grey elsewhere), -1 for none.
// wholeCanvas: without the crop, for the crop tool. originalSource: what the original
// shows when it is not the source itself (the development without the neural denoiser).
Preview preview(const ColorPipeline::WorkingImage &source, const Settings &settings, bool includeOriginal,
                double pixelScale = 1.0, RenderStageCache *cache = nullptr, int overlayPoint = -1, int overlayMask = -1,
                bool wholeCanvas = false, int overlayRetouch = -1,
                const ColorPipeline::WorkingImage *originalSource = nullptr);

} // namespace RenderChain

#endif // IPR_EDITOR_RENDERCHAIN_H
