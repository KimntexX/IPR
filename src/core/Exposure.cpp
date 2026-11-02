#include "Exposure.h"
#include "LocalValues.h"

#include <cmath>

namespace Exposure {
namespace {
float factor(double ev) { return static_cast<float>(std::exp2(ev)); }
}

void apply(cv::Mat &linearRgb, float exposureEv, const cv::Mat &localEv)
{
    if (localEv.empty() || !LocalValues::fits(localEv, linearRgb.size())) {
        if (exposureEv != 0.0f) linearRgb *= factor(double(exposureEv));
        return;
    }
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            const float *local = localEv.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const double ev = double(exposureEv) + double(local[x]);
                if (ev != 0.0) row[x] *= factor(ev);
            }
        }
    });
}
}
