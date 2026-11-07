#include "Arrange.h"

#include "Parallel.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ipr {

namespace {

void checkBuffer(const Image &image, const Image &out, Size size)
{
    if (image.empty())
        throw std::invalid_argument("Nothing to arrange");
    if (out.size() != size || out.depth() != image.depth() || out.channels() != image.channels())
        throw std::invalid_argument("The buffer has not the size, depth or channels this gives");
}

size_t pixelBytes(const Image &image)
{
    return depthBytes(image.depth()) * size_t(image.channels());
}

const unsigned char *pixelAt(const Image &image, int x, int y)
{
    return image.row<unsigned char>(y) + size_t(x) * pixelBytes(image);
}

// Where a position beyond [0, length) reads, mirrored without repeating the edge.
int reflected(int position, int length)
{
    if (length == 1)
        return 0;
    const int period = 2 * (length - 1);
    int wrapped = position % period;
    if (wrapped < 0)
        wrapped += period;
    return wrapped < length ? wrapped : period - wrapped;
}

// Copies one pixel of a size known at compile time, so that the copy is a few moves.
template <size_t Bytes>
void copyPixel(unsigned char *to, const unsigned char *from)
{
    std::memcpy(to, from, Bytes);
}

// Every output pixel from one source pixel, given where it reads; rows spread over threads.
template <typename Source>
void gather(const Image &image, Image &out, const Source &source)
{
    const size_t bytes = pixelBytes(image);
    const auto copy = [&](unsigned char *to, const unsigned char *from) {
        switch (bytes) {
        case 1: copyPixel<1>(to, from); break;
        case 2: copyPixel<2>(to, from); break;
        case 4: copyPixel<4>(to, from); break;
        case 6: copyPixel<6>(to, from); break;
        case 8: copyPixel<8>(to, from); break;
        case 12: copyPixel<12>(to, from); break;
        case 16: copyPixel<16>(to, from); break;
        default: std::memcpy(to, from, bytes);
        }
    };
    Image &target = out;
    parallelRows(out.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            unsigned char *row = target.mutableRow<unsigned char>(y);
            for (int x = 0; x < target.width(); ++x) {
                int sx = 0, sy = 0;
                source(x, y, sx, sy);
                copy(row + size_t(x) * bytes, pixelAt(image, sx, sy));
            }
        }
    });
}

// Whole rows: output row y is source row sourceRow(y); output column x reads source column
// first + x, mirrored beyond the image, or the row reversed. The run inside is copied whole.
template <typename SourceRow>
void copyRows(const Image &image, Image &out, int first, bool reversed, const SourceRow &sourceRow)
{
    const size_t bytes = pixelBytes(image);
    const int width = image.width(), outWidth = out.width();
    const int inside = std::max(0, -first), insideEnd = std::min(outWidth, width - first);
    Image &target = out;
    parallelRows(out.height(), [&](int top, int bottom) {
        for (int y = top; y < bottom; ++y) {
            unsigned char *row = target.mutableRow<unsigned char>(y);
            const unsigned char *in = image.row<unsigned char>(sourceRow(y));
            if (reversed) {
                for (int x = 0; x < outWidth; ++x)
                    std::memcpy(row + size_t(x) * bytes, in + size_t(width - 1 - x) * bytes, bytes);
                continue;
            }
            if (insideEnd > inside)
                std::memcpy(row + size_t(inside) * bytes, in + size_t(first + inside) * bytes, size_t(insideEnd - inside) * bytes);
            for (int x = 0; x < std::min(inside, outWidth); ++x)
                std::memcpy(row + size_t(x) * bytes, in + size_t(reflected(first + x, width)) * bytes, bytes);
            for (int x = std::max(insideEnd, 0); x < outWidth; ++x)
                std::memcpy(row + size_t(x) * bytes, in + size_t(reflected(first + x, width)) * bytes, bytes);
        }
    });
}

} // namespace

void turnInto(const Image &image, Image &out, int quarterTurns)
{
    const int turns = ((quarterTurns % 4) + 4) % 4;
    const int width = image.width(), height = image.height();
    checkBuffer(image, out, turns % 2 ? Size{height, width} : image.size());
    gather(image, out, [&](int x, int y, int &sx, int &sy) {
        if (turns == 0) { sx = x; sy = y; }
        else if (turns == 1) { sx = y; sy = height - 1 - x; }
        else if (turns == 2) { sx = width - 1 - x; sy = height - 1 - y; }
        else { sx = width - 1 - y; sy = x; }
    });
}

void mirrorInto(const Image &image, Image &out, bool leftToRight, bool topToBottom)
{
    checkBuffer(image, out, image.size());
    const int height = image.height();
    copyRows(image, out, 0, leftToRight, [&](int y) { return topToBottom ? height - 1 - y : y; });
}

