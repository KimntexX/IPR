#ifndef IPR_CORE_COLORGRADING_H
#define IPR_CORE_COLORGRADING_H

#include <opencv2/core.hpp>

#include <array>

// Colour grading of shadows, midtones and highlights following Lightroom's
// response, fitted on its neutral -> state pairs (tests/study_grading_response.py,
// etude-grading-v001-20261025T173113Z): in Oklab, each zone adds w_zone(L) s(saturation) c(hue), with w a
// curve of the pixel's lightness, s the strength of the saturation slider and c
// the change (dL, da, db) Lightroom gives a hue at saturation 50. The zones do not
// follow the image's key (28 scenes).
// Study: docs/validation/analyses/analyse-grading.md.
namespace ColorGrading {

// Linear Rec.2020 RGB image (CV_32FC3), in place; per zone (shadows, midtones,
// highlights) hue in degrees and saturation in [0, 100].
void apply(cv::Mat &linearRgb, const std::array<int, 3> &hue, const std::array<int, 3> &saturation);

} // namespace ColorGrading

#endif // IPR_CORE_COLORGRADING_H
