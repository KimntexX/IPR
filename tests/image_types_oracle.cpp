// O3: the small types of the image layer against OpenCV's: every operation the modules use, on random
// values and on the edges of rounding and range, compared to the bit. Writes one JSON proof.
#include "Pixels.h"
#include "Random.h"
#include "Types.h"

#include <opencv2/core.hpp>

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

using namespace ipr;

namespace {

std::mt19937_64 engine(29);

double real(double low, double high) { return std::uniform_real_distribution<double>(low, high)(engine); }
int whole(int low, int high) { return std::uniform_int_distribution<int>(low, high)(engine); }

template <typename A, typename B>
bool sameBits(const A &a, const B &b)
{
    static_assert(sizeof(A) == sizeof(B), "same layout");
    return std::memcmp(&a, &b, sizeof(A)) == 0;
}

// The values that matter to rounding and range: ties, signs, the ends of short.
double edgy(double low, double high)
{
    static const double edges[] = {0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 32767.4, 32767.6, -32768.6, 0.0, -0.0};
    return whole(0, 3) == 0 ? edges[size_t(whole(0, 10))] : real(low, high);
}

template <typename T>
T value()
{
    if constexpr (std::is_floating_point_v<T>)
        return T(edgy(-1000.0, 1000.0));
    else
        return T(whole(-20000, 20000));
}

template <typename T>
T nonZero()
{
    T v = value<T>();
    while (v == T(0))
        v = value<T>();
    return v;
}

template <typename T, int N>
std::pair<cv::Vec<T, N>, Vec<T, size_t(N)>> vecPair()
{
    cv::Vec<T, N> a;
    Vec<T, size_t(N)> b;
    for (int i = 0; i < N; ++i)
        a[i] = b[size_t(i)] = value<T>();
    return {a, b};
}

template <typename T>
std::pair<cv::Matx<T, 3, 3>, Matrix<T, 3, 3>> matPair()
{
    cv::Matx<T, 3, 3> a;
    Matrix<T, 3, 3> b;
    for (int i = 0; i < 9; ++i)
        a.val[i] = b.values[size_t(i)] = T(real(-3.0, 3.0));
    return {a, b};
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: iprImageTypesOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    std::ostringstream report;
    bool within = true, first = true;
    constexpr int trials = 20000;
    const auto check = [&](const std::string &name, const std::function<bool()> &trial) {
        int differing = 0;
        for (int t = 0; t < trials; ++t)
            differing += trial() ? 0 : 1;
        within = within && differing == 0;
        std::cout << name << ": " << differing << " of " << trials << " differ" << std::endl;
        report << (first ? "" : ",\n") << "  {\"case\": \"" << name << "\", \"trials\": " << trials << ", \"differing\": " << differing << "}";
        first = false;
    };

    // Rounding to integers.
    check("saturate double to int", [] { const double v = edgy(-1e6, 1e6); return cv::saturate_cast<int>(v) == saturate<int>(v); });
    check("saturate float to short", [] { const float v = float(edgy(-40000, 40000)); return cv::saturate_cast<short>(v) == saturate<std::int16_t>(v); });
    // Beyond the range of int, OpenCV's conversion gives INT_MIN whatever the sign, as the processor
    // does; the layer keeps the nearest end instead. A difference by design, measured apart.
    {
        int differing = 0;
        for (const double v : {1e10, -1e10, 3e9, -3e9})
            differing += cv::saturate_cast<int>(v) == saturate<int>(v) ? 0 : 1;
        std::cout << "beyond int, by design: " << differing << " of 4 differ" << std::endl;
        report << "  {\"case\": \"beyond the range of int, by design: OpenCV gives INT_MIN, the layer the nearest end\", \"trials\": 4, \"differing\": " << differing << "},\n";
    }
    check("saturate int to short", [] { const int v = whole(-70000, 70000); return cv::saturate_cast<short>(v) == saturate<std::int16_t>(v); });

    // Points.
    check("point2f arithmetic", [] {
        const float ax = value<float>(), ay = value<float>(), bx = value<float>(), by = value<float>();
        const cv::Point2f a(ax, ay), b(bx, by);
        const Point2f c(ax, ay), d(bx, by);
        const int i = nonZero<int>() % 50;
        const float f = nonZero<float>();
        const double g = nonZero<double>();
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(-a, -c) && sameBits(a * i, c * i) &&
               sameBits(a * f, c * f) && sameBits(a * g, c * g) && sameBits(f * a, f * c) && sameBits(a / f, c / f) &&
               sameBits(a / g, c / g) && sameBits(a / (i ? i : 1), c / (i ? i : 1)) && a.dot(b) == c.dot(d) &&
               sameBits(a.ddot(b), c.ddot(d)) && sameBits(a.cross(b), c.cross(d)) && sameBits(cv::norm(a), length(c));
    });
    check("point2d arithmetic and rounding to point", [] {
        const double ax = value<double>(), ay = value<double>(), bx = value<double>(), by = value<double>();
        const cv::Point2d a(ax, ay), b(bx, by);
        const Point2d c(ax, ay), d(bx, by);
        const double g = nonZero<double>();
        const cv::Point rounded = a;
        const Point2i mine = c;
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(a * g, c * g) && sameBits(a / g, c / g) &&
               sameBits(rounded, mine) && sameBits(cv::Point2f(a), Point2f(c)) && sameBits(cv::norm(a), length(c));
    });
    check("point arithmetic", [] {
        const cv::Point a(whole(-30000, 30000), whole(-30000, 30000)), b(whole(-30000, 30000), whole(-30000, 30000));
        const Point2i c(a.x, a.y), d(b.x, b.y);
        const int i = whole(1, 9);
        const double g = nonZero<double>();
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(a * i, c * i) && sameBits(a * g, c * g) &&
               sameBits(a / i, c / i) && a.dot(b) == c.dot(d);
    });

    // Sizes and rectangles.
    check("size", [] {
        const cv::Size a(whole(0, 5000), whole(0, 5000));
        const Size b(a.width, a.height);
        const int i = whole(1, 9);
        return a.area() == b.area() && a.empty() == b.empty() && sameBits(a * i, b * i) && sameBits(a / i, b / i);
    });
    check("rect intersection, union, corners", [] {
        const cv::Rect a(whole(-50, 50), whole(-50, 50), whole(-5, 60), whole(-5, 60)), b(whole(-50, 50), whole(-50, 50), whole(-5, 60), whole(-5, 60));
        const Recti c(a.x, a.y, a.width, a.height), d(b.x, b.y, b.width, b.height);
        const cv::Point p(whole(-60, 110), whole(-60, 110));
        const cv::Point2d q(real(-60, 110), real(-60, 110));
        const cv::Point2f r(float(real(-60, 110)), float(real(-60, 110)));
        return sameBits(a & b, c & d) && sameBits(a | b, c | d) && sameBits(a.tl(), c.tl()) && sameBits(a.br(), c.br()) &&
               a.area() == c.area() && a.empty() == c.empty() && a.contains(p) == c.contains(Point2i(p.x, p.y)) &&
               a.contains(q) == c.contains(Point2d(q.x, q.y)) && a.contains(r) == c.contains(Point2f(r.x, r.y)) &&
               sameBits(a + p, c + Point2i(p.x, p.y)) && sameBits(a - p, c - Point2i(p.x, p.y)) &&
               sameBits(cv::Rect(cv::Point(a.x, a.y), cv::Point(b.x, b.y)), Recti(Point2i(a.x, a.y), Point2i(b.x, b.y)));
    });
    check("rect2d intersection, union, rounding to rect", [] {
        const cv::Rect2d a(real(-50, 50), real(-50, 50), real(-5, 60), real(-5, 60)), b(real(-50, 50), real(-50, 50), real(-5, 60), real(-5, 60));
        const Rectd c(a.x, a.y, a.width, a.height), d(b.x, b.y, b.width, b.height);
        const cv::Rect rounded = a;
        const Recti mine = c;
        return sameBits(a & b, c & d) && sameBits(a | b, c | d) && sameBits(rounded, mine) && sameBits(a.area(), c.area());
    });

    // Vectors.
    check("vec3f arithmetic", [] {
        const auto [a, c] = vecPair<float, 3>();
        const auto [b, d] = vecPair<float, 3>();
        const int i = nonZero<int>() % 50 ? nonZero<int>() % 50 : 3;
        const float f = nonZero<float>();
        const double g = nonZero<double>();
        cv::Vec3f e = a;
        Vec3f h = c;
        e += b, h += d, e *= f, h *= f, e /= g, h /= g;
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(-a, -c) && sameBits(a * i, c * i) &&
               sameBits(a * f, c * f) && sameBits(a * g, c * g) && sameBits(f * a, f * c) && sameBits(g * a, g * c) &&
               sameBits(a / i, c / i) && sameBits(a / f, c / f) && sameBits(a / g, c / g) && sameBits(a.mul(b), c.mul(d)) &&
               sameBits(a.dot(b), c.dot(d)) && sameBits(a.ddot(b), c.ddot(d)) && sameBits(a.cross(b), c.cross(d)) &&
               sameBits(e, h) && sameBits(cv::norm(a), length(c)) && sameBits(cv::Vec3d(a), Vec3d(c));
    });
    check("vec3d arithmetic and rounding to floats", [] {
        const auto [a, c] = vecPair<double, 3>();
        const auto [b, d] = vecPair<double, 3>();
        const double g = nonZero<double>();
        const float f = nonZero<float>();
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(a * g, c * g) && sameBits(a * f, c * f) &&
               sameBits(a / g, c / g) && sameBits(a.dot(b), c.dot(d)) && sameBits(a.cross(b), c.cross(d)) &&
               sameBits(cv::Vec3f(a), Vec3f(c)) && sameBits(cv::norm(a), length(c)) && sameBits(cv::normalize(a), c * (length(c) ? 1. / length(c) : 0.));
    });
    check("vec2s, vec2f, vec4f", [] {
        const auto [a, c] = vecPair<short, 2>();
        const auto [b, d] = vecPair<short, 2>();
        const auto [e, h] = vecPair<float, 4>();
        const auto [k, m] = vecPair<float, 4>();
        const auto [n, o] = vecPair<float, 2>();
        const float f = nonZero<float>();
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(a * 3, c * 3) && sameBits(a.mul(b), c.mul(d)) &&
               sameBits(e + k, h + m) && sameBits(e * f, h * f) && sameBits(e.dot(k), h.dot(m)) && sameBits(n * f, o * f) &&
               sameBits(n / f, o / f);
    });

    // Matrices.
    check("matx33d arithmetic, product, inverse", [] {
        const auto [a, c] = matPair<double>();
        const auto [b, d] = matPair<double>();
        const auto [v, w] = vecPair<double, 3>();
        const double g = nonZero<double>();
        return sameBits(a + b, c + d) && sameBits(a - b, c - d) && sameBits(a * g, c * g) && sameBits(a * 2, c * 2) &&
               sameBits(a * b, c * d) && sameBits(a * v, c * w) && sameBits(a.t(), c.t()) && sameBits(a.inv(), c.inv()) &&
               sameBits(cv::determinant(a), determinant(c)) && sameBits(a.mul(b), c.mul(d)) && sameBits(a.dot(b), c.dot(d)) &&
               sameBits(-a, -c) && sameBits(cv::Matx33f(a), Matrix33f(c));
    });
    check("matx33f arithmetic, product, inverse", [] {
        const auto [a, c] = matPair<float>();
        const auto [b, d] = matPair<float>();
        const auto [v, w] = vecPair<float, 3>();
        return sameBits(a * b, c * d) && sameBits(a * v, c * w) && sameBits(a.inv(), c.inv()) &&
               sameBits(cv::determinant(a), determinant(c)) && sameBits(a * 0.5, c * 0.5) && sameBits(cv::Matx33d(a), Matrix33d(c));
    });
    // OpenCV inverts a 3 x 3 by cofactors whatever the method asked: Cholesky included, which then
    // refuses only a zero determinant.
    check("a 3 x 3 inverse asked by cholesky is the cofactor inverse", [] {
        const auto [a, c] = matPair<float>();
        bool theirOk = false, myOk = false;
        const cv::Matx33f theirs = a.inv(cv::DECOMP_CHOLESKY, &theirOk);
        const Matrix33f ours = c.inv(&myOk);
        bool badOk = false, myBadOk = false;
        const cv::Matx33f negative = (cv::Matx33f::eye() * -1.0f).inv(cv::DECOMP_CHOLESKY, &badOk);
        return sameBits(theirs, ours) && theirOk == myOk && sameBits(negative, (Matrix33f::identity() * -1.0f).inv(&myBadOk)) &&
               badOk == myBadOk;
    });
    check("matx33d identity and singular inverse", [] {
        cv::Matx33d a = cv::Matx33d::eye();
        Matrix33d c = Matrix33d::identity();
        const int row = whole(0, 2);
        for (int j = 0; j < 3; ++j)
            a(row, j) = c(size_t(row), size_t(j)) = 0.0;
        return sameBits(cv::Matx33d::eye(), Matrix33d::identity()) && sameBits(a.inv(), c.inv());
    });

    // What replaces the Mat's own tools: its generator, its Scalar fills, its conversions to floats.
    check("random generator", [] {
        const std::uint64_t seed = std::uint64_t(whole(0, 1 << 30)) * 7919u;
        const int high = whole(1, 100000);
        cv::RNG theirs(seed);
        Random mine(seed);
        for (int i = 0; i < 50; ++i)
            if (theirs.uniform(0, high) != mine.uniform(0, high) || unsigned(theirs) != mine.next())
                return false;
        return true;
    });
    check("fill as a scalar", [] {
        const double v = edgy(-40000.0, 40000.0);
        const cv::Mat a(1, 1, CV_8UC1, cv::Scalar(v)), b(1, 1, CV_16SC1, cv::Scalar(v)), c(1, 1, CV_32SC1, cv::Scalar(v)),
            d(1, 1, CV_32FC1, cv::Scalar(v)), e(1, 1, CV_16UC1, cv::Scalar(v));
        return a.at<std::uint8_t>(0, 0) == filled({1, 1}, Depth::U8, 1, v).row<std::uint8_t>(0)[0] &&
               b.at<std::int16_t>(0, 0) == filled({1, 1}, Depth::S16, 1, v).row<std::int16_t>(0)[0] &&
               c.at<std::int32_t>(0, 0) == filled({1, 1}, Depth::S32, 1, v).row<std::int32_t>(0)[0] &&
               sameBits(d.at<float>(0, 0), filled({1, 1}, Depth::F32, 1, v).row<float>(0)[0]) &&
               e.at<std::uint16_t>(0, 0) == filled({1, 1}, Depth::U16, 1, v).row<std::uint16_t>(0)[0];
    });
    check("conversion to floats", [] {
        constexpr int width = 37;
        cv::Mat u16(1, width, CV_16UC1), u8(1, width, CV_8UC1), s16(1, width, CV_16SC2);
        for (int x = 0; x < width; ++x) {
            u16.at<std::uint16_t>(0, x) = std::uint16_t(whole(0, 65535));
            u8.at<std::uint8_t>(0, x) = std::uint8_t(whole(0, 255));
            s16.at<cv::Vec2s>(0, x) = cv::Vec2s(short(whole(-32768, 32767)), short(whole(-32768, 32767)));
        }
        const double scale = real(1e-6, 2.0);
        const auto same = [&](const cv::Mat &from, double a, int channels, Depth depth) {
            cv::Mat theirs;
            from.convertTo(theirs, CV_32FC(channels), a);
            Image mine = Image::create({width, 1}, Depth::F32, channels);
            convertInto(Image::wrapReadOnly(from.data, {width, 1}, depth, channels, std::ptrdiff_t(from.step[0])), mine, a);
            return std::memcmp(theirs.data, mine.row<float>(0), sizeof(float) * size_t(width * channels)) == 0;
        };
        return same(u16, 1.0 / 65535.0, 1, Depth::U16) && same(u16, scale, 1, Depth::U16) &&
               same(u8, 1.0 / 255.0, 1, Depth::U8) && same(s16, 1.0, 2, Depth::S16);
    });
    // With a shift, OpenCV's vectors apply scale and shift with one rounding, as the layer does
    // everywhere; its last values, fewer than a vector, round twice. 64 values: vectors only.
    check("conversion to floats with a shift", [] {
        constexpr int width = 64;
        cv::Mat floats(1, width, CV_32FC1), bytes(1, width, CV_8UC1);
        for (int x = 0; x < width; ++x) {
            floats.at<float>(0, x) = float(real(-2.0, 2.0));
            bytes.at<std::uint8_t>(0, x) = std::uint8_t(whole(0, 255));
        }
        const double scale = real(-3.0, 3.0), shift = real(-1.0, 1.0);
        const auto same = [&](const cv::Mat &from, Depth depth) {
            cv::Mat theirs;
            from.convertTo(theirs, CV_32F, scale, shift);
            Image mine = Image::create({width, 1}, Depth::F32, 1);
            convertInto(Image::wrapReadOnly(from.data, {width, 1}, depth, 1, std::ptrdiff_t(from.step[0])), mine, scale, shift);
            return std::memcmp(theirs.data, mine.row<float>(0), sizeof(float) * size_t(width)) == 0;
        };
        return same(floats, Depth::F32) && same(bytes, Depth::U8);
    });

    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"small types\",\n \"revision\": \"" << argv[1] << "\",\n \"opencv\": \"" << CV_VERSION
        << "\",\n \"oracle\": \"OpenCV's own operators\",\n \"tolerance\": \"identical bits\",\n \"within_tolerance\": "
        << (within ? "true" : "false") << ",\n \"cases\": [\n" << report.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
