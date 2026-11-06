#include "Image.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

template <typename Exception, typename Call>
void throws(const Call &call, const char *what)
{
    try {
        call();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error(what);
}

Image numbered(Size size)
{
    Image image = Image::create(size, Depth::F32, 3);
    for (int y = 0; y < size.height; ++y) {
        auto *row = image.mutableRow<Vec3f>(y);
        for (int x = 0; x < size.width; ++x)
            row[x] = Vec3f{{float(x), float(y), float(x * y)}};
    }
    return image;
}

void aCopyByAssignmentDoesNotCompile()
{
    static_assert(!std::is_copy_constructible<Image>::value, "no hidden copy");
    static_assert(!std::is_copy_assignable<Image>::value, "no hidden copy");
    static_assert(std::is_nothrow_move_constructible<Image>::value, "moves are cheap");
    static_assert(std::is_nothrow_move_assignable<Image>::value, "moves are cheap");
    static_assert(sizeof(Vec3f) == 3 * sizeof(float), "a Vec lies on the pixels");
    static_assert(sizeof(Vec4f) == 4 * sizeof(float), "a Vec lies on the pixels");
}

void creation()
{
    const Image image = Image::create({5, 3}, Depth::F32, 3);
    check(image.width() == 5 && image.height() == 3 && image.channels() == 3, "layout");
    check(image.depth() == Depth::F32 && image.pixelBytes() == 12 && image.stepBytes() == 60, "bytes");
    check(image.continuous(), "rows one after the other");
    check(!image.frozen(), "a new image can be written");
    check(reinterpret_cast<std::uintptr_t>(image.row<float>(0)) % 64 == 0, "aligned");
    check(depthBytes(Depth::U8) == 1 && depthBytes(Depth::U16) == 2 && depthBytes(Depth::F64) == 8, "depths");

    check(Image().empty(), "default is empty");
    check(Image::create({0, 7}, Depth::U8, 1).empty(), "no width");
    check(Image::create({7, 0}, Depth::U8, 1).empty(), "no height");
    check(Image::create({0, 0}, Depth::F32, 3).channels() == 3, "an empty image keeps its layout");
    throws<std::invalid_argument>([] { Image::create({-1, 3}, Depth::U8, 1); }, "negative width");
    throws<std::invalid_argument>([] { Image::create({3, -1}, Depth::U8, 1); }, "negative height");
    throws<std::invalid_argument>([] { Image::create({3, 3}, Depth::U8, 0); }, "no channel");
    throws<std::invalid_argument>([] { Image::create({3, 3}, Depth::U8, 5); }, "too many channels");

    const Image zeros = Image::zeros({4, 4}, Depth::F32, 1);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            check(zeros.row<float>(y)[x] == 0.0f, "zeros are zero");
}

void aSizeThatDoesNotFitIsRefused()
{
    throws<std::bad_alloc>([] { Image::create({2147483647, 2147483647}, Depth::F64, 4); }, "overflow is refused");
}

void rowsAreChecked()
{
    Image image = numbered({4, 2});
    throws<std::out_of_range>([&] { image.row<float>(2); }, "past the end");
    throws<std::out_of_range>([&] { image.row<float>(-1); }, "before the start");
    throws<std::out_of_range>([&] { image.mutableRow<float>(2); }, "past the end, writing");
    const Image none;
    throws<std::out_of_range>([&] { none.row<float>(0); }, "an empty image has no row");
}

void aCloneIsIndependent()
{
    Image image = numbered({6, 4});
    Image copy = image.clone();
    check(!copy.sharesPixelsWith(image), "new pixels");
    check(copy.row<Vec3f>(2)[3] == image.row<Vec3f>(2)[3], "same values");
    copy.mutableRow<Vec3f>(2)[3] = Vec3f{{9.0f, 9.0f, 9.0f}};
    check(image.row<Vec3f>(2)[3][0] == 3.0f, "the original does not follow");

    // A clone of a part, or of frozen pixels, has its own and can be written.
    const Image part = image.view({1, 1, 3, 2});
    Image own = part.clone();
    check(own.continuous() && !part.continuous(), "a clone packs its rows");
    check(own.row<Vec3f>(0)[0] == part.row<Vec3f>(0)[0], "same values in a part");
    check(!own.frozen(), "a clone can be written");
    own.mutableRow<Vec3f>(0)[0] = Vec3f{};
    check(Image().clone().empty(), "nothing to clone");
}

void aSharedImageNeverChanges()
{
    Image image = numbered({4, 4});
    const Image shared = image.share();
    check(shared.sharesPixelsWith(image), "same pixels");
    check(shared.frozen() && image.frozen(), "frozen for both");
    throws<std::logic_error>([&] { image.mutableRow<float>(0); }, "the first holder cannot write any more");
    throws<std::logic_error>([&] { image.mutableView({0, 0, 1, 1}); }, "nor give a writable part");
    Image second = shared.share();
    throws<std::logic_error>([&] { second.mutableRow<float>(0); }, "nor can another");
    check(shared.row<Vec3f>(1)[2][0] == 2.0f, "reading stays");
    check(shared.block() == image.block() && shared.blockBytes() == 4 * 4 * sizeof(Vec3f), "one block, counted once");
    check(image.view({1, 1, 2, 2}).block() == image.block() && image.clone().block() != image.block(), "a part is in it");
    check(Image().block() == nullptr && Image().blockBytes() == 0, "no block for nothing");

    // Even once the others are gone: only a clone writes.
    Image alone = numbered({2, 2});
    { const Image gone = alone.share(); }
    throws<std::logic_error>([&] { alone.mutableRow<float>(0); }, "frozen for good");
    check(Image().share().empty(), "nothing to share");
}

void aViewReadsAPart()
{
    Image image = numbered({6, 5});
    const Image part = image.view({2, 1, 3, 2});
    check(part.width() == 3 && part.height() == 2, "its size");
    check(part.row<Vec3f>(1)[2][0] == 4.0f && part.row<Vec3f>(1)[2][1] == 2.0f, "its pixels");
    check(!part.continuous(), "with the rows of the image");
    check(part.sharesPixelsWith(image) && image.frozen(), "a view is a sharing");

    const Image inner = part.view({1, 1, 2, 1});
    check(inner.row<Vec3f>(0)[0][0] == 3.0f && inner.row<Vec3f>(0)[0][1] == 2.0f, "a view of a view");

    check(image.view({0, 0, 7, 1}).empty(), "too wide");
    check(image.view({0, 0, 1, 6}).empty(), "too high");
    check(image.view({-1, 0, 1, 1}).empty(), "before the left");
    check(image.view({0, -1, 1, 1}).empty(), "before the top");
    check(image.view({5, 4, 2, 1}).empty(), "beyond the corner");
    check(image.view({2147483647, 0, 2147483647, 1}).empty(), "an overflowing rectangle");
    check(image.view({1, 1, 0, 1}).empty(), "no width");
    check(Image().view({0, 0, 1, 1}).empty(), "nothing to view");

    Image untouched = numbered({2, 2});
    check(untouched.view({0, 0, 3, 3}).empty() && !untouched.frozen(), "a refused view freezes nothing");
}

void aMutableViewWritesAPart()
{
    Image image = numbered({6, 5});
    Image tile = image.mutableView({1, 1, 2, 2});
    check(!tile.frozen() && !image.frozen(), "both stay writable");
    tile.mutableRow<Vec3f>(1)[1] = Vec3f{{7.0f, 8.0f, 9.0f}};
    check(image.row<Vec3f>(2)[2][0] == 7.0f, "the image sees it");
    check(image.mutableView({0, 0, 9, 9}).empty(), "outside is refused");

    // Sharing a tile freezes all the pixels: they cannot go on changing under a reader.
    const Image shared = tile.share();
    check(image.frozen(), "the whole buffer is frozen");
    throws<std::logic_error>([&] { tile.mutableRow<float>(0); }, "the tile too");
}

void movingLeavesNothingBehind()
{
    Image image = numbered({3, 3});
    Image moved = std::move(image);
    check(image.empty() && image.width() == 0 && image.channels() == 0, "moved from is empty");
    check(moved.row<Vec3f>(1)[1][0] == 1.0f, "the pixels follow");
    Image other = numbered({2, 2});
    other = std::move(moved);
    check(other.width() == 3 && moved.empty(), "move assignment");
    Image &self = other;
    other = std::move(self);
    check(other.width() == 3, "onto itself");
}

void wrappedPixels()
{
    std::vector<float> buffer(20 * 4, 1.5f);
    // Rows of three pixels, every four.
    Image image = Image::wrap(buffer.data(), {3, 4}, Depth::F32, 1, 4 * sizeof(float));
    check(!image.continuous() && image.row<float>(2)[1] == 1.5f, "the caller's layout");
    image.mutableRow<float>(1)[2] = 4.0f;
    check(buffer[1 * 4 + 2] == 4.0f, "written in the buffer");

    const std::vector<float> constant(4, 2.0f);
    Image readOnly = Image::wrapReadOnly(constant.data(), {2, 2}, Depth::F32, 1, 2 * sizeof(float));
    check(readOnly.frozen() && readOnly.row<float>(1)[1] == 2.0f, "read only from the start");
    throws<std::logic_error>([&] { readOnly.mutableRow<float>(0); }, "and stays so");

    throws<std::invalid_argument>([] { Image::wrap(nullptr, {1, 1}, Depth::U8, 1, 1); }, "no pixels");
    throws<std::invalid_argument>([&] { Image::wrap(buffer.data(), {3, 1}, Depth::F32, 1, 2 * sizeof(float)); }, "too short a step");
    throws<std::invalid_argument>([&] { Image::wrap(buffer.data(), {3, 1}, Depth::F32, 1, -12); }, "a negative step");
    check(Image::wrap(nullptr, {0, 0}, Depth::U8, 1, 0).empty(), "no size needs no pixels");
}

void theOwnerGoesWithTheLastImage()
{
    auto alive = std::make_shared<std::atomic<int>>(0);
    struct Owner
    {
        std::shared_ptr<std::atomic<int>> alive;
        std::vector<float> pixels = std::vector<float>(16, 3.0f);
        explicit Owner(std::shared_ptr<std::atomic<int>> a) : alive(std::move(a)) { ++*alive; }
        ~Owner() { --*alive; }
    };
    {
        auto owner = std::make_shared<Owner>(alive);
        float *pixels = owner->pixels.data();
        Image first = Image::wrap(pixels, {4, 4}, Depth::F32, 1, 4 * sizeof(float), owner);
        owner.reset();
        check(*alive == 1, "the image keeps it");
        Image part = first.view({1, 1, 2, 2});
        first = Image();
        check(*alive == 1, "a view keeps it after the image");
        check(part.row<float>(1)[1] == 3.0f, "still readable");
    }
    check(*alive == 0, "released with the last one");

    // The same with pixels the image layer allocated: a view outlives its image.
    Image view;
    {
        const Image image = numbered({8, 8});
        view = image.view({2, 2, 4, 4});
    }
    check(view.row<Vec3f>(1)[1][0] == 3.0f, "alive through the view");
}

void sharingIsReadByPixels()
{
    Image a = numbered({4, 4}), b = numbered({4, 4});
    check(!a.sharesPixelsWith(b), "two images");
    check(!a.sharesPixelsWith(Image()) && !Image().sharesPixelsWith(a), "nothing shares with nothing");

    // Two images wrapped apart on the same memory: the danger a type must see.
    std::vector<float> buffer(16);
    const Image first = Image::wrap(buffer.data(), {4, 2}, Depth::F32, 1, 4 * sizeof(float));
    const Image second = Image::wrap(buffer.data() + 4, {4, 2}, Depth::F32, 1, 4 * sizeof(float));
    const Image third = Image::wrap(buffer.data() + 12, {4, 1}, Depth::F32, 1, 4 * sizeof(float));
    check(first.sharesPixelsWith(second), "overlapping");
    check(!first.sharesPixelsWith(third), "apart");
}

void readingFromSeveralThreads()
{
    Image image = numbered({256, 256});
    const Image shared = image.share();
    std::atomic<int> wrong{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t)
        readers.emplace_back([&shared, &wrong, t] {
            for (int pass = 0; pass < 64; ++pass) {
                const Image mine = shared.share();
                const Image part = mine.view({t, t, 128, 128});
                for (int y = 0; y < part.height(); ++y)
                    if (part.row<Vec3f>(y)[3][0] != float(t + 3))
                        ++wrong;
            }
        });
    for (std::thread &reader : readers)
        reader.join();
    check(wrong == 0, "every thread reads the same");
}

void writingSeparateRowsFromSeveralThreads()
{
    Image image = Image::zeros({64, 64}, Depth::F32, 1);
    std::vector<Image> tiles;
    for (int t = 0; t < 4; ++t)
        tiles.push_back(image.mutableView({0, t * 16, 64, 16}));
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
        writers.emplace_back([&tiles, t] {
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 64; ++x)
                    tiles[size_t(t)].mutableRow<float>(y)[x] = float(t + 1);
        });
    for (std::thread &writer : writers)
        writer.join();
    for (int y = 0; y < 64; ++y)
        check(image.row<float>(y)[y] == float(y / 16 + 1), "each tile wrote its part");
}

