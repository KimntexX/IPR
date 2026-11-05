#ifndef IPR_IMAGE_TYPES_H
#define IPR_IMAGE_TYPES_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// The small values of the image layer: sizes, points, rectangles, vectors and matrices of a
// known size. Standard C++ only. Their arithmetic is OpenCV's, expression for expression, so that
// a module moved over from OpenCV computes the same bits: the same conversions, the same order of
// sums, a division as a product by the inverse, a value rounded to the nearest, ties to even, when
// it becomes an integer.
namespace ipr {

// value converted to T: as is for a floating T, rounded to the nearest and kept within T's range
// for an integer one.
template <typename T, typename U>
constexpr T saturate(U value)
{
    if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(value);
    } else {
        long long whole = 0;
        if constexpr (std::is_floating_point_v<U>)
            whole = std::llrint(value);
        else
            whole = static_cast<long long>(value);
        return static_cast<T>(std::clamp<long long>(whole, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()));
    }
}

template <typename T>
struct SizeOf
{
    T width{};
    T height{};

    constexpr SizeOf() = default;
    constexpr SizeOf(T w, T h) : width(w), height(h) {}
    template <typename U>
    constexpr SizeOf(const SizeOf<U> &other) : width(saturate<T>(other.width)), height(saturate<T>(other.height)) {}

    T area() const { return width * height; }
    bool empty() const { return width <= 0 || height <= 0; }
    friend bool operator==(const SizeOf &a, const SizeOf &b) { return a.width == b.width && a.height == b.height; }
    friend bool operator!=(const SizeOf &a, const SizeOf &b) { return !(a == b); }
    friend SizeOf operator*(SizeOf a, T scale)
    {
        a.width *= scale;
        a.height *= scale;
        return a;
    }
    friend SizeOf operator/(SizeOf a, T scale)
    {
        a.width /= scale;
        a.height /= scale;
        return a;
    }
    friend SizeOf operator+(SizeOf a, const SizeOf &b)
    {
        a.width += b.width;
        a.height += b.height;
        return a;
    }
    friend SizeOf operator-(SizeOf a, const SizeOf &b)
    {
        a.width -= b.width;
        a.height -= b.height;
        return a;
    }
};

using Size = SizeOf<int>;
using Sized = SizeOf<double>;

template <typename T>
struct Point
{
    T x{};
    T y{};

    constexpr Point() = default;
    constexpr Point(T px, T py) : x(px), y(py) {}
    template <typename U>
    constexpr Point(const Point<U> &other) : x(saturate<T>(other.x)), y(saturate<T>(other.y)) {}

    T dot(const Point &p) const
    {
        if constexpr (std::is_same_v<T, int>) {
            return saturate<int>(std::int64_t(x) * p.x + std::int64_t(y) * p.y);
        } else {
            return saturate<T>(x * p.x + y * p.y);
        }
    }
    double ddot(const Point &p) const { return double(x) * double(p.x) + double(y) * double(p.y); }
    double cross(const Point &p) const { return double(x) * p.y - double(y) * p.x; }

