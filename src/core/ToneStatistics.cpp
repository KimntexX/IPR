#include "ToneStatistics.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>

namespace ToneRecovery {
Statistics measure(std::vector<float> log2Luminance)
{
    Statistics s;
    if (log2Luminance.empty()) return s;
    double sum = 0.0;
    for (float v : log2Luminance) sum += v;
    s.log2Luminance[0] = float(sum / double(log2Luminance.size()));
    const float quantiles[6] = {0.01f, 0.05f, 0.10f, 0.90f, 0.95f, 0.999f};
    for (int i = 0; i < 6; ++i) {
        const auto k = std::min(log2Luminance.size() - 1,
                                size_t(quantiles[i] * float(log2Luminance.size() - 1) + 0.5f));
        std::nth_element(log2Luminance.begin(), log2Luminance.begin() + std::ptrdiff_t(k), log2Luminance.end());
        s.log2Luminance[size_t(i) + 1] = log2Luminance[k];
    }
    return s;
}

Statistics measure(const cv::Mat &linearRgba)
{
    const int stride = std::max(1, int(std::sqrt(double(linearRgba.total()) / 1.0e6)));
    std::vector<float> values;
    values.reserve(linearRgba.total() / size_t(stride * stride) + linearRgba.rows);
    for (int y = 0; y < linearRgba.rows; y += stride) {
        const auto *row = linearRgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < linearRgba.cols; x += stride) {
            const cv::Vec3f rgb(row[x][0], row[x][1], row[x][2]);
            values.push_back(std::log2(std::max(WorkingColor::luminance(rgb), 0x1p-20f)));
        }
    }
    return measure(std::move(values));
}
}