void transposeInto(const Image &image, Image &out)
{
    checkBuffer(image, out, {image.height(), image.width()});
    gather(image, out, [](int x, int y, int &sx, int &sy) {
        sx = y;
        sy = x;
    });
}

void padInto(const Image &image, Image &out, int top, int bottom, int left, int right)
{
    if (top < 0 || bottom < 0 || left < 0 || right < 0)
        throw std::invalid_argument("Margins are not negative");
    padRegionInto(image, out, {0, 0, image.width(), image.height()}, top, bottom, left, right);
}

void padRegionInto(const Image &image, Image &out, Rect<int> region, int top, int bottom, int left, int right)
{
    if (top < 0 || bottom < 0 || left < 0 || right < 0)
        throw std::invalid_argument("Margins are not negative");
    if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 || region.x + region.width > image.width() ||
        region.y + region.height > image.height())
        throw std::invalid_argument("The region lies within the image");
    checkBuffer(image, out, {region.width + left + right, region.height + top + bottom});
    const int height = image.height();
    copyRows(image, out, region.x - left, false, [&](int y) { return reflected(region.y - top + y, height); });
}

Size pyramidDownSize(Size size)
{
    return {(size.width + 1) / 2, (size.height + 1) / 2};
}

void pyramidDownInto(const Image &image, Image &out, int threads)
{
    checkBuffer(image, out, pyramidDownSize(image.size()));
    if (image.depth() != Depth::F32)
        throw std::invalid_argument("The pyramid reads 32-bit floats");
    const int width = image.width(), height = image.height(), channels = image.channels();
    const int outWidth = out.width();
    // Along each source row first, at the kept columns; then the five rows of each kept row.
    const auto across = [&](int y, float *row) {
        const float *in = image.row<float>(y);
        for (int x = 0; x < outWidth; ++x) {
            const int centre = 2 * x;
            if (centre >= 2 && centre + 2 < width) {
                const float *p = in + centre * channels;
                for (int c = 0; c < channels; ++c)
                    row[x * channels + c] = p[c] * 6 + (p[c - channels] + p[c + channels]) * 4 + p[c - 2 * channels] +
                                            p[c + 2 * channels];
                continue;
            }
            for (int c = 0; c < channels; ++c) {
                const auto at = [&](int offset) { return in[reflected(centre + offset, width) * channels + c]; };
                row[x * channels + c] = at(0) * 6 + (at(-1) + at(1)) * 4 + at(-2) + at(2);
            }
        }
    };
    Image &target = out;
    parallelRows(out.height(), [&](int first, int last) {
        const size_t values = size_t(outWidth) * size_t(channels);
        std::vector<float> rows(values * 5);
        for (int y = first; y < last; ++y) {
            for (int k = 0; k < 5; ++k)
                across(reflected(2 * y + k - 2, height), rows.data() + values * size_t(k));
            const float *r0 = rows.data(), *r1 = r0 + values, *r2 = r1 + values, *r3 = r2 + values, *r4 = r3 + values;
            float *row = target.mutableRow<float>(y);
            for (size_t v = 0; v < values; ++v)
                row[v] = (r0[v] + r4[v] + (r1[v] + r3[v]) * 4 + r2[v] * 6) * (1.0f / 256.0f);
        }
    }, threads);
}

Image turned(const Image &image, int quarterTurns)
{
    if (image.empty())
        return {};
    const Size size = quarterTurns % 2 ? Size(image.height(), image.width()) : image.size();
    Image out = Image::create(size, image.depth(), image.channels());
    turnInto(image, out, quarterTurns);
    return out;
}

Image mirrored(const Image &image, bool leftToRight, bool topToBottom)
{
    if (image.empty())
        return {};
    Image out = Image::create(image.size(), image.depth(), image.channels());
    mirrorInto(image, out, leftToRight, topToBottom);
    return out;
}

Image transposed(const Image &image)
{
    if (image.empty())
        return {};
    Image out = Image::create({image.height(), image.width()}, image.depth(), image.channels());
    transposeInto(image, out);
    return out;
}

Image padded(const Image &image, int top, int bottom, int left, int right)
{
    if (image.empty())
        return {};
    Image out = Image::create({image.width() + left + right, image.height() + top + bottom}, image.depth(),
                              image.channels());
    padInto(image, out, top, bottom, left, right);
    return out;
}

Image paddedRegion(const Image &image, Rect<int> region, int top, int bottom, int left, int right)
{
    if (image.empty())
        return {};
    Image out = Image::create({region.width + left + right, region.height + top + bottom}, image.depth(),
                              image.channels());
    padRegionInto(image, out, region, top, bottom, left, right);
    return out;
}

Image pyramidDown(const Image &image)
{
    if (image.empty())
        return {};
    Image out = Image::create(pyramidDownSize(image.size()), image.depth(), image.channels());
    pyramidDownInto(image, out);
    return out;
}

} // namespace ipr
