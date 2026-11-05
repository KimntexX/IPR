#include "Filters.h"
#include "Parallel.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

Image noisy(Size size, int channels, unsigned seed = 3)
{
    Image image = Image::create(size, Depth::F32, channels);
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
    for (int y = 0; y < size.height; ++y) {
        float *row = image.mutableRow<float>(y);
        for (int v = 0; v < size.width * channels; ++v)
            row[v] = uniform(random);
    }
    return image;
}

bool identical(const Image &a, const Image &b)
{
    if (a.size() != b.size() || a.channels() != b.channels())
        return false;
    for (int y = 0; y < a.height(); ++y)
        if (std::memcmp(a.row<float>(y), b.row<float>(y), size_t(a.width()) * a.pixelBytes()) != 0)
            return false;
    return true;
}

void theRowsAreEachCoveredOnce()
{
    for (const int count : {1, 2, 7, 64, 1001})
        for (const int threads : {1, 2, 3, 8, 64}) {
            std::vector<std::atomic<int>> seen(static_cast<size_t>(count));
            parallelRows(count, [&](int first, int last) {
                for (int y = first; y < last; ++y)
                    ++seen[size_t(y)];
            }, threads);
            for (const std::atomic<int> &times : seen)
                check(times == 1, "each row once");
        }
    int calls = 0;
    parallelRows(0, [&](int, int) { ++calls; });
    parallelRows(-3, [&](int, int) { ++calls; });
    check(calls == 0, "nothing to do");
    check(workerThreads() >= 1 && workerThreads() <= 8, "the budget");
}

void anExceptionComesBackToTheCaller()
{
    for (const int threads : {1, 4}) {
        bool caught = false;
        try {
            parallelRows(100, [](int first, int) {
                if (first >= 0)
                    throw std::domain_error("from a worker");
            }, threads);
        } catch (const std::domain_error &) {
            caught = true;
        }
        check(caught, "thrown again on this thread, every thread joined");
    }
}

void theKernelIsNormalized()
{
    check(gaussianHalfKernel(0.0).empty() && gaussianHalfKernel(-1.0).empty(), "no sigma, no kernel");
    check(gaussianHalfKernel(std::numeric_limits<double>::quiet_NaN()).empty(), "not a number");
    check(gaussianHalfKernel(std::numeric_limits<double>::infinity()).empty(), "not finite");
    for (const double sigma : {0.3, 1.0, 2.5, 10.0, 40.0}) {
        const std::vector<float> half = gaussianHalfKernel(sigma);
        check(int(half.size()) == std::max(1, int(std::lround(sigma * 4))) + 1, "four sigmas");
        double total = half[0];
        for (size_t k = 1; k < half.size(); ++k) {
            total += 2.0 * half[k];
            check(half[k] < half[k - 1], "decreasing");
        }
        check(std::abs(total - 1.0) < 1e-5, "sums to one");
    }
}

void aBlurOfAnImpulseIsItsKernel()
{
    const std::vector<float> half = gaussianHalfKernel(2.0);
    Image image = Image::zeros({41, 41}, Depth::F32, 1);
    image.mutableRow<float>(20)[20] = 1.0f;
    const Image blurred = gaussianBlur(image, 2.0);
    for (int y = 12; y <= 28; ++y)
        for (int x = 12; x <= 28; ++x) {
            const float expected = half[size_t(std::abs(x - 20))] * half[size_t(std::abs(y - 20))];
            check(std::abs(blurred.row<float>(y)[x] - expected) < 1e-6f, "the kernel, on both axes");
        }
}

void aConstantStaysConstant()
{
    for (const int channels : {1, 3, 4}) {
        Image image = Image::create({23, 17}, Depth::F32, channels);
        for (int y = 0; y < 17; ++y)
            for (int v = 0; v < 23 * channels; ++v)
                image.mutableRow<float>(y)[v] = 0.25f + 0.1f * float(v % channels);
        const Image blurred = gaussianBlur(image, 3.0);
        for (int y = 0; y < 17; ++y)
            for (int v = 0; v < 23 * channels; ++v)
                check(std::abs(blurred.row<float>(y)[v] - image.row<float>(y)[v]) < 1e-5f, "each channel alone, up to the borders");
    }
}

