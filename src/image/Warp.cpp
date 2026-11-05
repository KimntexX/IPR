#include "Warp.h"

#include "Parallel.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ipr {

namespace {

// Positions are read at a 1/32 of a pixel, and the weights taken from a table of each fraction.
constexpr int fractions = 32;

int tapsOf(Resampling resampling)
{
    return resampling == Resampling::Linear ? 2 : resampling == Resampling::Cubic ? 4 : 8;
}

// The weights of one axis at fraction x, as OpenCV computes them in floats.
void weightsAt(Resampling resampling, float x, float *weights)
{
    if (resampling == Resampling::Linear) {
        weights[0] = 1.0f - x;
        weights[1] = x;
    } else if (resampling == Resampling::Cubic) {
        const float a = -0.75f;
        weights[0] = ((a * (x + 1) - 5 * a) * (x + 1) + 8 * a) * (x + 1) - 4 * a;
        weights[1] = ((a + 2) * x - (a + 3)) * x * x + 1;
        weights[2] = ((a + 2) * (1 - x) - (a + 3)) * (1 - x) * (1 - x) + 1;
        weights[3] = 1.0f - weights[0] - weights[1] - weights[2];
    } else {
        constexpr double s45 = 0.70710678118654752440084436210485;
        constexpr double pi = 3.14159265358979323846;
        static const double cs[8][2] = {{1, 0}, {-s45, -s45}, {0, 1}, {s45, -s45}, {-1, 0}, {s45, s45}, {0, -1}, {-s45, s45}};
        if (x < 1.1920928955078125e-07f) {
            for (int i = 0; i < 8; ++i)
                weights[i] = 0.0f;
            weights[3] = 1.0f;
            return;
        }
        float sum = 0.0f;
        const double y0 = -(x + 3) * pi * 0.25, s0 = std::sin(y0), c0 = std::cos(y0);
        for (int i = 0; i < 8; ++i) {
            const double y = -(x + 3 - i) * pi * 0.25;
            weights[i] = float((cs[i][0] * s0 + cs[i][1] * c0) / (y * y));
            sum += weights[i];
        }
        sum = 1.0f / sum;
        for (int i = 0; i < 8; ++i)
            weights[i] *= sum;
    }
}

// For each fraction along y and along x, the taps x taps weights: the product of the two axes.
struct Table
{
    int taps = 0;
    std::vector<float> weights;

