#include "Warp.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <random>
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

Matrix33d translation(double dx, double dy)
{
    Matrix33d m = Matrix33d::identity();
    m(0, 2) = dx;
    m(1, 2) = dy;
    return m;
}

const Resampling all[] = {Resampling::Linear, Resampling::Cubic, Resampling::Lanczos4};

// Read on whole pixels, every kernel gives each pixel back: its weights are one at the centre.
void wholePixelsComeBack()
{
    std::mt19937 random(4);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    const Image image = filled({23, 17}, 3, [&](int, int, int) { return uniform(random); });
    for (const Resampling resampling : all) {
        Image out = Image::create({20, 14}, Depth::F32, 3);
        warpPerspectiveInto(image, out, translation(2, 3), resampling, Outside::LastPixel);
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 20; ++x)
                for (int c = 0; c < 3; ++c)
                    check(std::abs(at(out, x, y, c) - at(image, x + 2, y + 3, c)) < 1e-6f, "a shift by whole pixels");
        const Image mapX = filled({20, 14}, 1, [](int x, int, int) { return float(x + 2); });
        const Image mapY = filled({20, 14}, 1, [](int, int y, int) { return float(y + 3); });
        Image remapped = Image::create({20, 14}, Depth::F32, 3);
        remapInto(image, remapped, mapX, mapY, resampling, Outside::Zero);
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 20; ++x)
                check(std::abs(at(remapped, x, y, 1) - at(image, x + 2, y + 3, 1)) < 1e-6f, "the same through maps");
    }
}

void aFlatImageStaysFlatInside()
{
    const Image flat = filled({30, 20}, 2, [](int, int, int c) { return 0.5f + 0.25f * float(c); });
    Matrix33d turn = Matrix33d::identity();
    turn(0, 0) = 0.9;
    turn(0, 1) = 0.2;
    turn(1, 0) = -0.2;
    turn(1, 1) = 0.9;
    turn(0, 2) = 4.3;
    turn(1, 2) = 2.7;
    for (const Resampling resampling : all) {
        Image out = Image::create({30, 20}, Depth::F32, 2);
        warpPerspectiveInto(flat, out, turn, resampling, Outside::LastPixel);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 30; ++x)
                check(std::abs(at(out, x, y, 1) - 0.75f) < 1e-5f, "the last pixel keeps a flat image flat");
    }
}

// Halfway between two pixels, linear takes their mean; far beyond, the last pixel or zero.
void betweenAndBeyond()
{
    const Image row = filled({4, 1}, 1, [](int x, int, int) { return float(x); });
    const Image mapX = filled({3, 1}, 1, [](int x, int, int) { return x == 0 ? 1.5f : x == 1 ? -9.0f : 40.0f; });
    const Image mapY = filled({3, 1}, 1, [](int, int, int) { return 0.0f; });
    Image out = Image::create({3, 1}, Depth::F32, 1);
    remapInto(row, out, mapX, mapY, Resampling::Linear, Outside::LastPixel);
    check(std::abs(at(out, 0, 0) - 1.5f) < 1e-6f, "the mean of two pixels");
    check(at(out, 1, 0) == 0.0f && at(out, 2, 0) == 3.0f, "the last pixel on each side");
    remapInto(row, out, mapX, mapY, Resampling::Linear, Outside::Zero);
    check(at(out, 1, 0) == 0.0f && at(out, 2, 0) == 0.0f, "zero beyond");
    const Image mapNaN = filled({3, 1}, 1, [](int, int, int) { return std::nanf(""); });
    remapInto(row, out, mapNaN, mapY, Resampling::Cubic, Outside::Zero);
    check(at(out, 0, 0) == 0.0f, "a position that is not a number reads beyond");
}

void threadsDoNotChangeTheResult()
{
    std::mt19937 random(7);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    const Image image = filled({301, 211}, 4, [&](int, int, int) { return uniform(random); });
    Matrix33d m = translation(3.3, -2.6);
    m(2, 0) = 1e-4;
    for (const Resampling resampling : all) {
        Image one = Image::create({290, 200}, Depth::F32, 4), many = Image::create({290, 200}, Depth::F32, 4);
        warpPerspectiveInto(image, one, m, resampling, Outside::Zero, 1);
        warpPerspectiveInto(image, many, m, resampling, Outside::Zero, 8);
        for (int y = 0; y < 200; ++y)
            for (int v = 0; v < 290 * 4; ++v)
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
    const Image image = Image::create({4, 4}, Depth::F32, 3);
    Image out = Image::create({4, 4}, Depth::F32, 3), other = Image::create({4, 4}, Depth::F32, 1);
    const Image map = Image::create({4, 4}, Depth::F32, 1), small = Image::create({3, 4}, Depth::F32, 1);
    check(refused([&] { Image none; warpPerspectiveInto(image, none, Matrix33d::identity(), Resampling::Linear, Outside::Zero); }), "no output");
    check(refused([&] { warpPerspectiveInto(image, other, Matrix33d::identity(), Resampling::Linear, Outside::Zero); }), "other channels");
    check(refused([&] { remapInto(image, out, small, map, Resampling::Linear, Outside::Zero); }), "a map of another size");
    check(refused([&] { remapInto(image, out, image, map, Resampling::Linear, Outside::Zero); }), "a map of three channels");
    check(refused([&] {
        Image words = Image::create({4, 4}, Depth::U16, 3);
        warpPerspectiveInto(words, out, Matrix33d::identity(), Resampling::Linear, Outside::Zero);
    }), "another depth");
}

} // namespace

int main()
{
    try {
        wholePixelsComeBack();
        aFlatImageStaysFlatInside();
        betweenAndBeyond();
        threadsDoNotChangeTheResult();
        refusals();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image warp: all passed\n";
    return 0;
}
