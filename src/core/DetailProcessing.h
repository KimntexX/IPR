#ifndef IPR_CORE_DETAILPROCESSING_H
#define IPR_CORE_DETAILPROCESSING_H

#include "LocalValues.h"

#include <opencv2/core.hpp>

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

// Both operations require a CV_32FC3 RGB image in linear Rec.2020 D65.
// HDR values are preserved. Denoising compresses chroma toward neutral when
// needed to keep RGB nonnegative without independently lifting its luminance.
// pixelScale: pixels of this image per pixel of the full image (1 for the master).
// local: per-pixel DenoiseLuminance and DenoiseChrominance added to the settings
// (LocalValues), the sums within [0, 100]. The filters depend on the strength and
// look at the whole image: each runs at the few values the map needs (at most 5,
// so up to 60 bytes a pixel while it renders), then the results mix per pixel; a
// pixel at zero stays untouched.
void reduceNoise(cv::Mat &linearRgb, const DenoiseSettings &settings, double pixelScale = 1.0,
                 const LocalValues::Maps &local = {});
// The sharpening radius is in pixels of the full image. local: per-pixel Sharpening
// added to the amount (LocalValues), the sum within [0, 200].
void sharpen(cv::Mat &linearRgb, const SharpenSettings &settings, double pixelScale = 1.0,
             const LocalValues::Maps &local = {});

}

#endif // IPR_CORE_DETAILPROCESSING_H
