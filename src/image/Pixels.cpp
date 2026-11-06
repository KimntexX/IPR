#include "Pixels.h"

#include "Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace ipr {

namespace {

void need(bool condition, const char *why)
{
    if (!condition)
        throw std::invalid_argument(why);
}

bool floats(const Image &image)
{
    return !image.empty() && image.depth() == Depth::F32;
}

void sameShape(const Image &a, const Image &b)
{
    need(a.size() == b.size() && a.channels() == b.channels() && a.depth() == b.depth(),
         "The images have the same size, channels and depth");
}

// Each row's values, a and out alike, spread over threads.
template <typename Work>
void byRows(const Image &a, Image &out, const Work &work)
{
    const size_t values = size_t(a.width()) * size_t(a.channels());
    Image &target = out;
    parallelRows(a.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y)
            work(y, values, target);
    });
}

// Loops on distinct pointers, out of their callers, which the compiler vectorizes after one check.
void multiplyValues(float *out, const float *a, const float *b, size_t count)
{
    for (size_t v = 0; v < count; ++v)
        out[v] = a[v] * b[v];
}

void divideValues(float *out, const float *a, const float *b, size_t count)
{
    for (size_t v = 0; v < count; ++v)
        out[v] = a[v] / b[v];
}

} // namespace

bool allFinite(const Image &image)
{
    if (image.empty() || (image.depth() != Depth::F32 && image.depth() != Depth::F64))
        return true;
    const size_t values = size_t(image.width()) * size_t(image.channels());
    for (int y = 0; y < image.height(); ++y) {
        if (image.depth() == Depth::F32) {
            const float *row = image.row<float>(y);
            for (size_t v = 0; v < values; ++v)
                if (!std::isfinite(row[v]))
                    return false;
        } else {
            const double *row = image.row<double>(y);
            for (size_t v = 0; v < values; ++v)
                if (!std::isfinite(row[v]))
                    return false;
        }
    }
    return true;
}

std::vector<Image> splitChannels(const Image &image)
{
    need(!image.empty(), "Nothing to split");
    std::vector<Image> planes;
    for (int c = 0; c < image.channels(); ++c) {
        planes.push_back(Image::create(image.size(), image.depth(), 1));
        channelInto(image, planes.back(), c);
    }
    return planes;
}

void mergeChannelsInto(const std::vector<const Image *> &planes, Image &out)
{
    need(!planes.empty() && int(planes.size()) == out.channels(), "One plane per channel of the buffer");
    for (const Image *plane : planes)
        need(plane && plane->channels() == 1 && plane->size() == out.size() && plane->depth() == out.depth(),
             "Planes of one channel, of the buffer's size and depth");
    const size_t bytes = depthBytes(out.depth()), channels = planes.size();
    Image &target = out;
    parallelRows(out.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            unsigned char *row = target.mutableRow<unsigned char>(y);
            for (size_t c = 0; c < channels; ++c) {
                const unsigned char *in = planes[c]->row<unsigned char>(y);
                for (int x = 0; x < target.width(); ++x)
                    std::memcpy(row + (size_t(x) * channels + c) * bytes, in + size_t(x) * bytes, bytes);
            }
        }
    });
}

void channelInto(const Image &image, Image &out, int channel)
{
    need(!image.empty() && channel >= 0 && channel < image.channels(), "A channel of the image");
    need(out.size() == image.size() && out.channels() == 1 && out.depth() == image.depth(), "A buffer of one channel");
    const size_t bytes = depthBytes(image.depth()), channels = size_t(image.channels());
    Image &target = out;
    parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const unsigned char *in = image.row<unsigned char>(y);
            unsigned char *row = target.mutableRow<unsigned char>(y);
            for (int x = 0; x < image.width(); ++x)
                std::memcpy(row + size_t(x) * bytes, in + (size_t(x) * channels + size_t(channel)) * bytes, bytes);
        }
    });
}

