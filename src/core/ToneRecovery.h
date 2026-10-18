#ifndef IPR_TONE_RECOVERY_H
#define IPR_TONE_RECOVERY_H
#include <algorithm>
#include <cmath>

// Shadows, Blacks, Highlights and Whites as one tone curve of luminance.
// Each control shifts tones by an EV amount weighted by a logistic zone of
// log2(Y): low end for Shadows/Blacks, high end for Highlights/Whites. Zone
// centres stay in the matching region of Lightroom's histogram; low-end zones
// fade out over the last 2 EV below white and Highlights over the last EV,
// which belongs to Whites. Blacks negative first darkens near-black detail
// through a soft toe y^2 / (y + toe), then raises a black point that grows
// late (true blacks at -100, as in Lightroom); Whites positive lowers the
// white point. Any brightened tone above the knee gets a soft shoulder, so no
// output reaches 1 from below; the editor passes the largest delivery channel
// through the same shoulder, so saturated colours roll off instead of being
// pinned at 1. Plan A parameters (fixed ranges), fitted on eight scenes against
// Lightroom under the separation rules of the export tests (18 October 2026).
// The curve depends on Y only and its slope stays positive, so tone order is
// never reversed.
namespace ToneRecovery {
struct Zone { float centreEv, width, positive, negative; bool lowEnd; float fadeEv; };
inline constexpr Zone Shadows{-4.155f, 0.700f, 1.507f, 1.855f, true, 2.0f};
inline constexpr Zone Blacks{-6.638f, 1.047f, 1.239f, 0.104f, true, 2.0f};
inline constexpr Zone Highlights{-1.018f, 1.796f, 0.585f, 1.149f, false, 1.0f};
inline constexpr Zone Whites{0.158f, 1.634f, 0.682f, 1.094f, false, 0.0f};
inline constexpr float BlackToe = 0.01653f, BlackPoint = 0.0424f, BlackPointExponent = 4.02f;
inline constexpr float WhitePoint = 0.0364f, WhitePointExponent = 1.986f;
inline constexpr float ShoulderKnee = 0.70f, FloorEv = -16.0f;

struct Amounts { float shadows = 0, blacks = 0, highlights = 0, whites = 0; };

inline bool identity(const Amounts &a)
{
    return a.shadows == 0 && a.blacks == 0 && a.highlights == 0 && a.whites == 0;
}

// Zone weight and its derivative with respect to x = log2(Y): a logistic
// window times a linear fade to zero at white over the zone's last fadeEv.
inline void weight(const Zone &zone, float x, float &w, float &slope)
{
    const float z = (x - zone.centreEv) / zone.width;
    const float e = std::exp(zone.lowEnd ? z : -z);
    const float logistic = 1.0f / (1.0f + e);
    const float dLogistic = (zone.lowEnd ? -1.0f : 1.0f) * logistic * (1.0f - logistic) / zone.width;
    float fade = 1.0f, dFade = 0.0f;
    if (zone.fadeEv > 0.0f && x > -zone.fadeEv) {
        fade = std::max(0.0f, -x / zone.fadeEv);
        dFade = x < 0.0f ? -1.0f / zone.fadeEv : 0.0f;
    }
    w = logistic * fade;
    slope = dLogistic * fade + logistic * dFade;
}

inline float zoneShift(const Zone &zone, float amount, float x, float &slope)
{
    float w = 0, dw = 0;
    weight(zone, x, w, dw);
    const float strength = (amount > 0 ? zone.positive : zone.negative) * amount;
    slope += strength * dw;
    return strength * w;
}

// Soft shoulder of a brightening from y (< 1) to out: tones above the knee
// approach 1 without reaching it; untouched or darkened tones pass through.
inline float shoulder(float y, float out)
{
    if (out <= y || out <= ShoulderKnee) return out;
    const float base = std::max(y, ShoulderKnee);
    return base + (1.0f - base) * std::tanh((out - base) / (1.0f - base));
}

// Target luminance for a source luminance y (linear working space).
// Values at or above 1 (existing HDR headroom) are left untouched.
inline float target(float y, const Amounts &a)
{
    if (y <= 0.0f || y >= 1.0f || identity(a)) return y;
    const float x = std::log2(std::max(y, std::exp2(FloorEv)));
    float slope = 0.0f;
    float shift = 0.0f;
    if (a.shadows != 0) shift += zoneShift(Shadows, a.shadows, x, slope);
    if (a.blacks != 0) shift += zoneShift(Blacks, a.blacks, x, slope);
    if (a.highlights != 0) shift += zoneShift(Highlights, a.highlights, x, slope);
    if (a.whites != 0) shift += zoneShift(Whites, a.whites, x, slope);
    // Combined controls: keep the curve increasing (d x'/dx >= 0.1).
    if (1.0f + slope < 0.1f) shift *= 0.9f / -slope;
    float out = y * std::exp2(shift);
    if (a.blacks < 0) {
        out = out * out / (out + BlackToe * -a.blacks);
        const float b = BlackPoint * std::pow(-a.blacks, BlackPointExponent);
        out = std::max(0.0f, (out - b) / (1.0f - b));
    }
    if (a.whites > 0)
        out /= std::max(0.05f, 1.0f - WhitePoint * std::pow(a.whites, WhitePointExponent));
    return shoulder(y, out);
}
}
#endif
