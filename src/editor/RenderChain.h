#ifndef IPR_EDITOR_RENDERCHAIN_H
#define IPR_EDITOR_RENDERCHAIN_H

#include "ColorPipeline.h"
#include "EditRecipe.h"
#include "PerceptualColour.h"
#include "RenderStageCache.h"

#include <QImage>
#include <QString>
#include <QVariantList>
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