void firstChannelsInto(const Image &image, Image &out)
{
    need(!image.empty() && out.size() == image.size() && out.depth() == image.depth() && out.channels() <= image.channels(),
         "A buffer of the image's size and depth, with fewer channels");
    const size_t bytes = depthBytes(image.depth()), from = size_t(image.channels()), kept = size_t(out.channels());
    Image &target = out;
    parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const unsigned char *in = image.row<unsigned char>(y);
            unsigned char *row = target.mutableRow<unsigned char>(y);
            for (int x = 0; x < image.width(); ++x)
                std::memcpy(row + size_t(x) * kept * bytes, in + size_t(x) * from * bytes, kept * bytes);
        }
    });
}

void multiplyInto(const Image &a, const Image &b, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, b);
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        multiplyValues(target.mutableRow<float>(y), a.row<float>(y), b.row<float>(y), values);
    });
}

void multiplyInto(const Image &a, const std::vector<double> &factors, Image &out)
{
    need(floats(a) && int(factors.size()) >= a.channels(), "Floats, and a factor per channel");
    sameShape(a, out);
    const int channels = a.channels();
    // As OpenCV's multiply by a Scalar: the Scalar first made floats, the product in floats.
    std::vector<float> f(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c)
        f[size_t(c)] = float(factors[size_t(c)]);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = in[v] * f[v % size_t(channels)];
    });
}

void divideInto(const Image &a, const Image &b, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, b);
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        divideValues(target.mutableRow<float>(y), a.row<float>(y), b.row<float>(y), values);
    });
}

void blendInto(const Image &a, double alpha, const Image &b, double beta, double gamma, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, b);
    sameShape(a, out);
    const float wa = float(alpha), wb = float(beta), add = float(gamma);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *x = a.row<float>(y), *z = b.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = x[v] * wa + z[v] * wb + add;
    });
}

void addScaledInto(const Image &a, double scale, const Image &b, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, b);
    sameShape(a, out);
    const float s = float(scale);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *x = a.row<float>(y), *z = b.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = x[v] * s + z[v];
    });
}

void subtractFromInto(double value, const Image &a, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, out);
    // As OpenCV's subtract of a Mat from a Scalar: the Scalar made a float first.
    const float from = float(value);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = from - in[v];
    });
}

void magnitudeInto(const Image &x, const Image &y, Image &out)
{
    need(floats(x) && x.channels() == 1, "Floats of one channel");
    sameShape(x, y);
    sameShape(x, out);
    byRows(x, out, [&](int r, size_t values, Image &target) {
        const float *a = x.row<float>(r), *b = y.row<float>(r);
        float *row = target.mutableRow<float>(r);
        for (size_t v = 0; v < values; ++v)
            row[v] = std::sqrt(a[v] * a[v] + b[v] * b[v]);
    });
}

void squareRootInto(const Image &a, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = std::sqrt(in[v]);
    });
}

void atLeastInto(const Image &a, float bound, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = std::max(in[v], bound);
    });
}

void atLeastInto(const Image &a, const std::vector<float> &bounds, Image &out)
{
    need(floats(a) && int(bounds.size()) >= a.channels(), "Floats, and a bound per channel");
    sameShape(a, out);
    const size_t channels = size_t(a.channels());
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = std::max(in[v], bounds[v % channels]);
    });
}

void atMostInto(const Image &a, float bound, Image &out)
{
    need(floats(a), "Floats");
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = std::min(in[v], bound);
    });
}

void transformInto(const Image &a, const Matrix<float, 3, 3> &matrix, Image &out)
{
    need(floats(a) && a.channels() == 3, "Floats of three channels");
    sameShape(a, out);
    const float *m = matrix.values.data();
    byRows(a, out, [&](int y, size_t, Image &target) {
        const float *in = a.row<float>(y);
        float *row = target.mutableRow<float>(y);
        for (int x = 0; x < a.width(); ++x) {
            const float r = in[3 * x], g = in[3 * x + 1], b = in[3 * x + 2];
            row[3 * x] = m[0] * r + m[1] * g + m[2] * b;
            row[3 * x + 1] = m[3] * r + m[4] * g + m[5] * b;
            row[3 * x + 2] = m[6] * r + m[7] * g + m[8] * b;
        }
    });
}

