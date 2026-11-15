#ifndef IPR_CORE_TONESTATISTICS_H
#define IPR_CORE_TONESTATISTICS_H

#include "Image.h"

#include <array>
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

// From one log2 luminance per sampled pixel (consumed, partially sorted).
Statistics measure(std::vector<float> log2Luminance);

// From a linear Rec.2020 RGBA image (F32 of four channels), sampled on about one million
// pixels at most.
Statistics measure(const ipr::Image &linearRgba);
}

#endif // IPR_CORE_TONESTATISTICS_H
