#include "StackMerge.h"

#include "Filters.h"
#include "Neighbourhood.h"
#include "Parallel.h"


#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace StackMerge {
namespace {
// MAD of a Gaussian to its standard deviation.
constexpr float madToSigma = 1.4826f;
// A floor on the spread, so that identical samples do not exclude a rounding difference.
constexpr float spreadFloor = 1e-5f;
// A sample's weight falls from 1 at the limit to 0 at this multiple of it.
constexpr float fadeEnd = 1.5f;

// The smoothed luminance the decisions read. One pixel's noise then does not flip them,
// which would leave a toothed line along a contrasted edge.
ipr::Image decisionLuminance(const ipr::Image &frame)
{
    ipr::Image luminance = ipr::Image::create(frame.size(), ipr::Depth::F32, 1);
    for (int y = 0; y < frame.height(); ++y) {
        const ipr::Vec3f *in = frame.row<ipr::Vec3f>(y);
        float *out = luminance.mutableRow<float>(y);
        for (int x = 0; x < frame.width(); ++x)
            out[x] = 0.25f * in[x][0] + 0.5f * in[x][1] + 0.25f * in[x][2];
    }
    return ipr::boxMean(luminance, ipr::Size(decisionWindow, decisionWindow), ipr::BlurBorder::LastPixel);
}
}

bool merge(const std::vector<ipr::Image> &frames, double rejectionSigma, ipr::Image &merged, Statistics *statistics)
{
    if (frames.empty())
        return false;
    const ipr::Size size = frames.front().size();
    for (const ipr::Image &frame : frames)
        if (frame.empty() || !(frame.depth() == ipr::Depth::F32 && frame.channels() == 3) || frame.size() != size)
            return false;
    merged = ipr::Image::create(size, ipr::Depth::F32, 3);
    const int count = int(frames.size());
    const bool rejecting = rejectionSigma > 0.0 && count >= minimumFramesForRejection;
    const float limit = float(rejectionSigma);
    std::atomic<quint64> rejected{0};

    std::vector<ipr::Image> luminance(frames.size());
    if (rejecting)
        ipr::parallelRows(count, [&](int first, int last) {
            for (int i = first; i < last; ++i)
                luminance[size_t(i)] = decisionLuminance(frames[size_t(i)]);
        });

    ipr::parallelRows(size.height, [&](int first, int last) {
        std::vector<const ipr::Vec3f *> rows(frames.size());
        std::vector<const float *> luminanceRows(frames.size());
        std::vector<float> order(frames.size());
        quint64 rejectedHere = 0;
        for (int y = first; y < last; ++y) {
            for (int i = 0; i < count; ++i) {
                rows[size_t(i)] = frames[size_t(i)].row<ipr::Vec3f>(y);
                if (rejecting)
                    luminanceRows[size_t(i)] = luminance[size_t(i)].row<float>(y);
            }
            ipr::Vec3f *out = merged.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < size.width; ++x) {
                float allowed = std::numeric_limits<float>::infinity();
                float median = 0.f;
                if (rejecting) {
                    for (int i = 0; i < count; ++i)
                        order[size_t(i)] = luminanceRows[size_t(i)][x];
                    const auto middle = order.begin() + count / 2;
                    std::nth_element(order.begin(), middle, order.end());
                    median = *middle;
                    for (int i = 0; i < count; ++i)
                        order[size_t(i)] = std::abs(luminanceRows[size_t(i)][x] - median);
                    std::nth_element(order.begin(), middle, order.end());
                    allowed = limit * std::max(madToSigma * *middle, spreadFloor);
                }
                ipr::Vec3f sum(0.f, 0.f, 0.f);
                float weights = 0.f;
                for (int i = 0; i < count; ++i) {
                    float weight = 1.f;
                    if (rejecting) {
                        const float distance = std::abs(luminanceRows[size_t(i)][x] - median);
                        if (distance > allowed) {
                            ++rejectedHere;
                            weight = std::max(0.f, (fadeEnd * allowed - distance) / ((fadeEnd - 1.f) * allowed));
                        }
                    }
                    sum += rows[size_t(i)][x] * weight;
                    weights += weight;
                }
                // The median's own frame keeps its whole weight, so weights is never zero.
                out[x] = sum * (1.f / weights);
            }
        }
        rejected += rejectedHere;
    });
    if (statistics) {
        statistics->samples = quint64(size.area()) * quint64(count);
        statistics->rejected = rejected.load();
    }
    return true;
}

} // namespace StackMerge