void replaceNaN(Image &image, float value)
{
    need(floats(image), "Floats");
    const size_t values = size_t(image.width()) * size_t(image.channels());
    for (int y = 0; y < image.height(); ++y) {
        float *row = image.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            if (std::isnan(row[v]))
                row[v] = value;
    }
}

namespace {

template <typename Op>
void bytes(const Image &a, const Image *b, Image &out, const Op &op)
{
    need(!a.empty() && a.depth() == Depth::U8, "Bytes");
    if (b)
        sameShape(a, *b);
    sameShape(a, out);
    byRows(a, out, [&](int y, size_t values, Image &target) {
        const std::uint8_t *x = a.row<std::uint8_t>(y), *z = b ? b->row<std::uint8_t>(y) : nullptr;
        std::uint8_t *row = target.mutableRow<std::uint8_t>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = op(x[v], z ? z[v] : std::uint8_t(0));
    });
}

} // namespace

void bitwiseOrInto(const Image &a, const Image &b, Image &out)
{
    bytes(a, &b, out, [](std::uint8_t x, std::uint8_t y) { return std::uint8_t(x | y); });
}

void bitwiseAndInto(const Image &a, const Image &b, Image &out)
{
    bytes(a, &b, out, [](std::uint8_t x, std::uint8_t y) { return std::uint8_t(x & y); });
}

void bitwiseNotInto(const Image &a, Image &out)
{
    bytes(a, nullptr, out, [](std::uint8_t x, std::uint8_t) { return std::uint8_t(~x); });
}

long long countNonZero(const Image &image)
{
    need(!image.empty() && image.channels() == 1 && (image.depth() == Depth::F32 || image.depth() == Depth::U8),
         "One channel of floats or bytes");
    long long count = 0;
    for (int y = 0; y < image.height(); ++y) {
        if (image.depth() == Depth::F32) {
            const float *row = image.row<float>(y);
            for (int x = 0; x < image.width(); ++x)
                count += row[x] != 0.0f;
        } else {
            const std::uint8_t *row = image.row<std::uint8_t>(y);
            for (int x = 0; x < image.width(); ++x)
                count += row[x] != 0;
        }
    }
    return count;
}

Extremes extremesOf(const Image &image)
{
    need(floats(image) && image.channels() == 1, "One channel of floats");
    Extremes result;
    bool first = true;
    for (int y = 0; y < image.height(); ++y) {
        const float *row = image.row<float>(y);
        for (int x = 0; x < image.width(); ++x) {
            const double value = row[x];
            if (first || value < result.smallest) {
                result.smallest = value;
                result.smallestAt = {x, y};
            }
            if (first || value > result.largest) {
                result.largest = value;
                result.largestAt = {x, y};
            }
            first = false;
        }
    }
    return result;
}

std::vector<double> meanOf(const Image &image)
{
    std::vector<double> mean, deviation;
    meanAndDeviationOf(image, mean, deviation);
    return mean;
}

void meanAndDeviationOf(const Image &image, std::vector<double> &mean, std::vector<double> &deviation)
{
    need(floats(image), "Floats");
    const int channels = image.channels();
    std::vector<double> sum(size_t(channels), 0.0), squares(size_t(channels), 0.0);
    for (int y = 0; y < image.height(); ++y) {
        const float *row = image.row<float>(y);
        for (int x = 0; x < image.width(); ++x)
            for (int c = 0; c < channels; ++c) {
                const double value = row[x * channels + c];
                sum[size_t(c)] += value;
                squares[size_t(c)] += value * value;
            }
    }
    const double count = double(image.width()) * double(image.height());
    mean.assign(size_t(channels), 0.0);
    deviation.assign(size_t(channels), 0.0);
    for (int c = 0; c < channels; ++c) {
        mean[size_t(c)] = sum[size_t(c)] / count;
        deviation[size_t(c)] = std::sqrt(std::max(squares[size_t(c)] / count - mean[size_t(c)] * mean[size_t(c)], 0.0));
    }
}

