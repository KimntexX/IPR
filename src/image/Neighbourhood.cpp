#include "Neighbourhood.h"

#include "Parallel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ipr {

namespace {

// Where a position beyond [0, length) reads.
int beyondEdge(int position, int length, BlurBorder border)
{
    if (length == 1)
        return 0;
    if (border == BlurBorder::LastPixel)
        return std::clamp(position, 0, length - 1);
    const bool repeatsEdge = border == BlurBorder::MirrorEdge;
    const int period = repeatsEdge ? 2 * length : 2 * (length - 1);
    int wrapped = position % period;
    if (wrapped < 0)
        wrapped += period;
    if (wrapped < length)
        return wrapped;
    return repeatsEdge ? period - 1 - wrapped : period - wrapped;
}

void checkFloats(const Image &image, const Image &out)
{
    if (image.empty())
        throw std::invalid_argument("Nothing to filter");
    if (image.depth() != Depth::F32 || out.depth() != Depth::F32 || out.size() != image.size() ||
        out.channels() != image.channels())
        throw std::invalid_argument("The filter reads and writes 32-bit floats of one size and channels");
}

// The three rows around y, mirrored without repeating the edge.
struct Three
{
    const float *above, *here, *below;
};

Three rowsAround(const Image &image, int y)
{
    const int height = image.height();
    return {image.row<float>(beyondEdge(y - 1, height, BlurBorder::Mirror)), image.row<float>(y),
            image.row<float>(beyondEdge(y + 1, height, BlurBorder::Mirror))};
}

} // namespace

// As OpenCV's separable Sobel: the derivative [-1 0 1] unscaled and the smoothing [1 2 1] carrying
// the scale, each pass in the order of its small symmetric filters.
void sobelInto(const Image &image, Image &out, bool alongX, double scale, int threads)
{
    checkFloats(image, out);
    const int width = image.width(), channels = image.channels();
    const bool unit = scale == 1.0;
    const float near = float(1.0 * scale), centre = float(2.0 * scale);
    Image &target = out;
    parallelRows(image.height(), [&](int first, int last) {
        std::vector<float> rows(size_t(width) * size_t(channels) * 3);
        for (int y = first; y < last; ++y) {
            const Three three = rowsAround(image, y);
            const float *source[3] = {three.above, three.here, three.below};
            // Along each of the three rows first.
            for (int r = 0; r < 3; ++r) {
                float *row = rows.data() + size_t(r) * size_t(width) * size_t(channels);
                for (int x = 0; x < width; ++x) {
                    const int left = beyondEdge(x - 1, width, BlurBorder::Mirror) * channels;
                    const int right = beyondEdge(x + 1, width, BlurBorder::Mirror) * channels;
                    for (int c = 0; c < channels; ++c) {
                        const float a = source[r][left + c], b = source[r][x * channels + c], d = source[r][right + c];
                        if (alongX)
                            row[x * channels + c] = d - a;
                        else
                            row[x * channels + c] = unit ? a + b * 2 + d : b * centre + (a + d) * near;
                    }
                }
            }
            const size_t values = size_t(width) * size_t(channels);
            const float *r0 = rows.data(), *r1 = r0 + values, *r2 = r1 + values;
            float *target_row = target.mutableRow<float>(y);
            for (size_t v = 0; v < values; ++v)
                target_row[v] = alongX ? (unit ? r0[v] + r1[v] * 2 + r2[v] + 0.0f : (r0[v] + r2[v]) * near + r1[v] * centre + 0.0f)
                                       : r2[v] - r0[v] + 0.0f;
        }
    }, threads);
}

