#ifndef IPR_TONE_RECOVERY_H
#define IPR_TONE_RECOVERY_H
#include "ToneRecoveryModel.h"
#include "ToneStatistics.h"
#include <algorithm>
#include <array>
#include <cmath>

// Shadows, Blacks, Highlights and Whites (plan B, 18 October 2026). Each control
// lifts or lowers tones by an EV amount read from Lightroom's own transfer
// curves (ToneRecoveryModel.h: knots every EV of log2 input, four slider
// levels, linear in between). Lightroom adapts Shadows, Highlights and positive
// Whites to the image: their lift is multiplied by a per-image gain predicted
// from the image's luminance statistics, bounded to the range seen in fitting.
// Blacks and negative Whites are the same on every image.
// Shadows and Highlights act on luminance (common RGB scale); Blacks and Whites
// act on each channel, as Lightroom does: its Blacks -100 saturates colours
// (x1.40), which a common scale cannot reproduce. Values at or above 1 (HDR
// headroom) are left untouched.
namespace ToneRecovery {
struct Amounts { float shadows = 0, blacks = 0, highlights = 0, whites = 0; };

inline bool identity(const Amounts &a)
{
    return a.shadows == 0 && a.blacks == 0 && a.highlights == 0 && a.whites == 0;
}

// Per-image gains, indexed like Model::Controls.
struct Gains { std::array<float, 8> value{1, 1, 1, 1, 1, 1, 1, 1}; };

inline Statistics typicalStatistics()
{
    Statistics s;
    for (int i = 0; i < Model::StatisticCount; ++i) s.log2Luminance[size_t(i)] = Model::TypicalStatistics[i];
    return s;
}

inline Gains gains(const Statistics &statistics)
{
    Gains g;
    for (int i = 0; i < 8; ++i) {
        const auto &c = Model::Controls[i];
        if (c.gainStatisticCount == 0) continue;
        float logGain = c.gainCoefficients[0];
        for (int k = 0; k < c.gainStatisticCount; ++k)
            logGain += c.gainCoefficients[k + 1] * statistics.log2Luminance[size_t(c.gainStatistics[k])];
        g.value[size_t(i)] = std::exp(std::clamp(logGain, c.logGainMin, c.logGainMax));
    }
    return g;
}

// EV lift of one control (0 Shadows, 1 Blacks, 2 Highlights, 3 Whites) at
// x = log2(input) for a slider amount in [-1, 1].
inline float lift(int control, float amount, float x, const Gains &gains)
{
    if (amount == 0.0f) return 0.0f;
    const int index = 2 * control + (amount > 0.0f ? 1 : 0);
    const auto &c = Model::Controls[index];
    const float knot = std::clamp(x - Model::FirstKnotEv, 0.0f, float(Model::KnotCount - 1));
    const int k = std::min(int(knot), Model::KnotCount - 2);
    const float t = knot - float(k);
    const auto at = [&](int level) {
        return c.lift[level][k] + (c.lift[level][k + 1] - c.lift[level][k]) * t;
    };
    const float level = std::min(std::abs(amount), 1.0f);
    float value;
    if (level <= Model::Levels[0]) value = at(0) * level / Model::Levels[0];
    else {
        int l = 0;
        while (l < 2 && level > Model::Levels[l + 1]) ++l;
        const float u = (level - Model::Levels[l]) / (Model::Levels[l + 1] - Model::Levels[l]);
        value = at(l) + (at(l + 1) - at(l)) * u;
    }
    return value * gains.value[size_t(index)];
}

// Soft shoulder of a brightening from y (< 1) to out: tones above the knee
// approach 1 without reaching it; untouched or darkened tones pass through.
inline float shoulder(float y, float out, float knee = 0.70f)
{
    if (out <= y || out <= knee) return out;
    const float base = std::max(y, knee);
    return base + (1.0f - base) * std::tanh((out - base) / (1.0f - base));
}

// Analytic transfer of the luminance controls (Shadows, Highlights) or the
// channel controls (Blacks, Whites), before monotone sampling. Channels roll
// off near white through the shoulder, as Lightroom does: a hard cap at 1
// clipped about ten times more channels than Lightroom with Whites +25. The
// luminance lift is capped at white; the editor then passes the largest sRGB
// delivery channel through the shoulder.
inline float transfer(float y, const Amounts &a, const Gains &g, bool perChannel)
{
    if (y <= 0.0f || y >= 1.0f) return y;
    const float x = std::log2(std::max(y, std::exp2(Model::FirstKnotEv)));
    const float shift = perChannel ? lift(1, a.blacks, x, g) + lift(3, a.whites, x, g)
                                   : lift(0, a.shadows, x, g) + lift(2, a.highlights, x, g);
    const float out = y * std::exp2(shift);
    return perChannel ? std::min(1.0f, shoulder(y, out)) : std::min(1.0f, out);
}

// Production curves, sampled once per render over the 16 EV below white and
// made non-decreasing by a running maximum (no tone reversal, whatever the
// combination); linear interpolation in log2 between samples.
class Curve {
public:
    Curve(const Amounts &amounts, const Gains &gains)
        : amounts_(amounts), gains_(gains),
          luminanceIdentity_(amounts.shadows == 0 && amounts.highlights == 0),
          channelIdentity_(amounts.blacks == 0 && amounts.whites == 0)
    {
        sample(luminance_, false);
        sample(channel_, true);
    }
    bool luminanceIdentity() const { return luminanceIdentity_; }
    bool channelIdentity() const { return channelIdentity_; }
    float luminance(float y) const { return luminanceIdentity_ ? y : read(luminance_, y, false); }
    float channel(float v) const { return channelIdentity_ ? v : read(channel_, v, true); }

private:
    static constexpr int Samples = 4096;
    static constexpr float SpanEv = 16.0f;
    using Table = std::array<float, Samples + 1>;
    void sample(Table &table, bool perChannel) const
    {
        float previous = 0.0f;
        for (int i = 0; i <= Samples; ++i) {
            // The last sample sits just below white: 1 itself is headroom.
            const float y = i == Samples ? std::nextafter(1.0f, 0.0f)
                                         : std::exp2(-SpanEv * (1.0f - float(i) / Samples));
            previous = std::max(previous, transfer(y, amounts_, gains_, perChannel));
            table[size_t(i)] = previous;
        }
    }
    float read(const Table &table, float y, bool perChannel) const
    {
        if (y <= 0.0f || y >= 1.0f) return y;
        const float position = (std::log2(y) + SpanEv) / SpanEv * Samples;
        if (position <= 0.0f) return std::min(transfer(y, amounts_, gains_, perChannel), table[0]);
        const int i = std::min(int(position), Samples - 1);
        const float f = position - float(i);
        return table[size_t(i)] + (table[size_t(i) + 1] - table[size_t(i)]) * f;
    }
    Amounts amounts_;
    Gains gains_;
    bool luminanceIdentity_, channelIdentity_;
    Table luminance_{}, channel_{};
};
}
#endif