namespace {

Image like(const Image &a, int channels)
{
    return Image::create(a.size(), a.depth(), channels);
}

// A new image of a's layout written by into, or empty for an empty a.
template <typename Into>
Image made(const Image &a, int channels, const Into &into)
{
    if (a.empty())
        return {};
    Image out = like(a, channels);
    into(out);
    return out;
}

} // namespace

Image merged(const std::vector<const Image *> &planes)
{
    if (planes.empty() || !planes.front() || planes.front()->empty())
        return {};
    return made(*planes.front(), int(planes.size()), [&](Image &out) { mergeChannelsInto(planes, out); });
}

Image merged(const std::vector<Image> &planes)
{
    std::vector<const Image *> pointers;
    for (const Image &plane : planes)
        pointers.push_back(&plane);
    return merged(pointers);
}

Image channel(const Image &image, int index)
{
    return made(image, 1, [&](Image &out) { channelInto(image, out, index); });
}

Image withoutAlpha(const Image &rgba)
{
    return made(rgba, 3, [&](Image &out) { firstChannelsInto(rgba, out); });
}

Image multiplied(const Image &a, const Image &b)
{
    return made(a, a.channels(), [&](Image &out) { multiplyInto(a, b, out); });
}

Image multiplied(const Image &a, const std::vector<double> &factors)
{
    return made(a, a.channels(), [&](Image &out) { multiplyInto(a, factors, out); });
}

Image divided(const Image &a, const Image &b)
{
    return made(a, a.channels(), [&](Image &out) { divideInto(a, b, out); });
}

Image blended(const Image &a, double alpha, const Image &b, double beta, double gamma)
{
    return made(a, a.channels(), [&](Image &out) { blendInto(a, alpha, b, beta, gamma, out); });
}

Image addScaled(const Image &a, double scale, const Image &b)
{
    return made(a, a.channels(), [&](Image &out) { addScaledInto(a, scale, b, out); });
}

Image subtractedFrom(double value, const Image &a)
{
    return made(a, a.channels(), [&](Image &out) { subtractFromInto(value, a, out); });
}

Image magnitude(const Image &x, const Image &y)
{
    return made(x, x.channels(), [&](Image &out) { magnitudeInto(x, y, out); });
}

Image squareRoot(const Image &a)
{
    return made(a, a.channels(), [&](Image &out) { squareRootInto(a, out); });
}

Image atLeast(const Image &a, float bound)
{
    return made(a, a.channels(), [&](Image &out) { atLeastInto(a, bound, out); });
}

Image atLeast(const Image &a, const std::vector<float> &bounds)
{
    return made(a, a.channels(), [&](Image &out) { atLeastInto(a, bounds, out); });
}

Image atMost(const Image &a, float bound)
{
    return made(a, a.channels(), [&](Image &out) { atMostInto(a, bound, out); });
}

Image transformed(const Image &a, const Matrix<float, 3, 3> &matrix)
{
    return made(a, a.channels(), [&](Image &out) { transformInto(a, matrix, out); });
}

Image bitwiseOr(const Image &a, const Image &b)
{
    return made(a, a.channels(), [&](Image &out) { bitwiseOrInto(a, b, out); });
}

Image bitwiseAnd(const Image &a, const Image &b)
{
    return made(a, a.channels(), [&](Image &out) { bitwiseAndInto(a, b, out); });
}

Image bitwiseNot(const Image &a)
{
    return made(a, a.channels(), [&](Image &out) { bitwiseNotInto(a, out); });
}

