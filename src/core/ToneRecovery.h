#ifndef IPR_CORE_TONERECOVERY_H
#define IPR_CORE_TONERECOVERY_H

#include "ToneStatistics.h"

#include <opencv2/core.hpp>

#include <array>

// Shadows, Blacks, Highlights and Whites (plan B, 18 October 2026). Each control
// lifts or lowers tones by an EV amount read from Lightroom's own transfer
// curves (ToneRecoveryModel.h: knots every EV of log2 input, four slider
// levels, linear in between). Lightroom adapts Shadows, Highlights and positive
// Whites to the image: their lift is multiplied by a per-image gain predicted
// from the image's luminance statistics, bounded to the range seen in fitting.
// Blacks and negative Whites are the same on every image.
// Shadows, Highlights and negative Whites act on luminance (common RGB scale);
// Blacks and positive Whites act on each linear sRGB channel, as Lightroom
// does: its Blacks -100 saturates colours (x1.40), which a common scale cannot
// reproduce. Values at or above 1 (HDR headroom) are left untouched.
namespace ToneRecovery {
struct Amounts { float shadows = 0, blacks = 0, highlights = 0, whites = 0; };

inline bool identity(const Amounts &a)
{
    return a.shadows == 0 && a.blacks == 0 && a.highlights == 0 && a.whites == 0;
}

// Applies the four controls (slider amounts in [-1, 1]) to a linear Rec.2020
// RGB image (CV_32FC3) in place, weighted by the image's statistics.
void apply(cv::Mat &rgb, const Amounts &amounts, const Statistics &statistics);

// Building blocks of apply(), exposed for the unit tests.

// Per-image gains, indexed like Model::Controls.
struct Gains { std::array<float, 8> value{1, 1, 1, 1, 1, 1, 1, 1}; };

// Median statistics of the fitting scenes, for images without measurement.
Statistics typicalStatistics();
Gains gains(const Statistics &statistics);

// EV lift of one control (0 Shadows, 1 Blacks, 2 Highlights, 3 Whites) at
// x = log2(input) for a slider amount in [-1, 1].
float lift(int control, float amount, float x, const Gains &gains);

// Soft shoulder of a brightening from y (< 1) to out: tones above the knee
// approach 1 without reaching it; untouched or darkened tones pass through.
float shoulder(float y, float out, float knee = 0.70f);

// Analytic transfer of the luminance controls or of the channel controls,
// before monotone sampling.
float transfer(float y, const Amounts &a, const Gains &g, bool perChannel);

// Brightening of a linear sRGB colour of luminance y to the luminance target,
// whitening a colour that would leave the gamut.
void brightenTowardWhite(float rgb[3], float y, float target);

// Darkening of a colour of luminance y to the luminance target, keeping and
// amplifying the colour of bright tones.
void darkenKeepingColour(float rgb[3], float y, float target);

// Production curves, sampled once per render over the 16 EV below white and
// made non-decreasing by a running maximum (no tone reversal, whatever the
// combination); linear interpolation in log2 between samples.
class Curve {
public:
    Curve(const Amounts &amounts, const Gains &gains);
    bool luminanceIdentity() const { return luminanceIdentity_; }
    bool channelIdentity() const { return channelIdentity_; }
    float luminance(float y) const;
    float channel(float v) const;

private:
    static constexpr int Samples = 4096;
    static constexpr float SpanEv = 16.0f;
    using Table = std::array<float, Samples + 1>;
    void sample(Table &table, bool perChannel) const;
    float read(const Table &table, float y, bool perChannel) const;
    Amounts amounts_;
    Gains gains_;
    bool luminanceIdentity_, channelIdentity_;
    Table luminance_{}, channel_{};
};
}

#endif // IPR_CORE_TONERECOVERY_H