void theMirrorIsTheOneAnnounced()
{
    // Three taps of known weights on a row: dcb|abcd|cba.
    const std::vector<float> half = gaussianHalfKernel(0.25);
    check(half.size() == 2, "one tap around the centre");
    Image image = Image::create({4, 1}, Depth::F32, 1);
    const float values[] = {1.0f, 2.0f, 4.0f, 8.0f};
    std::memcpy(image.mutableRow<float>(0), values, sizeof(values));
    const Image blurred = gaussianBlur(image, 0.25);
    // Along the height, one row is mirrored on itself: only its weight sum, one, remains.
    const float *row = blurred.row<float>(0);
    check(std::abs(row[0] - (half[0] * 1 + half[1] * (2 + 2))) < 1e-5f, "left border reads b twice");
    check(std::abs(row[3] - (half[0] * 8 + half[1] * (4 + 4))) < 1e-5f, "right border reads c twice");
    check(std::abs(row[1] - (half[0] * 2 + half[1] * (1 + 4))) < 1e-5f, "inside");
}

void theSizeDoesNotMatterToTheMirror()
{
    // Smaller than the kernel, even one pixel, on every axis.
    for (const Size size : {Size{1, 1}, Size{1, 9}, Size{9, 1}, Size{2, 2}, Size{3, 5}}) {
        Image image = noisy(size, 3);
        const Image blurred = gaussianBlur(image, 8.0);
        check(blurred.size() == size, "same size");
        for (int y = 0; y < size.height; ++y)
            for (int v = 0; v < size.width * 3; ++v)
                check(std::isfinite(blurred.row<float>(y)[v]), "finite");
    }
    Image single = Image::create({1, 1}, Depth::F32, 1);
    single.mutableRow<float>(0)[0] = 0.5f;
    check(std::abs(gaussianBlur(single, 5.0).row<float>(0)[0] - 0.5f) < 1e-5f, "one pixel keeps its value");
}

// The whole sum written out, for a row: the mirror is what is being checked.
void theEdgeMirrorRepeatsTheEdge()
{
    Image image = Image::create({4, 1}, Depth::F32, 1);
    const float values[4] = {1.0f, 2.0f, 4.0f, 8.0f};
    for (int x = 0; x < 4; ++x)
        image.mutableRow<float>(0)[x] = values[x];
    const std::vector<float> half = gaussianHalfKernel(1.0);
    const int radius = int(half.size()) - 1;
    check(radius == 4, "this sigma reaches past the row");
    // dcba|abcd|dcba, read at positions -4 to 7.
    const float reflected[12] = {8, 4, 2, 1, 1, 2, 4, 8, 8, 4, 2, 1};
    const Image blurred = gaussianBlur(image, 1.0, 1, BlurBorder::MirrorEdge);
    for (int x = 0; x < 4; ++x) {
        float expected = half[0] * reflected[x + 4];
        for (int k = 1; k <= radius; ++k)
            expected += half[size_t(k)] * (reflected[x + 4 - k] + reflected[x + 4 + k]);
        check(std::abs(blurred.row<float>(0)[x] - expected) < 1e-5f, "the edge is repeated");
    }
    const Image other = gaussianBlur(image, 1.0, 1, BlurBorder::Mirror);
    check(other.row<float>(0)[0] != blurred.row<float>(0)[0], "the two mirrors differ");
    // A flat image stays flat whatever the mirror, on every size against the kernel.
    for (const Size size : {Size{1, 1}, Size{2, 3}, Size{9, 1}, Size{40, 30}}) {
        Image flat = Image::create(size, Depth::F32, 2);
        for (int y = 0; y < size.height; ++y)
            for (int v = 0; v < size.width * 2; ++v)
                flat.mutableRow<float>(y)[v] = 0.25f;
        const Image out = gaussianBlur(flat, 6.0, 0, BlurBorder::MirrorEdge);
        for (int y = 0; y < size.height; ++y)
            for (int v = 0; v < size.width * 2; ++v)
                check(std::abs(out.row<float>(y)[v] - 0.25f) < 1e-5f, "flat stays flat");
    }
}

void theLastPixelRepeatsBeyondTheBorder()
{
    Image image = Image::create({4, 1}, Depth::F32, 1);
    const float values[4] = {1.0f, 2.0f, 4.0f, 8.0f};
    for (int x = 0; x < 4; ++x)
        image.mutableRow<float>(0)[x] = values[x];
    const std::vector<float> half = gaussianHalfKernel(1.0);
    const int radius = int(half.size()) - 1;
    const float extended[12] = {1, 1, 1, 1, 1, 2, 4, 8, 8, 8, 8, 8};
    const Image blurred = gaussianBlur(image, 1.0, 1, BlurBorder::LastPixel);
    for (int x = 0; x < 4; ++x) {
        float expected = half[0] * extended[x + 4];
        for (int k = 1; k <= radius; ++k)
            expected += half[size_t(k)] * (extended[x + 4 - k] + extended[x + 4 + k]);
        check(std::abs(blurred.row<float>(0)[x] - expected) < 1e-5f, "the last pixel repeats");
    }
}

