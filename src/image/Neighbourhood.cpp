#include "Neighbourhood.h"

#include "Parallel.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
            // Along each of the three rows first; the mirror only at the two edge pixels.
            for (int r = 0; r < 3; ++r) {
                float *row = rows.data() + size_t(r) * size_t(width) * size_t(channels);
                const float *s = source[r];
                const auto one = [&](int x) {
                    const int left = beyondEdge(x - 1, width, BlurBorder::Mirror) * channels;
                    const int right = beyondEdge(x + 1, width, BlurBorder::Mirror) * channels;
                    for (int c = 0; c < channels; ++c) {
                        const float a = s[left + c], b = s[x * channels + c], d = s[right + c];
                        row[x * channels + c] = alongX ? d - a : (unit ? a + b * 2 + d : b * centre + (a + d) * near);
                    }
                };
                one(0);
                const int insideEnd = (width - 1) * channels;
                if (alongX) {
                    for (int v = channels; v < insideEnd; ++v)
                        row[v] = s[v + channels] - s[v - channels];
                } else if (unit) {
                    for (int v = channels; v < insideEnd; ++v)
                        row[v] = s[v - channels] + s[v] * 2 + s[v + channels];
                } else {
                    for (int v = channels; v < insideEnd; ++v)
                        row[v] = s[v] * centre + (s[v - channels] + s[v + channels]) * near;
                }
                if (width > 1)
                    one(width - 1);
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
// each column from the top, scaled at the end. Bands of columns go to threads; each walks down
// the image once, keeping the row sums of its window in a ring that stays in the cache. A row's
// running sum starts at its band rather than at the row's start, a rounding of 1e-16 in double.
void boxMeanInto(const Image &image, Image &out, int width, int height, BlurBorder border, int threads)
{
    checkFloats(image, out);
    if (width < 1 || height < 1)
        throw std::invalid_argument("The box has a size");
    const int columns = image.width(), rows = image.height(), channels = image.channels();
    const int left = width / 2, top = height / 2;
    const double scale = 1.0 / (double(width) * double(height));
    constexpr int bandColumns = 256;
    const int bands = (columns + bandColumns - 1) / bandColumns;
    Image &target = out;
    parallelRows(bands, [&](int firstBand, int lastBand) {
        for (int band = firstBand; band < lastBand; ++band) {
            const int x0 = band * bandColumns, x1 = std::min(columns, x0 + bandColumns), bandWidth = x1 - x0;
            const size_t bandValues = size_t(bandWidth) * size_t(channels);
            // The band's columns of a row, extended by the window, and their running sums.
            std::vector<double> extended(size_t(bandWidth + width - 1) * size_t(channels));
            std::vector<double> ring(bandValues * size_t(height));
            std::vector<double> sum(bandValues, 0.0);
            std::vector<int> sources(size_t(bandWidth + width - 1));
            for (int k = 0; k < bandWidth + width - 1; ++k)
                sources[size_t(k)] = beyondEdge(x0 + k - left, columns, border) * channels;
            const auto rowSums = [&](int y, double *row) {
                const float *in = image.row<float>(beyondEdge(y, rows, border));
                for (int k = 0; k < bandWidth + width - 1; ++k)
                    for (int c = 0; c < channels; ++c)
                        extended[size_t(k) * size_t(channels) + size_t(c)] = double(in[sources[size_t(k)] + c]);
                for (int c = 0; c < channels; ++c) {
                    const double *e = extended.data() + c;
                    double s = 0.0;
                    for (int k = 0; k < width; ++k)
                        s += e[size_t(k) * size_t(channels)];
                    row[c] = s;
                    for (int x = 1; x < bandWidth; ++x) {
                        s += e[size_t(x + width - 1) * size_t(channels)] - e[size_t(x - 1) * size_t(channels)];
                        row[size_t(x) * size_t(channels) + size_t(c)] = s;
                    }
                }
            };
            // The first height - 1 rows of the first window, summed down.
            for (int k = 0; k < height - 1; ++k) {
                double *row = ring.data() + bandValues * size_t(k);
                rowSums(k - top, row);
                for (size_t v = 0; v < bandValues; ++v)
                    sum[v] += row[v];
            }
            for (int y = 0; y < rows; ++y) {
                double *entering = ring.data() + bandValues * size_t((y + height - 1) % height);
                rowSums(y + height - 1 - top, entering);
                const double *leaving = ring.data() + bandValues * size_t(y % height);
                float *row = target.mutableRow<float>(y) + size_t(x0) * size_t(channels);
                for (size_t v = 0; v < bandValues; ++v) {
                    const double s = sum[v] + entering[v];
                    row[v] = float(s * scale);
                    sum[v] = s - leaving[v];
                }
            }
        }
    }, threads);
}

namespace {

// What a window holds beyond the edge: the value that changes nothing, or zero.
template <typename T, bool Largest>
T padding(Beyond beyond)
{
    if (beyond == Beyond::Zero)
        return T(0);
    if constexpr (std::numeric_limits<T>::has_infinity)
        return Largest ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
    else
        return Largest ? std::numeric_limits<T>::lowest() : std::numeric_limits<T>::max();
}

// The extreme of every window of `window` lines, the lines `stride` values apart and padded already:
// line i of the result, its `values` values from `first`, is the extreme of lines i to
// i + window - 1. Doubling: extremes over 2, 4, 8 lines, each from the last, then two overlapping
// ones cover the window. Each step is one loop that reads ahead of what it writes.
template <typename T, bool Largest>
void slidingExtreme(T *lines, int count, size_t stride, size_t first, size_t values, int window)
{
    const auto better = [](T a, T b) { return Largest ? (a < b ? b : a) : (b < a ? b : a); };
    int span = 1;
    int reach = count;
    while (span * 2 <= window) {
        reach -= span;
        const size_t offset = size_t(span) * stride;
        for (int i = 0; i < reach; ++i) {
            T *line = lines + size_t(i) * stride + first;
            for (size_t v = 0; v < values; ++v)
                line[v] = better(line[v], line[v + offset]);
        }
        span *= 2;
    }
    if (span < window) {
        const size_t offset = size_t(window - span) * stride;
        for (int i = 0; i + window <= count; ++i) {
            T *line = lines + size_t(i) * stride + first;
            for (size_t v = 0; v < values; ++v)
                line[v] = better(line[v], line[v + offset]);
        }
    }
}

// Along one padded row, pixels as lines: each step is a single run along the row.
template <typename T, bool Largest>
void slidingAlongRow(T *row, int count, int channels, int window)
{
    const auto better = [](T a, T b) { return Largest ? (a < b ? b : a) : (b < a ? b : a); };
    const size_t total = size_t(count) * size_t(channels);
    int span = 1;
    size_t reach = total;
    while (span * 2 <= window) {
        const size_t offset = size_t(span) * size_t(channels);
        reach -= offset;
        for (size_t v = 0; v < reach; ++v)
            row[v] = better(row[v], row[v + offset]);
        span *= 2;
    }
    if (span < window) {
        const size_t offset = size_t(window - span) * size_t(channels);
        const size_t kept = size_t(count - window + 1) * size_t(channels);
        for (size_t v = 0; v < kept; ++v)
            row[v] = better(row[v], row[v + offset]);
    }
}

template <typename T, bool Largest>
void extremeInto(const Image &image, Image &out, int width, int height, Beyond beyond, int threads)
{
    const int columns = image.width(), rows = image.height(), channels = image.channels();
    const int left = width / 2, top = height / 2;
    const size_t values = size_t(columns) * size_t(channels);
    const T pad = padding<T, Largest>(beyond);
    // Rows padded above and below for the second pass; the first fills the rows in between.
    const int extendedRows = rows + height - 1;
    std::vector<T> across(values * size_t(extendedRows), pad);
    const int extendedColumns = columns + width - 1;
    parallelRows(rows, [&](int first, int last) {
        std::vector<T> line(size_t(extendedColumns) * size_t(channels));
        for (int y = first; y < last; ++y) {
            const T *in = image.row<T>(y);
            std::fill(line.begin(), line.begin() + std::ptrdiff_t(left) * channels, pad);
            std::copy(in, in + values, line.begin() + std::ptrdiff_t(left) * channels);
            std::fill(line.begin() + std::ptrdiff_t(left) * channels + std::ptrdiff_t(values), line.end(), pad);
            slidingAlongRow<T, Largest>(line.data(), extendedColumns, channels, width);
            std::copy(line.begin(), line.begin() + std::ptrdiff_t(values), across.begin() + std::ptrdiff_t(values) * (y + top));
        }
    }, threads);
    // Down the columns, in place, spread over threads by runs of columns.
    const int chunks = int((values + 1023) / 1024);
    parallelRows(chunks, [&](int first, int last) {
        const size_t begin = size_t(first) * 1024, end = std::min(values, size_t(last) * 1024);
        slidingExtreme<T, Largest>(across.data(), extendedRows, values, begin, end - begin, height);
    }, threads);
    Image &target = out;
    parallelRows(rows, [&](int first, int last) {
        for (int y = first; y < last; ++y)
            std::copy(across.begin() + std::ptrdiff_t(values) * y, across.begin() + std::ptrdiff_t(values) * (y + 1),
                      target.mutableRow<T>(y));
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