// As OpenCV's filter2D with this kernel: its non-zero weights added one by one, top to bottom.
void laplacianInto(const Image &image, Image &out, int threads)
{
    checkFloats(image, out);
    const int width = image.width(), channels = image.channels();
    Image &target = out;
    parallelRows(image.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const Three three = rowsAround(image, y);
            float *row = target.mutableRow<float>(y);
            for (int x = 0; x < width; ++x) {
                const int left = beyondEdge(x - 1, width, BlurBorder::Mirror) * channels;
                const int right = beyondEdge(x + 1, width, BlurBorder::Mirror) * channels;
                for (int c = 0; c < channels; ++c) {
                    const int at = x * channels + c;
                    float sum = 0.0f;
                    sum += 1.0f * three.above[at];
                    sum += 1.0f * three.here[left + c];
                    sum += -4.0f * three.here[at];
                    sum += 1.0f * three.here[right + c];
                    sum += 1.0f * three.below[at];
                    row[at] = sum;
                }
            }
        }
    }, threads);
}

void centralDifferenceInto(const Image &image, Image &out, bool alongX, int threads)
{
    checkFloats(image, out);
    const int width = image.width(), height = image.height(), channels = image.channels();
    Image &target = out;
    parallelRows(height, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            float *row = target.mutableRow<float>(y);
            const float *here = image.row<float>(y);
            const float *above = image.row<float>(beyondEdge(y - 1, height, BlurBorder::MirrorEdge));
            const float *below = image.row<float>(beyondEdge(y + 1, height, BlurBorder::MirrorEdge));
            for (int x = 0; x < width; ++x) {
                const int left = beyondEdge(x - 1, width, BlurBorder::MirrorEdge) * channels;
                const int right = beyondEdge(x + 1, width, BlurBorder::MirrorEdge) * channels;
                for (int c = 0; c < channels; ++c) {
                    const float before = alongX ? here[left + c] : above[x * channels + c];
                    const float after = alongX ? here[right + c] : below[x * channels + c];
                    float sum = 0.0f;
                    sum += -0.5f * before;
                    sum += 0.5f * after;
                    row[x * channels + c] = sum;
                }
            }
        }
    }, threads);
}

namespace {

// Selection networks for the median, Devillard's for 9 and 25 values: compare-and-swap pairs after
// which the middle place holds the median, whatever the values. Checked against a full sort in the
// tests.
const std::vector<std::pair<int, int>> &medianNetwork(int count)
{
    static const std::vector<std::pair<int, int>> nine = {
        {1, 2}, {4, 5}, {7, 8}, {0, 1}, {3, 4}, {6, 7}, {1, 2}, {4, 5}, {7, 8}, {0, 3},
        {5, 8}, {4, 7}, {3, 6}, {1, 4}, {2, 5}, {4, 7}, {4, 2}, {6, 4}, {4, 2}};
    static const std::vector<std::pair<int, int>> twentyFive = {
        {0, 1}, {3, 4}, {2, 4}, {2, 3}, {6, 7}, {5, 7}, {5, 6}, {9, 10}, {8, 10}, {8, 9},
        {12, 13}, {11, 13}, {11, 12}, {15, 16}, {14, 16}, {14, 15}, {18, 19}, {17, 19}, {17, 18}, {21, 22},
        {20, 22}, {20, 21}, {23, 24}, {2, 5}, {3, 6}, {0, 6}, {0, 3}, {4, 7}, {1, 7}, {1, 4},
        {11, 14}, {8, 14}, {8, 11}, {12, 15}, {9, 15}, {9, 12}, {13, 16}, {10, 16}, {10, 13}, {20, 23},
        {17, 23}, {17, 20}, {21, 24}, {18, 24}, {18, 21}, {19, 22}, {8, 17}, {9, 18}, {0, 18}, {0, 9},
        {10, 19}, {1, 19}, {1, 10}, {11, 20}, {2, 20}, {2, 11}, {12, 21}, {3, 21}, {3, 12}, {13, 22},
        {4, 22}, {4, 13}, {14, 23}, {5, 23}, {5, 14}, {15, 24}, {6, 24}, {6, 15}, {7, 16}, {7, 19},
        {13, 21}, {15, 23}, {7, 13}, {7, 15}, {1, 9}, {3, 11}, {5, 17}, {11, 17}, {9, 17}, {4, 10},
        {6, 12}, {7, 14}, {4, 6}, {4, 7}, {12, 14}, {10, 14}, {6, 7}, {10, 12}, {6, 10}, {6, 17},
        {12, 17}, {7, 17}, {7, 10}, {12, 18}, {7, 12}, {10, 18}, {12, 20}, {10, 20}, {10, 12}};
    return count == 9 ? nine : twentyFive;
}

} // namespace

