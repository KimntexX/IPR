#ifndef IPR_STACK_STACKALIGNMENT_H
#define IPR_STACK_STACKALIGNMENT_H

#include "Types.h"

#include <QString>
#include "Image.h"

#include <functional>
#include <limits>
#include <vector>

// Registration of a frame on the reference of a stack, from the pixels alone: a global
// shift on the coarsest level, then block matches refined down the pyramid and a robust
// fit of the transform. Deterministic. A frame without enough support fails with its
// reason: nothing is guessed from an untextured image.
namespace StackAlignment {

enum class Model {
    Homography,   // the matches span the image
    Similarity    // shift, rotation and scale: the matches hold a small part of it
};

struct Options
{
    int coarsestSide = 384;       // the pyramid stops once its longest side is at most this
    int blockHalf = 16;           // a matched block is 2 * half + 1 pixels wide
    int maximumBlocks = 400;      // per level, on a regular grid
    int coarseRadius = 16;        // search radius below the finest levels, pixels of the level
    int fineRadius = 5;           // search radius on the two finest levels
    double minimumScore = 0.5;    // normalized correlation of a kept match
    double inlierPixels = 1.5;    // distance of an inlier, pixels of the level
    int minimumInliers = 12;
    double minimumSupport = 0.35; // share of each side the inliers must span for a homography
};

struct Result
{
    bool valid = false;
    ipr::Matrix33d referenceToFrame = ipr::Matrix33d::identity(); // pixel indices, whole resolution
    Model model = Model::Homography;
    int blocks = 0;            // matches kept on the finest level
    int inliers = 0;           // of which agree with the transform
    double residual = 0.0;     // RMS distance of the inliers, pixels
    double support = 0.0;      // smallest share of a side spanned by the inliers
    ipr::Rectd inlierArea;     // what the inliers span in the reference, pixels
    QString error;
};

using Pyramid = std::vector<ipr::Image>; // F32, one channel; [0] is the whole resolution, each next half of it

// A linear camera image, F32 of three channels (possibly signed) as a pyramid of square-root luminance,
// so that a block weighs the same in shadows and in highlights.
Pyramid buildPyramid(const ipr::Image &cameraRgb, const Options &options = {});

// The linear luminance of a camera image, F32 of one channel: what a pyramid is built from.
// Empty for another type.
ipr::Image luminanceOf(const ipr::Image &cameraRgb);

// What a pyramid keeps of a linear luminance before its square root: scale * value bounded
// to [low, high], low at least 0. Two exposures of a scene compare on the range both see.
struct Bounds
{
    double scale = 1.0;
    double low = 0.0;
    double high = std::numeric_limits<double>::infinity();
};

Pyramid luminancePyramid(const ipr::Image &luminance, const Options &options = {}, const Bounds &bounds = {});

Result estimate(const Pyramid &reference, const Pyramid &frame, const Options &options = {},
                const std::function<bool()> &cancelled = {});

struct LocalOptions
{
    int spacing = 96;             // between two measured blocks, pixels
    int blockHalf = 32;
    int radius = 16;              // largest displacement measured, pixels
    double minimumScore = 0.6;
    double smoothing = 0.8;       // of the field, in grid cells
};

struct LocalField
{
    ipr::Image displacements;        // F32, two channels, whole size: where a reference pixel lies in the
                                  // registered frame, relative to its own place
    int measured = 0;             // blocks kept
    int sites = 0;                // blocks tried
    double largest = 0.0;         // longest displacement of the field, pixels
};

// What one transform leaves over a scene with depth (parallax): a smooth field measured by
// blocks between the reference and the frame already registered on it, both square-root
// luminance of the same size. Where nothing can be measured, the field falls to zero and
// the global transform decides.
LocalField localField(const ipr::Image &reference, const ipr::Image &registered, const LocalOptions &options = {});

// Half the support of the resampling kernel, pixels: what a resampled pixel reads around it.
constexpr int interpolationMargin = 4;

struct Resampled
{
    ipr::Image pixels;          // the frame on the reference's grid, of the reference's size
    int localBlocks = 0;        // blocks that measured the local field
    double localLargest = 0.0;  // longest local displacement, pixels
};

// A frame on the reference's grid: the transform, then, with localRefinement, the local field
// measured between the two levels 0 of their pyramids (square-root luminance, the reference's
// of its size). The pixels are resampled once, by a Lanczos kernel.
Resampled resample(const ipr::Image &frameRgb, const ipr::Image &frameLuminance, const ipr::Image &referenceLuminance,
                   const ipr::Matrix33d &referenceToFrame, bool localRefinement, const LocalOptions &local = {});

// Narrows spans, one [left, right) per reference row, to where a frame resampled by this
// transform and a local field of this reach has its pixels, with the kernel's whole support.
void narrowSpans(const ipr::Matrix33d &referenceToFrame, const ipr::Size &size, double fieldReach, std::vector<ipr::Vec2i> &spans);

} // namespace StackAlignment

#endif // IPR_STACK_STACKALIGNMENT_H
