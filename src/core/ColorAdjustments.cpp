#include "ColorAdjustments.h"

#include "WorkingColorSpace.h"

namespace ColorAdjustments {
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
}
