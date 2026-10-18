#include "ToneRecovery.h"
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

// Dense log ramp from 2^-16 to just below white.
template <typename F> void ramp(F visit)
{
    for (int i = 0; i <= 20000; ++i) visit(std::exp2(-16.0f + 16.0f * float(i) / 20000.0f) * 0.9999f);
}

void monotone(const ToneRecovery::Amounts &a, const char *message)
{
    float previous = -1.0f;
    ramp([&](float y) {
        const float out = ToneRecovery::target(y, a);
        check(std::isfinite(out) && out >= 0.0f && out <= 1.0f, "Tone curve leaves [0, 1] below white");
        check(out + 1e-7f >= previous, message);
        previous = out;
    });
}
}

int main()
{
    try {
        for (int control = 0; control < 4; ++control) {
            ramp([&](float y) { check(ToneRecovery::target(y, single(control, 0)) == y, "Zero must be identity"); });
            for (float amount : {-1.0f, -.5f, .5f, 1.0f})
                monotone(single(control, amount), "A single control reverses tones");
            // Slider progression: stronger settings move each tone further, never back.
            for (float y : {.001f, .01f, .05f, .18f, .5f, .9f}) {
                float up = y, down = y;
                for (int v = 1; v <= 100; ++v) {
                    const float plus = ToneRecovery::target(y, single(control, v / 100.0f));
                    const float minus = ToneRecovery::target(y, single(control, -v / 100.0f));
                    check(plus + 1e-7f >= up && minus <= down + 1e-7f, "Slider progression is not monotone");
                    up = plus; down = minus;
                }
            }
        }
        // Combinations of overlapping zones keep the tone order.
        for (float s : {-1.0f, 1.0f}) for (float b : {-1.0f, 1.0f})
            for (float h : {-1.0f, 1.0f}) for (float w : {-1.0f, 1.0f})
                monotone(ToneRecovery::Amounts{s, b, h, w}, "Combined controls reverse tones");

        using ToneRecovery::target;
        check(target(0.0f, single(0, 1)) == 0.0f && target(0.0f, single(1, 1)) == 0.0f, "Black must stay black");
        check(target(0.005f, single(1, -1)) == 0.0f, "Negative blacks must reach a true black point");
        check(target(0.6f, single(1, -1)) > 0.5f, "Blacks must not crush bright tones");
        // Each control stays in its zone of the histogram (fixed ranges, plan A).
        for (int control = 0; control < 4; ++control) {
            const bool low = control < 2;
            for (float amount : {-1.0f, 1.0f}) {
                const float far = low ? 0.9f : 0.003f;
                const float out = target(far, single(control, amount));
                check(std::abs(std::log2(out / far)) < (low ? 0.05f : 0.1f), "A control leaks into the opposite end");
            }
        }
        check(target(0.6f, single(2, 1)) > 0.6f && target(0.95f, single(2, 1)) - 0.95f < target(0.6f, single(2, 1)) - 0.6f,
              "Highlights must hand the last EV over to Whites");
        // Pairs sharing an end of the histogram combine without reversing tones.
        for (float u : {-.5f, .5f}) for (float v : {-.5f, .5f}) {
            monotone(ToneRecovery::Amounts{u, v, 0, 0}, "Shadows and Blacks pair reverses tones");
            monotone(ToneRecovery::Amounts{0, 0, u, v}, "Highlights and Whites pair reverses tones");
        }
        check(target(1.0f, single(3, 1)) == 1.0f && target(1.5f, single(2, -1)) == 1.5f,
              "Existing HDR headroom must stay untouched");
        // Lifts reach white smoothly through the shoulder, never beyond it.
        check(target(0.8f, single(3, 1)) <= 1.0f && target(0.8f, single(3, 1)) > 0.8f &&
              target(0.3f, single(3, 1)) < target(0.5f, single(3, 1)), "Whites shoulder");

        std::cout << "Tone controls: identity, tone order alone and combined, progression, black point, "
                     "zones, HDR and shoulder passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
