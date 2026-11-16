#include "HalfFloat.h"

#include <cstring>

namespace ipr {
namespace {

std::uint32_t bitsOf(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float floatOf(std::uint32_t bits)
{
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

constexpr std::uint16_t largestFinite = 0x7bff;

} // namespace

std::uint16_t toHalf(float value)
{
    const std::uint32_t bits = bitsOf(value);
    const std::uint16_t sign = std::uint16_t((bits >> 16) & 0x8000u);
    const std::uint32_t magnitude = bits & 0x7fffffffu;
    if (magnitude > 0x7f800000u)
        return std::uint16_t(sign | 0x7e00u);                     // NaN
    if (magnitude >= 0x477ff000u)                                  // rounds beyond 65504, infinity included
        return std::uint16_t(sign | largestFinite);
    if (magnitude >= 0x38800000u) {                                // a normal half
        const std::uint32_t mantissa = magnitude & 0x7fffffu;
        std::uint32_t half = ((magnitude >> 23) - 112u) << 10 | (mantissa >> 13);
        const std::uint32_t rest = mantissa & 0x1fffu;
        if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
            ++half;                                                // a carry into the exponent is right
        return std::uint16_t(sign | half);
    }
    if (magnitude < 0x33000000u)                                   // below half the smallest subnormal
        return sign;
    // A subnormal half: the significand with its leading one, shifted to 2^-24 units.
    const std::uint32_t exponent = magnitude >> 23;
    const std::uint32_t significand = (magnitude & 0x7fffffu) | 0x800000u;
    const std::uint32_t shift = 126u - exponent;                   // 14 to 24
    std::uint32_t half = significand >> shift;
    const std::uint32_t rest = significand & ((1u << shift) - 1u);
    const std::uint32_t middle = 1u << (shift - 1u);
    if (rest > middle || (rest == middle && (half & 1u)))
        ++half;
    return std::uint16_t(sign | half);
}

float fromHalf(std::uint16_t bits)
{
    const std::uint32_t sign = std::uint32_t(bits & 0x8000u) << 16;
    const std::uint32_t exponent = (bits >> 10) & 0x1fu;
    const std::uint32_t mantissa = bits & 0x3ffu;
    if (exponent == 0x1fu)
        return floatOf(sign | 0x7f800000u | (mantissa << 13));
    if (exponent != 0u)
        return floatOf(sign | ((exponent + 112u) << 23) | (mantissa << 13));
    // Zero or subnormal: mantissa units of 2^-24, exact in single precision.
    const float magnitude = float(mantissa) * (1.f / 16777216.f);
    return sign ? -magnitude : magnitude;
}

} // namespace ipr
