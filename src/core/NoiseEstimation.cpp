#include "NoiseEstimation.h"

#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>
#include <utility>

namespace NoiseEstimation {
namespace {
constexpr int kPatch = 16;
constexpr int kGrid = 24; // At most 576 native 16x16 tiles, irrespective of image size.
constexpr float kMadNormal = 0.67448975f;
struct Point { double signal, variance, structure; };
struct Cache
{
    QMutex mutex;
    std::deque<std::pair<QByteArray, Result>> entries;
};

float median(std::vector<float> values)
{
    if (values.empty()) return 0;
    const auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

Point measure(const cv::Mat &plane, const cv::Mat &luma, const cv::Rect &tile)
{
    double mean = 0, gradient = 0;
    for (int y = tile.y; y < tile.y + tile.height; y += 2) {
        const float *a = plane.ptr<float>(y), *b = plane.ptr<float>(y+1);
        const float *la = luma.ptr<float>(y), *lb = luma.ptr<float>(y+1);
        for (int x = tile.x; x < tile.x + tile.width; x += 2) {
            mean += (static_cast<double>(la[x])+la[x+1]+lb[x]+lb[x+1])*.25;
            // Low-frequency Haar directions rank spatial structure, not noise probability.
            gradient += std::abs(static_cast<double>(a[x])+a[x+1]-b[x]-b[x+1]) +
                        std::abs(static_cast<double>(a[x])-a[x+1]+b[x]-b[x+1]);
        }
    }
    const double sd = haarSigma(plane(tile));
    const int samples = tile.width*tile.height/4;
    return {mean / samples, sd*sd, gradient / samples};
}

Model fit(std::vector<Point> points)
{
    Model model;
    model.patches = static_cast<int>(points.size());
    // Exact constant tiles (for example black borders) must not suppress the
    // estimate of the remaining field. A wholly constant image stays zero.
    points.erase(std::remove_if(points.begin(), points.end(),
                                [](const Point &p) { return p.variance <= 1e-20; }), points.end());
    if (points.empty()) return model;
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.signal < b.signal; });
    model.minimumSignal = static_cast<float>(points.front().signal);
    model.maximumSignal = static_cast<float>(points.back().signal);
    // Keep low-structure tiles within each signal stratum, not just dark tiles.
    std::vector<Point> selected;
    for (size_t first = 0; first < points.size();) {
        const size_t last = std::min(points.size(), first + std::max(size_t(4), points.size()/8));
        std::sort(points.begin()+first, points.begin()+last,
                  [](const Point &a, const Point &b) { return a.structure < b.structure; });
        selected.insert(selected.end(), points.begin()+first, points.begin()+first+(last-first+1)/2);
        first = last;
    }
    std::vector<float> variances;
    for (const auto &p : selected) variances.push_back(static_cast<float>(p.variance));
    model.intercept = median(variances);
    if (selected.size() < 8 || model.maximumSignal-model.minimumSignal < 1e-5f)
        return model; // Constant effective variance; no invented signal dependence.
    double xx=0, x=0, weight=0, xy=0, y=0;
    for (const auto &p : selected) {
        const double w = 1.0 / std::pow(std::max(p.variance, 1e-14), 2);
        const double signal = std::max(0., p.signal);
        xx += w*signal*signal; x += w*signal; weight += w;
        xy += w*signal*p.variance; y += w*p.variance;
    }
    const double determinant = xx*weight-x*x;
    if (determinant <= 1e-12*std::max(xx*weight, 1e-30)) return model;
    double a=(xy*weight-x*y)/determinant, b=(xx*y-x*xy)/determinant;
    if (a < 0) { a=0; b=y/weight; }
    if (b < 0) { b=0; a=xx > 0 ? xy/xx : 0; }
    double relativeError=0;
    for (const auto &p : selected)
        relativeError += std::abs(a*std::max(0.,p.signal)+b-p.variance)/std::max(p.variance,1e-14);
    // Experimental quality guard; texture-contaminated fits fall back, not extrapolate.
    if (!std::isfinite(a) || !std::isfinite(b) || a > std::numeric_limits<float>::max() ||
        b > std::numeric_limits<float>::max() || relativeError/selected.size() > .5) return model;
    model.slope = static_cast<float>(a);
    model.intercept = static_cast<float>(b);
    model.affine = a > 0;
    return model;
}
}

