#include "Analysis.h"

#include "Parallel.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ipr {

bool symmetricEigen(const std::vector<double> &matrix, int n, std::vector<double> &values, std::vector<double> &vectors)
{
    if (n < 1 || matrix.size() != size_t(n) * size_t(n))
        return false;
    for (const double value : matrix)
        if (!std::isfinite(value))
            return false;
    // Cyclic Jacobi: rotations that zero each off-diagonal value in turn, until none is left.
    std::vector<double> a = matrix, v(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        v[size_t(i) * size_t(n) + size_t(i)] = 1.0;
    const auto at = [n](std::vector<double> &m, int r, int c) -> double & { return m[size_t(r) * size_t(n) + size_t(c)]; };
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0, scale = 0.0;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                (r == c ? scale : off) += at(a, r, c) * at(a, r, c);
        if (off <= 1e-30 * std::max(scale, 1e-300))
            break;
        for (int p = 0; p < n - 1; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = at(a, p, q);
                if (apq == 0.0)
                    continue;
                const double theta = (at(a, q, q) - at(a, p, p)) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = at(a, k, p), akq = at(a, k, q);
                    at(a, k, p) = c * akp - s * akq;
                    at(a, k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = at(a, p, k), aqk = at(a, q, k);
                    at(a, p, k) = c * apk - s * aqk;
                    at(a, q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = at(v, k, p), vkq = at(v, k, q);
                    at(v, k, p) = c * vkp - s * vkq;
                    at(v, k, q) = s * vkp + c * vkq;
                }
            }
    }
    std::vector<int> order(static_cast<size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return at(a, x, x) > at(a, y, y); });
    values.assign(size_t(n), 0.0);
    vectors.assign(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i) {
        values[size_t(i)] = at(a, order[size_t(i)], order[size_t(i)]);
        for (int k = 0; k < n; ++k)
            vectors[size_t(i) * size_t(n) + size_t(k)] = at(v, k, order[size_t(i)]);
    }
    return true;
}

bool solveCholesky(const std::vector<double> &a, const std::vector<double> &b, int n, std::vector<double> &x)
{
    if (n < 1 || a.size() != size_t(n) * size_t(n) || b.size() != size_t(n))
        return false;
    std::vector<double> l(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double s = a[size_t(i) * size_t(n) + size_t(j)];
            for (int k = 0; k < j; ++k)
                s -= l[size_t(i) * size_t(n) + size_t(k)] * l[size_t(j) * size_t(n) + size_t(k)];
            if (i == j) {
                // As OpenCV: a pivot below the precision of double is not positive.
                if (!(s >= std::numeric_limits<double>::epsilon()))
                    return false;
                l[size_t(i) * size_t(n) + size_t(i)] = std::sqrt(s);
            } else {
                l[size_t(i) * size_t(n) + size_t(j)] = s / l[size_t(j) * size_t(n) + size_t(j)];
            }
        }
    std::vector<double> y(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        double s = b[size_t(i)];
        for (int k = 0; k < i; ++k)
            s -= l[size_t(i) * size_t(n) + size_t(k)] * y[size_t(k)];
        y[size_t(i)] = s / l[size_t(i) * size_t(n) + size_t(i)];
    }
    x.assign(size_t(n), 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = y[size_t(i)];
        for (int k = i + 1; k < n; ++k)
            s -= l[size_t(k) * size_t(n) + size_t(i)] * x[size_t(k)];
        x[size_t(i)] = s / l[size_t(i) * size_t(n) + size_t(i)];
    }
    return true;
}

