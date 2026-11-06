#include "Grain.h"
#include "image_testing.h"
#include "Resize.h"
#include "Pixels.h"


#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

double encode(double v)
{
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

struct Statistics { double mean, deviation, lag1, lag4, chroma; };

// Mean and standard deviation of one channel of doubles, in double.
void meanAndDeviation(const ipr::Image &plane, std::vector<double> &mean, std::vector<double> &deviation)
{
    double sum = 0, squares = 0;
    for (int y = 0; y < plane.height(); ++y)
        for (int x = 0; x < plane.width(); ++x) {
            const double v = plane.row<double>(y)[x];
            sum += v;
            squares += v * v;
        }
    const double count = double(plane.width()) * plane.height();
    mean = {sum / count};
    deviation = {std::sqrt(std::max(squares / count - mean[0] * mean[0], 0.0))};
}

// Grain of a uniform field, in display-encoded green, as measured on Lightroom.
Statistics measure(const ipr::Image &rgb, double base)
{
    ipr::Image g = ipr::Image::create(rgb.size(), ipr::Depth::F64, 1), chroma = ipr::Image::create(rgb.size(), ipr::Depth::F64, 1);
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x) {
            const ipr::Vec3f p = imagetest::at<ipr::Vec3f>(rgb, y, x);
            imagetest::at<double>(g, y, x) = encode(p[1]) - base;
            imagetest::at<double>(chroma, y, x) = encode(p[0]) - encode(p[2]);
        }
    std::vector<double> mean, deviation;
    meanAndDeviation(g, mean, deviation);
    auto correlation = [&](int lag) {
        double sum = 0;
        int n = 0;
        for (int y = 0; y < g.height(); ++y)
            for (int x = 0; x + lag < g.width(); ++x, ++n)
                sum += (imagetest::at<double>(g, y, x) - mean[0]) * (imagetest::at<double>(g, y, x + lag) - mean[0]);
        return sum / n / (deviation[0] * deviation[0]);
    };
    std::vector<double> chromaMean, chromaDeviation;
    meanAndDeviation(chroma, chromaMean, chromaDeviation);
    return {mean[0], deviation[0], correlation(1), correlation(4), chromaDeviation[0]};
}

} // namespace

int main()
{
    const float grey = 0.18f;
    ipr::Image field = imagetest::filledWith({400, 400}, {grey, grey, grey});
    const double base = encode(grey);

    ipr::Image same = field.clone();
    Grain::apply(same, 0);
    if (!check(ipr::largestDifference(same, field) == 0, "zero leaves the image unchanged")) return 1;

    ipr::Image full = field.clone();
    Grain::apply(full, 100);
    const Statistics s = measure(full, base);
    if (!check(std::abs(s.deviation - 0.062) < 0.006, "+100: deviation 0.062 of the encoded values, as Lightroom")) return 1;
    if (!check(std::abs(s.mean) < 0.005, "the grain leaves the mean encoded value in place")) return 1;
    if (!check(s.lag1 > 0.5 && s.lag1 < 0.78 && std::abs(s.lag4) < 0.08,
               "fine grain: correlated at one pixel, not at four, as Lightroom")) return 1;
    if (!check(s.chroma < 1e-4, "monochrome grain")) return 1;

    ipr::Image half = field.clone();
    Grain::apply(half, 50);
    if (!check(std::abs(measure(half, base).deviation - 0.031) < 0.004, "+50: half the deviation")) return 1;

    ipr::Image again = field.clone();
    Grain::apply(again, 100);
    if (!check(ipr::largestDifference(again, full) == 0, "the same grain at every render")) return 1;

    ipr::Image dark = imagetest::filledWith({64, 64}, {0, 0, 0});
    Grain::apply(dark, 100);
    if (!check(imagetest::rangeOf(dark).low >= 0, "black stays nonnegative")) return 1;

    // On a 1/3 proxy, the grain is what the reduction leaves of the full image's.
    ipr::Image proxy = imagetest::filledWith({field.width() / 3, field.height() / 3}, {grey, grey, grey});
    const ipr::Image reducedFull = ipr::resized(full, proxy.size(), ipr::Interpolation::Area);
    Grain::apply(proxy, 100, 1.0 / 3.0);
    const double expected = measure(reducedFull, base).deviation, proxyDeviation = measure(proxy, base).deviation;
    if (!check(std::abs(proxyDeviation - expected) < 0.15 * expected,
               "a 1/3 proxy shows the deviation of the reduced full-resolution grain")) return 1;

    ipr::Image empty;
    Grain::apply(empty, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
