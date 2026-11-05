#include "Filters.h"

#include "Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace ipr {

namespace {

// Values taken at once by the loops over the taps: they stay in the first level of the cache,
// instead of going over a whole row once per tap.
constexpr int chunkValues = 1024;
// Under this many values, starting threads costs more than they give.
constexpr std::int64_t aloneValues = 1 << 16;

// Where a position beyond [0, length) reads.
int mirrored(int position, int length, BlurBorder border)
{
    if (length == 1)
        return 0;
    if (border == BlurBorder::LastPixel)
        return std::clamp(position, 0, length - 1);
    const bool repeatsEdge = border == BlurBorder::MirrorEdge;
    const int period = repeatsEdge ? 2 * length : 2 * (length - 1);
    int wrapped = position % period;
    if (wrapped < 0)
        wrapped += period;
    if (wrapped < length)
        return wrapped;
    return repeatsEdge ? period - 1 - wrapped : period - wrapped;
}

// The kernel along a row, channels side by side. Interior pixels run without any test, which is
// what lets a compiler vectorize this loop; the borders go through the mirror.
void blurRow(const float *in, float *out, int width, int channels, const std::vector<float> &half, BlurBorder border)
{
    const int radius = int(half.size()) - 1;
    const int interiorFirst = std::min(radius, width);
    const int interiorLast = std::max(interiorFirst, width - radius);
    const auto bordered = [&](int x) {
        for (int c = 0; c < channels; ++c) {
            float sum = half[0] * in[x * channels + c];
            for (int k = 1; k <= radius; ++k)
                sum += half[size_t(k)] * (in[mirrored(x - k, width, border) * channels + c] +
                                          in[mirrored(x + k, width, border) * channels + c]);
            out[x * channels + c] = sum;
        }
    };
    for (int x = 0; x < interiorFirst; ++x)
        bordered(x);
    const int firstValue = interiorFirst * channels, lastValue = interiorLast * channels;
    for (int chunk = firstValue; chunk < lastValue; chunk += chunkValues) {
        const int chunkEnd = std::min(chunk + chunkValues, lastValue);
        const float centre = half[0];
        for (int v = chunk; v < chunkEnd; ++v)
            out[v] = centre * in[v];
        for (int k = 1; k <= radius; ++k) {
            const float weight = half[size_t(k)];
            const int offset = k * channels;
            const float *before = in - offset, *after = in + offset;
            for (int v = chunk; v < chunkEnd; ++v)
                out[v] += weight * (before[v] + after[v]);
        }
    }
    for (int x = std::max(interiorLast, interiorFirst); x < width; ++x)
        bordered(x);
}

} // namespace

std::vector<float> gaussianHalfKernel(double sigma)
{
    if (!(sigma > 0.0) || !std::isfinite(sigma))
        return {};
    const int radius = (int(std::lrint(sigma * 8.0 + 1.0)) | 1) / 2;
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

Image gaussianBlur(const Image &image, double sigma, int threads, BlurBorder border)
{
    if (!image.empty() && image.depth() != Depth::F32)
        throw std::invalid_argument("The blur reads 32-bit floats");
    if (image.empty() || gaussianHalfKernel(sigma).empty())
        return image.clone();
    Image out = Image::create(image.size(), Depth::F32, image.channels());
    gaussianBlurInto(image, out, sigma, threads, border);
    return out;
}

void gaussianBlurInto(const Image &image, Image &out, double sigma, int threads, BlurBorder border)
{
    if (image.empty() != out.empty())
        throw std::invalid_argument("The blur needs a buffer of the image's size");
    if (image.empty())
        return;
    if (image.depth() != Depth::F32 || out.depth() != Depth::F32 || image.size() != out.size() ||
        image.channels() != out.channels())
        throw std::invalid_argument("The blur reads and writes 32-bit floats of one size");
    const std::vector<float> half = gaussianHalfKernel(sigma);
    if (half.empty()) {
        const size_t rowBytes = size_t(image.width()) * size_t(image.channels()) * sizeof(float);
        for (int y = 0; y < image.height(); ++y)
            std::memcpy(out.mutableRow<float>(y), image.row<float>(y), rowBytes);
        return;
    }

    const int width = image.width(), height = image.height(), channels = image.channels();
    const int radius = int(half.size()) - 1;
    if (std::int64_t(width) * height * channels < aloneValues)
        threads = 1;
    Image across = Image::create(image.size(), Depth::F32, channels);
    Image &acrossRef = across;
    parallelRows(height, [&](int first, int last) {
        for (int y = first; y < last; ++y)
            blurRow(image.row<float>(y), acrossRef.mutableRow<float>(y), width, channels, half, border);
    }, threads);

    Image &outRef = out;
    const size_t values = size_t(width) * size_t(channels);
    parallelRows(height, [&](int first, int last) {
        std::vector<const float *> rows(size_t(radius) * 2 + 1);
        for (int y = first; y < last; ++y) {
            float *target = outRef.mutableRow<float>(y);
            const float *centre = across.row<float>(y);
            // Asked once per row: the check of row() has no place in the loops over a chunk.
            for (int k = -radius; k <= radius; ++k)
                rows[size_t(k + radius)] = across.row<float>(mirrored(y + k, height, border));
            for (size_t chunk = 0; chunk < values; chunk += size_t(chunkValues)) {
                const size_t chunkEnd = std::min(chunk + size_t(chunkValues), values);
                const float middle = half[0];
                for (size_t v = chunk; v < chunkEnd; ++v)
                    target[v] = middle * centre[v];
                for (int k = 1; k <= radius; ++k) {
                    const float *above = rows[size_t(radius - k)];
                    const float *below = rows[size_t(radius + k)];
                    const float weight = half[size_t(k)];
                    for (size_t v = chunk; v < chunkEnd; ++v)
                        target[v] += weight * (above[v] + below[v]);
                }
            }
        }
    }, threads);
}

} // namespace ipr
