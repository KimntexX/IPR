#include "ToneStatistics.h"

#include "WorkingColorSpace.h"
#include "Types.h"

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

Statistics measure(const ipr::Image &linearRgba)
{
    const size_t total = size_t(linearRgba.width()) * size_t(linearRgba.height());
    const int stride = std::max(1, int(std::sqrt(double(total) / 1.0e6)));
    std::vector<float> values;
    values.reserve(total / size_t(stride * stride) + size_t(linearRgba.height()));
    for (int y = 0; y < linearRgba.height(); y += stride) {
        const auto *row = linearRgba.row<ipr::Vec4f>(y);
        for (int x = 0; x < linearRgba.width(); x += stride) {
            const ipr::Vec3f rgb(row[x][0], row[x][1], row[x][2]);
            values.push_back(std::log2(std::max(WorkingColor::luminance(rgb), 0x1p-20f)));
        }
    }
    return measure(std::move(values));
}
}
