#ifndef IPR_CORE_TONECURVE_H
#define IPR_CORE_TONECURVE_H

#include <QVector>
#include <opencv2/core.hpp>

#include <array>

// Point curves as Lightroom's (docs/validation/raw/etude-courbes-v001-*.json): a
// natural cubic spline through the points, on linear ProPhoto RGB encoded with the
// sRGB transfer, the composite RGB curve then each channel's curve, every curve on
// whole channels. Flat before the first point and after a last point below 1; past
// a last point at 1 (HDR headroom) the curve follows its end slope.
namespace ToneCurve {
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

// Luminance is the composite RGB curve (the UI's first curve).
enum Channel { Luminance, Red, Green, Blue, ChannelCount };
using Curves = std::array<QVector<Point>, ChannelCount>;

bool isIdentity(const Curves &curves);

// A curve's value at x, in its own encoded coordinates (for the UI drawing too).
float evaluate(const QVector<Point> &curve, float x);

// Linear Rec.2020 RGB (CV_32FC3), in place.
void apply(cv::Mat &linearRgb, const Curves &curves);
}

#endif // IPR_CORE_TONECURVE_H
