#include "ToneRecovery.h"

#include "ToneRecoveryModel.h"
#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace ToneRecovery {
Statistics typicalStatistics()
{
    Statistics s;
    for (int i = 0; i < Model::StatisticCount; ++i) s.log2Luminance[size_t(i)] = Model::TypicalStatistics[i];
    return s;
}

Gains gains(const Statistics &statistics)
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

float lift(int control, float amount, float x, const Gains &gains)
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

float shoulder(float y, float out, float knee)
{
    if (out <= y || out <= knee) return out;
    const float base = std::max(y, knee);
    return base + (1.0f - base) * std::tanh((out - base) / (1.0f - base));
}

// Channels roll off near white through the shoulder, as Lightroom does: a hard
// cap at 1 clipped about ten times more channels than Lightroom with Whites
// +25. The luminance lift is capped at white; apply() then fits the colour.
float transfer(float y, const Amounts &a, const Gains &g, bool perChannel)
{
    if (y <= 0.0f || y >= 1.0f) return y;
    const float x = std::log2(std::max(y, std::exp2(Model::FirstKnotEv)));
    // Whites act per channel when lifting and on luminance when lowering: in
    // Lightroom's renders, negative Whites keep colour ratios (saturation gap
    // -0.026 vs -0.040 per channel at -100), positive Whites do not.
    const float whitesChannel = a.whites > 0 ? a.whites : 0.0f;
    const float whitesLuminance = a.whites < 0 ? a.whites : 0.0f;
    const float shift = perChannel ? lift(1, a.blacks, x, g) + lift(3, whitesChannel, x, g)
                                   : lift(0, a.shadows, x, g) + lift(2, a.highlights, x, g)
                                         + lift(3, whitesLuminance, x, g);
    const float out = y * std::exp2(shift);
    // Luminance lifts roll off from 0.80 and stay just below white; the colour
    // is then fitted by brightenTowardWhite().
    return perChannel ? std::min(1.0f, shoulder(y, out)) : std::min(0.9999f, shoulder(y, out, 0.80f));
}

// The luminance is reached in full, and a colour that would leave the gamut is
// pulled toward white of that luminance just enough for its largest channel to
// stay in the soft shoulder. Lightroom whitens bright colours this way
// (Highlights +100, top quarter saturation 0.20 vs 0.42 when the colour was
// preserved and the lift held back, on 6V1A9950). Colours with a channel
// already at or above 1 (headroom) are left untouched.
void brightenTowardWhite(float rgb[3], float y, float target)
{
    const float peak = std::max({rgb[0], rgb[1], rgb[2]});
    if (peak >= 1.0f || y <= 0.0f || target <= y) return;
    const float scale = target / y;
    float m = 0.0f;
    for (int c = 0; c < 3; ++c) { rgb[c] *= scale; m = std::max(m, rgb[c]); }
    // 0.995 linear is sRGB code 254: a lifted colour nears white but does not
    // clip on an 8-bit display (tanh would also round to 1 in float).
    const float fit = std::min(shoulder(peak, m), 0.995f);
    if (m <= fit) return;
    const float t = std::clamp((fit - target) / std::max(m - target, 1e-9f), 0.0f, 1.0f);
    for (int c = 0; c < 3; ++c) rgb[c] = target + t * (rgb[c] - target);
}

// Any linear RGB with the same white and luminance weights. Dark tones keep
// their channel ratios; from y = 0.3 to 0.8 and above, the colour's absolute
// chroma is kept and then amplified in proportion to the darkening (factor 2.5
// at full), as Lightroom recovers bright skies: with ratios only, Highlights
// -100 left bright tones 0.20 less saturated than Lightroom (0.12 now; Whites
// -100 0.08 -> 0.01). Falls back to ratios if a channel would go negative.
void darkenKeepingColour(float rgb[3], float y, float target)
{
    if (y <= 0.0f || target >= y) return;
    constexpr float start = 0.3f, full = 0.8f, amplitude = 2.5f;
    const float ratio = target / y;
    const float alpha = amplitude * std::clamp((y - start) / (full - start), 0.0f, 1.0f);
    const float factor = (1.0f - alpha) * ratio + alpha;
    float out[3];
    for (int c = 0; c < 3; ++c) {
        out[c] = target + factor * (rgb[c] - y);
        if (out[c] < 0.0f) { for (int k = 0; k < 3; ++k) rgb[k] *= ratio; return; }
    }
    // The amplified colour never crosses white unless the channel already did.
    for (int c = 0; c < 3; ++c) rgb[c] = std::min(out[c], std::max(rgb[c], 0.9999f));
}

