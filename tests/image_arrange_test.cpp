#include "Arrange.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

Image numbered(Size size, int channels, Depth depth = Depth::U16)
{
    Image image = Image::create(size, depth, channels);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            for (int c = 0; c < channels; ++c) {
                const int value = (y * size.width + x) * channels + c;
                if (depth == Depth::U16)
                    image.mutableRow<std::uint16_t>(y)[x * channels + c] = std::uint16_t(value);
                else
                    image.mutableRow<float>(y)[x * channels + c] = float(value);
            }
    return image;
}

bool same(const Image &a, const Image &b)
{
    if (a.size() != b.size() || a.channels() != b.channels() || a.depth() != b.depth())
        return false;
    const size_t bytes = size_t(a.width()) * size_t(a.channels()) * depthBytes(a.depth());
    for (int y = 0; y < a.height(); ++y)
        for (size_t i = 0; i < bytes; ++i)
            if (a.row<unsigned char>(y)[i] != b.row<unsigned char>(y)[i])
                return false;
    return true;
}

std::uint16_t at(const Image &image, int x, int y, int c = 0)
{
    return image.row<std::uint16_t>(y)[x * image.channels() + c];
}

void turnsComeBackAfterFour()
{
    const Image image = numbered({5, 3}, 2);
    const Image once = turned(image, 1);
    check(once.size() == (Size{3, 5}), "a quarter turn swaps the sides");
    // Clockwise: the bottom-left pixel goes to the top-left.
    check(at(once, 0, 0, 1) == at(image, 0, 2, 1) && at(once, 2, 0) == at(image, 0, 0), "clockwise");
    check(same(turned(turned(turned(once, 1), 1), 1), image), "four quarter turns");
    check(same(turned(image, 2), turned(once, 1)), "a half turn is two quarters");
    check(same(turned(image, 3), turned(turned(image, 2), 1)), "three quarters");
}

void mirrorsAndTranspose()
{
    const Image image = numbered({4, 3}, 3);
    Image out = Image::create(image.size(), image.depth(), 3), back = Image::create(image.size(), image.depth(), 3);
    mirrorInto(image, out, true, false);
    check(at(out, 0, 1, 2) == at(image, 3, 1, 2), "left to right");
    mirrorInto(out, back, true, false);
    check(same(back, image), "twice is nothing");
    mirrorInto(image, out, true, true);
    check(same(out, turned(image, 2)), "both mirrors are a half turn");
    Image across = Image::create({3, 4}, image.depth(), 3), again = Image::create(image.size(), image.depth(), 3);
    transposeInto(image, across);
    check(at(across, 2, 1) == at(image, 1, 2), "rows become columns");
    transposeInto(across, again);
    check(same(again, image), "transposed twice");
}

void paddingMirrorsWithoutTheEdge()
{
    const Image image = numbered({3, 2}, 1);
    Image out = Image::create({3 + 2 + 1, 2 + 1 + 2}, image.depth(), 1);
    padInto(image, out, 1, 2, 2, 1);
    // Columns read c b | a b c | b, rows b | a b | a b.
    check(at(out, 0, 1) == at(image, 2, 0) && at(out, 1, 1) == at(image, 1, 0), "mirrored on the left");
    check(at(out, 5, 1) == at(image, 1, 0), "and on the right");
    check(at(out, 2, 0) == at(image, 0, 1) && at(out, 2, 3) == at(image, 0, 0), "above and below");
}

// A region takes the image's own pixels around it, mirrored only beyond the image.
void aRegionReadsItsNeighbours()
{
    const Image image = numbered({8, 6}, 2);
    Image out = Image::create({3 + 2 + 2, 2 + 2 + 2}, image.depth(), 2);
    padRegionInto(image, out, {1, 3, 3, 2}, 2, 2, 2, 2);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 7; ++x) {
            const int sx = 1 - 2 + x, sy = 3 - 2 + y;
            const int mx = sx < 0 ? -sx : sx >= 8 ? 2 * 7 - sx : sx;
            const int my = sy < 0 ? -sy : sy >= 6 ? 2 * 5 - sy : sy;
            check(at(out, x, y, 1) == at(image, mx, my, 1), "its neighbours, then the mirror");
        }
}

void thePyramidAveragesFiveByFive()
{
    Image flat = Image::create({9, 7}, Depth::F32, 2);
    for (int y = 0; y < 7; ++y)
        for (int v = 0; v < 18; ++v)
            flat.mutableRow<float>(y)[v] = v % 2 ? 0.25f : 0.75f;
    Image half = Image::create(pyramidDownSize(flat.size()), Depth::F32, 2);
    check(half.size() == (Size{5, 4}), "rounded up");
    pyramidDownInto(flat, half);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            check(std::abs(half.row<float>(y)[x * 2] - 0.75f) < 1e-6f && std::abs(half.row<float>(y)[x * 2 + 1] - 0.25f) < 1e-6f,
                  "flat stays flat");
    // One bright pixel in the middle spreads as 6 x 6 / 256 at its own place.
    Image dot = Image::create({9, 9}, Depth::F32, 1);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
            dot.mutableRow<float>(y)[x] = x == 4 && y == 4 ? 1.0f : 0.0f;
    Image small = Image::create(pyramidDownSize(dot.size()), Depth::F32, 1);
    pyramidDownInto(dot, small);
    check(std::abs(small.row<float>(2)[2] - 36.0f / 256.0f) < 1e-7f, "the centre weight");
    check(std::abs(small.row<float>(1)[2] - 6.0f / 256.0f) < 1e-7f, "two rows away");
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
    const Image image = numbered({4, 3}, 1);
    Image wrong = Image::create({4, 3}, Depth::U16, 1), floats = Image::create({3, 2}, Depth::F32, 1);
    check(refused([&] { turnInto(image, wrong, 1); }), "a turn needs the swapped size");
    check(refused([&] { Image none; mirrorInto(none, wrong, true, false); }), "no image");
    check(refused([&] { padInto(image, wrong, -1, 0, 0, 0); }), "no negative margin");
    check(refused([&] { Image out = Image::create({2, 2}, Depth::U16, 1); pyramidDownInto(image, out); }), "the pyramid reads floats");
    check(refused([&] { pyramidDownInto(numbered({4, 3}, 1, Depth::F32), floats); }), "the size it gives");
}

} // namespace

int main()
{
    try {
        turnsComeBackAfterFour();
        mirrorsAndTranspose();
        paddingMirrorsWithoutTheEdge();
        aRegionReadsItsNeighbours();
        thePyramidAveragesFiveByFive();
        refusals();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image arrange: all passed\n";
    return 0;
}