// Each window's values side by side along the row, then the same comparisons for every pixel at
// once: the median is exact, and the loops are those a compiler can vectorize.
void medianInto(const Image &image, Image &out, int side, int threads)
{
    checkFloats(image, out);
    if (side != 3 && side != 5)
        throw std::invalid_argument("The median takes 3 x 3 or 5 x 5");
    const int width = image.width(), height = image.height(), channels = image.channels(), half = side / 2;
    const int count = side * side, middle = count / 2;
    const std::vector<std::pair<int, int>> &network = medianNetwork(count);
    Image &target = out;
    parallelRows(height, [&](int first, int last) {
        std::vector<float> values(size_t(count) * size_t(width));
        std::vector<const float *> rows(static_cast<size_t>(side));
        for (int y = first; y < last; ++y) {
            for (int k = 0; k < side; ++k)
                rows[size_t(k)] = image.row<float>(beyondEdge(y + k - half, height, BlurBorder::LastPixel));
            float *row = target.mutableRow<float>(y);
            for (int c = 0; c < channels; ++c) {
                for (int ky = 0; ky < side; ++ky)
                    for (int kx = 0; kx < side; ++kx) {
                        float *lane = values.data() + size_t(ky * side + kx) * size_t(width);
                        const float *in = rows[size_t(ky)] + c;
                        const int shift = kx - half;
                        const int inFirst = std::max(0, -shift), inEnd = std::min(width, width - shift);
                        for (int x = 0; x < inFirst; ++x)
                            lane[x] = in[std::clamp(x + shift, 0, width - 1) * channels];
                        for (int x = inFirst; x < inEnd; ++x)
                            lane[x] = in[(x + shift) * channels];
                        for (int x = std::max(inEnd, 0); x < width; ++x)
                            lane[x] = in[std::clamp(x + shift, 0, width - 1) * channels];
                    }
                for (const auto &[a, b] : network) {
                    float *low = values.data() + size_t(a) * size_t(width);
                    float *high = values.data() + size_t(b) * size_t(width);
                    for (int x = 0; x < width; ++x) {
                        const float l = low[x], h = high[x];
                        low[x] = l < h ? l : h;
                        high[x] = l < h ? h : l;
                    }
                }
                const float *median = values.data() + size_t(middle) * size_t(width);
                for (int x = 0; x < width; ++x)
                    row[x * channels + c] = median[x];
            }
        }
    }, threads);
}

// As OpenCV's normalized box filter for floats: running sums in double along each row, then down
// each column from the top, scaled at the end. Columns are spread over threads, rows never: a
// running sum restarted mid-column would round otherwise.
void boxMeanInto(const Image &image, Image &out, int width, int height, BlurBorder border, int threads)
{
    checkFloats(image, out);
    if (width < 1 || height < 1)
        throw std::invalid_argument("The box has a size");
    const int columns = image.width(), rowsCount = image.height(), channels = image.channels();
    const int left = width / 2, top = height / 2;
    const size_t values = size_t(columns) * size_t(channels);
    std::vector<double> sums(values * size_t(rowsCount));
    parallelRows(rowsCount, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const float *in = image.row<float>(y);
            double *row = sums.data() + values * size_t(y);
            for (int c = 0; c < channels; ++c) {
                const auto at = [&](int x) { return double(in[beyondEdge(x - left, columns, border) * channels + c]); };
                double s = 0.0;
                for (int k = 0; k < width; ++k)
                    s += at(k);
                row[c] = s;
                for (int x = 1; x < columns; ++x) {
                    s += at(x + width - 1) - at(x - 1);
                    row[size_t(x) * size_t(channels) + size_t(c)] = s;
                }
            }
        }
    }, threads);
    const double scale = 1.0 / (double(width) * double(height));
    const auto sourceRow = [&](int y) { return sums.data() + values * size_t(beyondEdge(y - top, rowsCount, border)); };
    Image &target = out;
    const int chunks = int((values + 255) / 256);
    parallelRows(chunks, [&](int first, int last) {
        const size_t begin = size_t(first) * 256, end = std::min(values, size_t(last) * 256);
        std::vector<double> sum(end - begin, 0.0);
        for (int k = 0; k < height - 1; ++k) {
            const double *entering = sourceRow(k);
            for (size_t v = begin; v < end; ++v)
                sum[v - begin] += entering[v];
        }
        for (int y = 0; y < rowsCount; ++y) {
            const double *entering = sourceRow(y + height - 1), *leaving = sourceRow(y);
            float *row = target.mutableRow<float>(y);
            for (size_t v = begin; v < end; ++v) {
                const double s = sum[v - begin] + entering[v];
                row[v] = float(s * scale);
                sum[v - begin] = s - leaving[v];
            }
        }
    }, threads);
}

