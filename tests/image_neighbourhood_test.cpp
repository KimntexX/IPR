#include "Neighbourhood.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

Image same(const Image &image)
{
    return Image::create(image.size(), image.depth(), image.channels());
}

// The networks give the true median: checked against a full sort, on random windows, with ties.
void theMedianIsTheMiddleOfTheSortedWindow()
{
    std::mt19937 random(11);
    std::uniform_int_distribution<int> level(0, 6);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    for (const bool ties : {false, true}) {
        const Image image = filled({97, 61}, 2, [&](int, int, int) { return ties ? float(level(random)) : uniform(random); });
        for (const int side : {3, 5}) {
            Image out = same(image);
            medianInto(image, out, side);
            const int half = side / 2;
            for (int y = 0; y < 61; ++y)
                for (int x = 0; x < 97; ++x)
                    for (int c = 0; c < 2; ++c) {
                        std::vector<float> window;
                        for (int ky = -half; ky <= half; ++ky)
                            for (int kx = -half; kx <= half; ++kx)
                                window.push_back(at(image, std::clamp(x + kx, 0, 96), std::clamp(y + ky, 0, 60), c));
                        std::sort(window.begin(), window.end());
                        check(at(out, x, y, c) == window[window.size() / 2], "the middle of the sorted window");
                    }
        }
    }
}

// A ramp has a constant derivative inside: 2 per pixel times 4 for Sobel, and no Laplacian.
void derivativesOfARamp()
{
    const Image ramp = filled({12, 9}, 1, [](int x, int y, int) { return float(2 * x + 3 * y); });
    Image out = same(ramp);
    sobelInto(ramp, out, true);
    check(at(out, 5, 4) == 16.0f, "Sobel along x");
    sobelInto(ramp, out, false, 1.0 / 8.0);
    check(at(out, 5, 4) == 3.0f, "Sobel along y, scaled to one pixel");
    laplacianInto(ramp, out);
    check(at(out, 5, 4) == 0.0f, "no curvature");
    centralDifferenceInto(ramp, out, true);
    check(at(out, 5, 4) == 2.0f && at(out, 0, 4) == 1.0f, "half the step, the edge repeated");
}

void boxesAndExtremes()
{
    const Image flat = filled({20, 15}, 3, [](int, int, int c) { return 0.5f + 0.1f * float(c); });
    Image out = same(flat);
    boxMeanInto(flat, out, 7, 5, BlurBorder::LastPixel);
    check(std::abs(at(out, 0, 0, 2) - 0.7f) < 1e-6f && std::abs(at(out, 19, 14, 1) - 0.6f) < 1e-6f, "a flat mean");
    const Image dot = filled({9, 9}, 1, [](int x, int y, int) { return x == 4 && y == 4 ? 1.0f : 0.0f; });
    Image grown = same(dot);
    maximumInto(dot, grown, 3, 5, Beyond::Nothing);
    check(at(grown, 3, 2) == 1.0f && at(grown, 5, 6) == 1.0f && at(grown, 6, 4) == 0.0f && at(grown, 4, 7) == 0.0f,
          "the maximum reaches the window");
    Image bytes = Image::create({6, 4}, Depth::U8, 1), eroded = Image::create({6, 4}, Depth::U8, 1);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            bytes.mutableRow<std::uint8_t>(y)[x] = 255;
    minimumInto(bytes, eroded, 3, 3, Beyond::Nothing);
    check(eroded.row<std::uint8_t>(0)[0] == 255, "beyond nothing: the edge keeps its value");
    minimumInto(bytes, eroded, 3, 3, Beyond::Zero);
    check(eroded.row<std::uint8_t>(0)[0] == 0 && eroded.row<std::uint8_t>(2)[3] == 255, "beyond zero: the edge erodes");
}

void threadsDoNotChangeTheResult()
{
    std::mt19937 random(5);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    const Image image = filled({331, 207}, 3, [&](int, int, int) { return uniform(random); });
    const std::vector<std::function<void(Image &, int)>> filters = {
        [&](Image &o, int t) { sobelInto(image, o, true, 1.0, t); },
        [&](Image &o, int t) { medianInto(image, o, 5, t); },
        [&](Image &o, int t) { boxMeanInto(image, o, 9, 9, BlurBorder::LastPixel, t); },
        [&](Image &o, int t) { minimumInto(image, o, 7, 7, Beyond::Nothing, t); },
    };
    for (const auto &filter : filters) {
        Image one = same(image), many = same(image);
        filter(one, 1);
        filter(many, 8);
        for (int y = 0; y < 207; ++y)
            for (int v = 0; v < 331 * 3; ++v)
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
    const Image image = filled({5, 5}, 1, [](int, int, int) { return 0.0f; });
    Image out = same(image), small = Image::create({4, 5}, Depth::F32, 1), words = Image::create({5, 5}, Depth::U16, 1);
    check(refused([&] { medianInto(image, out, 7); }), "only 3 or 5");
    check(refused([&] { sobelInto(image, small, true); }), "another size");
    check(refused([&] { minimumInto(words, words, 3, 3, Beyond::Nothing); }), "floats or bytes");
    check(refused([&] { boxMeanInto(image, out, 0, 3, BlurBorder::Mirror); }), "a box has a size");
}

} // namespace

int main()
{
    try {
        theMedianIsTheMiddleOfTheSortedWindow();
        derivativesOfARamp();
        boxesAndExtremes();
        threadsDoNotChangeTheResult();
        refusals();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image neighbourhood: all passed\n";
    return 0;
}
