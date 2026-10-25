#include "ColorAdjustments.h"

#include "WorkingColorSpace.h"

#include <algorithm>

namespace ColorAdjustments {
namespace {
template <typename Values>
bool anyNonZero(const Values &values)
{
    return std::any_of(values.cbegin(), values.cend(), [](int value) { return value != 0; });
}
}

void toGrayscale(cv::Mat &linearRgb)
{
    for (int y = 0; y < linearRgb.rows; ++y) {
        auto *row = linearRgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < linearRgb.cols; ++x) {
            const float value = WorkingColor::luminance(row[x]);
            row[x] = {value, value, value};
        }
    }
}

void applyMixer(cv::Mat &linearRgb, const ColorMixer::Parameters &mixer)
{
    const bool hasColorMixer = anyNonZero(mixer.hue) || anyNonZero(mixer.saturation) || anyNonZero(mixer.luminance);
    if (!hasColorMixer || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    // Pixels are independent: rows run in parallel.
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &range) {
        for (int y = range.start; y < range.end; ++y) {
            cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x) row[x] = ColorMixer::apply(row[x], mixer);
        }
    });
}
}
