#ifndef IPR_RAW_CFANOISESTAGE_H
#define IPR_RAW_CFANOISESTAGE_H

#include "Image.h"
#include "NoiseLaw.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

// Pure helpers of the CFA denoising stage, between the black subtraction and the
// white balance of RawDevelopment::develop: the mosaic as four canonical planes
// (red, green on red rows, green on blue rows, blue) in the network's units, and
// the measured noise law in the same order and units. No runtime, no state.
namespace CfaNoiseStage {

using Pattern = std::array<std::array<int, 2>, 2>;  // RawInput::Colour of each 2x2 site

struct Planes
{
    std::size_t width = 0;   // cells, the mosaic's width / 2
    std::size_t height = 0;  // cells, the mosaic's height / 2
    std::vector<float> values;  // planar R, G1, G2, B, each width * height
};

// Canonical plane of each 2x2 site (R 0, G1 1, G2 2, B 3); nullopt for a non-Bayer pattern.
std::optional<std::array<std::array<int, 2>, 2>> planeOfSite(const Pattern &pattern);

// Whole 2x2 cells of a black-subtracted F32 mosaic, divided by `divisor`; an odd last
// row or column is left out (and left untouched by unpack).
std::optional<Planes> pack(const ipr::Image &mosaic, const Pattern &pattern, double divisor);

// Writes planes back into the mosaic, multiplied by `divisor`; false if shapes differ.
bool unpack(const Planes &planes, ipr::Image &mosaic, const Pattern &pattern, double divisor);

struct NormalizedLaw
{
    std::array<float, 4> a{};  // shot slope / divisor, canonical plane order
    std::array<float, 4> b{};  // read variance / divisor^2
};

// The catalogue gives the law per raster site of the measured camera's own 2x2 cell;
// `measuredPattern` is that cell, so each site's law lands on its colour's plane.
std::optional<NormalizedLaw> normalize(const NoiseLaw::Law &law, const Pattern &measuredPattern, double divisor);

}

#endif // IPR_RAW_CFANOISESTAGE_H
