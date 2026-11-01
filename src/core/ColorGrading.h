#ifndef IPR_CORE_COLORGRADING_H
#define IPR_CORE_COLORGRADING_H

#include <opencv2/core.hpp>

#include <array>

// Colour grading of shadows, midtones and highlights following Lightroom's
// response, fitted on its neutral -> state pairs (tests/study_grading_response.py,
// etude-grading-v001-20261025T173113Z): in Oklab, each zone adds w_zone(L) s(saturation) c(hue), with w a
// curve of the pixel's lightness, s the strength of the saturation slider and c
// the change (dL, da, db) Lightroom gives a hue at saturation 50. The zones do not
// follow the image's key (28 scenes). Blending widens or narrows the shadows and
// highlights, balance moves their boundary (tests/study_grading_blend.py). The global
// wheel colours every tone; each zone's luminance and the global one shift the
// lightness and scale the chroma (tests/study_grading_luminance.py).
// Study: docs/validation/analyses/analyse-grading.md.
namespace ColorGrading {

// Each zone's change at full weight, from its hue in degrees and saturation in
// [0, 100] (shadows, midtones, highlights, global), and its weight curve over Oklab
// lightness (21 knots): the three zones' from the blending [0, 100] and balance
// [-100, 100] they share, the global wheel's fixed. With the luminance [-100, 100]
// of each, the lightness shift and chroma gain they add up to. Computed once per render.
struct Zones {
    std::array<cv::Vec3f, 4> change{};
    std::array<bool, 4> active{};
    std::array<std::array<float, 21>, 4> curve{};
    std::array<float, 21> lightnessShift{};
    std::array<float, 21> chromaGain{};
    bool luminance = false;
};
Zones prepare(const std::array<int, 4> &hue, const std::array<int, 4> &saturation,
              const std::array<int, 4> &luminance, int blending, int balance);

// One Oklab colour, in place: the luminance shift and chroma gain of its lightness,
// then each active zone's change weighted by the shifted lightness. The image loop
// is PerceptualColour's.
void adjust(cv::Vec3d &lab, const Zones &zones);

// Encoded sRGB colour showing what a zone does at a hue and saturation, for the UI
// wheel and track: a light grey pushed along the zone's measured Oklab change (its
// chroma enlarged to be read, proportional to the saturation), fitted to sRGB.
cv::Vec3f displayColour(int zone, int hue, int saturation);

} // namespace ColorGrading

#endif // IPR_CORE_COLORGRADING_H
