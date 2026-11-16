#include "HalfFloat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool isNanHalf(std::uint16_t bits)
{
    return (bits & 0x7c00u) == 0x7c00u && (bits & 0x3ffu) != 0u;
}

void everyCode()
{
    // Each finite half comes back to itself, and its float is the exact value of the code.
    int mismatches = 0;
    for (std::uint32_t code = 0; code <= 0xffffu; ++code) {
        const std::uint16_t bits = std::uint16_t(code);
        if (isNanHalf(bits) || (bits & 0x7fffu) == 0x7c00u)
            continue;
        if (ipr::toHalf(ipr::fromHalf(bits)) != bits)
            ++mismatches;
    }
    check(mismatches == 0, "every finite code goes and comes back unchanged");
    check(ipr::fromHalf(0x3c00u) == 1.f && ipr::fromHalf(0xc000u) == -2.f, "one and minus two");
    check(ipr::fromHalf(0x7bffu) == 65504.f, "the largest finite value");
    check(ipr::fromHalf(0x0001u) == std::ldexp(1.f, -24), "the smallest subnormal");
    check(ipr::fromHalf(0x0400u) == std::ldexp(1.f, -14), "the smallest normal");
    check(std::isinf(ipr::fromHalf(0x7c00u)) && std::isnan(ipr::fromHalf(0x7e00u)), "infinity and NaN are read");
}

void rounding()
{
    // Between 1 and the next half, 1 + 2^-10: the middle goes to the even code, 1.
    check(ipr::toHalf(1.f + std::ldexp(1.f, -11)) == 0x3c00u, "a tie goes to the even code");
    check(ipr::toHalf(1.f + 3.f * std::ldexp(1.f, -11)) == 0x3c02u, "and the next tie to the next even code");
    check(ipr::toHalf(std::nextafter(1.f + std::ldexp(1.f, -11), 2.f)) == 0x3c01u, "past the middle goes up");
    check(ipr::toHalf(std::nextafter(1.f + std::ldexp(1.f, -11), 0.f)) == 0x3c00u, "short of it goes down");
    // The largest mantissa carries into the exponent.
    check(ipr::toHalf(2.f - std::ldexp(1.f, -12)) == 0x4000u, "a carry reaches the next power of two");
    // Subnormals: the same rule at 2^-24 units.
    check(ipr::toHalf(std::ldexp(1.f, -25)) == 0x0000u, "half the smallest subnormal ties to zero");
    check(ipr::toHalf(std::ldexp(3.f, -25)) == 0x0002u, "one and a half subnormals tie to two");
    check(ipr::toHalf(std::nextafter(std::ldexp(1.f, -25), 1.f)) == 0x0001u, "just above half a subnormal is one");
    check(ipr::toHalf(std::ldexp(1023.5f, -24)) == 0x0400u, "the last subnormal tie carries to the smallest normal");
    check(ipr::toHalf(1e-12f) == 0x0000u && ipr::toHalf(-1e-12f) == 0x8000u, "too small is a signed zero");
}

void limits()
{
    check(ipr::toHalf(65504.f) == 0x7bffu, "the largest finite value is kept");
    check(ipr::toHalf(65519.f) == 0x7bffu, "short of the middle to infinity rounds to it");
    check(ipr::toHalf(1e9f) == 0x7bffu && ipr::toHalf(-1e9f) == 0xfbffu, "beyond it, the largest value with its sign");
    check(ipr::toHalf(std::numeric_limits<float>::infinity()) == 0x7bffu, "an infinity is not written");
    check(isNanHalf(ipr::toHalf(std::numeric_limits<float>::quiet_NaN())), "NaN stays NaN");
    // The precision a signal keeps: 11 significant bits at every level.
    double worst = 0.0;
    for (float value = 1e-4f; value < 60000.f; value *= 1.0137f)
        worst = std::max(worst, std::abs(double(ipr::fromHalf(ipr::toHalf(value))) - value) / value);
    check(worst <= std::ldexp(1.0, -11), "a relative error of at most 2^-11");
}

} // namespace

int main()
{
    everyCode();
    rounding();
    limits();
    if (passed)
        std::cout << "half float: ok\n";
    return passed ? 0 : 1;
}
