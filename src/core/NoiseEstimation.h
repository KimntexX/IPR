#ifndef IPR_CORE_NOISEESTIMATION_H
#define IPR_CORE_NOISEESTIMATION_H

#include <opencv2/core.hpp>
#include <array>

// Effective noise of developed, linear Rec.2020 Y/Co/Cg, not sensor DN.
namespace NoiseEstimation {
struct Model
{
    float slope = 0;
    float intercept = 0;
    float minimumSignal = 0;
    float maximumSignal = 0;
    int patches = 0;
    bool affine = false;
};
struct Result
{
    std::array<Model, 3> planes{};
    bool valid = false;
};

// Bounded sampling of native adjacent pixels; never resize to estimate noise.
// A small bounded cache holds only statistics, not image buffers.
Result estimate(const cv::Mat &luma, const cv::Mat &co, const cv::Mat &cg);
// Shared Haar/MAD primitive used inside each tile; not a complete noise model.
float haarSigma(const cv::Mat &plane);
float sigma(const Model &model, float luminance);
}

#endif // IPR_CORE_NOISEESTIMATION_H