float haarSigma(const cv::Mat &plane)
{
    if (plane.empty() || plane.type()!=CV_32F) return 0;
    const int rows=plane.rows/2, cols=plane.cols/2;
    const int stride=std::max(1, static_cast<int>(std::sqrt(static_cast<double>(rows)*cols/16384.)));
    std::vector<float> values;
    for (int y=0; y<rows; y+=stride) {
        const float *a=plane.ptr<float>(2*y), *b=plane.ptr<float>(2*y+1);
        for (int x=0; x<cols; x+=stride) {
            const int i=2*x;
            if (!std::isfinite(a[i]) || !std::isfinite(a[i+1]) ||
                !std::isfinite(b[i]) || !std::isfinite(b[i+1])) return 0;
            if (a[i]==a[i+1] && a[i]==b[i] && a[i]==b[i+1]) continue;
            values.push_back((a[i]-a[i+1]-b[i]+b[i+1])*.5f);
        }
    }
    const float center=median(values);
    for (float &v : values) v=std::abs(v-center);
    return median(std::move(values))/kMadNormal;
}

Result estimate(const cv::Mat &luma, const cv::Mat &co, const cv::Mat &cg)
{
    const std::array<cv::Mat,3> planes{luma,co,cg};
    for (const auto &p : planes)
        if (p.empty() || p.type()!=CV_32F || p.size()!=luma.size()) return {};
    if (luma.rows < kPatch || luma.cols < kPatch) return {};
    std::vector<cv::Rect> tiles;
    const int rows=std::min(kGrid,luma.rows/kPatch), cols=std::min(kGrid,luma.cols/kPatch);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const int geometry[]{luma.rows,luma.cols};
    hash.addData(reinterpret_cast<const char *>(geometry),sizeof(geometry));
    for (int gy=0; gy<rows; ++gy) for (int gx=0; gx<cols; ++gx) {
        const int y=rows==1 ? 0 : gy*(luma.rows-kPatch)/(rows-1);
        const int x=cols==1 ? 0 : gx*(luma.cols-kPatch)/(cols-1);
        const cv::Rect tile(x,y,kPatch,kPatch);
        for (const auto &p : planes) for (int iy=y; iy<y+kPatch; ++iy) {
            const float *row=p.ptr<float>(iy)+x;
            for (int ix=0; ix<kPatch; ++ix) if (!std::isfinite(row[ix])) return {};
            hash.addData(reinterpret_cast<const char *>(row), kPatch*sizeof(float));
        }
        tiles.push_back(tile);
    }
    const QByteArray key=hash.result();
    static Cache cache;
    {
        QMutexLocker lock(&cache.mutex);
        for (const auto &entry : cache.entries) if (entry.first==key) return entry.second;
    }
    Result result;
    for (size_t i=0; i<planes.size(); ++i) {
        std::vector<Point> points;
        for (const auto &tile : tiles) {
            const auto point=measure(planes[i],luma,tile);
            if (!std::isfinite(point.signal) || !std::isfinite(point.variance) ||
                !std::isfinite(point.structure) || point.variance > std::numeric_limits<float>::max()) return {};
            points.push_back(point);
        }
        result.planes[i]=fit(std::move(points));
    }
    result.valid=true;
    {
        QMutexLocker lock(&cache.mutex);
        if (cache.entries.size()>=8) cache.entries.pop_front();
        cache.entries.emplace_back(key,result);
    }
    return result;
}

float sigma(const Model &model, float luminance)
{
    const float signal=std::clamp(luminance,std::max(0.f,model.minimumSignal),
                                 std::max(0.f,model.maximumSignal));
    return std::sqrt(std::max(0.f,model.slope*signal+model.intercept));
}
}
