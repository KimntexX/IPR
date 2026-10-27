#include "PointColor.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace PointColor {
namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;
// Below this Oklab chroma a colour has no reliable hue: it fades to grey identity.
constexpr double NearGreyChroma = 0.02;

// Measured on Lightroom exports (docs/validation/raw/etude-couleur-point-v002-*.json).
// Neighbourhood: distance where the weight falls to 0.5, per range slider value,
// median of six sources over 28 scenes.
constexpr double Ranges[] = {0, 25, 50, 75, 100};
constexpr double HueHalfWidth[] = {27.2, 32.6, 38.6, 40.2, 41.6};          // degrees
constexpr double ChromaHalfWidth[] = {0.450, 0.592, 0.926, 0.986, 1.148};  // natural log of the chroma ratio
constexpr double LightnessHalfWidth[] = {0.158, 0.211, 0.258, 0.279, 0.302}; // Oklab L
// Change of the source colour itself (weight 1) per slider value, source 6V1A0327.
constexpr double Shifts[] = {0, 25, 50, 100};
constexpr double HueRotationUp[] = {0, 14.95, 28.68, 52.68};               // degrees
constexpr double HueRotationDown[] = {0, -15.07, -29.04, -51.45};
constexpr double ChromaGainUp[] = {0, 0.248, 0.459, 0.758};                // log chroma; -100 keeps ~48 %
constexpr double ChromaGainDown[] = {0, -0.229, -0.407, -0.729};
constexpr double LightnessGainUp[] = {0, 0.0762, 0.1444, 0.270};           // log lightness
constexpr double LightnessGainDown[] = {0, -0.0722, -0.1353, -0.2405};
// Variance: gain of the deviation from the source, in hue and in log chroma.
constexpr double Variances[] = {0, 50, 100};
constexpr double HueSpreadUp[] = {0, 0.478, 0.915};
constexpr double HueSpreadDown[] = {0, -0.508, -1.0};                       // -100 gathers the hues on the source
constexpr double ChromaSpreadUp[] = {0, 0.341, 0.742};
constexpr double ChromaSpreadDown[] = {0, -0.221, -0.429};

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

// Smoothstep falloff: 1 at 0, 0.5 at 0.5, 0 from 1 on; a flat top and no long tail, as measured.
double falloff(double t)
{
    if (t >= 1.0) return 0.0;
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

double hueDegrees(const cv::Vec3d &lab) { return std::atan2(lab[2], lab[1]) * DegreesPerRadian; }

// Signed hue difference in (-180, 180].
double hueDifference(double hue, double reference)
{
    double d = std::fmod(hue - reference, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

int clampSlider(int value, int low = -100) { return std::clamp(value, low, 100); }
} // namespace

bool active(const Parameters &parameters)
{
    const size_t count = std::min(parameters.count, MaxPoints);
    return std::any_of(parameters.points.cbegin(), parameters.points.cbegin() + count, [](const Point &p) {
        return p.hueShift != 0 || p.saturationShift != 0 || p.luminanceShift != 0 || p.variance != 0;
    });
}

double weight(const cv::Vec3d &lab, const Point &point)
{
    const double chroma = std::hypot(lab[1], lab[2]);
    if (!(chroma >= 1e-12) || !std::isfinite(lab[0])) return 0.0;
    const double range = clampSlider(point.range, 0);
    // Half-widths at weight 0.5; the falloff reaches 0 at twice that distance.
    const auto along = [range](const double (&halfWidths)[5], double distance) {
        return falloff(std::abs(distance) / (2.0 * interpolate(Ranges, halfWidths, range)));
    };
    const double sourceChroma = std::hypot(point.source[1], point.source[2]);
    // A grey source has no hue: only its chroma and lightness select.
    const double hue = sourceChroma >= NearGreyChroma
        ? along(HueHalfWidth, hueDifference(hueDegrees(lab), hueDegrees(point.source))) : 1.0;
    const double saturation = along(ChromaHalfWidth, std::log(chroma / std::max(sourceChroma, 1e-6)));
    const double lightness = along(LightnessHalfWidth, lab[0] - point.source[0]);
    return hue * saturation * lightness * std::min(1.0, chroma / NearGreyChroma);
}

void adjust(cv::Vec3d &lab, const Parameters &parameters)
{
    const size_t count = std::min(parameters.count, MaxPoints);
    if (count == 0 || std::hypot(lab[1], lab[2]) < 1e-12) return;
    // Overlapping points share their reach: weights of the original colour summing
    // above 1 scale every point down.
    double total = 0.0;
    for (size_t i = 0; i < count; ++i) total += weight(lab, parameters.points[i]);
    const double share = 1.0 / std::max(1.0, total);
    // Each point acts on the colour the previous ones produced: the order matters.
    for (size_t i = 0; i < count; ++i) {
        const Point &point = parameters.points[i];
        const double w = weight(lab, point) * share;
        if (w <= 0.0) continue;
        const double variance = clampSlider(point.variance);
        const double chroma = std::hypot(lab[1], lab[2]);
        const double sourceChroma = std::max(std::hypot(point.source[1], point.source[2]), 1e-6);
        double rotation = w * response(Shifts, HueRotationUp, HueRotationDown, clampSlider(point.hueShift));
        // Variance scales the deviation from the source, in hue and in log chroma.
        if (sourceChroma >= NearGreyChroma)
            rotation += w * response(Variances, HueSpreadUp, HueSpreadDown, variance)
                        * hueDifference(hueDegrees(lab), hueDegrees(point.source));
        const double logGain = w * response(Variances, ChromaSpreadUp, ChromaSpreadDown, variance)
                                   * std::log(chroma / sourceChroma)
                               + w * response(Shifts, ChromaGainUp, ChromaGainDown, clampSlider(point.saturationShift));
        lab[0] *= std::exp(w * response(Shifts, LightnessGainUp, LightnessGainDown, clampSlider(point.luminanceShift)));
        const double gain = std::exp(logGain);
        const double angle = rotation / DegreesPerRadian;
        const double a = lab[1] * gain, b = lab[2] * gain;
        lab[1] = a * std::cos(angle) - b * std::sin(angle);
        lab[2] = a * std::sin(angle) + b * std::cos(angle);
    }
}

cv::Vec3f apply(const cv::Vec3f &rgb, const Parameters &parameters)
{
    if (!active(parameters) || (rgb[0] == rgb[1] && rgb[1] == rgb[2]))
        return rgb; // zero settings / exact grey identity, without matrix round trip
    cv::Vec3d lab = WorkingColor::workingToOklab(cv::Vec3d(rgb));
    adjust(lab, parameters);
    return cv::Vec3f(WorkingColor::oklabToWorking(lab));
}
} // namespace PointColor
