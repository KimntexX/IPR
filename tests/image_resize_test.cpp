#include "Resize.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

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

void aFlatImageStaysFlat()
{
    const Image flat = filled({37, 23}, 3, [](int, int, int c) { return 0.25f * float(c + 1); });
    for (const Interpolation interpolation : {Interpolation::Linear, Interpolation::Area})
        for (const Size size : {Size{9, 5}, Size{80, 61}, Size{1, 1}, Size{37, 7}, Size{100, 3}}) {
            const Image out = resized(flat, size, interpolation);
            check(out.size() == size && out.channels() == 3, "the size asked");
            for (int y = 0; y < size.height; ++y)
                for (int x = 0; x < size.width; ++x)
                    for (int c = 0; c < 3; ++c)
                        check(std::abs(at(out, x, y, c) - 0.25f * float(c + 1)) < 1e-6f, "flat stays flat");
        }
}

// A whole reduction averages each block; a fractional one weighs the pixels cut at its edges.
void areaAveragesWhatEachPixelCovers()
{
    const Image ramp = filled({8, 4}, 1, [](int x, int y, int) { return float(x + 10 * y); });
    const Image half = resized(ramp, {4, 2}, Interpolation::Area);
    check(std::abs(at(half, 0, 0) - (0 + 1 + 10 + 11) / 4.0f) < 1e-5f, "a block of four");
    check(std::abs(at(half, 3, 1) - (26 + 27 + 36 + 37) / 4.0f) < 1e-5f, "the last block");
    const Image row = filled({3, 1}, 1, [](int x, int, int) { return float(x * 3); });
    const Image two = resized(row, {2, 1}, Interpolation::Area);
    // Each new pixel covers 1.5 pixels: 0 whole and half of 3, then half of 3 and 6 whole.
    check(std::abs(at(two, 0, 0) - (0.0f + 0.5f * 3.0f) / 1.5f) < 1e-5f, "a pixel cut in half");
    check(std::abs(at(two, 1, 0) - (0.5f * 3.0f + 6.0f) / 1.5f) < 1e-5f, "on both sides");
}

// Centres aligned: doubling 0 and 1 gives a quarter and three quarters between, the edges kept.
void linearAlignsPixelCentres()
{
    const Image two = filled({2, 1}, 1, [](int x, int, int) { return float(x); });
    const Image four = resized(two, {4, 1}, Interpolation::Linear);
    const float expected[4] = {0.0f, 0.25f, 0.75f, 1.0f};
    for (int x = 0; x < 4; ++x)
        check(std::abs(at(four, x, 0) - expected[x]) < 1e-6f, "centres aligned, edges held");
    // Enlarging with Area holds each pixel whole, then blends only across its edge.
    const Image held = resized(two, {4, 1}, Interpolation::Area);
    check(at(held, 0, 0) == 0.0f && at(held, 3, 0) == 1.0f, "area enlarging keeps the ends");
    check(at(held, 1, 0) == 0.0f, "and the first pixel whole until its edge");
}

void aScaleGivesItsRoundedSize()
{
    const Image image = filled({10, 7}, 2, [](int x, int y, int c) { return float(x + y + c); });
    check(resizedBy(image, 0.35, 0.5, Interpolation::Area).size() == (Size{4, 4}), "rounded to even, as OpenCV");
    check(resizedBy(image, 2.0, 3.0, Interpolation::Linear).size() == (Size{20, 21}), "enlarged");
}

void threadsDoNotChangeTheResult()
{
    std::mt19937 random(9);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    const Image image = filled({701, 389}, 4, [&](int, int, int) { return uniform(random); });
    for (const Interpolation interpolation : {Interpolation::Linear, Interpolation::Area}) {
        const Image one = resized(image, {233, 129}, interpolation, 1);
        const Image many = resized(image, {233, 129}, interpolation, 8);
        for (int y = 0; y < 129; ++y)
            for (int v = 0; v < 233 * 4; ++v)
                check(one.row<float>(y)[v] == many.row<float>(y)[v], "bit for bit");
    }
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
    const Image image = Image::create({4, 4}, Depth::F32, 1);
    check(refused([&] { resized(Image(), {2, 2}, Interpolation::Linear); }), "no image");
    check(refused([&] { resized(image, {0, 2}, Interpolation::Linear); }), "no size");
    check(refused([&] { resized(Image::create({4, 4}, Depth::U16, 1), {2, 2}, Interpolation::Area); }), "another depth");
    check(refused([&] { resizedBy(image, 0.0, 1.0, Interpolation::Linear); }), "no scale");
    check(refused([&] { resizedBy(image, std::nan(""), 1.0, Interpolation::Linear); }), "not a number");
}

} // namespace

int main()
{
    try {
        aFlatImageStaysFlat();
        areaAveragesWhatEachPixelCovers();
        linearAlignsPixelCentres();
        aScaleGivesItsRoundedSize();
        threadsDoNotChangeTheResult();
        refusals();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image resize: all passed\n";
    return 0;
}
