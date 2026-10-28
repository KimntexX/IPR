#ifndef IPR_CORE_POINTCOLOR_H
#define IPR_CORE_POINTCOLOR_H

#include <array>
#include <cstddef>
#include <opencv2/core.hpp>

// Point colour, as Lightroom's: colours picked in the photo, each shifting the
// hue, saturation and value of the colours near it, in the HSV of linear ProPhoto
// RGB, where Lightroom's exports show it acts (saturation at constant value). The
// range widens the neighbourhood; the variance spreads or gathers the colours
// inside it (hue, saturation and value). Points act in the order they were
// picked, and overlapping points share their reach. Neighbourhood and responses
// are fitted on Lightroom exports (PointColor.cpp,
// docs/validation/protocols/protocole-couleur-point.md).
namespace PointColor {
inline constexpr size_t MaxPoints = 8;

// Hue (radians, hexcone, [0, 2 pi)), saturation (max - min) / max and value max of
// a linear ProPhoto RGB colour of the rendered image. Lightroom writes a source in
// the same form but in its own space, ahead of its rendering: its numbers are near,
// not equal (the studies fit the offset).
using Hsv = cv::Vec3d;

struct Point {
    Hsv source{};                // picked colour
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

// HSV of a linear working-space (Rec.2020) colour, and back; the source of a
// pixel picked in the photo.
Hsv toHsv(const cv::Vec3d &workingRgb);
cv::Vec3d fromHsv(const Hsv &hsv);

// Weight in [0, 1] of a colour in a point's neighbourhood: 1 near its source, 0
// beyond its range, 0 for greys. Also for the range overlay.
double weight(const Hsv &colour, const Point &point);

// One linear working-space colour, in place; values clamped to their ranges.
// Greys are left unchanged; no clipping. The image loop is PerceptualColour's.
void adjust(cv::Vec3d &workingRgb, const Parameters &parameters);

// Input/output: linear Rec.2020 D65 RGB, as adjust().
cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters);
} // namespace PointColor

#endif // IPR_CORE_POINTCOLOR_H