    explicit Table(Resampling resampling) : taps(tapsOf(resampling))
    {
        std::vector<float> axis(size_t(fractions) * size_t(taps));
        for (int i = 0; i < fractions; ++i)
            weightsAt(resampling, float(i) * (1.0f / fractions), axis.data() + i * taps);
        weights.resize(size_t(fractions) * fractions * size_t(taps) * size_t(taps));
        for (int fy = 0; fy < fractions; ++fy)
            for (int fx = 0; fx < fractions; ++fx)
                for (int ky = 0; ky < taps; ++ky)
                    for (int kx = 0; kx < taps; ++kx)
                        weights[((size_t(fy) * fractions + size_t(fx)) * size_t(taps) + size_t(ky)) * size_t(taps) + size_t(kx)] =
                            axis[size_t(fy * taps + ky)] * axis[size_t(fx * taps + kx)];
    }
    const float *at(int fy, int fx) const
    {
        return weights.data() + (size_t(fy) * fractions + size_t(fx)) * size_t(taps) * size_t(taps);
    }
};

// A position in 1/32 of a pixel: the whole pixel, rounded towards minus infinity, and the fraction.
void split(int position, int &whole, int &fraction)
{
    whole = position >= 0 ? position / fractions : -((-(position + 1)) / fractions) - 1;
    fraction = position - whole * fractions;
}

int roundedToInt(double value)
{
    if (!(value == value))
        return INT_MIN;
    value = std::max(double(INT_MIN), std::min(double(INT_MAX), value));
    return int(std::lrint(value));
}

// All the taps inside, added as OpenCV adds them there: the four of linear in one run, the others
// row by row. Taps and channels known at compile time; each channel in the same order as alone.
template <int Taps, int Channels>
void sumInside(const float *first, std::ptrdiff_t stepBytes, const float *w, float *out)
{
    float sum[Channels];
    for (int ky = 0; ky < Taps; ++ky) {
        const float *row = reinterpret_cast<const float *>(reinterpret_cast<const unsigned char *>(first) + ky * stepBytes);
        const float *rowWeights = w + ky * Taps;
        if constexpr (Taps == 2) {
            for (int c = 0; c < Channels; ++c) {
                sum[c] = ky == 0 ? row[c] * rowWeights[0] : sum[c] + row[c] * rowWeights[0];
                sum[c] += row[Channels + c] * rowWeights[1];
            }
        } else {
            float rowSum[Channels];
            for (int c = 0; c < Channels; ++c)
                rowSum[c] = row[c] * rowWeights[0];
            for (int kx = 1; kx < Taps; ++kx) {
                const float weight = rowWeights[kx];
                for (int c = 0; c < Channels; ++c)
                    rowSum[c] += row[kx * Channels + c] * weight;
            }
            for (int c = 0; c < Channels; ++c)
                sum[c] = ky == 0 ? rowSum[c] : sum[c] + rowSum[c];
        }
    }
    for (int c = 0; c < Channels; ++c)
        out[c] = sum[c];
}

template <int Taps>
bool sumInside(int channels, const float *first, std::ptrdiff_t stepBytes, const float *w, float *out)
{
    switch (channels) {
    case 1: sumInside<Taps, 1>(first, stepBytes, w, out); return true;
    case 2: sumInside<Taps, 2>(first, stepBytes, w, out); return true;
    case 3: sumInside<Taps, 3>(first, stepBytes, w, out); return true;
    case 4: sumInside<Taps, 4>(first, stepBytes, w, out); return true;
    default: return false;
    }
}

// One output pixel from the taps x taps pixels from (sx, sy), with these weights, rows first.
void readTaps(const Image &image, int taps, int sx, int sy, const float *w, Outside outside, float *out)
{
    const int channels = image.channels(), width = image.width(), height = image.height();
    if (sx >= 0 && sy >= 0 && sx + taps <= width && sy + taps <= height) {
        const float *first = image.row<float>(sy) + sx * channels;
        const std::ptrdiff_t step = image.stepBytes();
        const bool done = taps == 2   ? sumInside<2>(channels, first, step, w, out)
                          : taps == 4 ? sumInside<4>(channels, first, step, w, out)
                                      : sumInside<8>(channels, first, step, w, out);
        if (done)
            return;
    }
    // Some taps beyond: each tap alone, outside read as the last pixel or as zero.
    const bool zero = outside == Outside::Zero;
    bool any = false;
    for (int k = 0; k < taps && !any; ++k)
        any = (unsigned(sx + k) < unsigned(width));
    bool anyRow = false;
    for (int k = 0; k < taps && !anyRow; ++k)
        anyRow = (unsigned(sy + k) < unsigned(height));
    for (int c = 0; c < channels; ++c) {
        if (zero && (!any || !anyRow)) {
            out[c] = 0.0f;
            continue;
        }
        float sum = 0.0f;
        for (int ky = 0; ky < taps; ++ky) {
            int y = sy + ky;
            if (y < 0 || y >= height) {
                if (zero)
                    continue;
                y = std::clamp(y, 0, height - 1);
            }
            const float *row = image.row<float>(y);
            for (int kx = 0; kx < taps; ++kx) {
                int x = sx + kx;
                if (x < 0 || x >= width) {
                    if (zero)
                        continue;
                    x = std::clamp(x, 0, width - 1);
                }
                sum += row[x * channels + c] * w[ky * taps + kx];
            }
        }
        out[c] = sum;
    }
}

// From a position in 1/32 of a pixel, the weights read in the table.
void readAt(const Image &image, const Table &table, int positionX, int positionY, Outside outside, float *out)
{
    int sx = 0, sy = 0, fx = 0, fy = 0;
    split(positionX, sx, fx);
    split(positionY, sy, fy);
    const int before = table.taps / 2 - 1;
    readTaps(image, table.taps, sx - before, sy - before, table.at(fy, fx), outside, out);
}

// From an exact position, the weights computed for its own fraction.
void readExact(const Image &image, Resampling resampling, double x, double y, Outside outside, float *out)
{
    const int taps = tapsOf(resampling);
    if (!(x == x) || !(y == y)) {
        x = -1e9;
        y = -1e9;
    }
    x = std::max(-1e9, std::min(1e9, x));
    y = std::max(-1e9, std::min(1e9, y));
    const double wholeX = std::floor(x), wholeY = std::floor(y);
    float wx[8], wy[8], w[64];
    weightsAt(resampling, float(x - wholeX), wx);
    weightsAt(resampling, float(y - wholeY), wy);
    for (int ky = 0; ky < taps; ++ky)
        for (int kx = 0; kx < taps; ++kx)
            w[ky * taps + kx] = wy[ky] * wx[kx];
    const int before = taps / 2 - 1;
    readTaps(image, taps, int(wholeX) - before, int(wholeY) - before, w, outside, out);
}

void checkPair(const Image &image, const Image &out)
{
    if (image.empty() || out.empty())
        throw std::invalid_argument("A warp needs an image and a size");
    if (image.depth() != Depth::F32 || out.depth() != Depth::F32 || image.channels() != out.channels())
        throw std::invalid_argument("A warp reads and writes 32-bit floats of the same channels");
}

} // namespace

