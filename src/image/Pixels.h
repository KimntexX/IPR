#ifndef IPR_IMAGE_PIXELS_H
#define IPR_IMAGE_PIXELS_H

#include "Image.h"
#include "Types.h"

#include <cstddef>
#include <vector>

namespace ipr {

// Arithmetic pixel by pixel and over a whole image, as OpenCV computes it. Images are F32 unless
// said otherwise; a buffer is of the image's size and of the channels the operation gives, or the
// call throws std::invalid_argument. out may be the image itself where the operation reads each
// value once, before writing it: every elementwise operation here.

// No value is NaN or infinite. Integer images always pass.
bool allFinite(const Image &image);

// Channels taken apart and put together; a channel alone; the first channels kept, as RGBA to RGB.
std::vector<Image> splitChannels(const Image &image);
void mergeChannelsInto(const std::vector<const Image *> &planes, Image &out);
void channelInto(const Image &image, Image &out, int channel);
void firstChannelsInto(const Image &image, Image &out);

// a * b, a / b, value by value, of the same channels; or a by one factor per channel.
void multiplyInto(const Image &a, const Image &b, Image &out);
void multiplyInto(const Image &a, const std::vector<double> &factors, Image &out);
void divideInto(const Image &a, const Image &b, Image &out);
// a * alpha + b * beta + gamma, in floats, as addWeighted.
void blendInto(const Image &a, double alpha, const Image &b, double beta, double gamma, Image &out);
// a * scale + b, as scaleAdd.
void addScaledInto(const Image &a, double scale, const Image &b, Image &out);
// value - a.
void subtractFromInto(double value, const Image &a, Image &out);
// sqrt(x * x + y * y); sqrt(a).
void magnitudeInto(const Image &x, const Image &y, Image &out);
void squareRootInto(const Image &a, Image &out);
// The larger, or the smaller, of each value and a bound.
void atLeastInto(const Image &a, float bound, Image &out);
void atLeastInto(const Image &a, const std::vector<float> &bounds, Image &out);
void atMostInto(const Image &a, float bound, Image &out);
// out = matrix * pixel, three channels to three, in floats.
void transformInto(const Image &a, const Matrix<float, 3, 3> &matrix, Image &out);
// Each NaN becomes value, in place.
void replaceNaN(Image &image, float value);

// Bytes: a | b, a & b, ~a.
void bitwiseOrInto(const Image &a, const Image &b, Image &out);
void bitwiseAndInto(const Image &a, const Image &b, Image &out);
void bitwiseNotInto(const Image &a, Image &out);

// The same into new images. An empty image gives an empty one.
Image merged(const std::vector<const Image *> &planes);
Image merged(const std::vector<Image> &planes);
Image channel(const Image &image, int index);
Image withoutAlpha(const Image &rgba);
Image multiplied(const Image &a, const Image &b);
Image multiplied(const Image &a, const std::vector<double> &factors);
Image divided(const Image &a, const Image &b);
Image blended(const Image &a, double alpha, const Image &b, double beta, double gamma);
Image addScaled(const Image &a, double scale, const Image &b);
Image subtractedFrom(double value, const Image &a);
Image magnitude(const Image &x, const Image &y);
Image squareRoot(const Image &a);
Image atLeast(const Image &a, float bound);
Image atLeast(const Image &a, const std::vector<float> &bounds);
Image atMost(const Image &a, float bound);
Image transformed(const Image &a, const Matrix<float, 3, 3> &matrix);
Image bitwiseOr(const Image &a, const Image &b);
Image bitwiseAnd(const Image &a, const Image &b);
Image bitwiseNot(const Image &a);

// New pixels at one value, every channel alike, as OpenCV makes a Scalar the depth's: rounded half
// to even and saturated for integers. Any depth.
Image filled(Size size, Depth depth, int channels, double value);
// The same in place: one value, or one per channel (as many as channels); value where mask, bytes
// of one channel and of the image's size, is not zero.
void fill(Image &image, double value);
void fill(Image &image, const std::vector<double> &values);
void fillWhere(Image &image, double value, const Image &mask);
// from's pixels into to, of the same size, depth and channels, such as a tile into its place.
void copyInto(const Image &from, Image &to);
// Each value times scale plus shift into out, F32 of the same size and channels, from any depth,
// as OpenCV's convertTo: in floats, the scale and the shift made floats first; doubles in doubles.
void convertInto(const Image &from, Image &out, double scale = 1.0, double shift = 0.0);

// f(a), f(a, b) or f(a, b, c) value by value into a new F32 image, on F32 images of one size and
// channel count, or std::invalid_argument: the arithmetic a module writes in one line, in the
// order it is written. Serial: run within a caller's threads or on small images.
void checkMappable(const Image &a, const Image *b = nullptr, const Image *c = nullptr);
template <typename F>
Image mapped(const Image &a, F f)
{
    checkMappable(a);
    Image out = Image::create(a.size(), Depth::F32, a.channels());
    const std::size_t values = std::size_t(a.width()) * std::size_t(a.channels());
    for (int y = 0; y < a.height(); ++y) {
        const float *x = a.row<float>(y);
        float *row = out.mutableRow<float>(y);
        for (std::size_t v = 0; v < values; ++v)
            row[v] = f(x[v]);
    }
    return out;
}
template <typename F>
Image mapped(const Image &a, const Image &b, F f)
{
    checkMappable(a, &b);
    Image out = Image::create(a.size(), Depth::F32, a.channels());
    const std::size_t values = std::size_t(a.width()) * std::size_t(a.channels());
    for (int y = 0; y < a.height(); ++y) {
        const float *x = a.row<float>(y), *z = b.row<float>(y);
        float *row = out.mutableRow<float>(y);
        for (std::size_t v = 0; v < values; ++v)
            row[v] = f(x[v], z[v]);
    }
    return out;
}
template <typename F>
Image mapped(const Image &a, const Image &b, const Image &c, F f)
{
    checkMappable(a, &b, &c);
    Image out = Image::create(a.size(), Depth::F32, a.channels());
    const std::size_t values = std::size_t(a.width()) * std::size_t(a.channels());
    for (int y = 0; y < a.height(); ++y) {
        const float *x = a.row<float>(y), *z = b.row<float>(y), *w = c.row<float>(y);
        float *row = out.mutableRow<float>(y);
        for (std::size_t v = 0; v < values; ++v)
            row[v] = f(x[v], z[v], w[v]);
    }
    return out;
}
// The same in place on a writable F32 image.
template <typename F>
void mapInPlace(Image &a, F f)
{
    checkMappable(a);
    const std::size_t values = std::size_t(a.width()) * std::size_t(a.channels());
    for (int y = 0; y < a.height(); ++y) {
        float *row = a.mutableRow<float>(y);
        for (std::size_t v = 0; v < values; ++v)
            row[v] = f(row[v]);
    }
}

// Over a whole image of one channel: how many values are not zero (F32 or bytes); the smallest and
// largest and where each first lies, rows first.
long long countNonZero(const Image &image);
struct Extremes
{
    double smallest = 0.0, largest = 0.0;
    Point<int> smallestAt, largestAt;
};
Extremes extremesOf(const Image &image);
// The largest absolute difference between two images of one layout, over every value, in double:
// how far apart two results are. Any depth; zero for two empty images.
double largestDifference(const Image &a, const Image &b);

// Per channel: the mean, and the standard deviation over all the values, in double.
std::vector<double> meanOf(const Image &image);
void meanAndDeviationOf(const Image &image, std::vector<double> &mean, std::vector<double> &deviation);

} // namespace ipr

#endif // IPR_IMAGE_PIXELS_H
