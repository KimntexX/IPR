#include "Pixels.h"
#include "Random.h"

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

Image painted(Size size, int channels, const std::function<float(int x, int y, int c)> &value)
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
    const Image rgba = painted({5, 3}, 4, [](int x, int y, int c) { return float(x + 10 * y + 100 * c); });
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
    const Image a = painted({4, 2}, 1, [](int x, int, int) { return float(x + 1); });
    const Image b = painted({4, 2}, 1, [](int, int y, int) { return float(y + 2); });
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
    Image image = painted({6, 4}, 1, [](int x, int y, int) { return float((x * 7 + y * 3) % 5) - 1.0f; });
    const Extremes found = extremesOf(image);
    check(found.smallest == -1.0f && found.largest == 3.0f, "the extremes");
    check(found.smallestAt.x == 0 && found.smallestAt.y == 0, "the first smallest, rows first");
    check(found.largestAt.x == 2 && found.largestAt.y == 0, "the first largest, rows first");
    check(countNonZero(image) == 24 - 5, "values not zero");
    std::vector<double> mean, deviation;
    const Image constant = painted({3, 3}, 2, [](int, int, int c) { return float(c + 1); });
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
    const Image a = painted({4, 2}, 1, [](int, int, int) { return 1.0f; });
    Image other = Image::create({3, 2}, Depth::F32, 1), three = Image::create({4, 2}, Depth::F32, 3);
    check(refused([&] { multiplyInto(a, other, other); }), "another size");
    check(refused([&] { channelInto(three, other, 0); }), "a buffer of the image's size");
    check(refused([&] { channelInto(three, three, 5); }), "a channel that exists");
    check(refused([&] { bitwiseNotInto(a, other); }), "bytes only");
    check(refused([&] { multiplyInto(three, std::vector<double>{1.0}, three); }), "a factor per channel");
}

void newImagesAndFills()
{
    const Image a = painted({4, 2}, 1, [](int x, int, int) { return float(x + 1); });
    check(multiplied(a, a).row<float>(1)[3] == 16.0f && atMost(a, 2.0f).row<float>(0)[3] == 2.0f, "new images");
    check(multiplied(Image(), Image()).empty() && merged(std::vector<Image>{}).empty(), "empty for empty");
    const Image rgb = merged({&a, &a, &a});
    check(rgb.channels() == 3 && at(withoutAlpha(merged({&a, &a, &a, &a})), 2, 1, 2) == 3.0f, "planes together");
    const Image rgba = opaque(rgb);
    check(rgba.channels() == 4 && at(rgba, 3, 1, 2) == 4.0f && at(rgba, 3, 1, 3) == 1.0f, "an alpha of one");

    // As OpenCV's Scalar: rounded half to even and saturated for integers.
    const Image bytes = ipr::filled({2, 2}, Depth::U8, 1, 2.5);
    check(bytes.row<std::uint8_t>(1)[1] == 2, "2.5 makes 2");
    check(ipr::filled({1, 1}, Depth::U8, 1, 300.0).row<std::uint8_t>(0)[0] == 255, "saturated");
    check(ipr::filled({1, 1}, Depth::S16, 1, -1.0).row<std::int16_t>(0)[0] == -1, "signed labels");
    check(ipr::filled({1, 1}, Depth::S32, 2, -7.0).row<std::int32_t>(0)[1] == -7, "signed indices");
    Image colour = Image::create({2, 1}, Depth::F32, 3);
    fill(colour, std::vector<double>{0.25, 0.5, 0.75});
    check(at(colour, 1, 0, 2) == 0.75f && at(colour, 0, 0, 0) == 0.25f, "a value per channel");
    Image mask = ipr::filled({2, 1}, Depth::U8, 1, 0.0);
    mask.mutableRow<std::uint8_t>(0)[1] = 1;
    fillWhere(colour, 9.0, mask);
    check(at(colour, 0, 0, 1) == 0.5f && at(colour, 1, 0, 1) == 9.0f, "only where the mask is");

    // A tile into its place, and back to floats with a scale.
    Image whole = ipr::filled({4, 4}, Depth::F32, 1, 0.0);
    Image place = whole.mutableView({1, 2, 2, 1});
    copyInto(ipr::filled({2, 1}, Depth::F32, 1, 3.0), place);
    check(at(whole, 2, 2) == 3.0f && at(whole, 0, 2) == 0.0f && at(whole, 2, 1) == 0.0f, "copied in place");
    Image counts = ipr::filled({2, 1}, Depth::U16, 1, 65535.0), scaled = Image::create({2, 1}, Depth::F32, 1);
    convertInto(counts, scaled, 1.0 / 65535.0);
    check(at(scaled, 1, 0) == 65535.0f * float(1.0 / 65535.0), "scaled in floats");

    const Image sum = mapped(a, a, a, [](float x, float y, float z) { return x + y * z; });
    check(at(sum, 2, 1) == 12.0f, "arithmetic in a line");
    Image twice = a.clone();
    mapInPlace(twice, [](float x) { return 2.0f * x; });
    check(at(twice, 3, 0) == 8.0f, "in place");
    check(largestDifference(twice, a) == 4.0 && largestDifference(a, a) == 0.0, "how far apart");
    check(largestDifference(Image(), Image()) == 0.0, "nothing apart");
}

void openCvsGenerator()
{
    // Against cv::RNG itself in iprImageTypesOracle; here, what does not need OpenCV.
    Random random(0x1b5d), again(0x1b5d);
    for (int i = 0; i < 100; ++i)
        check(random.uniform(0, 1000) == again.uniform(0, 1000), "the same seed draws the same");
    check(Random(0).next() == Random(0xffffffffu).next(), "a zero seed is OpenCV's default");
    check(random.uniform(5, 5) == 5, "an empty range gives its end");
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
        newImagesAndFills();
        openCvsGenerator();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image pixels: all passed\n";
    return 0;
}
