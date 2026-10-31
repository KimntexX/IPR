#include "ToneCurve.h"

#include "WorkingColorSpace.h"

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

const cv::Matx33d &fromProPhoto()
{
    static const cv::Matx33d matrix = WorkingColor::toProPhoto().inv();
    return matrix;
}
} // namespace

bool isIdentity(const Curves &curves)
{
    return std::all_of(curves.cbegin(), curves.cend(), [](const QVector<Point> &curve) { return Spline(curve).identity; });
}

float evaluate(const QVector<Point> &curve, float x)
{
    return float(Spline(curve)(x));
}

void apply(cv::Mat &linearRgb, const Curves &curves)
{
    if (isIdentity(curves) || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    const Spline rgb(curves[Luminance]);
    const std::array<Spline, 3> channel{Spline(curves[Red]), Spline(curves[Green]), Spline(curves[Blue])};
    const cv::Matx33d &toProPhoto = WorkingColor::toProPhoto();
    const cv::Matx33d &back = fromProPhoto();
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                cv::Vec3d p = toProPhoto * cv::Vec3d(row[x]);
                for (int c = 0; c < 3; ++c) {
                    // Below black (out of gamut) a value keeps its offset from the curve's black.
                    const double e = encode(p[c]);
                    const double r = e < 0.0 ? rgb(0.0) + e : rgb(e);
                    p[c] = decode(r < 0.0 ? channel[size_t(c)](0.0) + r : channel[size_t(c)](r));
                }
                row[x] = cv::Vec3f(back * p);
            }
        }
    });
}
}
