#ifndef IPR_IMAGE_TYPES_H
#define IPR_IMAGE_TYPES_H

#include <algorithm>
#include <array>
#include <cstddef>

// The small values of the image layer: sizes, points, rectangles, vectors and matrices of a
// known size. Standard C++ only.
namespace ipr {

struct Size
{
    int width = 0;
    int height = 0;

    bool empty() const { return width <= 0 || height <= 0; }
    friend bool operator==(const Size &a, const Size &b) { return a.width == b.width && a.height == b.height; }
    friend bool operator!=(const Size &a, const Size &b) { return !(a == b); }
};

template <typename T>
struct Point
{
    T x{};
    T y{};

    friend Point operator+(const Point &a, const Point &b) { return {a.x + b.x, a.y + b.y}; }
    friend Point operator-(const Point &a, const Point &b) { return {a.x - b.x, a.y - b.y}; }
    friend Point operator*(const Point &a, T scale) { return {a.x * scale, a.y * scale}; }
    friend bool operator==(const Point &a, const Point &b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(const Point &a, const Point &b) { return !(a == b); }
};

using Point2i = Point<int>;
using Point2f = Point<float>;
using Point2d = Point<double>;

template <typename T>
struct Rect
{
    T x{};
    T y{};
    T width{};
    T height{};

    bool empty() const { return !(width > T{}) || !(height > T{}); }
    T right() const { return x + width; }
    T bottom() const { return y + height; }
    bool contains(const Point<T> &p) const { return p.x >= x && p.y >= y && p.x < right() && p.y < bottom(); }
    friend bool operator==(const Rect &a, const Rect &b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const Rect &a, const Rect &b) { return !(a == b); }
    // What two rectangles have in common; empty when nothing.
    friend Rect intersection(const Rect &a, const Rect &b)
    {
        const T left = std::max(a.x, b.x), top = std::max(a.y, b.y);
        const T width = std::min(a.right(), b.right()) - left, height = std::min(a.bottom(), b.bottom()) - top;
        if (!(width > T{}) || !(height > T{}))
            return {};
        return {left, top, width, height};
    }
};

using Recti = Rect<int>;
using Rectd = Rect<double>;

template <typename T, std::size_t N>
struct Vec
{
    std::array<T, N> values{};

    T &operator[](std::size_t i) { return values[i]; }
    const T &operator[](std::size_t i) const { return values[i]; }
    friend Vec operator+(Vec a, const Vec &b)
    {
        for (std::size_t i = 0; i < N; ++i) a[i] = a[i] + b[i];
        return a;
    }
    friend Vec operator-(Vec a, const Vec &b)
    {
        for (std::size_t i = 0; i < N; ++i) a[i] = a[i] - b[i];
        return a;
    }
    friend Vec operator*(Vec a, T scale)
    {
        for (std::size_t i = 0; i < N; ++i) a[i] = a[i] * scale;
        return a;
    }
    friend T dot(const Vec &a, const Vec &b)
    {
        T sum{};
        for (std::size_t i = 0; i < N; ++i) sum = sum + a[i] * b[i];
        return sum;
    }
    friend bool operator==(const Vec &a, const Vec &b) { return a.values == b.values; }
    friend bool operator!=(const Vec &a, const Vec &b) { return !(a == b); }
};

using Vec3f = Vec<float, 3>;
using Vec4f = Vec<float, 4>;
using Vec3d = Vec<double, 3>;

// By rows.
template <typename T, std::size_t Rows, std::size_t Columns>
struct Matrix
{
    std::array<T, Rows * Columns> values{};

    T &operator()(std::size_t row, std::size_t column) { return values[row * Columns + column]; }
    const T &operator()(std::size_t row, std::size_t column) const { return values[row * Columns + column]; }

    static Matrix identity()
    {
        Matrix out;
        for (std::size_t i = 0; i < std::min(Rows, Columns); ++i) out(i, i) = T(1);
        return out;
    }
    friend bool operator==(const Matrix &a, const Matrix &b) { return a.values == b.values; }
    friend bool operator!=(const Matrix &a, const Matrix &b) { return !(a == b); }
};

template <typename T, std::size_t Rows, std::size_t Middle, std::size_t Columns>
Matrix<T, Rows, Columns> operator*(const Matrix<T, Rows, Middle> &a, const Matrix<T, Middle, Columns> &b)
{
    Matrix<T, Rows, Columns> out;
    for (std::size_t r = 0; r < Rows; ++r)
        for (std::size_t c = 0; c < Columns; ++c)
            for (std::size_t m = 0; m < Middle; ++m)
                out(r, c) = out(r, c) + a(r, m) * b(m, c);
    return out;
}

template <typename T, std::size_t Rows, std::size_t Columns>
Vec<T, Rows> operator*(const Matrix<T, Rows, Columns> &a, const Vec<T, Columns> &v)
{
    Vec<T, Rows> out;
    for (std::size_t r = 0; r < Rows; ++r)
        for (std::size_t c = 0; c < Columns; ++c)
            out[r] = out[r] + a(r, c) * v[c];
    return out;
}

using Matrix33d = Matrix<double, 3, 3>;

} // namespace ipr

#endif // IPR_IMAGE_TYPES_H
