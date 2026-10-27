#include "PointColor.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace PointColor {
namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;
// Below this Oklab chroma a colour has no reliable hue: it fades to grey identity.
constexpr double NearGreyChroma = 0.02;

// Provisional neighbourhood and response, to be replaced by the values fitted
// on the Lightroom campaign. Half-widths at range 0 and 100, linear between.
struct Calibration {
    double hueHalfWidth[2] = {12.0, 60.0};        // degrees
    double chromaHalfWidth[2] = {0.35, 1.2};      // natural log of the chroma ratio
    double lightnessHalfWidth[2] = {0.08, 0.40};  // Oklab L
    double maxHueDegrees = 30.0;                  // hue shift ±100
    double maxLogChroma = 0.7;                    // saturation +100; -100 reaches grey
    double maxLogLightness = 0.35;                // luminance ±100
    double maxVariance = 0.6;                     // variance ±100 scales deviations by 1 ± 0.6
};
constexpr Calibration Model;

double lerp(const double (&ends)[2], double t) { return ends[0] + (ends[1] - ends[0]) * t; }

// Smooth falloff: 1 at 0, 0 from 1 on, zero slope at both ends.
double falloff(double t)
{
    const double u = 1.0 - t * t;
    return u > 0.0 ? u * u : 0.0;
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
    const double r = clampSlider(point.range, 0) / 100.0;
    const double sourceChroma = std::hypot(point.source[1], point.source[2]);
    // A grey source has no hue: only its chroma and lightness select.
    const double hue = sourceChroma >= NearGreyChroma
        ? falloff(std::abs(hueDifference(hueDegrees(lab), hueDegrees(point.source))) / lerp(Model.hueHalfWidth, r))
        : 1.0;
    const double logRatio = std::log(chroma / std::max(sourceChroma, 1e-6));
    const double saturation = falloff(std::abs(logRatio) / lerp(Model.chromaHalfWidth, r));
    const double lightness = falloff(std::abs(lab[0] - point.source[0]) / lerp(Model.lightnessHalfWidth, r));
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
        const double hueShift = clampSlider(point.hueShift) / 100.0;
        const double saturationShift = clampSlider(point.saturationShift) / 100.0;
        const double luminanceShift = clampSlider(point.luminanceShift) / 100.0;
        const double variance = clampSlider(point.variance) / 100.0 * Model.maxVariance * w;
        const double chroma = std::hypot(lab[1], lab[2]);
        const double sourceChroma = std::max(std::hypot(point.source[1], point.source[2]), 1e-6);
        // Variance scales the deviation from the source, in hue and in log chroma.
        double rotation = w * hueShift * Model.maxHueDegrees;
        if (sourceChroma >= NearGreyChroma)
            rotation += variance * hueDifference(hueDegrees(lab), hueDegrees(point.source));
        double gain = std::exp(variance * std::log(chroma / sourceChroma));
        gain *= saturationShift < 0.0 ? std::max(0.0, 1.0 + w * saturationShift)
                                      : std::exp(w * saturationShift * Model.maxLogChroma);
        lab[0] *= std::exp(w * luminanceShift * Model.maxLogLightness);
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
