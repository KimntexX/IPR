#ifndef IPR_CORE_WORKINGCOLORSPACE_H
#define IPR_CORE_WORKINGCOLORSPACE_H

#include <opencv2/core.hpp>

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
