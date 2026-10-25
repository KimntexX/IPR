#ifndef IPR_CORE_WORKINGCOLORSPACE_H
#define IPR_CORE_WORKINGCOLORSPACE_H

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>

// Linear RGB, D65. Matrices derived from the published CIE xy primaries;
// no transfer function, gamut mapping or range clipping in these operations.
namespace WorkingColor {
inline const cv::Matx33d &toXyz()
{
    static const cv::Matx33d matrix(
        .6369580483012914, .1446169035862083, .1688809751641721,
        .2627002120112671, .6779980715188708, .0593017164698620,
        .0000000000000000, .0280726930490874, 1.060985057710791);
    return matrix;
}
inline const cv::Matx33d &srgbToXyz()
{
    static const cv::Matx33d matrix(
        .4123907992659595, .3575843393838779, .1804807884018343,
        .2126390058715104, .7151686787677559, .0721923153607337,
        .0193308187155919, .1191947797946259, .9505321522496606);
    return matrix;
}
inline const cv::Matx33d &fromSrgb()
{
    static const cv::Matx33d matrix = toXyz().inv() * srgbToXyz();
    return matrix;
}
inline const cv::Matx33d &toSrgb()
{
    static const cv::Matx33d matrix = fromSrgb().inv();
    return matrix;
}
inline const cv::Matx33d &fromXyzD50()
{
    // Preserve the established DCP PCS/D50 connection (including its published
    // rounded coefficients), composed directly into the new working basis.
    // Replacing that connection with a different precision/white convention
    // also changes neutral openings; that would be a separate calibration.
    // There is no intermediate sRGB pixel buffer or gamut mapping here.
    static const cv::Matx33d matrix = fromSrgb() * cv::Matx33d(
        3.2404542, -1.5371385, -.4986108,
        -.9692660, 1.8760108, .0415560,
        .0556434, -.2040259, 1.0572252) * cv::Matx33d(
        .9555766, -.0230393, .0631636,
        -.0282895, 1.0099416, .0210077,
        .0122982, -.0204830, 1.3299098);
    return matrix;
}
inline const cv::Vec3f &lumaWeights()
{
    static const cv::Vec3f weights(float(toXyz()(1, 0)),
                                   float(toXyz()(1, 1)), float(toXyz()(1, 2)));
    return weights;
}
inline float luminance(const cv::Vec3f &rgb) { return rgb.dot(lumaWeights()); }
inline float nonNegativeLuminance(const cv::Vec3f &rgb) { return std::max(0.0f, luminance(rgb)); }

// Scales a colour from its luminance to the target luminance (channel ratios
// kept); luminances at or below 1e-6 are left untouched.
inline void scaleToLuminance(cv::Vec3f &rgb, float sourceLuminance, float targetLuminance)
{
    if (sourceLuminance > 1e-6f) rgb *= std::max(0.0f, targetLuminance) / sourceLuminance;
}

// sRGB transfer function (IEC 61966-2-1), without clamping.
inline float srgbEncode(float linear)
{
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}
inline double srgbEncode(double linear)
{
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}
inline float srgbDecode(float encoded)
{
    return encoded <= .04045f ? encoded / 12.92f : std::pow((encoded + .055f) / 1.055f, 2.4f);
}
inline double srgbDecode(double encoded)
{
    return encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
}

// Linear sRGB D65 -> XYZ -> Bradford D65 to D50 -> linear ProPhoto RGB.
inline const cv::Matx33d &srgbToProPhoto()
{
    static const cv::Matx33d matrix = cv::Matx33d(
        .7976749, .1351917, .0313534,
        .2880402, .7118741, .0000857,
        .0000000, .0000000, .8252100).inv() * cv::Matx33d(
        1.0478112, .0228866, -.0501270,
        .0295424, .9904844, -.0170491,
        -.0092345, .0150436, .7521316) * srgbToXyz();
    return matrix;
}

// Relative saturation (max - min) / max of a working-space colour in linear
// ProPhoto RGB, clamped to [0, 1]; 0 when the maximum is not positive. One matrix
// from Rec.2020 (sRGB -> ProPhoto composed with Rec.2020 -> sRGB, computed once).
inline const cv::Matx33d &toProPhoto()
{
    static const cv::Matx33d matrix = srgbToProPhoto() * toSrgb();
    return matrix;
}
inline double proPhotoSaturation(const cv::Vec3d &rgb)
{
    const cv::Vec3d p = toProPhoto() * rgb;
    const double peak = std::max({p[0], p[1], p[2]});
    const double low = std::min({p[0], p[1], p[2]});
    return peak > 1e-6 ? std::clamp((peak - low) / peak, 0.0, 1.0) : 0.0;
}

// Ottosson Oklab (2020) from and to linear sRGB; signed cube roots keep
// HDR and out-of-gamut values defined.
inline cv::Vec3d toOklab(const cv::Vec3d &p)
{
    const double l = std::cbrt(.4122214708 * p[0] + .5363325363 * p[1] + .0514459929 * p[2]);
    const double m = std::cbrt(.2119034982 * p[0] + .6806995451 * p[1] + .1073969566 * p[2]);
    const double s = std::cbrt(.0883024619 * p[0] + .2817188376 * p[1] + .6299787005 * p[2]);
    return {.2104542553 * l + .7936177850 * m - .0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + .4505937099 * s,
            .0259040371 * l + .7827717662 * m - .8086757660 * s};
}
inline cv::Vec3d fromOklab(const cv::Vec3d &p)
{
    const double l = p[0] + .3963377774 * p[1] + .2158037573 * p[2];
    const double m = p[0] - .1055613458 * p[1] - .0638541728 * p[2];
    const double s = p[0] - .0894841775 * p[1] - 1.2914855480 * p[2];
    return {4.0767416621 * l * l * l - 3.3077115913 * m * m * m + .2309699292 * s * s * s,
            -1.2684380046 * l * l * l + 2.6097574011 * m * m * m - .3413193965 * s * s * s,
            -.0041960863 * l * l * l - .7034186147 * m * m * m + 1.7076147010 * s * s * s};
}

// Oklab from and to the linear Rec.2020 working space in one matrix: Ottosson's
// linear sRGB -> LMS matrix composed with toSrgb(), computed once. His published
// XYZ -> LMS matrix maps white 1e-4 off (1, 1, 1), so greys would gain a tint; the
// sRGB one, refitted on white, keeps them neutral. Same Oklab as toOklab() at every
// colour, out-of-gamut ones included.
inline const cv::Matx33d &toLms()
{
    static const cv::Matx33d matrix = cv::Matx33d(
        .4122214708, .5363325363, .0514459929,
        .2119034982, .6806995451, .1073969566,
        .0883024619, .2817188376, .6299787005) * toSrgb();
    return matrix;
}
inline const cv::Matx33d &fromLms()
{
    static const cv::Matx33d matrix = toLms().inv();
    return matrix;
}
inline cv::Vec3d workingToOklab(const cv::Vec3d &rgb)
{
    const cv::Vec3d lms = toLms() * rgb;
    const double l = std::cbrt(lms[0]), m = std::cbrt(lms[1]), s = std::cbrt(lms[2]);
    return {.2104542553 * l + .7936177850 * m - .0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + .4505937099 * s,
            .0259040371 * l + .7827717662 * m - .8086757660 * s};
}
inline cv::Vec3d oklabToWorking(const cv::Vec3d &p)
{
    const double l = p[0] + .3963377774 * p[1] + .2158037573 * p[2];
    const double m = p[0] - .1055613458 * p[1] - .0638541728 * p[2];
    const double s = p[0] - .0894841775 * p[1] - 1.2914855480 * p[2];
    return fromLms() * cv::Vec3d(l * l * l, m * m * m, s * s * s);
}

// In-place, preserves straight alpha bit-for-bit, including extended RGB.
inline void transform(cv::Mat &image, const cv::Matx33d &matrix)
{
    CV_Assert(image.type() == CV_32FC3 || image.type() == CV_32FC4);
    const int channels = image.channels();
    for (int y = 0; y < image.rows; ++y) {
        float *row = image.ptr<float>(y);
        for (int x = 0; x < image.cols; ++x) {
            float *p = row + x * channels;
            const cv::Vec3d converted = matrix * cv::Vec3d(p[0], p[1], p[2]);
            p[0] = float(converted[0]); p[1] = float(converted[1]); p[2] = float(converted[2]);
        }
    }
}
}
#endif // IPR_CORE_WORKINGCOLORSPACE_H