bool singularValues(const std::vector<double> &a, int rows, int cols, std::vector<double> &w, std::vector<double> &vt)
{
    if (rows < 1 || cols < 1 || a.size() != size_t(rows) * size_t(cols))
        return false;
    for (const double value : a)
        if (!std::isfinite(value))
            return false;
    // Columns of u rotated two by two until all are orthogonal; v keeps the rotations.
    std::vector<double> u = a, v(size_t(cols) * size_t(cols), 0.0);
    for (int i = 0; i < cols; ++i)
        v[size_t(i) * size_t(cols) + size_t(i)] = 1.0;
    const auto uAt = [&](int r, int c) -> double & { return u[size_t(r) * size_t(cols) + size_t(c)]; };
    const auto vAt = [&](int r, int c) -> double & { return v[size_t(r) * size_t(cols) + size_t(c)]; };
    for (int sweep = 0; sweep < 60; ++sweep) {
        bool rotated = false;
        for (int p = 0; p < cols - 1; ++p)
            for (int q = p + 1; q < cols; ++q) {
                double alpha = 0.0, beta = 0.0, gamma = 0.0;
                for (int r = 0; r < rows; ++r) {
                    alpha += uAt(r, p) * uAt(r, p);
                    beta += uAt(r, q) * uAt(r, q);
                    gamma += uAt(r, p) * uAt(r, q);
                }
                if (std::abs(gamma) <= 1e-15 * std::sqrt(alpha * beta) || gamma == 0.0)
                    continue;
                rotated = true;
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t = (zeta >= 0 ? 1.0 : -1.0) / (std::abs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t), s = c * t;
                for (int r = 0; r < rows; ++r) {
                    const double up = uAt(r, p), uq = uAt(r, q);
                    uAt(r, p) = c * up - s * uq;
                    uAt(r, q) = s * up + c * uq;
                }
                for (int r = 0; r < cols; ++r) {
                    const double vp = vAt(r, p), vq = vAt(r, q);
                    vAt(r, p) = c * vp - s * vq;
                    vAt(r, q) = s * vp + c * vq;
                }
            }
        if (!rotated)
            break;
    }
    std::vector<double> norms(static_cast<size_t>(cols), 0.0);
    for (int c = 0; c < cols; ++c) {
        double sum = 0.0;
        for (int r = 0; r < rows; ++r)
            sum += uAt(r, c) * uAt(r, c);
        norms[size_t(c)] = std::sqrt(sum);
    }
    std::vector<int> order(static_cast<size_t>(cols));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return norms[size_t(x)] > norms[size_t(y)]; });
    w.assign(size_t(cols), 0.0);
    vt.assign(size_t(cols) * size_t(cols), 0.0);
    for (int i = 0; i < cols; ++i) {
        w[size_t(i)] = norms[size_t(order[size_t(i)])];
        for (int k = 0; k < cols; ++k)
            vt[size_t(i) * size_t(cols) + size_t(k)] = vAt(k, order[size_t(i)]);
    }
    return true;
}

