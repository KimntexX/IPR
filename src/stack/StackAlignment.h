#ifndef IPR_STACK_STACKALIGNMENT_H
#define IPR_STACK_STACKALIGNMENT_H

#include <QString>
#include <opencv2/core.hpp>

#include <functional>
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
    cv::Matx33d referenceToFrame = cv::Matx33d::eye(); // pixel indices, whole resolution
    Model model = Model::Homography;
    int blocks = 0;            // matches kept on the finest level
    int inliers = 0;           // of which agree with the transform
    double residual = 0.0;     // RMS distance of the inliers, pixels
    double support = 0.0;      // smallest share of a side spanned by the inliers
    cv::Rect2d inlierArea;     // what the inliers span in the reference, pixels
    QString error;
};

using Pyramid = std::vector<cv::Mat>; // CV_32FC1; [0] is the whole resolution, each next half of it

// A CV_32FC3 linear camera image (possibly signed) as a pyramid of square-root luminance,
// so that a block weighs the same in shadows and in highlights.
Pyramid buildPyramid(const cv::Mat &cameraRgb, const Options &options = {});

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
    cv::Mat displacements;        // CV_32FC2, whole size: where a reference pixel lies in the
                                  // registered frame, relative to its own place
    int measured = 0;             // blocks kept
    int sites = 0;                // blocks tried
    double largest = 0.0;         // longest displacement of the field, pixels
};

// What one transform leaves over a scene with depth (parallax): a smooth field measured by
// blocks between the reference and the frame already registered on it, both square-root
// luminance of the same size. Where nothing can be measured, the field falls to zero and
// the global transform decides.
LocalField localField(const cv::Mat &reference, const cv::Mat &registered, const LocalOptions &options = {});

} // namespace StackAlignment

#endif // IPR_STACK_STACKALIGNMENT_H