Curve::Curve(const Amounts &amounts, const Gains &gains)
    : amounts_(amounts), gains_(gains),
      luminanceIdentity_(amounts.shadows == 0 && amounts.highlights == 0 && amounts.whites >= 0),
      channelIdentity_(amounts.blacks == 0 && amounts.whites <= 0)
{
    sample(luminance_, false);
    sample(channel_, true);
}

float Curve::luminance(float y) const { return luminanceIdentity_ ? y : read(luminance_, y, false); }
float Curve::channel(float v) const { return channelIdentity_ ? v : read(channel_, v, true); }

void Curve::sample(Table &table, bool perChannel) const
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

float Curve::read(const Table &table, float y, bool perChannel) const
{
    if (y <= 0.0f || y >= 1.0f) return y;
    const float position = (std::log2(y) + SpanEv) / SpanEv * Samples;
    if (position <= 0.0f) return std::min(transfer(y, amounts_, gains_, perChannel), table[0]);
    const int i = std::min(int(position), Samples - 1);
    const float f = position - float(i);
    return table[size_t(i)] + (table[size_t(i) + 1] - table[size_t(i)]) * f;
}

// Shadows, Highlights and negative Whites scale RGB by a luminance ratio: a
// brightening reaches its luminance and pulls a colour that would leave the
// sRGB delivery gamut toward white; a darkening keeps, then amplifies, the
// colour of bright tones. Blacks and positive Whites then act on each linear
// sRGB channel, where Lightroom's curves were measured: in the wider Rec.2020,
// Blacks -100 oversaturated colours (0.99 vs 0.92 on 6V1A0037).
void apply(cv::Mat &image, const Amounts &amounts, const Statistics &statistics)
{
    if (identity(amounts)) return;
    const Curve toneCurve(amounts, gains(statistics));
    for (int y = 0; y < image.rows; ++y) {
        cv::Vec3f *row = image.ptr<cv::Vec3f>(y);
        for (int x = 0; x < image.cols; ++x) {
            cv::Vec3f &pixel = row[x];
            if (!toneCurve.luminanceIdentity()) {
                const float luminance = WorkingColor::nonNegativeLuminance(pixel);
                const float target = toneCurve.luminance(luminance);
                if (target > luminance && luminance > 0.0f) {
                    const cv::Vec3d delivery = WorkingColor::toSrgb() * cv::Vec3d(pixel);
                    float rgb[3] = {float(delivery[0]), float(delivery[1]), float(delivery[2])};
                    brightenTowardWhite(rgb, luminance, target);
                    pixel = cv::Vec3f(WorkingColor::fromSrgb() * cv::Vec3d(rgb[0], rgb[1], rgb[2]));
                } else if (target < luminance) {
                    float rgb[3] = {pixel[0], pixel[1], pixel[2]};
                    darkenKeepingColour(rgb, luminance, target);
                    pixel = cv::Vec3f(rgb[0], rgb[1], rgb[2]);
                }
            }
            if (!toneCurve.channelIdentity()) {
                cv::Vec3d srgb = WorkingColor::toSrgb() * cv::Vec3d(pixel);
                for (int c = 0; c < 3; ++c) srgb[c] = toneCurve.channel(float(srgb[c]));
                pixel = cv::Vec3f(WorkingColor::fromSrgb() * srgb);
            }
        }
    }
}
}
