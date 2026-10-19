#include "ToneRecovery.h"
#include "ToneRecoveryModel.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

ToneRecovery::Amounts single(int control, float amount)
{
    ToneRecovery::Amounts a;
    (control == 0 ? a.shadows : control == 1 ? a.blacks : control == 2 ? a.highlights : a.whites) = amount;
    return a;
}

// Blacks and positive Whites act per channel; Shadows, Highlights and
// negative Whites on luminance.
float apply(const ToneRecovery::Curve &curve, int control, float y, float amount = 0.0f)
{
    return control == 1 || (control == 3 && amount > 0) ? curve.channel(y) : curve.luminance(y);
}

// Dense log ramp from 2^-16 to just below white.
template <typename F> void ramp(F visit)
{
    for (int i = 0; i <= 20000; ++i) visit(std::exp2(-16.0f + 16.0f * float(i) / 20000.0f) * 0.9999f);
}

void monotone(const ToneRecovery::Amounts &a, const ToneRecovery::Gains &g, const char *message)
{
    const ToneRecovery::Curve curve(a, g);
    float previous = -1.0f;
    ramp([&](float y) {
        const float out = curve.channel(curve.luminance(y));
        check(std::isfinite(out) && out >= 0.0f && out <= 1.0f, "Tone curve leaves [0, 1] below white");
        check(out + 1e-7f >= previous, message);
        previous = out;
    });
}

// Gains at the bounds fitted on the eight scenes.
ToneRecovery::Gains bound(bool high)
{
    ToneRecovery::Gains g;
    for (int i = 0; i < 8; ++i) {
        const auto &c = ToneRecovery::Model::Controls[i];
        if (c.gainStatisticCount) g.value[size_t(i)] = std::exp(high ? c.logGainMax : c.logGainMin);
    }
    return g;
}
}

