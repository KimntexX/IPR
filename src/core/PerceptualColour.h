#ifndef IPR_CORE_PERCEPTUALCOLOUR_H
#define IPR_CORE_PERCEPTUALCOLOUR_H

#include "ColorMixer.h"
#include "PointColor.h"

#include <opencv2/core.hpp>

#include <array>
#include <optional>

// The perceptual colour stage: saturation and vibrance, the colour mixer, the
// point colour and the colour grading, in this order, in Oklab. Each pixel goes to Oklab once and back
// once, instead of once per adjustment; the adjustments themselves (Vibrance,
// ColorMixer, PointColor, ColorGrading) work on one Oklab colour.
namespace PerceptualColour {

struct Parameters {
    int saturation = 0;                       // [-100, 100]
    int vibrance = 0;                         // [-100, 100]
    ColorMixer::Parameters mixer;
    PointColor::Parameters pointColor;
    std::array<int, 4> gradingHue{};          // shadows, midtones, highlights, global; degrees
    std::array<int, 4> gradingSaturation{};   // [0, 100]
    std::array<int, 4> gradingLuminance{};    // [-100, 100]
    int gradingBlending = 50;                 // [0, 100]
    int gradingBalance = 0;                   // [-100, 100]
};

// Linear Rec.2020 image (CV_32FC3), in place. No clipping: the output handles gamut.
void apply(cv::Mat &linearRgb, const Parameters &parameters);

// The colour the point colour sees at a place of the stage's input (the tones'
// output, CV_32FC3): the mean over a square of the given radius around (x, y), in
// [0, 1] of the image, after saturation, vibrance and the mixer. Empty outside the
// image or on an empty input.
std::optional<cv::Vec3d> colourBeforePointColour(const cv::Mat &linearRgb, double x, double y, int radius,
                                                 const Parameters &parameters);

} // namespace PerceptualColour

#endif // IPR_CORE_PERCEPTUALCOLOUR_H