void remapInto(const Image &image, Image &out, const Image &mapX, const Image &mapY, Resampling resampling,
               Outside outside, int threads)
{
    checkPair(image, out);
    if (mapX.size() != out.size() || mapY.size() != out.size() || mapX.depth() != Depth::F32 ||
        mapY.depth() != Depth::F32 || mapX.channels() != 1 || mapY.channels() != 1)
        throw std::invalid_argument("A remap needs two maps of single floats, of the output's size");
    const Table table(resampling);
    const int channels = out.channels();
    Image &target = out;
    parallelRows(out.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const float *xs = mapX.row<float>(y), *ys = mapY.row<float>(y);
            float *row = target.mutableRow<float>(y);
            for (int x = 0; x < target.width(); ++x) {
                if (resampling != Resampling::Lanczos4) {
                    readExact(image, resampling, xs[x], ys[x], outside, row + x * channels);
                    continue;
                }
                const float px = xs[x] * float(fractions), py = ys[x] * float(fractions);
                const int positionX = px == px ? int(std::lrint(std::max(-2.1e9f, std::min(2.1e9f, px)))) : INT_MIN;
                const int positionY = py == py ? int(std::lrint(std::max(-2.1e9f, std::min(2.1e9f, py)))) : INT_MIN;
                readAt(image, table, positionX, positionY, outside, row + x * channels);
            }
        }
    }, threads);
}

void warpPerspectiveInto(const Image &image, Image &out, const Matrix33d &outToImage, Resampling resampling,
                         Outside outside, int threads)
{
    checkPair(image, out);
    const Table table(resampling);
    const int channels = out.channels(), width = out.width(), height = out.height();
    // OpenCV computes positions from the start of blocks of this width, and its rounding follows.
    constexpr int block = 32;
    const int blockWidth = std::min(block * block / std::min(block / 2, height), width);
    const double *m = outToImage.values.data();
    Image &target = out;
    parallelRows(height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            float *row = target.mutableRow<float>(y);
            for (int start = 0; start < width; start += blockWidth) {
                const double x0 = m[0] * start + m[1] * y + m[2];
                const double y0 = m[3] * start + m[4] * y + m[5];
                const double w0 = m[6] * start + m[7] * y + m[8];
                const int end = std::min(width, start + blockWidth);
                for (int x = start; x < end; ++x) {
                    const int offset = x - start;
                    if (resampling != Resampling::Lanczos4) {
                        // Exact positions, in double: OpenCV 5 computes them in floats, OpenCV 4 rounds
                        // them to a 1/32 of a pixel, and the two already disagree.
                        const double exactW = m[6] * x + m[7] * y + m[8];
                        const double px = exactW != 0.0 ? (m[0] * x + m[1] * y + m[2]) / exactW : 0.0;
                        const double py = exactW != 0.0 ? (m[3] * x + m[4] * y + m[5]) / exactW : 0.0;
                        readExact(image, resampling, px, py, outside, row + x * channels);
                        continue;
                    }
                    double w = w0 + m[6] * offset;
                    w = w != 0.0 ? fractions / w : 0.0;
                    const int positionX = roundedToInt((x0 + m[0] * offset) * w);
                    const int positionY = roundedToInt((y0 + m[3] * offset) * w);
                    readAt(image, table, positionX, positionY, outside, row + x * channels);
                }
            }
        }
    }, threads);
}

} // namespace ipr