int main()
{
    try {
        const ToneRecovery::Gains typical = ToneRecovery::gains(ToneRecovery::typicalStatistics());
        const ToneRecovery::Gains gainSets[3] = {typical, bound(false), bound(true)};
        for (const auto &g : gainSets)
            for (int control = 0; control < 4; ++control) {
                const ToneRecovery::Curve zero(single(control, 0), g);
                ramp([&](float y) { check(apply(zero, control, y) == y, "Zero must be identity"); });
                for (float amount : {-1.0f, -.6f, -.25f, .1f, .5f, 1.0f})
                    monotone(single(control, amount), g, "A single control reverses tones");
            }
        // Combinations keep the tone order, whatever the image weights.
        for (const auto &g : gainSets)
            for (float s : {-1.0f, 1.0f}) for (float b : {-1.0f, 1.0f})
                for (float h : {-1.0f, 1.0f}) for (float w : {-1.0f, 1.0f})
                    monotone(ToneRecovery::Amounts{s, b, h, w}, g, "Combined controls reverse tones");

        // Slider direction: positive brightens, negative darkens, never the reverse.
        for (int control = 0; control < 4; ++control)
            for (float y : {.002f, .02f, .18f, .5f}) {
                const ToneRecovery::Curve up(single(control, 1), typical), down(single(control, -1), typical);
                check(apply(up, control, y, 1) + 1e-6f >= y && apply(down, control, y, -1) <= y + 1e-6f,
                      "A control moves tones the wrong way");
            }

        // Graded zones: a control may reach the whole range, but the opposite
        // end of the histogram moves least (in EV), as in Lightroom. Tones
        // close to white are left out: their lift is capped by headroom.
        for (int control = 0; control < 4; ++control) {
            const bool low = control < 2;
            for (float amount : {-1.0f, -.5f, .5f, 1.0f}) {
                const ToneRecovery::Curve curve(single(control, amount), typical);
                const auto shift = [&](float y) { return std::abs(std::log2(std::max(apply(curve, control, y, amount), 1e-9f) / y)); };
                const float far = shift(low ? 0.8f : 0.004f);
                for (float y : low ? std::initializer_list<float>{.004f, .02f, .1f}
                                   : std::initializer_list<float>{.05f, .18f})
                    check(far <= shift(y) + 0.05f, "The opposite end of the histogram must move least");
            }
        }

        const ToneRecovery::Curve blacksDown(single(1, -1), typical), shadowsUp(single(0, 1), typical);
        check(blacksDown.channel(0.0f) == 0.0f && shadowsUp.luminance(0.0f) == 0.0f, "Black must stay black");
        check(blacksDown.channel(0.005f) < 1e-4f, "Blacks -100 must reach a true black point, as in Lightroom");
        check(blacksDown.channel(0.6f) > 0.5f, "Blacks must not crush bright tones");
        // No measured data below 2^-14: the noise floor is not lifted.
        check(shadowsUp.luminance(std::exp2(-16.5f)) < std::exp2(-16.0f), "Shadows lift the sensor noise floor");
        const ToneRecovery::Curve whitesUp(single(3, 1), typical), highlightsDown(single(2, -1), typical);
        check(whitesUp.channel(1.0f) == 1.0f && highlightsDown.luminance(1.5f) == 1.5f,
              "Existing HDR headroom must stay untouched");

        // Brightening a colour: full luminance, channels in gamut, and only the
        // colours that would leave it move toward white.
        const auto luma = [](const float *c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
        for (const auto &colour : {std::array<float, 3>{0.9f, 0.3f, 0.05f}, std::array<float, 3>{0.05f, 0.2f, 0.85f},
                                   std::array<float, 3>{0.2f, 0.2f, 0.2f}}) {
            float rgb[3] = {colour[0], colour[1], colour[2]};
            const float y = luma(rgb), target = std::min(0.95f, y * 3.0f);
            ToneRecovery::brightenTowardWhite(rgb, y, target);
            check(std::abs(luma(rgb) - target) < 1e-4f, "Brightening must reach its luminance target");
            check(std::max({rgb[0], rgb[1], rgb[2]}) < 1.0f, "Brightening must keep channels below white");
            const auto saturation = [](const float *c) {
                const float hi = std::max({c[0], c[1], c[2]}), lo = std::min({c[0], c[1], c[2]});
                return (hi - lo) / std::max(hi, 1e-6f);
            };
            check(saturation(rgb) <= saturation(colour.data()) + 1e-5f, "Brightening must not add saturation");
            check(colour[0] != colour[1] || std::abs(rgb[0] - rgb[1]) < 1e-5f, "A grey must stay grey");
        }
        float small[3] = {0.10f, 0.05f, 0.02f};
        const float ySmall = luma(small);
        ToneRecovery::brightenTowardWhite(small, ySmall, ySmall * 1.5f);
        check(std::abs(small[0] / small[1] - 2.0f) < 1e-4f, "A colour with headroom must keep its ratios");
        float hdr[3] = {1.2f, 0.4f, 0.1f};
        ToneRecovery::brightenTowardWhite(hdr, luma(hdr), 0.99f);
        check(hdr[0] == 1.2f && hdr[1] == 0.4f, "HDR headroom must stay untouched");

        // Darkening: luminance target reached; dark tones keep their ratios;
        // bright colours keep more colour than a plain ratio, never below zero.
        {
            const auto sat = [](const float *c) {
                const float hi = std::max({c[0], c[1], c[2]}), lo = std::min({c[0], c[1], c[2]});
                return (hi - lo) / std::max(hi, 1e-6f);
            };
            float dark[3] = {0.12f, 0.06f, 0.03f};
            const float yDark = luma(dark);
            ToneRecovery::darkenKeepingColour(dark, yDark, yDark * 0.5f);
            check(std::abs(dark[0] / dark[1] - 2.0f) < 1e-4f && std::abs(luma(dark) - yDark * 0.5f) < 1e-5f,
                  "Dark tones must darken with their channel ratios");
            float sky[3] = {0.80f, 0.86f, 0.95f};
            const float ySky = luma(sky), before = sat(sky);
            ToneRecovery::darkenKeepingColour(sky, ySky, ySky * 0.7f);
            check(std::abs(luma(sky) - ySky * 0.7f) < 1e-4f, "Darkening must reach its luminance target");
            check(sat(sky) > before && std::min({sky[0], sky[1], sky[2]}) >= 0.0f,
                  "Darkening a bright sky must recover its colour, as Lightroom does");
            float vivid[3] = {0.95f, 0.70f, 0.01f};
            const float yVivid = luma(vivid);
            ToneRecovery::darkenKeepingColour(vivid, yVivid, yVivid * 0.4f);
            check(std::min({vivid[0], vivid[1], vivid[2]}) >= 0.0f, "Darkening must not create negative channels");
        }

        // Per-image weights: typical images get moderate gains; any image stays
        // within the fitted range.
        for (int i = 0; i < 8; ++i)
            check(typical.value[size_t(i)] > 0.4f && typical.value[size_t(i)] < 2.5f, "Typical image gain");
        for (float level : {-20.0f, 0.0f}) {
            ToneRecovery::Statistics extreme;
            extreme.log2Luminance.fill(level);
            const auto g = ToneRecovery::gains(extreme);
            for (int i = 0; i < 8; ++i) {
                const auto &c = ToneRecovery::Model::Controls[i];
                check(g.value[size_t(i)] >= std::exp(c.logGainMin) * 0.999f &&
                          g.value[size_t(i)] <= std::exp(c.logGainMax) * 1.001f,
                      "Image gains must stay in the fitted range");
            }
        }
        std::vector<float> samples;
        for (int i = 0; i < 1000; ++i) samples.push_back(-12.0f + 12.0f * float(i) / 999.0f);
        const auto measured = ToneRecovery::measure(samples);
        check(std::abs(measured.log2Luminance[0] + 6.0f) < 1e-3f && std::abs(measured.log2Luminance[4] + 1.2f) < 0.05f,
              "Statistics: mean and quantiles");

        std::cout << "Tone controls: identity, tone order alone and combined at every weight, direction, "
                     "graded zones, black point, noise floor, HDR, per-image gains and statistics passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
