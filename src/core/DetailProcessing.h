#ifndef IPR_CORE_DETAILPROCESSING_H
#define IPR_CORE_DETAILPROCESSING_H

#include "Image.h"
#include "LocalValues.h"

#include <vector>

namespace DetailProcessing {

struct DenoiseSettings
{
    int luminance = 0;
    int chrominance = 0;
};

// The law of the editor's single noise reduction slider. Amount is an integer in
// [0,100]; invalid input leaves settings unchanged and returns false.
// L=amount; C=round(100*(1-(1-amount/100)^6)). Zero is exact identity.
// An ergonomic mapping, NOT a Lightroom-equivalent strength calibration.
bool denoiseFromAmount(int amount, DenoiseSettings &settings);

struct SharpenSettings
{
    int amount = 0;
    float radius = 1.0f;
    int masking = 0;
};

// Both operations require a F32 of three channels RGB image in linear Rec.2020 D65.
// HDR values are preserved. Denoising compresses chroma toward neutral when
// needed to keep RGB nonnegative without independently lifting its luminance.
// pixelScale: pixels of this image per pixel of the full image (1 for the master).
// local: per-pixel DenoiseLuminance and DenoiseChrominance added to the settings
// (LocalValues), the sums within [0, 100]. The filters depend on the strength and
// look at the whole image: each runs at the few values the map needs (at most 5,
// so up to 60 bytes a pixel while it renders), then the results mix per pixel; a
// pixel at zero stays untouched.
// What the noise reduction measures on the whole image: kept so that a part of it renders alone,
// the same pixels. Valid after a render without local values that recorded them.
struct NoiseMeasures
{
    bool valid = false;
    float lumaSigma = 0.0f;
    float positiveScale = 1.0f, negativeScale = 1.0f;   // the balance of the luminance changes
    float guideSigma = 0.0f;                            // the colour guide's noise
    std::vector<float> levelCenters, levelMedians;      // the colour edges' typical gradient per level
};

// record: receives the measures of this render (whole image, no local value). given: the whole
// image's measures, used instead of measuring this image, a part of it grown by reduceNoiseReach.
void reduceNoise(ipr::Image &linearRgb, const DenoiseSettings &settings, double pixelScale = 1.0,
                 const LocalValues::Maps &local = {}, NoiseMeasures *record = nullptr,
                 const NoiseMeasures *given = nullptr);
// How far around a pixel the noise reduction reads, in pixels of this image.
int reduceNoiseReach(const DenoiseSettings &settings, double pixelScale = 1.0);
// Unsharp mask of the sRGB-encoded luminance, calibrated on Lightroom's sharpening (same amount,
// radius and masking give the same edges; at masking 0 IPR still spares the grain).
// The sharpening radius is in pixels of the full image. local: per-pixel Sharpening
// added to the amount (LocalValues), the sum within [0, 200]. noiseSigma: the noise the
// sharpening measures on its whole input (sharpenNoiseSigma), given when this image is a part
// of it; measured on this image otherwise.
void sharpen(ipr::Image &linearRgb, const SharpenSettings &settings, double pixelScale = 1.0,
             const LocalValues::Maps &local = {}, const float *noiseSigma = nullptr);
// How far around a pixel the sharpening reads, in pixels of this image.
int sharpenReach(const SharpenSettings &settings, double pixelScale = 1.0);

// The sharpening's noise measure, taken in parts: samples on the rows of the input that are
// multiples of sharpenNoiseStep of its size, each read with sharpenNoiseReach rows around it.
// A band holds the full width of its rows, from bandTop of an input of planeHeight rows; a
// sampled row whose neighbourhood the band does not hold whole is left to another band. The
// samples of all bands give sharpenNoiseSigma, equal to the measure on the whole input.
int sharpenNoiseStep(const ipr::Size &plane);
int sharpenNoiseReach();
void appendSharpenNoiseSamples(const ipr::Image &linearRgbBand, int bandTop, int planeHeight, int step,
                               std::vector<float> &samples);
float sharpenNoiseSigma(std::vector<float> samples);

}

#endif // IPR_CORE_DETAILPROCESSING_H
