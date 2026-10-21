#ifndef IPR_CORE_VIBRANCE_H
#define IPR_CORE_VIBRANCE_H

#include <algorithm>
#include <cmath>
#include <opencv2/core.hpp>

namespace Vibrance {

// A1/A2 prototype, linear sRGB D65. Tuning, not physical constants or a
// measured skin detector. No clipping here: the shared output handles gamut.
inline constexpr double negativeEndpointReduction = .035;
inline constexpr double positiveEndpointBoost = .065;
inline constexpr double negativeChromaProtection = .1;

inline double smooth(double start, double end, double value)
{
    const double t = std::clamp((value-start)/(end-start), 0., 1.);
    return t*t*(3.-2.*t);
}

inline double effectiveAmount(double amount)
{
    amount = std::clamp(amount, -1., 1.);
    const double adjustment = amount < 0 ? -negativeEndpointReduction : positiveEndpointBoost;
    return amount*(1.+adjustment*smooth(.75, 1., std::abs(amount)));
}

// Ottosson Oklab matrices (2021); signed cube roots retain HDR/signed values.
inline cv::Vec3d toOklab(const cv::Vec3d &p)
{
    const double l = std::cbrt(.4122214708*p[0]+.5363325363*p[1]+.0514459929*p[2]);
    const double m = std::cbrt(.2119034982*p[0]+.6806995451*p[1]+.1073969566*p[2]);
    const double s = std::cbrt(.0883024619*p[0]+.2817188376*p[1]+.6299787005*p[2]);
    return {.2104542553*l+.7936177850*m-.0040720468*s,
            1.9779984951*l-2.4285922050*m+.4505937099*s,
            .0259040371*l+.7827717662*m-.8086757660*s};
}

inline cv::Vec3d fromOklab(const cv::Vec3d &p)
{
    const double l = p[0]+.3963377774*p[1]+.2158037573*p[2];
    const double m = p[0]-.1055613458*p[1]-.0638541728*p[2];
    const double s = p[0]-.0894841775*p[1]-1.2914855480*p[2];
    return {4.0767416621*l*l*l-3.3077115913*m*m*m+.2309699292*s*s*s,
            -1.2684380046*l*l*l+2.6097574011*m*m*m-.3413193965*s*s*s,
            -.0041960863*l*l*l-.7034186147*m*m*m+1.7076147010*s*s*s};
}

inline cv::Vec3f apply(const cv::Vec3f &rgb, double vibrance, double saturation)
{
    vibrance = std::clamp(vibrance, -1., 1.);
    saturation = std::clamp(saturation, -1., 1.);
    if ((vibrance == 0 && saturation == 0) || (rgb[0] == rgb[1] && rgb[1] == rgb[2]))
        return rgb; // exact zero / gray identity, without matrix round-trip
    cv::Vec3d lab = toOklab(cv::Vec3d(rgb));
    const double chroma = std::hypot(lab[1], lab[2]);
    if (chroma < 1e-12)
        return rgb;
    const double relative = std::clamp(chroma/std::max(std::abs(lab[0]), .05), 0., 1.);
    constexpr double radiansToDegrees = 180./3.14159265358979323846;
    const double hue = std::atan2(lab[2], lab[1])*radiansToDegrees;
    const double distance = std::remainder(hue-50., 360.);
    const double warmProtection = 1.-.4*std::exp(-std::pow(distance/25., 2))
        *smooth(.10, .35, lab[0])*(1.-smooth(.85, 1.10, lab[0]))*smooth(.005, .025, chroma);
    const double weight = vibrance >= 0 ? (1.-relative)*(1.-relative)*warmProtection
                                       : 1.-negativeChromaProtection*relative;
    const double gain = std::max(0., (1.+saturation)*(1.+effectiveAmount(vibrance)*weight));
    lab[1] *= gain;
    lab[2] *= gain;
    return cv::Vec3f(fromOklab(lab)); // preserves L/h before output, not Y
}

} // namespace Vibrance

#endif // IPR_CORE_VIBRANCE_H
