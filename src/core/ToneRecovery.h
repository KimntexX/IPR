#ifndef IPR_TONE_RECOVERY_H
#define IPR_TONE_RECOVERY_H
#include <algorithm>
#include <array>
#include <cmath>

// Shadows, Blacks, Highlights and Whites as one tone curve of luminance.
// Each control shifts tones by an EV amount weighted by a logistic zone of
// log2(Y): low end for Shadows/Blacks, high end for Highlights/Whites. As in
// Lightroom, a control may reach the whole tonal range, graded: strongest at
// its own end, weakest at the opposite end (rule of 18 October 2026). Low-end zones
// fade out over the last 2 EV below white and Highlights over the last EV,
// which belongs to Whites. Blacks negative first darkens near-black detail
// through a soft toe y^2 / (y + toe), then raises a black point that grows
// late (true blacks at -100, as in Lightroom); Whites positive lowers the
// white point. Any brightened tone above the knee gets a soft shoulder, so no
// output reaches 1 from below; the editor passes the largest delivery channel
// through the same shoulder, so saturated colours roll off instead of being
// pinned at 1. Plan A parameters (fixed ranges), fitted on eight scenes against
// Lightroom under that graded rule (18 October 2026).
// The curve depends on Y only and its slope stays positive, so tone order is
// never reversed.
namespace ToneRecovery {
struct Zone { float centreEv, width, positive, negative; bool lowEnd; float fadeEv; };
inline constexpr Zone Shadows{-4.155f, 0.700f, 1.507f, 1.855f, true, 2.0f};
inline constexpr Zone Blacks{-2.888f, 1.013f, 1.390f, 2.054f, true, 2.0f};
inline constexpr Zone Highlights{-1.285f, 2.958f, 0.664f, 1.154f, false, 1.0f};
inline constexpr Zone Whites{-0.207f, 2.925f, 0.813f, 0.649f, false, 0.0f};
inline constexpr float BlackToe = 0.00401f, BlackPoint = 0.0365f, BlackPointExponent = 6.17f;
inline constexpr float WhitePoint = 0.1186f, WhitePointExponent = 2.287f;
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

// Production form of target(): sampled once per render over the 16 EV below
// white and made non-decreasing by a running maximum, so strong combined
// controls can never reverse tone order (the slope guard of target() is only
// local); linear interpolation in log2(Y) between samples.
class Curve {
public:
    explicit Curve(const Amounts &amounts) : amounts_(amounts), identity_(identity(amounts))
    {
        if (identity_) return;
        float previous = 0.0f;
        for (int i = 0; i <= Samples; ++i) {
            // The last sample sits just below white: 1 itself is headroom, left untouched.
            const float y = i == Samples ? std::nextafter(1.0f, 0.0f)
                                         : std::exp2(FloorEv * (1.0f - float(i) / Samples));
            previous = std::max(previous, target(y, amounts_));
            out_[i] = previous;
        }
    }
    float operator()(float y) const
    {
        if (identity_ || y <= 0.0f || y >= 1.0f) return y;
        const float position = (std::log2(y) - FloorEv) / -FloorEv * Samples;
        if (position <= 0.0f) return std::min(target(y, amounts_), out_[0]);
        const int i = std::min(int(position), Samples - 1);
        const float f = position - float(i);
        return out_[i] + (out_[i + 1] - out_[i]) * f;
    }

private:
    static constexpr int Samples = 4096;
    Amounts amounts_;
    bool identity_;
    std::array<float, Samples + 1> out_{};
};
}
#endif