    friend Point operator+(const Point &a, const Point &b) { return {saturate<T>(a.x + b.x), saturate<T>(a.y + b.y)}; }
    friend Point operator-(const Point &a, const Point &b) { return {saturate<T>(a.x - b.x), saturate<T>(a.y - b.y)}; }
    friend Point operator-(const Point &a) { return {saturate<T>(-a.x), saturate<T>(-a.y)}; }
    Point &operator+=(const Point &b)
    {
        x = saturate<T>(x + b.x);
        y = saturate<T>(y + b.y);
        return *this;
    }
    Point &operator-=(const Point &b)
    {
        x = saturate<T>(x - b.x);
        y = saturate<T>(y - b.y);
        return *this;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Point operator*(const Point &a, U scale)
    {
        return {saturate<T>(a.x * scale), saturate<T>(a.y * scale)};
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Point operator*(U scale, const Point &a)
    {
        return {saturate<T>(a.x * scale), saturate<T>(a.y * scale)};
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    Point &operator*=(U scale)
    {
        x = saturate<T>(x * scale);
        y = saturate<T>(y * scale);
        return *this;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Point operator/(const Point &a, U scale)
    {
        return {saturate<T>(a.x / scale), saturate<T>(a.y / scale)};
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    Point &operator/=(U scale)
    {
        x = saturate<T>(x / scale);
        y = saturate<T>(y / scale);
        return *this;
    }
    friend bool operator==(const Point &a, const Point &b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(const Point &a, const Point &b) { return !(a == b); }
};

using Point2i = Point<int>;
using Point2f = Point<float>;
using Point2d = Point<double>;

template <typename T>
double length(const Point<T> &p)
{
    return std::sqrt(double(p.x) * p.x + double(p.y) * p.y);
}

template <typename T>
struct Rect
{
    T x{};
    T y{};
    T width{};
    T height{};

    constexpr Rect() = default;
    constexpr Rect(T rx, T ry, T w, T h) : x(rx), y(ry), width(w), height(h) {}
    constexpr Rect(const Point<T> &topLeft, const SizeOf<T> &size) : x(topLeft.x), y(topLeft.y), width(size.width), height(size.height) {}
    // The rectangle spanned by two corners, whichever they are.
    Rect(const Point<T> &a, const Point<T> &b)
        : x(std::min(a.x, b.x)), y(std::min(a.y, b.y)), width(std::max(a.x, b.x) - std::min(a.x, b.x)),
          height(std::max(a.y, b.y) - std::min(a.y, b.y))
    {
    }
    template <typename U>
    constexpr Rect(const Rect<U> &other)
        : x(saturate<T>(other.x)), y(saturate<T>(other.y)), width(saturate<T>(other.width)), height(saturate<T>(other.height))
    {
    }

    Point<T> tl() const { return {x, y}; }
    Point<T> br() const { return {x + width, y + height}; }
    SizeOf<T> size() const { return {width, height}; }
    T area() const { return width * height; }
    bool empty() const { return width <= 0 || height <= 0; }
    T right() const { return x + width; }
    T bottom() const { return y + height; }
    // Edges included at the top left, excluded at the bottom right. A point in floats is compared in
    // doubles, as OpenCV does for a rectangle of integers.
    template <typename U>
    bool contains(const Point<U> &p) const
    {
        using C = std::conditional_t<std::is_floating_point_v<T> || std::is_floating_point_v<U>, double, std::common_type_t<T, U>>;
        const C left = C(x), top = C(y), px = C(p.x), py = C(p.y);
        return left <= px && px < left + C(width) && top <= py && py < top + C(height);
    }

    // What two rectangles have in common, by OpenCV's formula; empty when nothing.
    Rect &operator&=(const Rect &b)
    {
        Rect &a = *this;
        if (a.empty() || b.empty())
            return a = Rect();
        const Rect &xMin = a.x < b.x ? a : b, &xMax = a.x < b.x ? b : a;
        const Rect &yMin = a.y < b.y ? a : b, &yMax = a.y < b.y ? b : a;
        if ((xMin.x < 0 && xMin.x + xMin.width < xMax.x) || (yMin.y < 0 && yMin.y + yMin.height < yMax.y))
            return a = Rect();
        const T w = std::min(xMin.width - (xMax.x - xMin.x), xMax.width);
        const T h = std::min(yMin.height - (yMax.y - yMin.y), yMax.height);
        const T left = xMax.x, top = yMax.y;
        a = Rect(left, top, w, h);
        if (a.empty())
            a = Rect();
        return a;
    }
    // The smallest rectangle around both; one of them when the other is empty.
    Rect &operator|=(const Rect &b)
    {
        if (empty()) {
            *this = b;
        } else if (!b.empty()) {
            const T left = std::min(x, b.x), top = std::min(y, b.y);
            width = std::max(x + width, b.x + b.width) - left;
            height = std::max(y + height, b.y + b.height) - top;
            x = left;
            y = top;
        }
        return *this;
    }
    friend Rect operator&(Rect a, const Rect &b) { return a &= b; }
    friend Rect operator|(Rect a, const Rect &b) { return a |= b; }
    friend Rect operator+(const Rect &a, const Point<T> &b) { return {a.x + b.x, a.y + b.y, a.width, a.height}; }
    friend Rect operator-(const Rect &a, const Point<T> &b) { return {a.x - b.x, a.y - b.y, a.width, a.height}; }
    friend Rect operator+(const Rect &a, const SizeOf<T> &b) { return {a.x, a.y, a.width + b.width, a.height + b.height}; }
    friend bool operator==(const Rect &a, const Rect &b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const Rect &a, const Rect &b) { return !(a == b); }
};

using Recti = Rect<int>;
using Rectd = Rect<double>;

template <typename T, std::size_t N>
struct Vec
{
    std::array<T, N> values{};

    constexpr Vec() = default;
    constexpr Vec(const std::array<T, N> &all) : values(all) {}
    template <typename... Args,
              typename = std::enable_if_t<sizeof...(Args) == N && (N > 1) && (std::is_arithmetic_v<Args> && ...)>>
    constexpr Vec(Args... args) : values{{static_cast<T>(args)...}}
    {
    }
    template <typename U>
    constexpr Vec(const Vec<U, N> &other)
    {
        for (std::size_t i = 0; i < N; ++i)
            values[i] = saturate<T>(other[i]);
    }
    static Vec all(T value)
    {
        Vec out;
        out.values.fill(value);
        return out;
    }

    T &operator[](std::size_t i) { return values[i]; }
    const T &operator[](std::size_t i) const { return values[i]; }

    Vec mul(const Vec &b) const
    {
        Vec out;
        for (std::size_t i = 0; i < N; ++i)
            out[i] = saturate<T>(values[i] * b[i]);
        return out;
    }
    // Summed in T from zero, as OpenCV's dot; ddot sums in doubles.
    T dot(const Vec &b) const
    {
        T sum = 0;
        for (std::size_t i = 0; i < N; ++i)
            sum += values[i] * b[i];
        return sum;
    }
    double ddot(const Vec &b) const
    {
        double sum = 0;
        for (std::size_t i = 0; i < N; ++i)
            sum += double(values[i]) * b[i];
        return sum;
    }
    Vec cross(const Vec &b) const
    {
        static_assert(N == 3, "a cross product of three values");
        const Vec &a = *this;
        return {saturate<T>(a[1] * b[2] - a[2] * b[1]), saturate<T>(a[2] * b[0] - a[0] * b[2]),
                saturate<T>(a[0] * b[1] - a[1] * b[0])};
    }

    friend Vec operator+(Vec a, const Vec &b) { return a += b; }
    friend Vec operator-(Vec a, const Vec &b) { return a -= b; }
    friend Vec operator-(const Vec &a)
    {
        Vec out;
        for (std::size_t i = 0; i < N; ++i)
            out[i] = saturate<T>(-a[i]);
        return out;
    }
    Vec &operator+=(const Vec &b)
    {
        for (std::size_t i = 0; i < N; ++i)
            values[i] = saturate<T>(values[i] + b[i]);
        return *this;
    }
    Vec &operator-=(const Vec &b)
    {
        for (std::size_t i = 0; i < N; ++i)
            values[i] = saturate<T>(values[i] - b[i]);
        return *this;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    Vec &operator*=(U scale)
    {
        for (std::size_t i = 0; i < N; ++i)
            values[i] = saturate<T>(values[i] * scale);
        return *this;
    }
    // A division is a product by the inverse, taken in floats for a float and in doubles otherwise.
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    Vec &operator/=(U scale)
    {
        return *this *= inverseOf(scale);
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Vec operator*(Vec a, U scale)
    {
        return a *= scale;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Vec operator*(U scale, Vec a)
    {
        return a *= scale;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Vec operator/(Vec a, U scale)
    {
        return a /= scale;
    }
    friend bool operator==(const Vec &a, const Vec &b) { return a.values == b.values; }
    friend bool operator!=(const Vec &a, const Vec &b) { return !(a == b); }

    template <typename U>
    static auto inverseOf(U scale)
    {
        if constexpr (std::is_same_v<U, float>)
            return 1.f / scale;
        else
            return 1. / double(scale);
    }
};

using Vec2s = Vec<std::int16_t, 2>;
using Vec2i = Vec<int, 2>;
using Vec2f = Vec<float, 2>;
using Vec2d = Vec<double, 2>;
using Vec3f = Vec<float, 3>;
using Vec3d = Vec<double, 3>;
using Vec4f = Vec<float, 4>;

static_assert(sizeof(Vec3f) == 3 * sizeof(float), "a Vec lies on the pixels");

// Its length, the root of the sum of its squares taken in doubles.
template <typename T, std::size_t N>
double length(const Vec<T, N> &v)
{
    double sum = 0;
    for (std::size_t i = 0; i < N; ++i)
        sum += double(v[i]) * double(v[i]);
    return std::sqrt(sum);
}

// By rows.
template <typename T, std::size_t Rows, std::size_t Columns>
struct Matrix
{
    std::array<T, Rows * Columns> values{};

    constexpr Matrix() = default;
    constexpr Matrix(const std::array<T, Rows * Columns> &all) : values(all) {}
    template <typename... Args, typename = std::enable_if_t<sizeof...(Args) == Rows * Columns && (Rows * Columns > 1) &&
                                                            (std::is_arithmetic_v<Args> && ...)>>
    constexpr Matrix(Args... args) : values{{static_cast<T>(args)...}}
    {
    }
    template <typename U>
    constexpr Matrix(const Matrix<U, Rows, Columns> &other)
    {
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            values[i] = saturate<T>(other.values[i]);
    }

    T &operator()(std::size_t row, std::size_t column) { return values[row * Columns + column]; }
    const T &operator()(std::size_t row, std::size_t column) const { return values[row * Columns + column]; }

    static Matrix identity()
    {
        Matrix out;
        for (std::size_t i = 0; i < std::min(Rows, Columns); ++i)
            out(i, i) = T(1);
        return out;
    }
    static Matrix zeros() { return Matrix(); }
    static Matrix all(T value)
    {
        Matrix out;
        out.values.fill(value);
        return out;
    }

    Matrix<T, Columns, Rows> t() const
    {
        Matrix<T, Columns, Rows> out;
        for (std::size_t r = 0; r < Rows; ++r)
            for (std::size_t c = 0; c < Columns; ++c)
                out(c, r) = (*this)(r, c);
        return out;
    }
    Matrix mul(const Matrix &b) const
    {
        Matrix out;
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            out.values[i] = saturate<T>(values[i] * b.values[i]);
        return out;
    }
    T dot(const Matrix &b) const
    {
        T sum = 0;
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            sum += values[i] * b.values[i];
        return sum;
    }
    // The inverse of a 3 x 3 matrix by its cofactors, as OpenCV's Matx::inv; zeros when singular,
    // and ok, if given, false.
    Matrix inv(bool *ok = nullptr) const;

    friend Matrix operator+(Matrix a, const Matrix &b)
    {
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            a.values[i] = saturate<T>(a.values[i] + b.values[i]);
        return a;
    }
    friend Matrix operator-(Matrix a, const Matrix &b)
    {
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            a.values[i] = saturate<T>(a.values[i] - b.values[i]);
        return a;
    }
    friend Matrix operator-(const Matrix &a) { return a * -1; }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Matrix operator*(Matrix a, U scale)
    {
        for (std::size_t i = 0; i < Rows * Columns; ++i)
            a.values[i] = saturate<T>(a.values[i] * scale);
        return a;
    }
    template <typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend Matrix operator*(U scale, const Matrix &a)
    {
        return a * scale;
    }
    friend bool operator==(const Matrix &a, const Matrix &b) { return a.values == b.values; }
    friend bool operator!=(const Matrix &a, const Matrix &b) { return !(a == b); }
};

template <typename T, std::size_t Rows, std::size_t Middle, std::size_t Columns>
Matrix<T, Rows, Columns> operator*(const Matrix<T, Rows, Middle> &a, const Matrix<T, Middle, Columns> &b)
{
    Matrix<T, Rows, Columns> out;
    for (std::size_t r = 0; r < Rows; ++r)
        for (std::size_t c = 0; c < Columns; ++c) {
            T sum = 0;
            for (std::size_t m = 0; m < Middle; ++m)
                sum += a(r, m) * b(m, c);
            out(r, c) = sum;
        }
    return out;
}

template <typename T, std::size_t Rows, std::size_t Columns>
Vec<T, Rows> operator*(const Matrix<T, Rows, Columns> &a, const Vec<T, Columns> &v)
{
    Vec<T, Rows> out;
    for (std::size_t r = 0; r < Rows; ++r) {
        T sum = 0;
        for (std::size_t c = 0; c < Columns; ++c)
            sum += a(r, c) * v[c];
        out[r] = sum;
    }
    return out;
}

using Matrix33f = Matrix<float, 3, 3>;
using Matrix33d = Matrix<double, 3, 3>;

// The determinant of a 3 x 3 matrix, as OpenCV expands it, in T, given as a double.
template <typename T>
double determinant(const Matrix<T, 3, 3> &a)
{
    return a(0, 0) * (a(1, 1) * a(2, 2) - a(2, 1) * a(1, 2)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(2, 0) * a(1, 2)) +
           a(0, 2) * (a(1, 0) * a(2, 1) - a(2, 0) * a(1, 1));
}

template <typename T, std::size_t Rows, std::size_t Columns>
Matrix<T, Rows, Columns> Matrix<T, Rows, Columns>::inv(bool *ok) const
{
    static_assert(Rows == 3 && Columns == 3, "the inverse of a 3 x 3 matrix");
    const Matrix &a = *this;
    Matrix b;
    T d = T(determinant(a));
    if (ok)
        *ok = d != 0;
    if (d == 0)
        return b;
    d = 1 / d;
    b(0, 0) = (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) * d;
    b(0, 1) = (a(0, 2) * a(2, 1) - a(0, 1) * a(2, 2)) * d;
    b(0, 2) = (a(0, 1) * a(1, 2) - a(0, 2) * a(1, 1)) * d;
    b(1, 0) = (a(1, 2) * a(2, 0) - a(1, 0) * a(2, 2)) * d;
    b(1, 1) = (a(0, 0) * a(2, 2) - a(0, 2) * a(2, 0)) * d;
    b(1, 2) = (a(0, 2) * a(1, 0) - a(0, 0) * a(1, 2)) * d;
    b(2, 0) = (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0)) * d;
    b(2, 1) = (a(0, 1) * a(2, 0) - a(0, 0) * a(2, 1)) * d;
    b(2, 2) = (a(0, 0) * a(1, 1) - a(0, 1) * a(1, 0)) * d;
    return b;
}

} // namespace ipr

#endif // IPR_IMAGE_TYPES_H
