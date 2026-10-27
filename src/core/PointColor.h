#ifndef IPR_CORE_POINTCOLOR_H
#define IPR_CORE_POINTCOLOR_H

#include <array>
#include <cstddef>
#include <opencv2/core.hpp>

// Point colour, as Lightroom's: colours picked in the photo, each shifting the
// hue, saturation and luminance of the colours near it in Oklab. The range
// widens or narrows that neighbourhood; the variance spreads or gathers the
// colours inside it. Points act in the order they were picked, and overlapping
// points share their reach. The neighbourhood and the response are provisional
// (Calibration in PointColor.cpp) until fitted on Lightroom exports
// (docs/validation/protocols/protocole-couleur-point.md).
namespace PointColor {
inline constexpr size_t MaxPoints = 8;

struct Point {
    cv::Vec3d source{};          // picked colour, Oklab
    int hueShift = 0;            // [-100, 100]
    int saturationShift = 0;     // [-100, 100]
    int luminanceShift = 0;      // [-100, 100]
    int variance = 0;            // [-100, 100]
    int range = 50;              // [0, 100]
};

struct Parameters {
    std::array<Point, MaxPoints> points{};
    size_t count = 0;            // points in use, in picking order
};

// True when a point in use has a shift or a variance.
bool active(const Parameters &parameters);

// Weight in [0, 1] of an Oklab colour in a point's neighbourhood: 1 at its
// source, 0 beyond its range, 0 for greys. Also for the range overlay.
double weight(const cv::Vec3d &lab, const Point &point);

// One Oklab colour, in place; values clamped to their ranges. Greys are left
// unchanged; no clipping. The image loop is PerceptualColour's.
void adjust(cv::Vec3d &lab, const Parameters &parameters);

// Input/output: linear Rec.2020 D65 RGB: adjust() with its conversions.
cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters);
} // namespace PointColor

#endif // IPR_CORE_POINTCOLOR_H
