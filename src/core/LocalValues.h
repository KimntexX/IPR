#ifndef IPR_CORE_LOCALVALUES_H
#define IPR_CORE_LOCALVALUES_H

#include <opencv2/core.hpp>

#include <array>

// Local values of the in-chain local adjustments (ADR-0021): for a setting, one
// float per pixel of the rendered image (CV_32FC1), in the units of its global
// slider: the masks' values weighted by their coverage, summed. A module applies
// at each pixel its global value plus the local one. An empty map changes
// nothing: the module keeps its global path, identical to the bit.
namespace LocalValues {
enum Parameter { Exposure, Contrast, Saturation, Vibrance, ParameterCount };

struct Maps
{
    std::array<cv::Mat, ParameterCount> values;
    cv::Mat &operator[](Parameter parameter) { return values[size_t(parameter)]; }
    const cv::Mat &operator[](Parameter parameter) const { return values[size_t(parameter)]; }
};

// A map a module can read for an image of this size: empty, or one float per pixel.
inline bool fits(const cv::Mat &map, const cv::Size &size)
{
    return map.empty() || (map.type() == CV_32FC1 && map.size() == size);
}
}

#endif // IPR_CORE_LOCALVALUES_H
