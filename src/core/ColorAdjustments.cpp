#include "ColorAdjustments.h"

#include "WorkingColorSpace.h"

#include <algorithm>

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

void showWeights(cv::Mat &linearRgba, const cv::Mat &weights)
{
    if (linearRgba.type() != CV_32FC4 || weights.type() != CV_32FC1 || linearRgba.size() != weights.size())
        return;
    for (int y = 0; y < linearRgba.rows; ++y) {
        auto *row = linearRgba.ptr<cv::Vec4f>(y);
        const float *weight = weights.ptr<float>(y);
        for (int x = 0; x < linearRgba.cols; ++x) {
            const cv::Vec3f rgb(row[x][0], row[x][1], row[x][2]);
            const float grey = WorkingColor::luminance(rgb);
            const float w = std::clamp(weight[x], 0.0f, 1.0f);
            for (int c = 0; c < 3; ++c) row[x][c] = grey + w * (rgb[c] - grey);
        }
    }
}
}