void smallValues()
{
    check(Size{}.empty() && !Size{1, 1}.empty() && Size{1, 0}.empty(), "sizes");
    check(Size{2, 3} == Size{2, 3} && Size{2, 3} != Size{3, 2}, "size equality");
    check(Point2i{1, 2} + Point2i{3, 4} == Point2i{4, 6} && Point2i{1, 2} - Point2i{3, 4} == Point2i{-2, -2}, "points");
    check(Point2d{1.5, 2.0} * 2.0 == Point2d{3.0, 4.0}, "a scaled point");

    const Recti r{1, 2, 3, 4};
    check(r.right() == 4 && r.bottom() == 6, "rectangle sides");
    check(r.contains(Point2i{1, 2}) && !r.contains(Point2i{4, 2}) && !r.contains(Point2i{1, 6}), "contains, its far sides out");
    check((r & Recti{2, 3, 10, 1}) == Recti{2, 3, 2, 1}, "common part");
    check((r & Recti{4, 2, 1, 1}).empty(), "touching rectangles have nothing in common");
    check(Rectd{0, 0, 1, 0}.empty(), "empty rectangle");

    const Vec3d a{{1, 2, 3}}, b{{4, 5, 6}};
    check(a.dot(b) == 32.0, "dot");
    check(a + b == Vec3d{{5, 7, 9}} && b - a == Vec3d{{3, 3, 3}} && a * 2.0 == Vec3d{{2, 4, 6}}, "vector arithmetic");

    Matrix33d m = Matrix33d::identity();
    check(m * a == a, "identity keeps a vector");
    check(m * m == m, "identity times itself");
    m(0, 2) = 5.0;
    check((m * a)[0] == 16.0, "a translation in homogeneous coordinates");
    Matrix<double, 2, 3> wide;
    wide(0, 0) = 1; wide(1, 2) = 2;
    const Matrix<double, 3, 2> tall = [] { Matrix<double, 3, 2> t; t(0, 0) = 3; t(2, 1) = 4; return t; }();
    const Matrix<double, 2, 2> product = wide * tall;
    check(product(0, 0) == 3.0 && product(1, 1) == 8.0 && product(0, 1) == 0.0, "different sizes multiply");
}

} // namespace

int main()
{
    try {
        aCopyByAssignmentDoesNotCompile();
        creation();
        aSizeThatDoesNotFitIsRefused();
        rowsAreChecked();
        aCloneIsIndependent();
        aSharedImageNeverChanges();
        aViewReadsAPart();
        aMutableViewWritesAPart();
        movingLeavesNothingBehind();
        wrappedPixels();
        theOwnerGoesWithTheLastImage();
        sharingIsReadByPixels();
        readingFromSeveralThreads();
        writingSeparateRowsFromSeveralThreads();
        smallValues();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image core: all passed\n";
    return 0;
}
