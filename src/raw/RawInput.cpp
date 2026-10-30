#include "RawInput.h"

#include <algorithm>
#include <cmath>

namespace RawInput {

bool valid(const Input &input)
{
    if (input.mosaic.empty() || input.mosaic.type() != CV_16UC1 || input.spatialBlack)
        return false;
    std::array<int, 4> sites{};
    for (const auto &row : input.pattern)
        for (int colour : row) {
            if (colour < Red || colour > Green2) return false;
            ++sites[size_t(colour)];
        }
    // One red, one blue, two greens (the second may be labelled Green2).
    if (sites[Red] != 1 || sites[Blue] != 1 || sites[Green] + sites[Green2] != 2)
        return false;
    const double blackest = *std::max_element(input.black.cbegin(), input.black.cend());
    if (!std::isfinite(input.white) || !(input.white > blackest) ||
        std::any_of(input.black.cbegin(), input.black.cend(), [](double b) { return !std::isfinite(b) || b < 0.0; }))
        return false;
    for (int c : {Red, Green, Blue})
        if (!(input.asShotMultipliers[size_t(c)] > 0.0) || !std::isfinite(input.asShotMultipliers[size_t(c)]))
            return false;
    return true;
}

} // namespace RawInput