namespace {

template <typename T>
void fillValues(Image &image, const std::vector<double> &values, const Image *mask)
{
    const int channels = image.channels();
    std::vector<T> pixel(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c)
        pixel[size_t(c)] = saturate<T>(values[size_t(c)]);
    for (int y = 0; y < image.height(); ++y) {
        T *row = image.mutableRow<T>(y);
        const std::uint8_t *where = mask ? mask->row<std::uint8_t>(y) : nullptr;
        for (int x = 0; x < image.width(); ++x)
            if (!where || where[x])
                for (int c = 0; c < channels; ++c)
                    row[x * channels + c] = pixel[size_t(c)];
    }
}

void fillValues(Image &image, const std::vector<double> &values, const Image *mask)
{
    need(int(values.size()) >= image.channels(), "A value per channel");
    switch (image.depth()) {
    case Depth::U8: fillValues<std::uint8_t>(image, values, mask); break;
    case Depth::U16: fillValues<std::uint16_t>(image, values, mask); break;
    case Depth::S16: fillValues<std::int16_t>(image, values, mask); break;
    case Depth::S32: fillValues<std::int32_t>(image, values, mask); break;
    case Depth::F32: fillValues<float>(image, values, mask); break;
    case Depth::F64: fillValues<double>(image, values, mask); break;
    }
}

template <typename T>
void convertValues(const Image &from, Image &out, float scale, float shift)
{
    const size_t values = size_t(from.width()) * size_t(from.channels());
    for (int y = 0; y < from.height(); ++y) {
        const T *in = from.row<T>(y);
        float *row = out.mutableRow<float>(y);
        for (size_t v = 0; v < values; ++v)
            row[v] = float(in[v]) * scale + shift;
    }
}

} // namespace

Image filled(Size size, Depth depth, int channels, double value)
{
    Image image = Image::create(size, depth, channels);
    fill(image, value);
    return image;
}

void fill(Image &image, double value)
{
    fillValues(image, std::vector<double>(size_t(std::max(image.channels(), 1)), value), nullptr);
}

void fill(Image &image, const std::vector<double> &values)
{
    fillValues(image, values, nullptr);
}

void fillWhere(Image &image, double value, const Image &mask)
{
    need(mask.depth() == Depth::U8 && mask.channels() == 1 && mask.size() == image.size(),
         "A mask of bytes, one channel, of the image's size");
    fillValues(image, std::vector<double>(size_t(std::max(image.channels(), 1)), value), &mask);
}

void copyInto(const Image &from, Image &to)
{
    sameShape(from, to);
    const size_t length = size_t(from.width()) * from.pixelBytes();
    for (int y = 0; y < from.height(); ++y)
        std::memcpy(to.mutableRow<std::uint8_t>(y), from.row<std::uint8_t>(y), length);
}

void convertInto(const Image &from, Image &out, double scale, double shift)
{
    need(out.depth() == Depth::F32 && out.size() == from.size() && out.channels() == from.channels(),
         "Floats of the image's size and channels");
    const float a = float(scale), b = float(shift);
    switch (from.depth()) {
    case Depth::U8: convertValues<std::uint8_t>(from, out, a, b); break;
    case Depth::U16: convertValues<std::uint16_t>(from, out, a, b); break;
    case Depth::S16: convertValues<std::int16_t>(from, out, a, b); break;
    case Depth::S32: convertValues<std::int32_t>(from, out, a, b); break;
    case Depth::F32: convertValues<float>(from, out, a, b); break;
    case Depth::F64:
        // Doubles are scaled in doubles, then rounded.
        for (int y = 0; y < from.height(); ++y) {
            const double *in = from.row<double>(y);
            float *row = out.mutableRow<float>(y);
            for (size_t v = 0; v < size_t(from.width()) * size_t(from.channels()); ++v)
                row[v] = float(in[v] * scale + shift);
        }
        break;
    }
}

void checkMappable(const Image &a, const Image *b, const Image *c)
{
    need(a.empty() || a.depth() == Depth::F32, "Floats");
    if (b)
        sameShape(a, *b);
    if (c)
        sameShape(a, *c);
}

} // namespace ipr
