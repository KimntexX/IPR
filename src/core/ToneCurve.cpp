#include "ToneCurve.h"

#include "WorkingColorSpace.h"
#include "Parallel.h"
#include "Types.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ToneCurve {
namespace {
// A curve ready to evaluate: its points and the spline's second derivatives.
struct Spline
{
    std::vector<double> x, y, second;
    bool identity = true;

    explicit Spline(const QVector<Point> &curve)
    {
        for (const Point &p : curve) {
            if (!x.empty() && p.x - x.back() < 1e-6) continue;   // repeated abscissa: first point kept
            x.push_back(p.x);
            y.push_back(p.y);
        }
        const size_t n = x.size();
        identity = n < 2 || (n == 2 && x[0] == 0.0 && y[0] == 0.0 && x[1] == 1.0 && y[1] == 1.0);
        second.assign(n, 0.0);
        if (identity || n < 3) return;
        // Natural spline: zero second derivative at both ends (tridiagonal solve).
        std::vector<double> u(n, 0.0);
        for (size_t i = 1; i + 1 < n; ++i) {
            const double sig = (x[i] - x[i - 1]) / (x[i + 1] - x[i - 1]);
            const double p = sig * second[i - 1] + 2.0;
            second[i] = (sig - 1.0) / p;
            const double slopes = (y[i + 1] - y[i]) / (x[i + 1] - x[i]) - (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
            u[i] = (6.0 * slopes / (x[i + 1] - x[i - 1]) - sig * u[i - 1]) / p;
        }
        second[n - 1] = 0.0;
        for (size_t k = n - 1; k-- > 0;) second[k] = second[k] * second[k + 1] + u[k];
    }

    double endSlope() const
    {
        const size_t n = x.size();
        const double h = x[n - 1] - x[n - 2];
        return (y[n - 1] - y[n - 2]) / h + h * (2.0 * second[n - 1] + second[n - 2]) / 6.0;
    }

    double operator()(double v) const
    {
        if (identity) return v;
        if (v <= x.front()) return y.front();
        if (v >= x.back())
            return x.back() >= 1.0 ? y.back() + (v - x.back()) * endSlope() : y.back();
        const size_t hi = size_t(std::upper_bound(x.begin(), x.end(), v) - x.begin());
        const size_t lo = hi - 1;
        const double h = x[hi] - x[lo], a = (x[hi] - v) / h, b = (v - x[lo]) / h;
        return a * y[lo] + b * y[hi] + ((a * a * a - a) * second[lo] + (b * b * b - b) * second[hi]) * h * h / 6.0;
    }
};

// sRGB transfer, odd so that out-of-gamut negatives keep their sign.
double encode(double v) { return v < 0.0 ? -WorkingColor::srgbEncode(-v) : WorkingColor::srgbEncode(v); }
double decode(double v) { return v < 0.0 ? -WorkingColor::srgbDecode(-v) : WorkingColor::srgbDecode(v); }

const ipr::Matrix33d &fromProPhoto()
{
    static const ipr::Matrix33d matrix = WorkingColor::toProPhoto().inv();
    return matrix;
}
} // namespace

QVector<Point> normalized(QVector<Point> points)
{
    points.erase(std::remove_if(points.begin(), points.end(), [](const Point &p) {
        return !std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0.0f || p.x > 1.0f;
    }), points.end());
    for (Point &p : points) p.y = std::clamp(p.y, 0.0f, 1.0f);
    std::stable_sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.x < b.x; });
    QVector<Point> curve;
    curve.reserve(points.size());
    for (const Point &p : points)
        if (curve.isEmpty() || p.x - curve.constLast().x >= 0.001f) curve.append(p);
    if (curve.size() < 2) curve = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    return curve;
}

bool isIdentity(const Curves &curves)
{
    return std::all_of(curves.cbegin(), curves.cend(), [](const QVector<Point> &curve) { return Spline(curve).identity; });
}

float evaluate(const QVector<Point> &curve, float x)
{
    return float(Spline(curve)(x));
}

void apply(ipr::Image &linearRgb, const Curves &curves)
{
    if (isIdentity(curves) || linearRgb.empty() || !(linearRgb.depth() == ipr::Depth::F32 && linearRgb.channels() == 3)) return;
    const Spline rgb(curves[Luminance]);
    const std::array<Spline, 3> channel{Spline(curves[Red]), Spline(curves[Green]), Spline(curves[Blue])};
    const ipr::Matrix33d &toProPhoto = WorkingColor::toProPhoto();
    const ipr::Matrix33d &back = fromProPhoto();
    ipr::parallelRows(linearRgb.height(), [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            auto *row = linearRgb.mutableRow<ipr::Vec3f>(y);
            for (int x = 0; x < linearRgb.width(); ++x) {
                ipr::Vec3d p = toProPhoto * ipr::Vec3d(row[x]);
                for (int c = 0; c < 3; ++c) {
                    // Below black (out of gamut) a value keeps its offset from the curve's black.
                    const double e = encode(p[c]);
                    const double r = e < 0.0 ? rgb(0.0) + e : rgb(e);
                    p[c] = decode(r < 0.0 ? channel[size_t(c)](0.0) + r : channel[size_t(c)](r));
                }
                row[x] = ipr::Vec3f(back * p);
            }
        }
    });
}
}
