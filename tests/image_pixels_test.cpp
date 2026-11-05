#include "Pixels.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

Image filled(Size size, int channels, const std::function<float(int x, int y, int c)> &value)
{
    Image image = Image::create(size, Depth::F32, channels);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            for (int c = 0; c < channels; ++c)
                image.mutableRow<float>(y)[x * channels + c] = value(x, y, c);
    return image;
}

float at(const Image &image, int x, int y, int c = 0)
{
    return image.row<float>(y)[x * image.channels() + c];
}

Image like(const Image &image, int channels = 0)
{
    return Image::create(image.size(), image.depth(), channels ? channels : image.channels());
}

void channelsComeApartAndBack()
{
    const Image rgba = filled({5, 3}, 4, [](int x, int y, int c) { return float(x + 10 * y + 100 * c); });
    const std::vector<Image> planes = splitChannels(rgba);
    check(planes.size() == 4 && at(planes[2], 3, 1) == 213.0f, "each plane its channel");
    Image back = like(rgba);
    mergeChannelsInto({&planes[0], &planes[1], &planes[2], &planes[3]}, back);
    for (int y = 0; y < 3; ++y)
        for (int v = 0; v < 20; ++v)
            check(back.row<float>(y)[v] == rgba.row<float>(y)[v], "merged back whole");
    Image rgb = like(rgba, 3), alpha = like(rgba, 1);
    firstChannelsInto(rgba, rgb);
    channelInto(rgba, alpha, 3);
    check(at(rgb, 4, 2, 2) == 224.0f && at(alpha, 4, 2) == 324.0f, "colour without alpha, and alpha alone");
}

void arithmetic()
{
    const Image a = filled({4, 2}, 1, [](int x, int, int) { return float(x + 1); });
    const Image b = filled({4, 2}, 1, [](int, int y, int) { return float(y + 2); });
    Image out = like(a);
    multiplyInto(a, b, out);
    check(at(out, 3, 1) == 12.0f, "a times b");
    divideInto(a, b, out);
    check(at(out, 3, 0) == 2.0f, "a over b");
    blendInto(a, 0.5, b, 2.0, 1.0, out);
    check(at(out, 1, 1) == 1.0f + 6.0f + 1.0f, "a blend");
    subtractFromInto(1.0, a, out);
    check(at(out, 2, 0) == -2.0f, "one minus a");
    magnitudeInto(a, b, out);
    check(at(out, 3, 0) == std::sqrt(16.0f + 4.0f), "a magnitude");
    atLeastInto(a, 2.5f, out);
    check(at(out, 0, 0) == 2.5f && at(out, 3, 0) == 4.0f, "a lower bound");
    atMostInto(a, 2.5f, out);
    check(at(out, 0, 0) == 1.0f && at(out, 3, 0) == 2.5f, "an upper bound");
    multiplyInto(a, a, out);
    multiplyInto(out, out, out);
    check(at(out, 1, 0) == 16.0f, "in place");
}

void reductions()
{
    Image image = filled({6, 4}, 1, [](int x, int y, int) { return float((x * 7 + y * 3) % 5) - 1.0f; });
    const Extremes found = extremesOf(image);
    check(found.smallest == -1.0f && found.largest == 3.0f, "the extremes");
    check(found.smallestAt.x == 0 && found.smallestAt.y == 0, "the first smallest, rows first");
    check(found.largestAt.x == 2 && found.largestAt.y == 0, "the first largest, rows first");
    check(countNonZero(image) == 24 - 5, "values not zero");
    std::vector<double> mean, deviation;
    const Image constant = filled({3, 3}, 2, [](int, int, int c) { return float(c + 1); });
    meanAndDeviationOf(constant, mean, deviation);
    check(mean[0] == 1.0 && mean[1] == 2.0 && deviation[0] == 0.0, "a constant's mean and deviation");
    check(allFinite(image), "finite");
    image.mutableRow<float>(2)[3] = std::numeric_limits<float>::quiet_NaN();
    check(!allFinite(image), "a NaN is caught");
    replaceNaN(image, 0.0f);
    check(allFinite(image) && at(image, 3, 2) == 0.0f, "and replaced");
    image.mutableRow<float>(1)[1] = std::numeric_limits<float>::infinity();
    check(!allFinite(image), "an infinity too");
}

void bytes()
{
    Image a = Image::create({3, 1}, Depth::U8, 1), b = Image::create({3, 1}, Depth::U8, 1), out = Image::create({3, 1}, Depth::U8, 1);
    for (int x = 0; x < 3; ++x) {
        a.mutableRow<std::uint8_t>(0)[x] = std::uint8_t(x == 0 ? 255 : 0);
        b.mutableRow<std::uint8_t>(0)[x] = std::uint8_t(x == 1 ? 255 : 0);
    }
    bitwiseOrInto(a, b, out);
    check(out.row<std::uint8_t>(0)[0] == 255 && out.row<std::uint8_t>(0)[1] == 255 && out.row<std::uint8_t>(0)[2] == 0, "or");
    bitwiseAndInto(a, b, out);
    check(out.row<std::uint8_t>(0)[0] == 0, "and");
    bitwiseNotInto(a, out);
    check(out.row<std::uint8_t>(0)[0] == 0 && out.row<std::uint8_t>(0)[2] == 255, "not");
}

void refusals()
{
    const auto refused = [](const std::function<void()> &call) {
        try {
            call();
        } catch (const std::invalid_argument &) {
            return true;
        }
        return false;
    };
    const Image a = filled({4, 2}, 1, [](int, int, int) { return 1.0f; });
    Image other = Image::create({3, 2}, Depth::F32, 1), three = Image::create({4, 2}, Depth::F32, 3);
    check(refused([&] { multiplyInto(a, other, other); }), "another size");
    check(refused([&] { channelInto(three, other, 0); }), "a buffer of the image's size");
    check(refused([&] { channelInto(three, three, 5); }), "a channel that exists");
    check(refused([&] { bitwiseNotInto(a, other); }), "bytes only");
    check(refused([&] { multiplyInto(three, std::vector<double>{1.0}, three); }), "a factor per channel");
}

} // namespace

int main()
{
    try {
        channelsComeApartAndBack();
        arithmetic();
        reductions();
        bytes();
        refusals();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image pixels: all passed\n";
    return 0;
}