// OpenCV's rule for floats: an odd size of 8 sigmas plus one, rounded to even.
void theRadiusIsTheOneOfOpenCv()
{
    const struct { double sigma; size_t size; } cases[] = {
        {0.5, 3}, {0.6, 4}, {0.8, 4}, {1.0, 5}, {1.1, 6}, {1.37, 7}, {2.0, 9}, {3.9, 17}, {10.0, 41},
    };
    for (const auto &c : cases)
        check(gaussianHalfKernel(c.sigma).size() == c.size, "the radius of a fractional sigma");
    check(gaussianHalfKernel(0.01).size() == 1, "a sigma far below a pixel has only its centre");
    Image one = noisy({9, 7}, 1);
    check(identical(gaussianBlur(one, 0.01, 1), one), "and changes nothing");
}

void aBufferReceivesTheSamePixels()
{
    const Image image = noisy({97, 61}, 3);
    Image out = Image::create(image.size(), Depth::F32, 3);
    gaussianBlurInto(image, out, 2.5, 0, BlurBorder::MirrorEdge);
    check(identical(out, gaussianBlur(image, 2.5, 0, BlurBorder::MirrorEdge)), "the same as a new image");
    gaussianBlurInto(image, out, 0.0);
    check(identical(out, image), "no sigma copies");
    const auto refused = [&](Image buffer) {
        try {
            gaussianBlurInto(image, buffer, 2.0);
        } catch (const std::invalid_argument &) {
            return true;
        }
        return false;
    };
    check(refused(Image::create({96, 61}, Depth::F32, 3)), "another size");
    check(refused(Image::create(image.size(), Depth::F32, 1)), "other channels");
    check(refused(Image::create(image.size(), Depth::U16, 3)), "another depth");
    check(refused(Image()), "no buffer");
    Image none;
    gaussianBlurInto(Image(), none, 2.0);
    check(none.empty(), "nothing into nothing");
}

void threadsDoNotChangeTheResult()
{
    const Image image = noisy({320, 211}, 3);
    const Image one = gaussianBlur(image, 4.0, 1);
    for (const int threads : {2, 3, 8, 32})
        check(identical(one, gaussianBlur(image, 4.0, threads)), "bit for bit");
}

void aPartOfAnImageHasItsRows()
{
    Image image = noisy({120, 90}, 3);
    const Image part = image.view({11, 7, 60, 40});
    const Image own = part.clone();
    check(identical(gaussianBlur(part, 3.0), gaussianBlur(own, 3.0)), "the step is followed");
}

void itLeavesItsInputAlone()
{
    Image image = noisy({64, 64}, 1);
    const Image before = image.clone();
    const Image blurred = gaussianBlur(image, 2.0);
    check(identical(image, before), "the input is unchanged");
    check(!image.frozen(), "and stays writable");
    check(!blurred.sharesPixelsWith(image) && !blurred.frozen(), "the result has its own pixels");
}

void nothingToBlur()
{
    check(gaussianBlur(Image(), 2.0).empty(), "empty");
    const Image image = noisy({8, 8}, 1);
    const Image same = gaussianBlur(image, 0.0);
    check(identical(same, image) && !same.sharesPixelsWith(image), "no sigma gives a clone");
    check(identical(gaussianBlur(image, -2.0), image), "a negative sigma too");
    bool refused = false;
    try {
        gaussianBlur(Image::zeros({4, 4}, Depth::U16, 1), 1.0);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "another depth");
}

void aFrozenImageCanBeBlurred()
{
    Image image = noisy({32, 32}, 3);
    const Image shared = image.share();
    check(identical(gaussianBlur(shared, 2.0), gaussianBlur(shared.clone(), 2.0)), "reading is all it needs");
}

} // namespace

int main()
{
    try {
        theRowsAreEachCoveredOnce();
        anExceptionComesBackToTheCaller();
        theKernelIsNormalized();
        aBlurOfAnImpulseIsItsKernel();
        aConstantStaysConstant();
        theMirrorIsTheOneAnnounced();
        theSizeDoesNotMatterToTheMirror();
        theEdgeMirrorRepeatsTheEdge();
        theLastPixelRepeatsBeyondTheBorder();
        theRadiusIsTheOneOfOpenCv();
        aBufferReceivesTheSamePixels();
        threadsDoNotChangeTheResult();
        aPartOfAnImageHasItsRows();
        itLeavesItsInputAlone();
        nothingToBlur();
        aFrozenImageCanBeBlurred();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image filters: all passed\n";
    return 0;
}
