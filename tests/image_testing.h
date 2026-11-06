#ifndef IPR_TESTS_IMAGE_TESTING_H
#define IPR_TESTS_IMAGE_TESTING_H

// What the tests write often about images: an image of given values, a pixel, the range of the
// values, two images equal value for value. Checks are the tests' own.
#include "Image.h"
#include "Pixels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace imagetest {

// F32 values, rows first, as many as the size and channels ask.
inline ipr::Image floats(ipr::Size size, int channels, std::initializer_list<float> values)
{
    if (values.size() != std::size_t(size.area()) * std::size_t(channels))
        throw std::invalid_argument("As many values as the image holds");
    ipr::Image image = ipr::Image::create(size, ipr::Depth::F32, channels);
    auto value = values.begin();
    for (int y = 0; y < size.height; ++y)
        for (int v = 0; v < size.width * channels; ++v)
            image.mutableRow<float>(y)[v] = *value++;
    return image;
}

template <typename T>
const T &at(const ipr::Image &image, int y, int x)
{
    return image.row<T>(y)[x];
}

template <typename T>
T &at(ipr::Image &image, int y, int x)
{
    return image.mutableRow<T>(y)[x];
}

// The smallest and the largest value of an F32 image, over every channel.
struct Range
{
    double low = std::numeric_limits<double>::infinity(), high = -std::numeric_limits<double>::infinity();
};

inline Range rangeOf(const ipr::Image &image)
{
    Range range;
    for (int y = 0; y < image.height(); ++y) {
        const float *row = image.row<float>(y);
        for (int v = 0; v < image.width() * image.channels(); ++v) {
            range.low = std::min(range.low, double(row[v]));
            range.high = std::max(range.high, double(row[v]));
        }
    }
    return range;
}

// The same size, depth and channels, and the same values.
inline bool same(const ipr::Image &a, const ipr::Image &b)
{
    return a.size() == b.size() && a.depth() == b.depth() && a.channels() == b.channels() &&
           ipr::largestDifference(a, b) == 0.0;
}

// The Euclidean distance between two F32 images of one layout, over every value.
inline double distance(const ipr::Image &a, const ipr::Image &b)
{
    if (a.size() != b.size() || a.channels() != b.channels())
        throw std::invalid_argument("Images of one layout");
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y)
        for (int v = 0; v < a.width() * a.channels(); ++v) {
            const double d = double(a.row<float>(y)[v]) - double(b.row<float>(y)[v]);
            sum += d * d;
        }
    return std::sqrt(sum);
}

// The largest difference where mask, bytes of one channel, is not zero.
inline double largestDifferenceWhere(const ipr::Image &a, const ipr::Image &b, const ipr::Image &mask)
{
    if (a.size() != b.size() || a.channels() != b.channels() || mask.size() != a.size())
        throw std::invalid_argument("Images and mask of one size");
    double largest = 0.0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (mask.row<std::uint8_t>(y)[x])
                for (int c = 0; c < a.channels(); ++c)
                    largest = std::max(largest, std::abs(double(a.row<float>(y)[x * a.channels() + c]) -
                                                         double(b.row<float>(y)[x * a.channels() + c])));
    return largest;
}

} // namespace imagetest

#endif // IPR_TESTS_IMAGE_TESTING_H