double determinant(const Matrix33d &m)
{
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
           m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

Matrix33d nearestRotation(const Matrix33d &m)
{
    // m = u s vt: v from the eigenvectors of mt m, u from m v / s.
    std::vector<double> mtm(9, 0.0);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                mtm[size_t(r * 3 + c)] += m(size_t(k), size_t(r)) * m(size_t(k), size_t(c));
    std::vector<double> values, vectors;
    symmetricEigen(mtm, 3, values, vectors);
    Matrix33d v, u;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            v(size_t(k), size_t(i)) = vectors[size_t(i * 3 + k)];
    for (int i = 0; i < 2; ++i) {
        const double s = std::sqrt(std::max(values[size_t(i)], 0.0));
        for (int k = 0; k < 3; ++k) {
            double sum = 0.0;
            for (int j = 0; j < 3; ++j)
                sum += m(size_t(k), size_t(j)) * v(size_t(j), size_t(i));
            u(size_t(k), size_t(i)) = s > 0.0 ? sum / s : (k == i ? 1.0 : 0.0);
        }
    }
    // The third column of u completes the two others: the cross product.
    u(0, 2) = u(1, 0) * u(2, 1) - u(2, 0) * u(1, 1);
    u(1, 2) = u(2, 0) * u(0, 1) - u(0, 0) * u(2, 1);
    u(2, 2) = u(0, 0) * u(1, 1) - u(1, 0) * u(0, 1);
    // Its sign as the third singular direction gives it: m v3 = s3 u3.
    double along = 0.0;
    for (int k = 0; k < 3; ++k) {
        double sum = 0.0;
        for (int j = 0; j < 3; ++j)
            sum += m(size_t(k), size_t(j)) * v(size_t(j), 2);
        along += sum * u(size_t(k), 2);
    }
    if (along < 0.0)
        for (int k = 0; k < 3; ++k)
            u(size_t(k), 2) = -u(size_t(k), 2);
    const auto product = [&]() {
        Matrix33d r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r(size_t(i), size_t(j)) += u(size_t(i), size_t(k)) * v(size_t(j), size_t(k));
        return r;
    };
    Matrix33d r = product();
    if (determinant(r) < 0.0) {
        for (int i = 0; i < 3; ++i)
            u(size_t(i), 2) = -u(size_t(i), 2);
        r = product();
    }
    return r;
}

void hannWindowInto(Image &out)
{
    if (out.empty() || out.depth() != Depth::F32 || out.channels() != 1)
        throw std::invalid_argument("A window of one channel of floats");
    const int rows = out.height(), cols = out.width();
    if (rows < 2 || cols < 2)
        throw std::invalid_argument("A window of two values or more each way");
    const double coeff0 = 2.0 * 3.14159265358979323846 / double(cols - 1);
    const double coeff1 = 2.0 * 3.14159265358979323846 / double(rows - 1);
    std::vector<double> across(static_cast<size_t>(cols));
    for (int j = 0; j < cols; ++j)
        across[size_t(j)] = 0.5 * (1.0 - std::cos(coeff0 * j));
    for (int i = 0; i < rows; ++i) {
        const double down = 0.5 * (1.0 - std::cos(coeff1 * i));
        float *row = out.mutableRow<float>(i);
        for (int j = 0; j < cols; ++j)
            row[j] = std::sqrt(float(down * across[size_t(j)]));
    }
}

namespace {

using Complex = std::complex<double>;

// The smallest size at least n made of factors 2, 3 and 5 only, as OpenCV's getOptimalDFTSize.
int optimalSize(int n)
{
    for (int size = std::max(n, 1);; ++size) {
        int rest = size;
        for (const int f : {2, 3, 5})
            while (rest % f == 0)
                rest /= f;
        if (rest == 1)
            return size;
    }
}

// The discrete Fourier transform of one length: its factors, largest radix first, each with the
// length left after it, and the n roots of unity.
struct Transform
{
    int n = 1;
    std::vector<int> factors;
    std::vector<Complex> roots;

    Transform(int length, bool inverse)
        : n(length)
    {
        int rest = length;
        for (const int f : {5, 3, 2})
            while (rest % f == 0) {
                rest /= f;
                factors.push_back(f);
                factors.push_back(rest);
            }
        if (rest != 1) {
            factors.push_back(rest);
            factors.push_back(1);
        }
        roots.resize(static_cast<size_t>(length));
        const double turn = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / double(length);
        for (int k = 0; k < length; ++k)
            roots[size_t(k)] = std::polar(1.0, turn * double(k));
    }

    // out[0..n) the transform of in[0], in[stride], ...; out and in apart.
    void run(Complex *out, const Complex *in, size_t stride) const { work(out, in, stride, 1, factors.data()); }

private:
    // Decimation in time: the p interleaved sequences of length m transformed into out, then
    // combined by p-point butterflies. step is how far apart the roots of this level are.
    void work(Complex *out, const Complex *in, size_t stride, int step, const int *factor) const
    {
        const int p = factor[0], m = factor[1];
        if (m == 1) {
            for (int q = 0; q < p; ++q)
                out[q] = in[size_t(q) * size_t(step) * stride];
        } else {
            for (int q = 0; q < p; ++q)
                work(out + size_t(q) * size_t(m), in + size_t(q) * size_t(step) * stride, stride, step * p, factor + 2);
        }
        Complex terms[5];
        std::vector<Complex> many(p > 5 ? size_t(p) : 0);
        Complex *const t = p > 5 ? many.data() : terms;
        for (int k = 0; k < m; ++k) {
            for (int q = 0; q < p; ++q)
                t[q] = out[size_t(q) * size_t(m) + size_t(k)];
            for (int u = 0; u < p; ++u) {
                const int at = k + u * m;
                const int advance = int((long long)(step) * at % n);
                Complex sum = t[0];
                for (int q = 1, root = advance; q < p; ++q, root = root + advance >= n ? root + advance - n : root + advance)
                    sum += t[q] * roots[size_t(root)];
                out[size_t(at)] = sum;
            }
        }
    }
};

// The two-dimensional transform of rows x cols values in place, rows then columns, in parallel.
void transform2d(std::vector<Complex> &values, int rows, int cols, bool inverse)
{
    const Transform across(cols, inverse), down(rows, inverse);
    parallelRows(rows, [&](int first, int last) {
        std::vector<Complex> line(static_cast<size_t>(cols));
        for (int y = first; y < last; ++y) {
            Complex *row = values.data() + size_t(y) * size_t(cols);
            std::copy(row, row + cols, line.begin());
            across.run(row, line.data(), 1);
        }
    });
    parallelRows(cols, [&](int first, int last) {
        std::vector<Complex> line(static_cast<size_t>(rows)), done(static_cast<size_t>(rows));
        for (int x = first; x < last; ++x) {
            for (int y = 0; y < rows; ++y)
                line[size_t(y)] = values[size_t(y) * size_t(cols) + size_t(x)];
            down.run(done.data(), line.data(), 1);
            for (int y = 0; y < rows; ++y)
                values[size_t(y) * size_t(cols) + size_t(x)] = done[size_t(y)];
        }
    });
}

} // namespace

Point<double> phaseCorrelation(const Image &a, const Image &b, const Image &window)
{
    const auto plane = [](const Image &image) { return !image.empty() && image.depth() == Depth::F32 && image.channels() == 1; };
    if (!plane(a) || !plane(b) || a.size() != b.size() || (!window.empty() && (!plane(window) || window.size() != a.size())))
        throw std::invalid_argument("Two planes of floats of one size, and their window");
    const int rows = optimalSize(a.height()), cols = optimalSize(a.width());
    // Both real images windowed and zero padded into one complex plane, a + i b: one transform for two.
    std::vector<Complex> z(size_t(rows) * size_t(cols), 0.0);
    for (int y = 0; y < a.height(); ++y) {
        const float *first = a.row<float>(y), *second = b.row<float>(y);
        const float *w = window.empty() ? nullptr : window.row<float>(y);
        for (int x = 0; x < a.width(); ++x)
            z[size_t(y) * size_t(cols) + size_t(x)] =
                w ? Complex(double(w[x] * first[x]), double(w[x] * second[x])) : Complex(double(first[x]), double(second[x]));
    }
    transform2d(z, rows, cols, false);
    // The transforms of a and b apart again: A = (Z(k) + conj Z(-k)) / 2, B = (Z(k) - conj Z(-k)) / 2i.
    // The cross power spectrum normalized as OpenCV does in floats: bins of their own conjugate are
    // real and divided by their size, the others by their size squared plus FLT_EPSILON.
    std::vector<Complex> c(z.size());
    const double eps = double(std::numeric_limits<float>::epsilon());
    for (int y = 0; y < rows; ++y) {
        const int yMirror = y == 0 ? 0 : rows - y;
        for (int x = 0; x < cols; ++x) {
            const int xMirror = x == 0 ? 0 : cols - x;
            const Complex here = z[size_t(y) * size_t(cols) + size_t(x)];
            const Complex there = std::conj(z[size_t(yMirror) * size_t(cols) + size_t(xMirror)]);
            const Complex p = 0.5 * (here + there) * std::conj((here - there) / Complex(0.0, 2.0));
            Complex &out = c[size_t(y) * size_t(cols) + size_t(x)];
            if (y == yMirror && x == xMirror)
                out = p.real() / (std::abs(p.real()) + eps);
            else {
                const double magnitude = std::abs(p);
                out = p * (magnitude / (magnitude * magnitude + eps));
            }
        }
    }
    transform2d(c, rows, cols, true);
    // Its real part, rolled by half a size each way as OpenCV's fftShift does, odd sizes included.
    const int xMid = cols >> 1, yMid = rows >> 1;
    std::vector<double> shifted(c.size());
    for (int y = 0; y < rows; ++y) {
        const int toY = (y + yMid) % rows;
        for (int x = 0; x < cols; ++x)
            shifted[size_t(toY) * size_t(cols) + size_t((x + xMid) % cols)] = c[size_t(y) * size_t(cols) + size_t(x)].real();
    }
    size_t best = 0;
    for (size_t i = 1; i < shifted.size(); ++i)
        if (shifted[i] > shifted[best])
            best = i;
    const int peakX = int(best % size_t(cols)), peakY = int(best / size_t(cols));
    // The weighted centroid of the 5 x 5 values around the peak.
    double sumX = 0.0, sumY = 0.0, sum = 0.0;
    for (int y = std::max(0, peakY - 2); y <= std::min(rows - 1, peakY + 2); ++y)
        for (int x = std::max(0, peakX - 2); x <= std::min(cols - 1, peakX + 2); ++x) {
            const double value = shifted[size_t(y) * size_t(cols) + size_t(x)];
            sumX += double(x) * value;
            sumY += double(y) * value;
            sum += value;
        }
    sum += std::numeric_limits<double>::epsilon();
    return {double(cols) / 2.0 - sumX / sum, double(rows) / 2.0 - sumY / sum};
}

namespace {

// out[x] += in[x] * w[0] + in[x + 1] * w[1] + ... + in[x + 7] * w[7], added one product after
// the other as a direct sum would, on arrays apart: the compiler vectorizes it along x, and the
// partial sums are read and written once for eight taps.
void addEightTaps(float *out, const float *in, const float *w, int count)
{
    const float w0 = w[0], w1 = w[1], w2 = w[2], w3 = w[3], w4 = w[4], w5 = w[5], w6 = w[6], w7 = w[7];
    const float *in1 = in + 1, *in2 = in + 2, *in3 = in + 3, *in4 = in + 4, *in5 = in + 5, *in6 = in + 6, *in7 = in + 7;
    for (int x = 0; x < count; ++x)
        out[x] = out[x] + in[x] * w0 + in1[x] * w1 + in2[x] * w2 + in3[x] * w3 + in4[x] * w4 + in5[x] * w5 +
                 in6[x] * w6 + in7[x] * w7;
}

void addOneTap(float *out, const float *in, float w, int count)
{
    for (int x = 0; x < count; ++x)
        out[x] += in[x] * w;
}

void addRow(double *out, const float *in, int count)
{
    for (int x = 0; x < count; ++x)
        out[x] += double(in[x]);
}

} // namespace

void matchTemplateInto(const Image &image, const Image &templ, Image &out)
{
    const auto plane = [](const Image &i) { return !i.empty() && i.depth() == Depth::F32 && i.channels() == 1; };
    if (!plane(image) || !plane(templ) || templ.width() > image.width() || templ.height() > image.height())
        throw std::invalid_argument("A plane of floats and a smaller one");
    const int tw = templ.width(), th = templ.height();
    const Size size{image.width() - tw + 1, image.height() - th + 1};
    if (out.size() != size || out.depth() != Depth::F32 || out.channels() != 1)
        throw std::invalid_argument("A buffer of the places of the template");
    const double area = double(tw) * double(th), invArea = 1.0 / area;
    double templSum = 0.0, templSquares = 0.0;
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            const double t = templ.row<float>(y)[x];
            templSum += t;
            templSquares += t * t;
        }
    const double templMean = templSum * invArea;
    const double templVariance = templSquares * invArea - templMean * templMean;
    Image &target = out;
    if (templVariance < std::numeric_limits<double>::epsilon()) {
        // A flat template correlates fully everywhere, as OpenCV answers.
        for (int y = 0; y < size.height; ++y)
            std::fill(target.mutableRow<float>(y), target.mutableRow<float>(y) + size.width, 1.0f);
        return;
    }
    const double templNorm = std::sqrt(templVariance) / std::sqrt(invArea);
    // Sums and sums of squares of the image, cumulated, for the mean and spread of each window.
    const int w = image.width(), h = image.height();
    std::vector<double> sums(size_t(w + 1) * size_t(h + 1), 0.0), squares(sums.size(), 0.0);
    for (int y = 0; y < h; ++y) {
        const float *in = image.row<float>(y);
        double rowSum = 0.0, rowSquares = 0.0;
        for (int x = 0; x < w; ++x) {
            rowSum += in[x];
            rowSquares += double(in[x]) * in[x];
            sums[size_t(y + 1) * size_t(w + 1) + size_t(x + 1)] = sums[size_t(y) * size_t(w + 1) + size_t(x + 1)] + rowSum;
            squares[size_t(y + 1) * size_t(w + 1) + size_t(x + 1)] = squares[size_t(y) * size_t(w + 1) + size_t(x + 1)] + rowSquares;
        }
    }
    const auto boxOf = [&](const std::vector<double> &table, int x, int y) {
        const size_t stride = size_t(w + 1);
        return table[size_t(y + th) * stride + size_t(x + tw)] - table[size_t(y) * stride + size_t(x + tw)] -
               table[size_t(y + th) * stride + size_t(x)] + table[size_t(y) * stride + size_t(x)];
    };
    // Each template row correlated along a whole row of places at once, in the order of the direct
    // sum: the products of a row added in floats from its first tap, the rows then in doubles.
    const auto rows = [&](int first, int last) {
        std::vector<float> rowSums(static_cast<size_t>(size.width));
        std::vector<double> correlations(static_cast<size_t>(size.width));
        for (int y = first; y < last; ++y) {
            std::fill(correlations.begin(), correlations.end(), 0.0);
            for (int ty = 0; ty < th; ++ty) {
                const float *in = image.row<float>(y + ty), *t = templ.row<float>(ty);
                std::fill(rowSums.begin(), rowSums.end(), 0.0f);
                int tx = 0;
                for (; tx + 8 <= tw; tx += 8)
                    addEightTaps(rowSums.data(), in + tx, t + tx, size.width);
                for (; tx < tw; ++tx)
                    addOneTap(rowSums.data(), in + tx, t[tx], size.width);
                addRow(correlations.data(), rowSums.data(), size.width);
            }
            float *row = target.mutableRow<float>(y);
            for (int x = 0; x < size.width; ++x) {
                const double windowSum = boxOf(sums, x, y);
                double num = correlations[size_t(x)] - windowSum * templMean;
                const double windowMean2 = windowSum * windowSum * invArea;
                const double t = std::sqrt(std::max(boxOf(squares, x, y) - windowMean2, 0.0)) * templNorm;
                if (std::abs(num) < t)
                    num /= t;
                else if (std::abs(num) < t * 1.125)
                    num = num > 0 ? 1.0 : -1.0;
                else
                    num = 0.0;
                row[x] = float(num);
            }
        }
    };
    // Threads only for a large search: the blocks of a registration, matched by the thousand
    // from loops that are already parallel, would spend more on starting threads than on work.
    const double products = double(size.width) * double(size.height) * area;
    if (products < 32.0 * 1024.0 * 1024.0)
        rows(0, size.height);
    else
        parallelRows(size.height, rows);
}

} // namespace ipr
