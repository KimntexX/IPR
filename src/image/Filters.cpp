#include "Filters.h"

#include "Parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ipr {

namespace {

// Where a position beyond [0, length) reads, mirrored without repeating the edge.
int mirrored(int position, int length)
{
    if (length == 1)
        return 0;
    const int period = 2 * (length - 1);
    int wrapped = position % period;
    if (wrapped < 0)
        wrapped += period;
    return wrapped < length ? wrapped : period - wrapped;
}

// The kernel along a row, channels side by side. Interior pixels run without any test, which is
// what lets a compiler vectorize this loop; the borders go through the mirror.
void blurRow(const float *in, float *out, int width, int channels, const std::vector<float> &half)
{
    const int radius = int(half.size()) - 1;
    const int interiorFirst = std::min(radius, width);
    const int interiorLast = std::max(interiorFirst, width - radius);
    const auto bordered = [&](int x) {
        for (int c = 0; c < channels; ++c) {
            float sum = half[0] * in[x * channels + c];
            for (int k = 1; k <= radius; ++k)
                sum += half[size_t(k)] * (in[mirrored(x - k, width) * channels + c] + in[mirrored(x + k, width) * channels + c]);
            out[x * channels + c] = sum;
        }
    };
    for (int x = 0; x < interiorFirst; ++x)
        bordered(x);
    const int firstValue = interiorFirst * channels, lastValue = interiorLast * channels;
    for (int v = firstValue; v < lastValue; ++v)
        out[v] = half[0] * in[v];
    for (int k = 1; k <= radius; ++k) {
        const float weight = half[size_t(k)];
        const int offset = k * channels;
        const float *before = in - offset, *after = in + offset;
        for (int v = firstValue; v < lastValue; ++v)
            out[v] += weight * (before[v] + after[v]);
    }
    for (int x = std::max(interiorLast, interiorFirst); x < width; ++x)
        bordered(x);
}

} // namespace

std::vector<float> gaussianHalfKernel(double sigma)
{
    if (!(sigma > 0.0) || !std::isfinite(sigma))
        return {};
    const int radius = std::max(1, int(std::lround(sigma * 4.0)));
    std::vector<double> weights(size_t(radius) + 1);
    double total = 0;
    for (int k = 0; k <= radius; ++k) {
        weights[size_t(k)] = std::exp(-double(k) * double(k) / (2.0 * sigma * sigma));
        total += k == 0 ? weights[size_t(k)] : 2.0 * weights[size_t(k)];
    }
    std::vector<float> half(weights.size());
    for (size_t k = 0; k < half.size(); ++k)
        half[k] = float(weights[k] / total);
    return half;
}

Image gaussianBlur(const Image &image, double sigma, int threads)
{
    if (!image.empty() && image.depth() != Depth::F32)
        throw std::invalid_argument("The blur reads 32-bit floats");
    const std::vector<float> half = gaussianHalfKernel(sigma);
    if (image.empty() || half.empty())
        return image.clone();

    const int width = image.width(), height = image.height(), channels = image.channels();
    const int radius = int(half.size()) - 1;
    Image across = Image::create(image.size(), Depth::F32, channels);
    Image out = Image::create(image.size(), Depth::F32, channels);
    Image &acrossRef = across;
    parallelRows(height, [&](int first, int last) {
        for (int y = first; y < last; ++y)
            blurRow(image.row<float>(y), acrossRef.mutableRow<float>(y), width, channels, half);
    }, threads);

    Image &outRef = out;
    const size_t values = size_t(width) * size_t(channels);
    parallelRows(height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            float *target = outRef.mutableRow<float>(y);
            const float *centre = across.row<float>(y);
            for (size_t v = 0; v < values; ++v)
                target[v] = half[0] * centre[v];
            for (int k = 1; k <= radius; ++k) {
                const float *above = across.row<float>(mirrored(y - k, height));
                const float *below = across.row<float>(mirrored(y + k, height));
                const float weight = half[size_t(k)];
                for (size_t v = 0; v < values; ++v)
                    target[v] += weight * (above[v] + below[v]);
            }
        }
    }, threads);
    return out;
}

} // namespace ipr