namespace {

template <typename T, bool Largest>
void extremeInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads)
{
    const int columns = image.width(), rows = image.height(), channels = image.channels();
    const int left = width / 2, top = height / 2;
    const size_t values = size_t(columns) * size_t(channels);
    const auto better = [](T a, T b) { return Largest ? std::max(a, b) : std::min(a, b); };
    // Along rows into a buffer, then down columns: a rectangle's extreme is separable.
    std::vector<T> across(values * size_t(rows));
    parallelRows(rows, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const T *in = image.row<T>(y);
            T *row = across.data() + values * size_t(y);
            for (int x = 0; x < columns; ++x)
                for (int c = 0; c < channels; ++c) {
                    bool outside = false;
                    T value = in[x * channels + c];
                    for (int k = -left; k < width - left; ++k) {
                        const int sx = x + k;
                        if (sx < 0 || sx >= columns) {
                            outside = true;
                            continue;
                        }
                        value = better(value, in[sx * channels + c]);
                    }
                    if (outside && beyond == Beyond::Zero)
                        value = better(value, T(0));
                    row[size_t(x) * size_t(channels) + size_t(c)] = value;
                }
        }
    }, threads);
    Image &target = out;
    parallelRows(rows, [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            T *row = target.mutableRow<T>(y);
            bool outside = false;
            for (size_t v = 0; v < values; ++v)
                row[v] = across[values * size_t(y) + v];
            for (int k = -top; k < height - top; ++k) {
                const int sy = y + k;
                if (sy < 0 || sy >= rows) {
                    outside = true;
                    continue;
                }
                const T *other = across.data() + values * size_t(sy);
                for (size_t v = 0; v < values; ++v)
                    row[v] = better(row[v], other[v]);
            }
            if (outside && beyond == Beyond::Zero)
                for (size_t v = 0; v < values; ++v)
                    row[v] = better(row[v], T(0));
        }
    }, threads);
}

template <bool Largest>
void extreme(const Image &image, Image &out, int width, int height, Beyond beyond, int threads)
{
    if (image.empty() || out.size() != image.size() || out.depth() != image.depth() || out.channels() != image.channels())
        throw std::invalid_argument("The buffer has not the image's size, depth or channels");
    if (width < 1 || height < 1)
        throw std::invalid_argument("The window has a size");
    if (image.depth() == Depth::F32)
        extremeInto<float, Largest>(image, out, width, height, beyond, threads);
    else if (image.depth() == Depth::U8)
        extremeInto<std::uint8_t, Largest>(image, out, width, height, beyond, threads);
    else
        throw std::invalid_argument("The window reads floats or bytes");
}

} // namespace

void minimumInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads)
{
    extreme<false>(image, out, width, height, beyond, threads);
}

void maximumInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads)
{
    extreme<true>(image, out, width, height, beyond, threads);
}

} // namespace ipr
