#ifndef IPR_CORE_PERCEPTUALCOLOUR_H
#define IPR_CORE_PERCEPTUALCOLOUR_H

#include "ColorMixer.h"

#include <opencv2/core.hpp>

#include <array>

// The perceptual colour stage: saturation and vibrance, the colour mixer and the
// colour grading, in this order, in Oklab. Each pixel goes to Oklab once and back
// once, instead of once per adjustment; the adjustments themselves (Vibrance,
// ColorMixer, ColorGrading) work on one Oklab colour.
namespace PerceptualColour {

struct Parameters {
    int saturation = 0;                       // [-100, 100]
    int vibrance = 0;                         // [-100, 100]
    ColorMixer::Parameters mixer;
    std::array<int, 3> gradingHue{};          // shadows, midtones, highlights, degrees
    std::array<int, 3> gradingSaturation{};   // [0, 100]
};

// Linear Rec.2020 image (CV_32FC3), in place. No clipping: the output handles gamut.
void apply(cv::Mat &linearRgb, const Parameters &parameters);

} // namespace PerceptualColour

#endif // IPR_CORE_PERCEPTUALCOLOUR_H
