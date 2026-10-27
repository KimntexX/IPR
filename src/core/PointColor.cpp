#include "PointColor.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace PointColor {
namespace {
constexpr double Pi = 3.14159265358979323846;
// Below this saturation a colour has no reliable hue: it fades to grey identity.
constexpr double NearGreySaturation = 0.02;
// Lightroom centres its luminance sub-range on the value encoded with this gamma.
constexpr double ValueGamma = 1.0 / 2.2;

// Measured on Lightroom exports (docs/validation/raw/etude-couleur-point-tsv-v001-*.json).
// Neighbourhood: a trapezoid per axis, 1 within the plateau, linear to 0 over the
// ramp; median of six sources over 28 scenes, per range slider value.
constexpr double Ranges[] = {0, 25, 50, 75, 100};
constexpr double HuePlateau[] = {0.1613, 0.1812, 0.2086, 0.2139, 0.2254};    // radians
constexpr double HueRamp[] = {0.4909, 0.5292, 0.5645, 0.5558, 0.5465};
constexpr double SaturationPlateau[] = {0.0740, 0.1238, 0.1895, 0.2289, 0.2679};
constexpr double SaturationRamp[] = {0.2329, 0.3105, 0.4052, 0.3818, 0.3548};
constexpr double ValuePlateau[] = {0.0836, 0.1235, 0.1568, 0.1843, 0.1986};  // on value^(1/2.2)
constexpr double ValueRamp[] = {0.2583, 0.2930, 0.4081, 0.4095, 0.4017};
// Change of the source colour itself (weight 1) per slider value, source 6V1A0327;
// the hue turn is the same on all six sources.
constexpr double Shifts[] = {0, 25, 50, 100};
constexpr double HueTurnUp[] = {0, 0.1528, 0.3052, 0.6104};                     // radians
constexpr double HueTurnDown[] = {0, -0.1531, -0.3065, -0.6124};
constexpr double SaturationGainUp[] = {0, 0.2091, 0.3825, 0.6299};              // log saturation
constexpr double SaturationGainDown[] = {0, -0.1995, -0.3580, -0.6420};         // -100 keeps 52.6 %
constexpr double ValueGainUp[] = {0, 0.2224, 0.4208, 0.7870};                   // log value
constexpr double ValueGainDown[] = {0, -0.2097, -0.3922, -0.6955};
// Variance: gain of the deviation from the source, in hue and in saturation.
constexpr double Variances[] = {0, 50, 100};
constexpr double HueSpreadUp[] = {0, 0.4055, 0.8115};
constexpr double HueSpreadDown[] = {0, -0.3994, -0.7975};
constexpr double SaturationSpreadUp[] = {0, 0.3315, 0.6642};
constexpr double SaturationSpreadDown[] = {0, -0.2437, -0.4877};

// Piecewise linear over measured points, constant beyond the last one.
template <size_t N>
double interpolate(const double (&at)[N], const double (&values)[N], double x)
{
    if (x <= at[0]) return values[0];
    for (size_t i = 1; i < N; ++i)
        if (x <= at[i]) return values[i - 1] + (values[i] - values[i - 1]) * (x - at[i - 1]) / (at[i] - at[i - 1]);
    return values[N - 1];
}

// Response of a slider value, from the values measured on its side.
template <size_t N>
double response(const double (&at)[N], const double (&up)[N], const double (&down)[N], double value)
{
    return value >= 0.0 ? interpolate(at, up, value) : interpolate(at, down, -value);
}

double trapezoid(double distance, double plateau, double ramp)
{
    return std::clamp(1.0 - (std::abs(distance) - plateau) / ramp, 0.0, 1.0);
}

// Signed hue difference in (-pi, pi].
double hueDifference(double hue, double reference)
{
    double d = std::fmod(hue - reference, 2.0 * Pi);
    if (d > Pi) d -= 2.0 * Pi;
    if (d <= -Pi) d += 2.0 * Pi;
    return d;
}

double encodedValue(double value) { return std::pow(std::max(value, 0.0), ValueGamma); }

int clampSlider(int value, int low = -100) { return std::clamp(value, low, 100); }

const cv::Matx33d &fromProPhoto()
{
    static const cv::Matx33d matrix = WorkingColor::toProPhoto().inv();
    return matrix;
}
} // namespace

bool active(const Parameters &parameters)
{
    const size_t count = std::min(parameters.count, MaxPoints);
    return std::any_of(parameters.points.cbegin(), parameters.points.cbegin() + count, [](const Point &p) {
        return p.hueShift != 0 || p.saturationShift != 0 || p.luminanceShift != 0 || p.variance != 0;
    });
}

