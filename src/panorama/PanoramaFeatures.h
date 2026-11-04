#ifndef IPR_PANORAMA_PANORAMAFEATURES_H
#define IPR_PANORAMA_PANORAMAFEATURES_H

#include <opencv2/core.hpp>

#include <array>
#include <vector>

// What two views of a panorama share, found from the pixels alone: corners on two scales,
// each described by the normalized patch around it, turned along its local gradient (the
// multi-scale oriented patches of Brown, Szeliski and Winder). The views need not be close:
// only a part of each frame is common, so a search around a global shift cannot start.
namespace PanoramaFeatures {

struct Feature
{
    cv::Point2f position;                // pixels of the image given
    std::array<float, 64> descriptor{};  // mean 0, deviation 1
};

struct Options
{
    int cornersPerLevel = 2500;
    int levels = 2;              // each next one is half of the previous
    double ratio = 0.8;          // of the closest to the second closest descriptor
};

struct Correspondence
{
    cv::Point2f a;
    cv::Point2f b;
};

// image: CV_32FC1, a perceptual scale. Deterministic.
std::vector<Feature> detect(const cv::Mat &image, const Options &options = {});

// The pairs that choose each other, and clearly so against the second candidate.
std::vector<Correspondence> match(const std::vector<Feature> &a, const std::vector<Feature> &b, const Options &options = {});

} // namespace PanoramaFeatures

#endif // IPR_PANORAMA_PANORAMAFEATURES_H
