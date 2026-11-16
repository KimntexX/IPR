#ifndef IPR_IMAGE_HALFFLOAT_H
#define IPR_IMAGE_HALFFLOAT_H

#include <cstdint>

// IEEE 754 binary16, the samples of a floating-point DNG: 11 significant bits, finite up to
// 65504. Standard C++ has no such type before C++23, and no compiler extension is used.
namespace ipr {

// Rounded to the nearest, ties to even. Beyond the largest finite value: that value, with
// its sign, so that a bright outlier never becomes an infinity in a file. NaN stays NaN.
std::uint16_t toHalf(float value);

float fromHalf(std::uint16_t bits);

} // namespace ipr

#endif // IPR_IMAGE_HALFFLOAT_H
