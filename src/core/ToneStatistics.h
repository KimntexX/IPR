#ifndef IPR_CORE_TONESTATISTICS_H
#define IPR_CORE_TONESTATISTICS_H
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Luminance statistics of an image at neutral settings, measured once when it
// opens and kept with it (cache, proxy, export master): Shadows, Highlights and
// Whites adapt their strength to them, as Lightroom does, while preview and
// export use the same values.
namespace ToneRecovery {
struct Statistics
{
    // log2 of linear luminance: mean, then quantiles 1, 5, 10, 90, 95, 99.9 %
    // (order of ToneRecovery::Model::Statistic).
    std::array<float, 7> log2Luminance{};
};

// log2Luminance: one value per sampled pixel; consumed (partially sorted).
inline Statistics measure(std::vector<float> log2Luminance)
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
}
#endif // IPR_CORE_TONESTATISTICS_H
