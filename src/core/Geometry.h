#ifndef IPR_CORE_GEOMETRY_H
#define IPR_CORE_GEOMETRY_H

#include "Types.h"

#include "Image.h"

#include <optional>

// The manual geometry of an image (ADR-0021, B1): quarter turns and mirrors, then
// perspective, aspect, fine rotation, scale and offsets, all in one matrix applied
// in a single resampling; the crop is a final cut on the transformed canvas.
//
// Spaces. Source: the oriented, lens-corrected master, u and v in [0, 1] across its
// pixels' outer edges (pixel x covers [x, x + 1] / width), the same at every
// resolution. Canvas: the transformed frame, same convention; a quarter turn swaps
// its sides. Crop: a rectangle of the canvas. A mask drawn on the screen goes through
// viewToCanvas when the crop is shown, then toSource if it needs the source.
namespace Geometry {

struct Settings
{
    int quarterTurns = 0;         // clockwise, in [0, 3]
    bool flipHorizontal = false;  // mirrored after the turns
    bool flipVertical = false;
    float rotation = 0;           // degrees, clockwise, in [-45, 45]
    int vertical = 0;             // [-100, 100]: positive widens the top
    int horizontal = 0;           // [-100, 100]: positive widens the right
    int aspect = 0;               // [-100, 100]: positive widens, area kept
    int scale = 100;              // percent, in [50, 150]
    int offsetX = 0;              // [-100, 100]: percent of half the canvas width, right
    int offsetY = 0;              // [-100, 100]: percent of half the canvas height, down
    double cropX = 0;             // on the canvas, normalized
    double cropY = 0;
    double cropWidth = 1;
    double cropHeight = 1;
    double cropRatio = 0;         // width / height on the canvas's pixels; 0 is free
    bool constrain = false;       // keep the crop inside the image, no empty zone
};

enum class Quality { Preview, Export };   // Bicubic, then Lanczos

bool operator==(const Settings &a, const Settings &b);
inline bool operator!=(const Settings &a, const Settings &b) { return !(a == b); }

bool isIdentity(const Settings &settings);
// Whether the pixels move at all; false leaves the buffers shared and untouched.
bool transformsPixels(const Settings &settings);
bool hasCrop(const Settings &settings);

// Clamps every field to its range and orders the crop; false on a non-finite value.
bool bound(Settings &settings);

ipr::Size canvasSize(const Settings &settings, const ipr::Size &source);
// The rectangle of a canvas of this size; the same at every resolution, at least 1 px.
ipr::Recti cropRect(const Settings &settings, const ipr::Size &canvas);

// From normalized source to normalized canvas, for a source of this size.
ipr::Matrix33d homography(const Settings &settings, const ipr::Size &source);
// Empty when the point lies behind the projection.
std::optional<ipr::Point2d> toCanvas(const Settings &settings, const ipr::Size &source, const ipr::Point2d &point);
std::optional<ipr::Point2d> toSource(const Settings &settings, const ipr::Size &source, const ipr::Point2d &point);
// From the normalized displayed crop to the normalized canvas.
ipr::Point2d viewToCanvas(const Settings &settings, const ipr::Point2d &point);

// F32, three channels. Beyond the source, the border repeats, so neighbourhood filters see
// no false edge; the coverage (warpCoverage) makes these pixels transparent. Turns and
// mirrors alone are exact copies. Without transformsPixels, returns the input itself: the
// image is taken by value, to move in, or to share for a caller that keeps it.
ipr::Image warpRgb(ipr::Image rgb, const Settings &settings, Quality quality);
// F32, one channel, in [0, 1]; zero beyond the source.
ipr::Image warpCoverage(ipr::Image alpha, const Settings &settings, Quality quality);
// F32, four channels: straight alpha; a pixel of no coverage is transparent black.
ipr::Image warpRgba(ipr::Image rgba, const Settings &settings, Quality quality);
// Blackens the fully transparent pixels of an RGBA image of floats in place.
void clearTransparent(ipr::Image &rgba);
// The crop of a canvas-sized image, of any type, as new pixels; the image itself without a crop.
ipr::Image crop(ipr::Image canvasImage, const Settings &settings);

// Commands, as the panel gives them; the crop follows the image.
void turn(Settings &settings, const ipr::Size &source);         // a quarter turn, clockwise
void mirror(Settings &settings, bool horizontal, const ipr::Size &source);
// Whether every corner of this crop lies on the image.
bool cropInsideImage(const Settings &settings, const ipr::Size &source);
// With constrain, shrinks the crop around its centre until it lies on the image,
// keeping its proportions. Without constrain, leaves it.
void applyConstraint(Settings &settings, const ipr::Size &source);
// Sets a crop within the canvas; a ratio shapes it around its centre.
void setCrop(Settings &settings, const ipr::Size &source, double x, double y, double width, double height);
void setCropRatio(Settings &settings, const ipr::Size &source, double ratio);

} // namespace Geometry

#endif // IPR_CORE_GEOMETRY_H
