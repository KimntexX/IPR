#include "Resize.h"

#include "Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ipr {

namespace {

// Under this many values, starting threads costs more than they give.
constexpr std::int64_t aloneValues = 1 << 16;

// What each position along an axis reads: taps [start[d], start[d + 1]) of source and weight.
struct Taps
{
    std::vector<int> start;
    std::vector<int> source;
    std::vector<float> weight;

    void add(int index, float value)
    {
        source.push_back(index);
        weight.push_back(value);
    }
    void close() { start.push_back(int(source.size())); }
};

// Two neighbours, centres aligned. In area mode, OpenCV's rule for enlarging with INTER_AREA: the
// new pixel takes its source pixel whole until its edge, then blends over the last fraction.
Taps linearTaps(int sourceLength, int length, double scale, double inverseScale, bool areaMode)
{
    Taps taps;
    taps.start.push_back(0);
    for (int d = 0; d < length; ++d) {
        float fraction = float((d + 0.5) * scale - 0.5);
        int s = int(std::floor(fraction));
        fraction -= float(s);
        if (areaMode) {
            s = int(std::floor(d * scale));
            fraction = float((d + 1) - (s + 1) * inverseScale);
            fraction = fraction <= 0 ? 0.0f : fraction - std::floor(fraction);
        }
        if (s < 0) {
            s = 0;
            fraction = 0.0f;
        }
        if (s >= sourceLength - 1) {
            s = sourceLength - 1;
            fraction = 0.0f;
        }
        taps.add(s, 1.0f - fraction);
        if (fraction != 0.0f)
            taps.add(s + 1, fraction);
        taps.close();
    }
    return taps;
}

// What each new pixel covers of the source, partial pixels at its two ends, as OpenCV weighs them.
Taps areaTaps(int sourceLength, int length, double scale)
{
    Taps taps;
    taps.start.push_back(0);
    for (int d = 0; d < length; ++d) {
        const double first = d * scale, last = first + scale;
        const double cell = std::min(scale, sourceLength - first);
        int whole = int(std::ceil(first)), end = int(std::floor(last));
        end = std::min(end, sourceLength - 1);
        whole = std::min(whole, end);
        if (whole - first > 1e-3)
            taps.add(whole - 1, float((whole - first) / cell));
        for (int s = whole; s < end; ++s)
            taps.add(s, float(1.0 / cell));
        if (last - end > 1e-3)
            taps.add(end, float(std::min(std::min(last - end, 1.0), cell) / cell));
        taps.close();
    }
    return taps;
}

void resample(const Image &image, Image &out, double inverseScaleX, double inverseScaleY, Interpolation interpolation,
              int threads)
{
    if (image.empty() || out.empty())
        throw std::invalid_argument("Resizing needs an image and a size");
    if (image.depth() != Depth::F32 || out.depth() != Depth::F32 || out.channels() != image.channels())
        throw std::invalid_argument("Resizing reads and writes 32-bit floats of the same channels");
    const int channels = image.channels();
    const Size size = out.size();
    if (size == image.size()) {
        const size_t rowBytes = size_t(size.width) * size_t(channels) * sizeof(float);
        for (int y = 0; y < size.height; ++y)
            std::memcpy(out.mutableRow<float>(y), image.row<float>(y), rowBytes);
        return;
    }
    const double scaleX = 1.0 / inverseScaleX, scaleY = 1.0 / inverseScaleY;
    const bool area = interpolation == Interpolation::Area && scaleX >= 1 && scaleY >= 1;
    const bool areaMode = interpolation == Interpolation::Area && !area;
    const Taps across = area ? areaTaps(image.width(), size.width, scaleX)
                             : linearTaps(image.width(), size.width, scaleX, inverseScaleX, areaMode);
    const Taps down = area ? areaTaps(image.height(), size.height, scaleY)
                           : linearTaps(image.height(), size.height, scaleY, inverseScaleY, areaMode);
    if (std::int64_t(image.width()) * image.height() * channels < aloneValues)
        threads = 1;

    // Along the rows first, every source row, then down the columns.
    Image wide = Image::create({size.width, image.height()}, Depth::F32, channels);
    Image &wideRef = wide;
    parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const float *in = image.row<float>(y);
            float *row = wideRef.mutableRow<float>(y);
            for (int x = 0; x < size.width; ++x)
                for (int c = 0; c < channels; ++c) {
                    float sum = 0.0f;
                    for (int t = across.start[size_t(x)]; t < across.start[size_t(x) + 1]; ++t)
                        sum += in[across.source[size_t(t)] * channels + c] * across.weight[size_t(t)];
                    row[x * channels + c] = sum;
                }
        }
    }, threads);

    Image &outRef = out;
    const int values = size.width * channels;
    parallelRows(size.height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            float *target = outRef.mutableRow<float>(y);
            const int begin = down.start[size_t(y)], end = down.start[size_t(y) + 1];
            const float *row = wide.row<float>(down.source[size_t(begin)]);
            const float weight = down.weight[size_t(begin)];
            for (int v = 0; v < values; ++v)
                target[v] = row[v] * weight;
            for (int t = begin + 1; t < end; ++t) {
                const float *next = wide.row<float>(down.source[size_t(t)]);
                const float w = down.weight[size_t(t)];
                for (int v = 0; v < values; ++v)
                    target[v] += next[v] * w;
            }
        }
    }, threads);
}

bool validScales(double scaleX, double scaleY)
{
    return scaleX > 0 && scaleY > 0 && std::isfinite(scaleX) && std::isfinite(scaleY);
}

} // namespace

Size scaledSize(Size size, double scaleX, double scaleY)
{
    return {int(std::lrint(size.width * scaleX)), int(std::lrint(size.height * scaleY))};
}

Image resized(const Image &image, Size size, Interpolation interpolation, int threads)
{
    if (image.empty() || size.empty())
        throw std::invalid_argument("Resizing needs an image and a size");
    Image out = Image::create(size, Depth::F32, image.channels());
    resizeInto(image, out, interpolation, threads);
    return out;
}

Image resizedBy(const Image &image, double scaleX, double scaleY, Interpolation interpolation, int threads)
{
    if (image.empty() || !validScales(scaleX, scaleY))
        throw std::invalid_argument("Resizing needs an image and scales above zero");
    const Size size = scaledSize(image.size(), scaleX, scaleY);
    if (size.empty())
        throw std::invalid_argument("Resizing needs an image and a size");
    Image out = Image::create(size, Depth::F32, image.channels());
    resample(image, out, scaleX, scaleY, interpolation, threads);
    return out;
}

void resizeInto(const Image &image, Image &out, Interpolation interpolation, int threads)
{
    if (image.empty() || out.empty())
        throw std::invalid_argument("Resizing needs an image and a size");
    resample(image, out, double(out.width()) / image.width(), double(out.height()) / image.height(), interpolation,
             threads);
}

void resizeInto(const Image &image, Image &out, double scaleX, double scaleY, Interpolation interpolation, int threads)
{
    if (image.empty() || !validScales(scaleX, scaleY))
        throw std::invalid_argument("Resizing needs an image and scales above zero");
    if (out.size() != scaledSize(image.size(), scaleX, scaleY))
        throw std::invalid_argument("The buffer has not the size these scales give");
    resample(image, out, scaleX, scaleY, interpolation, threads);
}

} // namespace ipr
