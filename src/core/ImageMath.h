#ifndef IPR_CORE_IMAGEMATH_H
#define IPR_CORE_IMAGEMATH_H

#include <algorithm>

// Small numeric helpers shared by the processing modules.
namespace ImageMath {
// Hermite step from 0 at edge0 to 1 at edge1.
inline float smoothStep(float edge0, float edge1, float value)
{
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}

#endif // IPR_CORE_IMAGEMATH_H
