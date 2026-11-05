#ifndef IPR_PANORAMA_PANORAMAGEOMETRY_H
#define IPR_PANORAMA_PANORAMAGEOMETRY_H

#include "PanoramaFeatures.h"
#include "Types.h"

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

// Where each frame of a panorama looks. A camera turning about its centre: one rotation per
// frame, one focal length and one radial distortion (what no lens profile has corrected),
// adjusted together on all the overlaps at once, so that errors do not add up along the series
// and any arrangement works, one row or several, in any order.
// Coordinates are whole-resolution pixels from the centre of the frame.
namespace PanoramaGeometry {

enum class Projection { Automatic, Rectilinear, Cylindrical, Spherical };
enum class Layout { Horizontal, Vertical };

struct PairOptions
{
    int iterations = 2000;
    double inlierPixels = 6.0;      // whole-resolution pixels
    int minimumInliers = 16;
    double minimumArea = 0.01;      // share of a frame the inliers must span
};

// A verified overlap between two frames.
struct Pair
{
    int a = 0;
    int b = 0;
    std::vector<ipr::Point2d> pointsA;
    std::vector<ipr::Point2d> pointsB;
    ipr::Matrix33d aToB = ipr::Matrix33d::identity(); // homography, centred pixels
    double residual = 0.0;                 // RMS after the adjustment, pixels
};

// False when the correspondences do not hold one plane-to-plane mapping with enough support:
// an ordinary pair of frames that do not overlap. Deterministic.
bool verifyPair(const std::vector<PanoramaFeatures::Correspondence> &correspondences, const ipr::Size &frame,
                const PairOptions &options, Pair &pair);

// What the files already say of the lens. What is known is not left to the adjustment: on a
// series with parallax, a free focal length or distortion takes up what they do not cause,
// and bends the whole panorama in exchange for a smaller residual.
struct LensKnowledge
{
    double focalPixels = 0.0;          // 0 when unknown; else the adjustment stays close to it
    bool distortionCorrected = false;  // by a lens profile: none is left to find
};

struct SolveOptions
{
    double dropResidual = 12.0;     // RMS of a pair that cannot belong, pixels
    int maximumPoints = 150;        // of a pair in the adjustment
};

struct Solution
{
    bool valid = false;
    double focal = 0.0;                     // pixels
    double distortion = 0.0;                // radial, at the frame's corner: a pixel moves out by this share
    std::vector<ipr::Matrix33d> worldToCamera; // per frame; identity outside the panorama
    std::vector<int> members;               // the frames of the panorama, each overlapping an earlier one
    std::vector<Pair> pairs;                // those that remain
    double residual = 0.0;                  // RMS over all pairs, pixels
    double worstPair = 0.0;
};

// The largest set of connected frames.
Solution solve(int frameCount, std::vector<Pair> pairs, const ipr::Size &frame, const LensKnowledge &lens, const SolveOptions &options = {},
               const std::function<bool()> &cancelled = {});

// Measures the correspondences again on finer images, through what the solution predicts,
// and solves once more. images: per frame, CV_32FC1, of size frame * imageScale; empty for
// those outside the panorama. Fewer points are kept where the two views differ.
void refine(Solution &solution, const std::vector<cv::Mat> &images, double imageScale, const ipr::Size &frame, const LensKnowledge &lens,
            const SolveOptions &options = {}, const std::function<bool()> &cancelled = {});

struct Shape
{
    Layout layout = Layout::Horizontal;
    Projection projection = Projection::Cylindrical;
    int rows = 1;             // across the panorama's direction
    double spanAlong = 0.0;   // field covered along it, radians
    double spanAcross = 0.0;  // field covered across it, radians
};

// Turns the world so that the bottom of the view runs level, the line a series leans on, and
// the mean view looks straight ahead; tells the direction and the rows of the panorama.
// requested: Automatic chooses from the field.
Shape straighten(Solution &solution, const ipr::Size &frame, Projection requested);

// The surface a panorama is unrolled on.
struct Surface
{
    Projection projection = Projection::Cylindrical;
    Layout layout = Layout::Horizontal;
    double scale = 1.0;         // canvas pixels per unit of the projection
    ipr::Point2d origin;         // of the canvas, in canvas pixels from the view's axis

    // The direction a canvas pixel looks along.
    ipr::Vec3d ray(double x, double y) const;
    // false: behind a flat projection.
    bool place(const ipr::Vec3d &ray, ipr::Point2d &canvas) const;
};

// A canvas pixel's source in a frame: centre-based, then shifted to pixel indices.
struct FrameView
{
    ipr::Matrix33d worldToCamera = ipr::Matrix33d::identity();
    double focal = 1.0;
    double distortion = 0.0;
    ipr::Size frame;
};

// What a frame covers on a surface with no origin yet, from its outline. false: the frame
// reaches behind a flat projection.
bool outline(const Surface &surface, const FrameView &view, ipr::Rectd &area);

// The source coordinates of a canvas area (CV_32FC1 maps), -1 where the frame's pixels,
// margin included, do not reach; the valid mask is CV_8UC1.
void sourceMaps(const Surface &surface, const FrameView &view, const ipr::Recti &area, double margin,
                cv::Mat &mapX, cv::Mat &mapY, cv::Mat &valid);

} // namespace PanoramaGeometry

#endif // IPR_PANORAMA_PANORAMAGEOMETRY_H
