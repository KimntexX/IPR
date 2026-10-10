#ifndef IPR_TONE_RECOVERY_H
#define IPR_TONE_RECOVERY_H
#include <algorithm>
namespace ToneRecovery {
inline float smoothStep(float lo, float hi, float value)
{
    const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// Additional Y compatible with uniform RGB scaling; existing HDR is not clipped.
inline float rgbHeadroom(float sourceY, float currentY, float maximumChannel)
{
    if (sourceY <= 0 || maximumChannel <= 0 || maximumChannel >= 1) return 0;
    return std::max(0.0f, sourceY / maximumChannel - currentY);
}
inline float positiveShadowDelta(float y, float maximumChannel, float amount)
{
    if (y <= 0) return 0;
    // No black pedestal: gain <=4 (+2 EV). k locates maximum lift at Y=.025.
    // Delta=3*a*Y/(1+Y/k)^2, <=.01875 before masking. The unmasked output
    // derivative is >=1-3/27: tone ordering is preserved, unlike inverse-root lift.
    constexpr float maximumExtraGain = 3.0f;
    constexpr float knee = 0.025f;
    const float u = y / knee;
    const float mask = 1.0f - smoothStep(0.10f, 0.56f, y);
    const float delta = std::clamp(amount, 0.0f, 1.0f) * maximumExtraGain * y * mask /
                        ((1.0f + u) * (1.0f + u));
    // Retain 10% channel headroom instead of clipping RGB independently.
    return std::min(delta, 0.90f * rgbHeadroom(y, y, maximumChannel));
}
inline float positiveHighlightDelta(float sourceY, float currentY,
                                   float maximumChannel, float amount, float mask)
{
    // Y<=1 does not guarantee RGB<=1. Keep 30% actual RGB headroom at +100.
    constexpr float amplitude = 0.70f;
    return rgbHeadroom(sourceY, currentY, maximumChannel) * amplitude *
           std::clamp(amount, 0.0f, 1.0f) * std::clamp(mask, 0.0f, 1.0f);
}
}
#endif