Hsv toHsv(const cv::Vec3d &workingRgb)
{
    const cv::Vec3d p = WorkingColor::toProPhoto() * workingRgb;
    const double high = std::max({p[0], p[1], p[2]}), low = std::min({p[0], p[1], p[2]});
    const double delta = high - low;
    if (!(high > 1e-12) || !(delta > 1e-12)) return {0.0, 0.0, std::max(high, 0.0)};
    double sector;
    if (high == p[0]) sector = std::fmod((p[1] - p[2]) / delta + 6.0, 6.0);
    else if (high == p[1]) sector = (p[2] - p[0]) / delta + 2.0;
    else sector = (p[0] - p[1]) / delta + 4.0;
    return {sector * Pi / 3.0, delta / high, high};
}

cv::Vec3d fromHsv(const Hsv &hsv)
{
    const double value = hsv[2], saturation = std::max(hsv[1], 0.0);
    const double sector = std::fmod(std::fmod(hsv[0], 2.0 * Pi) + 2.0 * Pi, 2.0 * Pi) * 3.0 / Pi;
    const int index = std::min(int(sector), 5);
    const double f = sector - index;
    const double low = value * (1.0 - saturation), falling = value * (1.0 - saturation * f),
                 rising = value * (1.0 - saturation * (1.0 - f));
    cv::Vec3d p;
    switch (index) {
    case 0: p = {value, rising, low}; break;
    case 1: p = {falling, value, low}; break;
    case 2: p = {low, value, rising}; break;
    case 3: p = {low, falling, value}; break;
    case 4: p = {rising, low, value}; break;
    default: p = {value, low, falling}; break;
    }
    return fromProPhoto() * p;
}

double weight(const Hsv &colour, const Point &point)
{
    if (!(colour[1] > 0.0) || !std::isfinite(colour[0]) || !std::isfinite(colour[2])) return 0.0;
    const double range = clampSlider(point.range, 0);
    const auto along = [range](const double (&plateaus)[5], const double (&ramps)[5], double distance) {
        return trapezoid(distance, interpolate(Ranges, plateaus, range), interpolate(Ranges, ramps, range));
    };
    // A grey source has no hue: only its saturation and value select.
    const double hue = point.source[1] >= NearGreySaturation
        ? along(HuePlateau, HueRamp, hueDifference(colour[0], point.source[0])) : 1.0;
    const double saturation = along(SaturationPlateau, SaturationRamp, colour[1] - point.source[1]);
    const double value = along(ValuePlateau, ValueRamp, encodedValue(colour[2]) - encodedValue(point.source[2]));
    return hue * saturation * value * std::min(1.0, colour[1] / NearGreySaturation);
}

void adjust(cv::Vec3d &workingRgb, const Parameters &parameters)
{
    const size_t count = std::min(parameters.count, MaxPoints);
    if (count == 0) return;
    Hsv colour = toHsv(workingRgb);
    if (!(colour[1] > 0.0)) return;
    // Overlapping points share their reach: weights of the original colour summing
    // above 1 scale every point down.
    double total = 0.0;
    for (size_t i = 0; i < count; ++i) total += weight(colour, parameters.points[i]);
    if (total <= 0.0) return;
    const double share = 1.0 / std::max(1.0, total);
    // Each point acts on the colour the previous ones produced: the order matters.
    for (size_t i = 0; i < count; ++i) {
        const Point &point = parameters.points[i];
        const double w = weight(colour, point) * share;
        if (w <= 0.0) continue;
        const double variance = clampSlider(point.variance);
        // Variance scales the deviation from the source, in hue and in saturation.
        double hue = colour[0] + w * response(Shifts, HueTurnUp, HueTurnDown, clampSlider(point.hueShift));
        if (point.source[1] >= NearGreySaturation)
            hue += w * response(Variances, HueSpreadUp, HueSpreadDown, variance)
                   * hueDifference(colour[0], point.source[0]);
        double saturation = colour[1] + w * response(Variances, SaturationSpreadUp, SaturationSpreadDown, variance)
                                            * (colour[1] - point.source[1]);
        saturation = std::max(0.0, saturation)
                     * std::exp(w * response(Shifts, SaturationGainUp, SaturationGainDown, clampSlider(point.saturationShift)));
        const double value = colour[2]
                             * std::exp(w * response(Shifts, ValueGainUp, ValueGainDown, clampSlider(point.luminanceShift)));
        colour = {std::fmod(std::fmod(hue, 2.0 * Pi) + 2.0 * Pi, 2.0 * Pi), saturation, value};
    }
    workingRgb = fromHsv(colour);
}

cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters)
{
    if (!active(parameters) || (rgb[0] == rgb[1] && rgb[1] == rgb[2]))
        return rgb; // zero settings / exact grey identity, without matrix round trip
    cv::Vec3d working(rgb);
    adjust(working, parameters);
    return cv::Vec3f(working);
}
} // namespace PointColor
