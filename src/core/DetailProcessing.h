#ifndef IPR_CORE_DETAILPROCESSING_H
#define IPR_CORE_DETAILPROCESSING_H

#include <opencv2/core.hpp>

namespace DetailProcessing {

struct DenoiseSettings
{
    int luminance = 0;
    int chrominance = 0;
};

struct SharpenSettings
{
    int amount = 0;
    float radius = 1.0f;
    int masking = 0;
};

// Both operations require a CV_32FC3 RGB image in linear Rec.2020 D65.
// HDR values are preserved. Denoising compresses chroma toward neutral when
// needed to keep RGB nonnegative without independently lifting its luminance.
void reduceNoise(cv::Mat &linearRgb, const DenoiseSettings &settings);
void sharpen(cv::Mat &linearRgb, const SharpenSettings &settings);

}

#endif // IPR_CORE_DETAILPROCESSING_H
