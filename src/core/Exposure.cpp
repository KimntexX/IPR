#include "Exposure.h"
#include "LocalValues.h"
#include "Parallel.h"
#include "Pixels.h"

#include <cmath>

namespace Exposure {
namespace {
float factor(double ev) { return static_cast<float>(std::exp2(ev)); }
}

void apply(ipr::Image &linearRgb, float exposureEv, const ipr::Image &localEv)
{
    if (localEv.empty() || !LocalValues::fits(localEv, linearRgb.size())) {
        // As OpenCV scaled a Mat: the product plus a zero shift, so that -0 comes out as +0.
        if (exposureEv != 0.0f) ipr::mapInPlace(linearRgb, [f = factor(double(exposureEv))](float v) { return v * f + 0.0f; });
        return;
    }
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *row = linearRgb.mutableRow<ipr::Vec3f>(y);
            const float *local = localEv.row<float>(y);
            for (int x = 0; x < linearRgb.width(); ++x) {
                const double ev = double(exposureEv) + double(local[x]);
                if (ev != 0.0) row[x] *= factor(ev);
            }
        }
    });
}
}
