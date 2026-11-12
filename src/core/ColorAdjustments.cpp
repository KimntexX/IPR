#include "ColorAdjustments.h"

#include "WorkingColorSpace.h"
#include "Types.h"

#include <algorithm>

namespace ColorAdjustments {
void toGrayscale(ipr::Image &linearRgb)
{
    for (int y = 0; y < linearRgb.height(); ++y) {
        auto *row = linearRgb.mutableRow<ipr::Vec3f>(y);
        for (int x = 0; x < linearRgb.width(); ++x) {
            const float value = WorkingColor::luminance(row[x]);
            row[x] = {value, value, value};
        }
    }
}

void showWeights(ipr::Image &linearRgba, const ipr::Image &weights)
{
    if (!(linearRgba.depth() == ipr::Depth::F32 && linearRgba.channels() == 4) || !(weights.depth() == ipr::Depth::F32 && weights.channels() == 1) || linearRgba.size() != weights.size())
        return;
    for (int y = 0; y < linearRgba.height(); ++y) {
        auto *row = linearRgba.mutableRow<ipr::Vec4f>(y);
        const float *weight = weights.row<float>(y);
        for (int x = 0; x < linearRgba.width(); ++x) {
            const ipr::Vec3f rgb(row[x][0], row[x][1], row[x][2]);
            const float grey = WorkingColor::luminance(rgb);
            const float w = std::clamp(weight[x], 0.0f, 1.0f);
            for (int c = 0; c < 3; ++c) row[x][c] = grey + w * (rgb[c] - grey);
        }
    }
}
}
