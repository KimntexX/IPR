#ifndef IPR_CORE_TONECURVE_H
#define IPR_CORE_TONECURVE_H

#include <QVector>
#include <opencv2/core.hpp>

#include <array>

// Tone curves through user points (Lagrange interpolation in barycentric
// form), on luminance then on each RGB channel; beyond 1 (HDR headroom) the
// curve continues along its end slope.
namespace ToneCurve {
struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

enum Channel { Luminance, Red, Green, Blue, ChannelCount };
using Curves = std::array<QVector<Point>, ChannelCount>;

bool isIdentity(const Curves &curves);

// Linear Rec.2020 RGB (CV_32FC3), in place.
void apply(cv::Mat &linearRgb, const Curves &curves);
}

#endif // IPR_CORE_TONECURVE_H
