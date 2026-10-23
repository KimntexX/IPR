// Experimental adaptive-neighbourhood denoising study (protocole-bruit-voisinages-adaptatifs).
// Not a production path: the candidates live only in this probe. Every candidate
// receives the same input, the neutral linear Rec.2020 master just before the
// production denoiser, rendered in memory from the RAW (no TIFF written for
// measurement). Usage:
//   noiseNeighbourhoodProbe OUTPUT.json CROPS_DIR RAW...
#include "DetailProcessing.h"
#include "NoiseEstimation.h"
#include "TestRecipes.h"
#include "WorkingColorSpace.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QThread>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int Inner = 256;          // measured square of each zone, native pixels
constexpr int Margin = 24;          // processing margin around it (>= search + patch radii)
constexpr std::array<int, 4> Radii{1, 2, 4, 8}; // 3x3, 5x5, 9x9, 17x17 windows
constexpr int PatchRadius = 2;      // 5x5 blocks
constexpr int SearchRadius = 8;     // 17x17 search window
constexpr float SigmaFloor = 1e-6f;

void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

struct Planes {
    cv::Mat y, co, cg;
    std::array<cv::Mat *, 3> all() { return {&y, &co, &cg}; }
    std::array<const cv::Mat *, 3> all() const { return {&y, &co, &cg}; }
};

// Same luma/chroma basis as the production denoiser: Y, Co = R - B, Cg = G - (R + B) / 2.
Planes split(const cv::Mat &rgb)
{
    Planes p{cv::Mat(rgb.size(), CV_32F), cv::Mat(rgb.size(), CV_32F), cv::Mat(rgb.size(), CV_32F)};
    for (int y = 0; y < rgb.rows; ++y) {
        const auto *s = rgb.ptr<cv::Vec3f>(y);
        auto *py = p.y.ptr<float>(y), *pco = p.co.ptr<float>(y), *pcg = p.cg.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            py[x] = WorkingColor::luminance(s[x]);
            pco[x] = s[x][0] - s[x][2];
            pcg[x] = s[x][1] - 0.5f * (s[x][0] + s[x][2]);
        }
    }
    return p;
}

// Inverse of split; chroma compressed toward the grey of the same Y to stay nonnegative.
cv::Mat merge(const Planes &p)
{
    cv::Mat rgb(p.y.size(), CV_32FC3);
    const auto &w = WorkingColor::lumaWeights();
    const float k = w[0] + .5f * w[1];
    for (int y = 0; y < rgb.rows; ++y) {
        auto *t = rgb.ptr<cv::Vec3f>(y);
        const float *py = p.y.ptr<float>(y), *pco = p.co.ptr<float>(y), *pcg = p.cg.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            const float yv = std::max(0.f, py[x]);
            const cv::Vec3f d((1.f - k) * pco[x] - w[1] * pcg[x], (.5f - k) * pco[x] + (1.f - w[1]) * pcg[x],
                              -k * pco[x] - w[1] * pcg[x]);
            const float lo = std::min({d[0], d[1], d[2]});
            const float scale = lo < -yv ? yv / -lo : 1.f;
            for (int c = 0; c < 3; ++c) t[x][c] = std::max(0.f, yv + scale * d[c]);
        }
    }
    return rgb;
}

cv::Mat sigmaMap(const NoiseEstimation::Model &model, const cv::Mat &guide)
{
    cv::Mat s(guide.size(), CV_32F);
    for (int y = 0; y < guide.rows; ++y) {
        const float *g = guide.ptr<float>(y);
        float *o = s.ptr<float>(y);
        for (int x = 0; x < guide.cols; ++x) o[x] = std::max(SigmaFloor, NoiseEstimation::sigma(model, g[x]));
    }
    return s;
}

cv::Mat boxMean(const cv::Mat &m, int radius)
{
    cv::Mat out;
    cv::boxFilter(m, out, CV_32F, cv::Size(2 * radius + 1, 2 * radius + 1), cv::Point(-1, -1), true, cv::BORDER_REFLECT);
    return out;
}

struct Zone {
    std::string kind; // flat, dark, detail
    cv::Rect inner;
    double meanY = 0, structure = 0;
};

struct Input {
    cv::Mat padded;              // linear RGB of the zone with its margin
    Planes planes;
    std::array<cv::Mat, 3> modelSigma; // per-pixel sigma of Y, Co, Cg from the full-image model (finest scale)
    std::array<cv::Mat, 3> sigma;      // the same, scaled to the per-pixel noise (correlation factor)
};

// ---------------------------------------------------------------- candidates
// A. Adaptive neighbourhood: largest window whose local variance, in every
// plane, stays within (1 + kappa) times the expected noise variance; mean over it.
struct AdaptiveResult { Planes out; cv::Mat radiusIndex; };

AdaptiveResult adaptive(const Input &in, float kappa)
{
    const cv::Size size = in.planes.y.size();
    cv::Mat chosen(size, CV_8U, cv::Scalar(0));
    std::array<std::array<cv::Mat, 3>, Radii.size()> means;
    cv::Mat stillHomogeneous(size, CV_8U, cv::Scalar(1));
    for (size_t r = 0; r < Radii.size(); ++r) {
        cv::Mat ok(size, CV_8U, cv::Scalar(1));
        for (int c = 0; c < 3; ++c) {
            const cv::Mat &plane = *in.planes.all()[size_t(c)];
            means[r][size_t(c)] = boxMean(plane, Radii[r]);
            cv::Mat variance = boxMean(plane.mul(plane), Radii[r]) - means[r][size_t(c)].mul(means[r][size_t(c)]);
            cv::Mat limit = in.sigma[size_t(c)].mul(in.sigma[size_t(c)]) * (1.0 + kappa);
            cv::Mat pass = variance <= limit;
            cv::bitwise_and(ok, pass / 255, ok);
        }
        cv::bitwise_and(stillHomogeneous, ok, stillHomogeneous);
        chosen.setTo(int(r + 1), stillHomogeneous);
    }
    Planes out{in.planes.y.clone(), in.planes.co.clone(), in.planes.cg.clone()};
    for (int y = 0; y < size.height; ++y) {
        const uchar *ch = chosen.ptr<uchar>(y);
        for (int c = 0; c < 3; ++c) {
            float *o = out.all()[size_t(c)]->ptr<float>(y);
            for (int x = 0; x < size.width; ++x)
                if (ch[x]) o[x] = means[size_t(ch[x] - 1)][size_t(c)].at<float>(y, x);
        }
    }
    return {out, chosen};
}

// B / C. Block similarity (non-local means) on noise-normalised Y, Co, Cg jointly:
// mean squared normalised difference of 5x5 blocks, expected 2 for pure noise.
// C restricts the search to the window chosen by A at each pixel.
struct BlockResult { Planes out; cv::Mat effectiveNeighbours; };

BlockResult blocks(const Input &in, float h2, const cv::Mat *radiusIndex)
{
    const cv::Size size = in.planes.y.size();
    std::array<cv::Mat, 3> normalised;
    for (int c = 0; c < 3; ++c) cv::divide(*in.planes.all()[size_t(c)], in.sigma[size_t(c)], normalised[size_t(c)]);
    const int border = SearchRadius;
    const cv::Rect core(border, border, size.width - 2 * border, size.height - 2 * border);
    require(core.width > 0 && core.height > 0, "zone too small for the search window");
    std::array<cv::Mat, 3> numerator;
    for (auto &n : numerator) n = cv::Mat::zeros(core.size(), CV_32F);
    cv::Mat sum = cv::Mat::zeros(core.size(), CV_32F), sum2 = cv::Mat::zeros(core.size(), CV_32F);
    cv::Mat allowed(core.size(), CV_32F);
    const float spatial = 2.f * SearchRadius * SearchRadius;
    for (int dy = -SearchRadius; dy <= SearchRadius; ++dy) {
        for (int dx = -SearchRadius; dx <= SearchRadius; ++dx) {
            const cv::Rect shifted = core + cv::Point(dx, dy);
            cv::Mat distance = cv::Mat::zeros(core.size(), CV_32F);
            for (int c = 0; c < 3; ++c) {
                cv::Mat d = normalised[size_t(c)](core) - normalised[size_t(c)](shifted);
                distance += d.mul(d);
            }
            distance = boxMean(distance, PatchRadius) / 3.f;  // per channel, expected 2
            cv::Mat weight;
            cv::Mat excess = cv::max(distance - 2.f, 0.f) / std::max(h2 * 2.f, 1e-6f);
            cv::exp(-excess, weight);
            weight *= std::exp(-float(dx * dx + dy * dy) / spatial);
            if (radiusIndex) {
                const int reach = std::max(std::abs(dx), std::abs(dy));
                for (int y = 0; y < core.height; ++y) {
                    const uchar *ri = radiusIndex->ptr<uchar>(y + border) + border;
                    float *a = allowed.ptr<float>(y);
                    for (int x = 0; x < core.width; ++x)
                        a[x] = reach <= (ri[x] ? Radii[size_t(ri[x] - 1)] : 1) ? 1.f : 0.f;
                }
                weight = weight.mul(allowed);
            }
            for (int c = 0; c < 3; ++c)
                numerator[size_t(c)] += weight.mul((*in.planes.all()[size_t(c)])(shifted));
            sum += weight;
            sum2 += weight.mul(weight);
        }
    }
    Planes out{in.planes.y.clone(), in.planes.co.clone(), in.planes.cg.clone()};
    for (int c = 0; c < 3; ++c) {
        cv::Mat estimate;
        cv::divide(numerator[size_t(c)], sum, estimate);
        estimate.copyTo((*out.all()[size_t(c)])(core));
    }
    cv::Mat neff(size, CV_32F, cv::Scalar(1));
    cv::Mat n;
    cv::divide(sum.mul(sum), cv::max(sum2, 1e-12f), n);
    n.copyTo(neff(core));
    return {out, neff};
}

// D. Fusion of a robust median, the C estimate and the production result, with
// weights from the confidence of C (effective neighbours). When C and the
// production result disagree beyond tau noise sigmas, fall back toward the
// estimator that changed the pixel least (conservative).
// Robust variant: low confidence leans on the 5x5 median (isolated outliers have
// no similar block, so C keeps them; the median removes them); no fallback toward
// the least-changing estimator, which would keep such outliers.
Planes fuseRobust(const Input &in, const Planes &nlm, const cv::Mat &neff, const Planes &production)
{
    Planes median;
    for (int c = 0; c < 3; ++c) cv::medianBlur(*in.planes.all()[size_t(c)], *median.all()[size_t(c)], 5);
    Planes out{in.planes.y.clone(), in.planes.co.clone(), in.planes.cg.clone()};
    constexpr float n0 = 4.f;
    for (int y = 0; y < out.y.rows; ++y)
        for (int x = 0; x < out.y.cols; ++x) {
            const float n = neff.at<float>(y, x);
            const float aN = n / (n + n0), aM = (1.f - aN) * 0.7f, aP = (1.f - aN) * 0.3f;
            for (int c = 0; c < 3; ++c)
                out.all()[size_t(c)]->at<float>(y, x) = aN * nlm.all()[size_t(c)]->at<float>(y, x) +
                    aM * median.all()[size_t(c)]->at<float>(y, x) + aP * production.all()[size_t(c)]->at<float>(y, x);
        }
    return out;
}

Planes fuse(const Input &in, const Planes &nlm, const cv::Mat &neff, const Planes &production, bool confidence)
{
    Planes median;
    for (int c = 0; c < 3; ++c) cv::medianBlur(*in.planes.all()[size_t(c)], *median.all()[size_t(c)], 5);
    Planes out{in.planes.y.clone(), in.planes.co.clone(), in.planes.cg.clone()};
    constexpr float n0 = 4.f, tau = 2.f;
    for (int y = 0; y < out.y.rows; ++y) {
        for (int x = 0; x < out.y.cols; ++x) {
            float aN = 1.f / 3, aP = 1.f / 3, aM = 1.f / 3;
            if (confidence) {
                const float n = neff.at<float>(y, x);
                aN = n / (n + n0);
                aP = (1.f - aN) * 0.7f;
                aM = (1.f - aN) * 0.3f;
            }
            float disagreement = 0.f;
            std::array<float, 3> fused{}, conservative{};
            float leastChange = 1e30f;
            int least = 0;
            for (int c = 0; c < 3; ++c) {
                const float s = in.sigma[size_t(c)].at<float>(y, x);
                const float vn = nlm.all()[size_t(c)]->at<float>(y, x);
                const float vp = production.all()[size_t(c)]->at<float>(y, x);
                const float vm = median.all()[size_t(c)]->at<float>(y, x);
                fused[size_t(c)] = aN * vn + aP * vp + aM * vm;
                disagreement += ((vn - vp) / s) * ((vn - vp) / s);
            }
            disagreement = std::sqrt(disagreement / 3.f);
            for (int k = 0; k < 3; ++k) {
                float change = 0.f;
                for (int c = 0; c < 3; ++c) {
                    const Planes &e = k == 0 ? nlm : k == 1 ? production : median;
                    const float d = (e.all()[size_t(c)]->at<float>(y, x) - in.planes.all()[size_t(c)]->at<float>(y, x)) /
                                    in.sigma[size_t(c)].at<float>(y, x);
                    change += d * d;
                }
                if (change < leastChange) { leastChange = change; least = k; }
            }
            const Planes &cons = least == 0 ? nlm : least == 1 ? production : median;
            const float keep = confidence ? 1.f / (1.f + (disagreement / tau) * (disagreement / tau)) : 1.f;
            for (int c = 0; c < 3; ++c) {
                conservative[size_t(c)] = cons.all()[size_t(c)]->at<float>(y, x);
                out.all()[size_t(c)]->at<float>(y, x) =
                    conservative[size_t(c)] + keep * (fused[size_t(c)] - conservative[size_t(c)]);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- measures
double meanOf(const cv::Mat &m) { return cv::mean(m)[0]; }

double madSigma(const cv::Mat &m)
{
    std::vector<float> v(m.begin<float>(), m.end<float>());
    if (v.empty()) return 0;
    auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    const float median = *mid;
    for (float &x : v) x = std::abs(x - median);
    std::nth_element(v.begin(), mid, v.end());
    return *mid * 1.4826;
}

cv::Mat bandPass(const cv::Mat &m)
{
    cv::Mat a, b;
    cv::GaussianBlur(m, a, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    cv::GaussianBlur(m, b, cv::Size(0, 0), 4.0, 4.0, cv::BORDER_REFLECT);
    return a - b;
}

double correlation(const cv::Mat &a, const cv::Mat &b)
{
    cv::Scalar ma, sa, mb, sb;
    cv::meanStdDev(a, ma, sa);
    cv::meanStdDev(b, mb, sb);
    if (sa[0] <= 0 || sb[0] <= 0) return 0;
    return cv::mean((a - ma[0]).mul(b - mb[0]))[0] / (sa[0] * sb[0]);
}

QJsonObject measure(const Planes &in, const Planes &out, const cv::Rect &inner)
{
    const auto chroma = [](double a, double b) { return std::sqrt((a * a + b * b) / 2); };
    const cv::Mat yi = in.y(inner), yo = out.y(inner);
    const double hhYi = NoiseEstimation::haarSigma(yi), hhYo = NoiseEstimation::haarSigma(yo);
    const double hhCi = chroma(NoiseEstimation::haarSigma(in.co(inner)), NoiseEstimation::haarSigma(in.cg(inner)));
    const double hhCo = chroma(NoiseEstimation::haarSigma(out.co(inner)), NoiseEstimation::haarSigma(out.cg(inner)));
    const cv::Mat bpYi = bandPass(in.y)(inner), bpYo = bandPass(out.y)(inner);
    const double midCi = chroma(madSigma(bandPass(in.co)(inner)), madSigma(bandPass(in.cg)(inner)));
    const double midCo = chroma(madSigma(bandPass(out.co)(inner)), madSigma(bandPass(out.cg)(inner)));
    cv::Mat smooth, gx, gy, gradient;
    cv::GaussianBlur(in.y, smooth, cv::Size(0, 0), 2.0, 2.0, cv::BORDER_REFLECT);
    cv::Sobel(smooth, gx, CV_32F, 1, 0, 3, 1.0 / 8.0);
    cv::Sobel(smooth, gy, CV_32F, 0, 1, 3, 1.0 / 8.0);
    cv::magnitude(gx, gy, gradient);
    const cv::Mat removed = cv::abs(in.y - out.y);
    const double meanYi = meanOf(yi);
    return {
        {"luma_hh_ratio", hhYi > 0 ? hhYo / hhYi : 1.0},
        {"chroma_hh_ratio", hhCi > 0 ? hhCo / hhCi : 1.0},
        {"luma_midband_ratio", madSigma(bpYi) > 0 ? madSigma(bpYo) / madSigma(bpYi) : 1.0},
        {"chroma_midband_ratio", midCi > 0 ? midCo / midCi : 1.0},
        {"luma_midband_correlation", correlation(bpYi, bpYo)},
        {"luma_drift_relative", meanYi > 0 ? (meanOf(yo) - meanYi) / meanYi : 0.0},
        {"chroma_drift_sigma", hhCi > 0 ? std::hypot(meanOf(out.co(inner)) - meanOf(in.co(inner)),
                                                     meanOf(out.cg(inner)) - meanOf(in.cg(inner))) / hhCi : 0.0},
        {"removed_structure_correlation", correlation(removed(inner), gradient(inner))},
    };
}

// ---------------------------------------------------------------- zones and crops
std::vector<Zone> selectZones(const Planes &full, const NoiseEstimation::Result &model)
{
    cv::Mat smooth, gx, gy, gradient;
    cv::GaussianBlur(full.y, smooth, cv::Size(0, 0), 2.0, 2.0, cv::BORDER_REFLECT);
    cv::Sobel(smooth, gx, CV_32F, 1, 0, 3, 1.0 / 8.0);
    cv::Sobel(smooth, gy, CV_32F, 0, 1, 3, 1.0 / 8.0);
    cv::magnitude(gx, gy, gradient);
    std::vector<Zone> candidates;
    const int span = Inner + 2 * Margin;
    for (int y = Margin; y + Inner + Margin <= full.y.rows; y += Inner / 2) {
        for (int x = Margin; x + Inner + Margin <= full.y.cols; x += Inner / 2) {
            const cv::Rect r(x, y, Inner, Inner);
            const double meanY = meanOf(full.y(r));
            if (meanY < 0.002 || meanY > 0.8) continue;
            const double sigma = std::max(1e-6, double(NoiseEstimation::sigma(model.planes[0], float(meanY))));
            candidates.push_back({"", r, meanY, meanOf(gradient(r)) / sigma});
        }
    }
    (void)span;
    std::sort(candidates.begin(), candidates.end(), [](const Zone &a, const Zone &b) { return a.structure < b.structure; });
    std::vector<Zone> chosen;
    const auto overlaps = [&](const cv::Rect &r) {
        return std::any_of(chosen.begin(), chosen.end(), [&](const Zone &z) { return (z.inner & r).area() > 0; });
    };
    const auto take = [&](const std::string &kind, int count, const std::function<bool(const Zone &)> &accept, bool reverse) {
        int taken = 0;
        for (size_t i = 0; i < candidates.size() && taken < count; ++i) {
            const Zone &z = candidates[reverse ? candidates.size() - 1 - i : i];
            if (!accept(z) || overlaps(z.inner)) continue;
            chosen.push_back(z);
            chosen.back().kind = kind;
            ++taken;
        }
    };
    take("dark", 2, [](const Zone &z) { return z.meanY < 0.02; }, false);
    take("flat", 4, [](const Zone &z) { return z.meanY >= 0.02; }, false);
    take("detail", 4, [](const Zone &z) { return z.meanY >= 0.01; }, true);
    return chosen;
}

QImage display(const cv::Mat &rgb, const cv::Rect &inner, double scale)
{
    QImage image(inner.width, inner.height, QImage::Format_RGB888);
    for (int y = 0; y < inner.height; ++y) {
        const auto *s = rgb.ptr<cv::Vec3f>(y + inner.y) + inner.x;
        uchar *d = image.scanLine(y);
        for (int x = 0; x < inner.width; ++x)
            for (int c = 0; c < 3; ++c)
                d[3 * x + c] = uchar(std::lround(255.0 * std::clamp(WorkingColor::srgbEncode(double(s[x][c]) * scale), 0.0, 1.0)));
    }
    return image;
}

QString sha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

struct Variant {
    QString name, family;
    QJsonObject parameters;
};

// ---------------------------------------------------------------- pair truth (step A)
// Two exposures of the same static scene: D = (A - B) / sqrt(2) holds only the
// developed noise (demosaic and rendering included), M = (A + B) / 2 the signal.
// On a grid of 32x32 tiles whose M is not structured, measure per plane and band
// the noise of D (truth) and of A alone (what a blind single-image estimator sees),
// binned by the tile's mean Y; also the production model's prediction.
constexpr int Tile = 32;
constexpr int BinCount = 16;          // log10 Y from -4 to 0, quarter decades
const std::array<const char *, 5> BandNames{"haar_hh", "fine", "mid", "coarse", "pixel"};

int yBin(double y)
{
    if (!(y > 0)) return -1;
    const int b = int(std::floor((std::log10(y) + 4.0) * 4.0));
    return b >= 0 && b < BinCount ? b : -1;
}

// Band images of a plane: fine x - G1, mid G1 - G4, coarse G4 - G16, pixel x - G16.
std::array<cv::Mat, 4> bands(const cv::Mat &plane)
{
    cv::Mat g1, g4, g16;
    cv::GaussianBlur(plane, g1, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    cv::GaussianBlur(plane, g4, cv::Size(0, 0), 4.0, 4.0, cv::BORDER_REFLECT);
    cv::GaussianBlur(plane, g16, cv::Size(0, 0), 16.0, 16.0, cv::BORDER_REFLECT);
    return {cv::Mat(plane - g1), cv::Mat(g1 - g4), cv::Mat(g4 - g16), cv::Mat(plane - g16)};
}

cv::Mat renderMaster(const QString &raw)
{
    const auto loaded = TestRecipes::load(raw);
    const auto working = ImageEditor::renderLinear(loaded.image, TestRecipes::neutral(loaded));
    require(!working.isNull(), "render failed: " + raw.toStdString());
    cv::Mat master;
    cv::cvtColor(working.linearRgba, master, cv::COLOR_RGBA2RGB);
    return master;
}

double quantileOf(std::vector<double> v, double q)
{
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, size_t(q * double(v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(k), v.end());
    return v[k];
}


// Blind single-image noise profile on the same tile grid and bands as the pair
// truth: per Y bin, quantiles of the tile noise of each plane and band.
QJsonArray blindProfile(const Planes &planes)
{
    std::vector<int> tileBins;
    for (int y = 0; y + Tile <= planes.y.rows; y += Tile)
        for (int x = 0; x + Tile <= planes.y.cols; x += Tile)
            tileBins.push_back(yBin(meanOf(planes.y(cv::Rect(x, y, Tile, Tile)))));
    std::array<std::array<std::array<std::vector<double>, BinCount>, 5>, 3> values;
    for (int c = 0; c < 3; ++c) {
        const cv::Mat &plane = *planes.all()[size_t(c)];
        const auto b = bands(plane);
        size_t t = 0;
        for (int y = 0; y + Tile <= plane.rows; y += Tile)
            for (int x = 0; x + Tile <= plane.cols; x += Tile, ++t) {
                const int bin = tileBins[t];
                if (bin < 0) continue;
                const cv::Rect r(x, y, Tile, Tile);
                values[size_t(c)][0][size_t(bin)].push_back(NoiseEstimation::haarSigma(plane(r)));
                for (int k = 0; k < 4; ++k) values[size_t(c)][size_t(k + 1)][size_t(bin)].push_back(madSigma(b[size_t(k)](r)));
            }
    }
    const char *planeNames[3] = {"y", "co", "cg"};
    QJsonArray bins;
    for (int bi = 0; bi < BinCount; ++bi) {
        QJsonObject planesRecord;
        bool any = false;
        for (int c = 0; c < 3; ++c) {
            QJsonObject bandRecords;
            for (int k = 0; k < 5; ++k) {
                const auto &v = values[size_t(c)][size_t(k)][size_t(bi)];
                if (v.size() < 8) continue;
                any = true;
                bandRecords.insert(BandNames[size_t(k)], QJsonObject{
                    {"q05", quantileOf(v, 0.05)}, {"q10", quantileOf(v, 0.10)}, {"q25", quantileOf(v, 0.25)},
                    {"median", quantileOf(v, 0.5)}, {"tiles", int(v.size())}});
            }
            planesRecord.insert(planeNames[c], bandRecords);
        }
        if (any) bins.append(QJsonObject{{"bin", bi}, {"y_centre", std::pow(10.0, -4.0 + (bi + 0.5) / 4.0)},
                                         {"planes", planesRecord}});
    }
    return bins;
}


// ---------------------------------------------------------------- candidate E
// Scale-aware profile: per plane, band and Y bin, the blind tile estimate
// (q25 of the tile noise, divided by its bias measured on the pair truth),
// empty bins filled from the nearest valid bin.
struct NoiseProfile {
    std::array<std::array<std::array<double, BinCount>, 5>, 3> sigma{};
    double at(int plane, int band, double y) const
    {
        const double position = std::clamp((std::log10(std::max(y, 1e-4)) + 4.0) * 4.0 - 0.5, 0.0, double(BinCount - 1));
        const int i = std::min(int(position), BinCount - 2);
        const double t = position - i;
        const auto &v = sigma[size_t(plane)][size_t(band)];
        return v[size_t(i)] * (1 - t) + v[size_t(i + 1)] * t;
    }
};

// Spectral signature of the developed noise, measured on 61 exposure-matched pairs
// (etude-bruit-paires-developpe-v001): band sigma / Haar HH sigma, stable from ISO 100
// to 51200 on the R6 Mark III. Index: haar, fine, mid, coarse, pixel.
constexpr std::array<double, 5> LumaSpectrum{1.0, 1.17, 0.52, 0.135, 1.5};
constexpr std::array<double, 5> ChromaSpectrum{1.0, 1.43, 1.57, 0.54, 2.8};

// Blind profile: only the Haar HH level is estimated on the image (q25 of the tile
// noise divided by its bias, the band least affected by texture); the other bands
// follow from the spectral signature.
NoiseProfile profileFrom(const Planes &planes, double bias)
{
    std::vector<int> tileBins;
    for (int y = 0; y + Tile <= planes.y.rows; y += Tile)
        for (int x = 0; x + Tile <= planes.y.cols; x += Tile)
            tileBins.push_back(yBin(meanOf(planes.y(cv::Rect(x, y, Tile, Tile)))));
    NoiseProfile profile;
    for (int c = 0; c < 3; ++c) {
        const cv::Mat &plane = *planes.all()[size_t(c)];
        std::array<std::vector<double>, BinCount> values;
        size_t t = 0;
        for (int y = 0; y + Tile <= plane.rows; y += Tile)
            for (int x = 0; x + Tile <= plane.cols; x += Tile, ++t)
                if (tileBins[t] >= 0) values[size_t(tileBins[t])].push_back(NoiseEstimation::haarSigma(plane(cv::Rect(x, y, Tile, Tile))));
        std::array<double, BinCount> haar{};
        std::array<bool, BinCount> valid{};
        for (int bi = 0; bi < BinCount; ++bi)
            if (values[size_t(bi)].size() >= 8) {
                haar[size_t(bi)] = quantileOf(values[size_t(bi)], 0.25) / bias;
                valid[size_t(bi)] = true;
            }
        for (int bi = 0; bi < BinCount; ++bi) {
            if (valid[size_t(bi)]) continue;
            for (int d = 1; d < BinCount; ++d) {
                if (bi - d >= 0 && valid[size_t(bi - d)]) { haar[size_t(bi)] = haar[size_t(bi - d)]; break; }
                if (bi + d < BinCount && valid[size_t(bi + d)]) { haar[size_t(bi)] = haar[size_t(bi + d)]; break; }
            }
        }
        const auto &spectrum = c == 0 ? LumaSpectrum : ChromaSpectrum;
        for (int k = 0; k < 5; ++k)
            for (int bi = 0; bi < BinCount; ++bi) profile.sigma[size_t(c)][size_t(k)][size_t(bi)] = haar[size_t(bi)] * spectrum[size_t(k)];
    }
    return profile;
}

// Local Wiener shrinkage of a band: gain max(0, 1 - alpha sigma^2 / local energy).
cv::Mat wiener(const cv::Mat &band, const cv::Mat &sigma, double alpha, int radius)
{
    const cv::Mat energy = boxMean(band.mul(band), radius);
    cv::Mat out(band.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, band.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const float *b = band.ptr<float>(y), *e = energy.ptr<float>(y), *s = sigma.ptr<float>(y);
            float *o = out.ptr<float>(y);
            for (int x = 0; x < band.cols; ++x)
                o[x] = b[x] * std::max(0.f, 1.f - float(alpha) * s[x] * s[x] / std::max(e[x], 1e-12f));
        }
    });
    return out;
}

cv::Mat sigmaOf(const NoiseProfile &profile, int plane, int band, const cv::Mat &guideY)
{
    cv::Mat s(guideY.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, guideY.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const float *g = guideY.ptr<float>(y);
            float *o = s.ptr<float>(y);
            for (int x = 0; x < guideY.cols; ++x) o[x] = float(std::max(1e-7, profile.at(plane, band, g[x])));
        }
    });
    return s;
}

// He et al. guided filter: p filtered with guide I, radius r, regularisation eps.
cv::Mat guided(const cv::Mat &I, const cv::Mat &p, int r, double eps)
{
    const cv::Mat mI = boxMean(I, r), mP = boxMean(p, r);
    const cv::Mat cov = boxMean(I.mul(p), r) - mI.mul(mP);
    const cv::Mat var = boxMean(I.mul(I), r) - mI.mul(mI);
    cv::Mat a;
    cv::divide(cov, var + eps, a);
    const cv::Mat b = mP - a.mul(mI);
    return boxMean(a, r).mul(I) + boxMean(b, r);
}

// Joint block similarity on half-resolution chroma, guided by luminance blocks.
cv::Mat chromaBlocks(const cv::Mat &yHalf, const cv::Mat &cHalf, float sigmaY, float sigmaC, float h2, int search)
{
    const int border = search;
    cv::Mat yp, cp;
    cv::copyMakeBorder(yHalf / sigmaY, yp, border, border, border, border, cv::BORDER_REFLECT);
    cv::copyMakeBorder(cHalf / sigmaC, cp, border, border, border, border, cv::BORDER_REFLECT);
    const cv::Rect core(border, border, yHalf.cols, yHalf.rows);
    cv::Mat numerator = cv::Mat::zeros(yHalf.size(), CV_32F), sum = cv::Mat::zeros(yHalf.size(), CV_32F);
    for (int dy = -search; dy <= search; ++dy)
        for (int dx = -search; dx <= search; ++dx) {
            const cv::Rect shifted = core + cv::Point(dx, dy);
            const cv::Mat dY = yp(core) - yp(shifted), dC = cp(core) - cp(shifted);
            cv::Mat distance = boxMean(dY.mul(dY) + dC.mul(dC), 1) * 0.5f;   // per channel, expected 2
            cv::Mat weight;
            cv::exp(-cv::max(distance - 2.f, 0.f) / std::max(h2 * 2.f, 1e-6f), weight);
            numerator += weight.mul(cp(shifted));
            sum += weight;
        }
    cv::Mat estimate;
    cv::divide(numerator, sum, estimate);
    return estimate * sigmaC;
}

struct CandidateE { double lumaAlpha, chromaH2, chromaFineAlpha; };

Planes candidateE(const Input &in, const NoiseProfile &profile, const CandidateE &k)
{
    cv::Mat guideY, level;
    cv::GaussianBlur(in.planes.y, guideY, cv::Size(0, 0), 1.0, 1.0, cv::BORDER_REFLECT);
    // The noise level is read on G4: on G1 the noise itself raises sigma where it is
    // positive, shrinks bright deviations more than dark ones and darkens the shadows.
    cv::GaussianBlur(in.planes.y, level, cv::Size(0, 0), 4.0, 4.0, cv::BORDER_REFLECT);
    Planes out;
    // Luminance: Wiener shrinkage per band, sigma of each band at the local level.
    {
        const auto b = bands(in.planes.y);   // fine, mid, coarse, pixel
        cv::Mat g16;
        cv::GaussianBlur(in.planes.y, g16, cv::Size(0, 0), 16.0, 16.0, cv::BORDER_REFLECT);
        out.y = g16 + wiener(b[0], sigmaOf(profile, 0, 1, level), k.lumaAlpha, 2)
                    + wiener(b[1], sigmaOf(profile, 0, 2, level), k.lumaAlpha, 3)
                    + wiener(b[2], sigmaOf(profile, 0, 3, level), k.lumaAlpha, 6);
        // Near black the noise is skewed and the shrunk bands no longer average to
        // zero: the G16 mean of the input is restored, as for the chroma.
        cv::Mat meanOut;
        cv::GaussianBlur(out.y, meanOut, cv::Size(0, 0), 16.0, 16.0, cv::BORDER_REFLECT);
        out.y += g16 - meanOut;
    }
    // Chrominance: half-resolution impulse median + joint blocks, guided upsampling,
    // fine residual shrunk.
    cv::Mat yHalf;
    cv::resize(in.planes.y, yHalf, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
    const float sigmaYHalf = float(std::max(1e-7, profile.at(0, 2, meanOf(in.planes.y))));
    for (int c = 1; c < 3; ++c) {
        const cv::Mat &plane = *in.planes.all()[size_t(c)];
        cv::Mat area, half, upLow, denoisedHalf, up;
        cv::resize(plane, area, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        cv::medianBlur(area, half, 3);   // not in place
        const float sigmaCHalf = float(std::max(1e-7, profile.at(c, 2, meanOf(in.planes.y))));
        denoisedHalf = chromaBlocks(yHalf, half, sigmaYHalf, sigmaCHalf, float(k.chromaH2), 4);
        cv::resize(denoisedHalf, up, plane.size(), 0, 0, cv::INTER_LINEAR);
        cv::Mat areaHalf;
        cv::resize(plane, areaHalf, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
        cv::resize(areaHalf, upLow, plane.size(), 0, 0, cv::INTER_LINEAR);
        const double eps = std::pow(profile.at(0, 4, meanOf(in.planes.y)), 2) * 4.0;
        const cv::Mat smoothChroma = guided(guideY, up, 3, eps);
        const cv::Mat fineResidual = plane - upLow;
        cv::Mat denoised = smoothChroma + wiener(fineResidual, sigmaOf(profile, c, 1, level), k.chromaFineAlpha, 2);
        // The median and the block weights bias the skewed chroma noise near black:
        // the G16 mean of the input is restored (the noise has almost no energy there).
        cv::Mat meanIn, meanOut;
        cv::GaussianBlur(plane, meanIn, cv::Size(0, 0), 16.0, 16.0, cv::BORDER_REFLECT);
        cv::GaussianBlur(denoised, meanOut, cv::Size(0, 0), 16.0, 16.0, cv::BORDER_REFLECT);
        *out.all()[size_t(c)] = denoised + meanIn - meanOut;
    }
    return out;
}


// Tiled E: independent 512x512 tiles with a 64-pixel margin (G16 support, guided
// filter, half-resolution search), processed in parallel; memory stays bounded by
// the tiles in flight, and the thread count is the host's (cv::setNumThreads).
Planes candidateETiled(const Planes &full, const NoiseProfile &profile, const CandidateE &k)
{
    constexpr int TileSize = 512, TileMargin = 64;
    // Starts as the input: a last odd row or column left out by the even tiles stays unchanged.
    Planes out{full.y.clone(), full.co.clone(), full.cg.clone()};
    std::vector<cv::Rect> tiles;
    for (int y = 0; y < full.y.rows; y += TileSize)
        for (int x = 0; x < full.y.cols; x += TileSize)
            tiles.emplace_back(x, y, std::min(TileSize, full.y.cols - x), std::min(TileSize, full.y.rows - y));
    cv::parallel_for_(cv::Range(0, int(tiles.size())), [&](const cv::Range &range) {
        for (int i = range.start; i < range.end; ++i) {
            const cv::Rect core = tiles[size_t(i)];
            const cv::Rect padded = (core + cv::Size(2 * TileMargin, 2 * TileMargin) - cv::Point(TileMargin, TileMargin)) &
                                    cv::Rect(0, 0, full.y.cols, full.y.rows);
            // even padded size keeps the half-resolution resize exact
            const cv::Rect even(padded.x, padded.y, padded.width & ~1, padded.height & ~1);
            Input tile;
            tile.planes = Planes{full.y(even).clone(), full.co(even).clone(), full.cg(even).clone()};
            const Planes result = candidateE(tile, profile, k);
            const cv::Rect inside(core.x - even.x, core.y - even.y, std::min(core.width, even.x + even.width - core.x),
                                  std::min(core.height, even.y + even.height - core.y));
            const cv::Rect target(core.x, core.y, inside.width, inside.height);
            for (int c = 0; c < 3; ++c) (*result.all()[size_t(c)])(inside).copyTo((*out.all()[size_t(c)])(target));
        }
    }, double(tiles.size()));
    return out;
}

QJsonObject pairTruth(const QString &rawA, const QString &rawB)
{
    const cv::Mat a = renderMaster(rawA), b = renderMaster(rawB);
    require(a.size() == b.size(), "pair geometry differs");
    const Planes pa = split(a), pb = split(b);
    Planes d, m;
    for (int c = 0; c < 3; ++c) {
        *d.all()[size_t(c)] = (*pa.all()[size_t(c)] - *pb.all()[size_t(c)]) / std::sqrt(2.0);
        *m.all()[size_t(c)] = (*pa.all()[size_t(c)] + *pb.all()[size_t(c)]) * 0.5;
    }
    // Exposure drift between the frames would leave signal in D: report it.
    const double driftY = meanOf(pa.y) > 0 ? (meanOf(pa.y) - meanOf(pb.y)) / meanOf(pa.y) : 0.0;
    // Tile structure from the smoothed signal M (misregistration or motion leaves edges in D).
    cv::Mat smooth, gx, gy, gradient;
    cv::GaussianBlur(m.y, smooth, cv::Size(0, 0), 2.0, 2.0, cv::BORDER_REFLECT);
    cv::Sobel(smooth, gx, CV_32F, 1, 0, 3, 1.0 / 8.0);
    cv::Sobel(smooth, gy, CV_32F, 0, 1, 3, 1.0 / 8.0);
    cv::magnitude(gx, gy, gradient);
    const auto model = NoiseEstimation::estimate(pa.y, pa.co, pa.cg);
    struct TileStat { int bin; double meanY, structure; };
    std::vector<TileStat> tiles;
    for (int y = 0; y + Tile <= a.rows; y += Tile)
        for (int x = 0; x + Tile <= a.cols; x += Tile) {
            const cv::Rect r(x, y, Tile, Tile);
            tiles.push_back({yBin(meanOf(m.y(r))), meanOf(m.y(r)), meanOf(gradient(r))});
        }
    // truth[plane][band][bin] and blind[plane][band][bin] tile values
    std::array<std::array<std::array<std::vector<double>, BinCount>, 5>, 3> truth, blind;
    const char *planeNames[3] = {"y", "co", "cg"};
    for (int c = 0; c < 3; ++c) {
        const cv::Mat &pd = *d.all()[size_t(c)], &pA = *pa.all()[size_t(c)];
        const auto bd = bands(pd), bA = bands(pA);
        size_t t = 0;
        for (int y = 0; y + Tile <= a.rows; y += Tile)
            for (int x = 0; x + Tile <= a.cols; x += Tile, ++t) {
                const TileStat &ts = tiles[t];
                if (ts.bin < 0) continue;
                const cv::Rect r(x, y, Tile, Tile);
                // signal-free tiles only for the truth: M gradient below 3 noise sigmas of Y
                const double ySigma = std::max(1e-6, double(NoiseEstimation::haarSigma(d.y(r))));
                const bool quiet = ts.structure < 3.0 * ySigma;
                const auto put = [&](int band, double vd, double vA) {
                    if (quiet) truth[size_t(c)][size_t(band)][size_t(ts.bin)].push_back(vd);
                    blind[size_t(c)][size_t(band)][size_t(ts.bin)].push_back(vA);
                };
                put(0, NoiseEstimation::haarSigma(pd(r)), NoiseEstimation::haarSigma(pA(r)));
                for (int k = 0; k < 4; ++k) put(k + 1, madSigma(bd[size_t(k)](r)), madSigma(bA[size_t(k)](r)));
            }
    }
    QJsonArray bins;
    for (int bi = 0; bi < BinCount; ++bi) {
        const double yCentre = std::pow(10.0, -4.0 + (bi + 0.5) / 4.0);
        QJsonObject planes;
        bool any = false;
        for (int c = 0; c < 3; ++c) {
            QJsonObject bandRecords;
            for (int k = 0; k < 5; ++k) {
                const auto &tv = truth[size_t(c)][size_t(k)][size_t(bi)];
                const auto &bv = blind[size_t(c)][size_t(k)][size_t(bi)];
                if (tv.size() < 8 || bv.size() < 8) continue;
                any = true;
                bandRecords.insert(BandNames[size_t(k)], QJsonObject{
                    {"truth_median", quantileOf(tv, 0.5)}, {"truth_tiles", int(tv.size())},
                    {"blind_q05", quantileOf(bv, 0.05)}, {"blind_q10", quantileOf(bv, 0.10)},
                    {"blind_q25", quantileOf(bv, 0.25)}, {"blind_median", quantileOf(bv, 0.5)},
                    {"blind_tiles", int(bv.size())}});
            }
            if (model.valid)
                bandRecords.insert("production_model_haar", double(NoiseEstimation::sigma(model.planes[size_t(c)], float(yCentre))));
            planes.insert(planeNames[c], bandRecords);
        }
        if (any) bins.append(QJsonObject{{"bin", bi}, {"y_centre", yCentre}, {"planes", planes}});
    }
    return {{"a", QFileInfo(rawA).fileName()}, {"b", QFileInfo(rawB).fileName()},
            {"a_sha256", sha256(rawA)}, {"b_sha256", sha256(rawB)},
            {"width", a.cols}, {"height", a.rows}, {"mean_y_drift_a_vs_b", driftY},
            {"tile", Tile}, {"quiet_rule", "M gradient (Gaussian 2) < 3 Haar sigma of D(Y) in the tile"},
            {"bins", bins}};
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Unicode arguments (paths such as colorimétrie); argv is the ANSI code page on Windows.
    const QStringList arguments = QCoreApplication::arguments();
    try {
        if (argc >= 2 && arguments.at(1) == QStringLiteral("--pairs")) {
            require(argc >= 5 && (argc - 3) % 2 == 0, "usage: noiseNeighbourhoodProbe --pairs OUTPUT.json A1 B1 [A2 B2 ...]");
            const QString output = arguments.at(2);
            require(!QFileInfo::exists(output), "output exists, nothing overwritten");
            QJsonArray pairs;
            for (int i = 3; i + 1 < argc; i += 2) {
                pairs.append(pairTruth(arguments.at(i), arguments.at(i + 1)));
                std::cout << arguments.at(i).toStdString() << " / " << arguments.at(i + 1).toStdString() << std::endl;
            }
            QFile f(output);
            require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), "cannot create " + output.toStdString());
            f.write(QJsonDocument(QJsonObject{{"pairs", pairs}}).toJson(QJsonDocument::Indented));
            return 0;
        }
        require(argc >= 4, "usage: noiseNeighbourhoodProbe OUTPUT.json CROPS_DIR RAW...");
        const QString output = arguments.at(1);
        const QString cropsDir = arguments.at(2);
        require(!QFileInfo::exists(output), "output exists, nothing overwritten");
        require(QDir().mkpath(cropsDir), "cannot create the crops directory");
        QJsonArray scenes;
        for (int a = 3; a < argc; ++a) {
            const QString raw = arguments.at(a);
            const auto loaded = TestRecipes::load(raw);
            const auto settings = TestRecipes::neutral(loaded);
            const auto working = ImageEditor::renderLinear(loaded.image, settings);
            require(!working.isNull(), "render failed: " + raw.toStdString());
            cv::Mat master;
            cv::cvtColor(working.linearRgba, master, cv::COLOR_RGBA2RGB);
            const Planes full = split(master);
            const auto model = NoiseEstimation::estimate(full.y, full.co, full.cg);
            require(model.valid, "noise estimation failed: " + raw.toStdString());

            // Production reference on the whole image, as the editor runs it.
            QJsonObject timings;
            std::vector<std::pair<QString, Planes>> productionResults;
            for (int strength : {50, 100}) {
                cv::Mat denoised = master.clone();
                const auto t0 = Clock::now();
                DetailProcessing::reduceNoise(denoised, {strength, strength});
                const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                const QString name = QStringLiteral("reference-%1").arg(strength);
                timings.insert(name, ms / (master.total() / 1e6));
                productionResults.emplace_back(name, split(denoised));
            }

            const auto zones = selectZones(full, model);
            // Blind scale-aware profile for candidate E; bias of q25 measured on the pair truth.
            constexpr double ProfileBias = 0.87;
            const auto profileStart = Clock::now();
            const NoiseProfile profile = profileFrom(full, ProfileBias);
            const double profileMs = std::chrono::duration<double, std::milli>(Clock::now() - profileStart).count();
            // Developed noise is spatially correlated: the model fits the finest (Haar)
            // scale, the per-pixel deviation of flat zones is larger. Measure the factor
            // per plane on the flat and dark zones, on the residual after a wide blur
            // (removes gentle gradients) with a median estimator (robust to texture);
            // the candidates use the resulting per-pixel sigma.
            constexpr double ResidualBlur = 6.0;
            std::array<double, 3> correlation{1, 1, 1};
            {
                std::array<std::vector<double>, 3> ratios;
                const std::array<const cv::Mat *, 3> planes = full.all();
                for (const Zone &z : zones) {
                    if (z.kind == "detail") continue;
                    const cv::Rect around(z.inner.x - Margin, z.inner.y - Margin, Inner + 2 * Margin, Inner + 2 * Margin);
                    const cv::Rect inside(Margin, Margin, Inner, Inner);
                    for (int c = 0; c < 3; ++c) {
                        const cv::Mat plane = (*planes[size_t(c)])(around);
                        cv::Mat trend;
                        cv::GaussianBlur(plane, trend, cv::Size(0, 0), ResidualBlur, ResidualBlur, cv::BORDER_REFLECT);
                        const double deviation = madSigma(cv::Mat(plane - trend)(inside));
                        const double predicted = cv::mean(sigmaMap(model.planes[size_t(c)], full.y(z.inner)))[0];
                        if (predicted > 0) ratios[size_t(c)].push_back(deviation / predicted);
                    }
                }
                for (int c = 0; c < 3; ++c) {
                    auto &r = ratios[size_t(c)];
                    if (r.empty()) continue;
                    std::nth_element(r.begin(), r.begin() + std::ptrdiff_t(r.size() / 2), r.end());
                    correlation[size_t(c)] = std::max(1.0, r[r.size() / 2]);
                }
            }
            QJsonArray zoneRecords;
            std::map<QString, double> variantMs;
            double zonePixels = 0;
            for (size_t zi = 0; zi < zones.size(); ++zi) {
                const Zone &z = zones[zi];
                const cv::Rect padded(z.inner.x - Margin, z.inner.y - Margin, Inner + 2 * Margin, Inner + 2 * Margin);
                const cv::Rect inner(Margin, Margin, Inner, Inner);
                Input in;
                in.padded = master(padded).clone();
                in.planes = split(in.padded);
                for (int c = 0; c < 3; ++c) {
                    in.modelSigma[size_t(c)] = sigmaMap(model.planes[size_t(c)], in.planes.y);
                    in.sigma[size_t(c)] = in.modelSigma[size_t(c)] * correlation[size_t(c)];
                }
                zonePixels += double(padded.area());

                std::vector<std::pair<Variant, Planes>> results;
                for (const auto &[name, planes] : productionResults)
                    results.push_back({{name, "reference", {{"luminance", name.endsWith("100") ? 100 : 50},
                                                            {"chrominance", name.endsWith("100") ? 100 : 50}}},
                                       Planes{planes.y(padded).clone(), planes.co(padded).clone(), planes.cg(padded).clone()}});
                const auto timed = [&](const QString &name, auto &&run) {
                    const auto t0 = Clock::now();
                    auto value = run();
                    variantMs[name] += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                    return value;
                };
                for (float kappa : {0.5f, 1.5f, 3.0f}) {
                    const QString name = QStringLiteral("A-kappa-%1").arg(double(kappa));
                    const auto r = timed(name, [&] { return adaptive(in, kappa); });
                    results.push_back({{name, "A", {{"kappa", double(kappa)}, {"radii", "3,5,9,17"}}}, r.out});
                }
                const AdaptiveResult guide = adaptive(in, 1.5f);
                BlockResult cForFusion;
                for (float h2 : {0.5f, 1.0f, 2.0f}) {
                    const QString nameB = QStringLiteral("B-h2-%1").arg(double(h2));
                    const auto b = timed(nameB, [&] { return blocks(in, h2, nullptr); });
                    results.push_back({{nameB, "B", {{"h2", double(h2)}, {"patch", 5}, {"search", 17}}}, b.out});
                    const QString nameC = QStringLiteral("C-h2-%1").arg(double(h2));
                    const auto c = timed(nameC, [&] { return blocks(in, h2, &guide.radiusIndex); });
                    results.push_back({{nameC, "C", {{"h2", double(h2)}, {"kappa", 1.5}, {"patch", 5}, {"search", 17}}}, c.out});
                    if (h2 == 1.0f) cForFusion = c;
                }
                // A copy (shared pixel data): results keeps growing, a reference would dangle.
                const Planes production100 = results[1].second;
                results.push_back({{"D-confidence", "D", {{"from", "C-h2-1"}, {"n0", 4}, {"tau", 2}, {"weights", "neff, 0.7 production, 0.3 median"}}},
                                   timed("D-confidence", [&] { return fuse(in, cForFusion.out, cForFusion.effectiveNeighbours, production100, true); })});
                results.push_back({{"D-fixed", "D", {{"from", "C-h2-1"}, {"weights", "1/3 each, no fallback"}}},
                                   timed("D-fixed", [&] { return fuse(in, cForFusion.out, cForFusion.effectiveNeighbours, production100, false); })});
                for (const CandidateE &k : {CandidateE{1.0, 1.0, 2.0}, CandidateE{2.0, 1.0, 3.0}, CandidateE{2.0, 2.0, 4.0}}) {
                    const QString name = QStringLiteral("E-l%1-c%2-f%3").arg(k.lumaAlpha).arg(k.chromaH2).arg(k.chromaFineAlpha);
                    results.push_back({{name, "E", {{"luma_wiener_alpha", k.lumaAlpha}, {"chroma_h2", k.chromaH2},
                                                     {"chroma_fine_alpha", k.chromaFineAlpha}, {"profile_bias", ProfileBias},
                                                     {"chroma", "half-res median 3 + joint blocks 3x3 in 9x9, guided upsampling r3"}}},
                                       timed(name, [&] { return candidateE(in, profile, k); })});
                }
                results.push_back({{"D-robust", "D", {{"from", "C-h2-1"}, {"n0", 4}, {"weights", "neff, 0.7 median, 0.3 production"}}},
                                   timed("D-robust", [&] { return fuseRobust(in, cForFusion.out, cForFusion.effectiveNeighbours, production100); })});

                QJsonArray measures;
                std::vector<QImage> tiles;
                double p99 = 0;
                {
                    std::vector<float> v(in.planes.y(inner).clone().reshape(1, 1));
                    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() * 99 / 100), v.end());
                    p99 = std::max(1e-6f, v[v.size() * 99 / 100]);
                }
                const double scale = 0.8 / p99;
                tiles.push_back(display(in.padded, inner, scale));
                for (const auto &[variant, planes] : results) {
                    QJsonObject m = measure(in.planes, planes, inner);
                    m.insert("variant", variant.name);
                    m.insert("family", variant.family);
                    m.insert("parameters", variant.parameters);
                    measures.append(m);
                    tiles.push_back(display(merge(planes), inner, scale));
                }
                QImage montage(int(tiles.size()) * Inner, Inner, QImage::Format_RGB888);
                montage.fill(Qt::black);
                for (size_t i = 0; i < tiles.size(); ++i)
                    for (int y = 0; y < Inner; ++y)
                        std::copy_n(tiles[i].constScanLine(y), Inner * 3, montage.scanLine(y) + i * Inner * 3);
                const QString crop = QDir(cropsDir).filePath(QStringLiteral("%1-%2-%3.png")
                    .arg(QFileInfo(raw).completeBaseName()).arg(int(zi)).arg(QString::fromStdString(z.kind)));
                require(montage.save(crop), "cannot write " + crop.toStdString());
                QJsonArray order{"input"};
                for (const auto &r : results) order.append(r.first.name);
                // Calibration: noise predicted by the model vs measured on the zone (Haar HH, per plane).
                QJsonObject calibration;
                const char *planeNames[3] = {"y", "co", "cg"};
                for (int c = 0; c < 3; ++c) {
                    calibration.insert(QStringLiteral("%1_model_sigma").arg(planeNames[c]),
                                       cv::mean(in.modelSigma[size_t(c)](inner))[0]);
                    calibration.insert(QStringLiteral("%1_measured_hh").arg(planeNames[c]),
                                       double(NoiseEstimation::haarSigma((*in.planes.all()[size_t(c)])(inner))));
                    cv::Scalar mean, deviation;
                    cv::meanStdDev((*in.planes.all()[size_t(c)])(inner), mean, deviation);
                    calibration.insert(QStringLiteral("%1_pixel_std").arg(planeNames[c]), deviation[0]);
                }
                zoneRecords.append(QJsonObject{
                    {"kind", QString::fromStdString(z.kind)},
                    {"calibration", calibration},
                    {"rect", QJsonArray{z.inner.x, z.inner.y, z.inner.width, z.inner.height}},
                    {"mean_y", z.meanY}, {"structure", z.structure},
                    {"display_scale", scale}, {"crop", QFileInfo(crop).fileName()}, {"crop_order", order},
                    {"measures", measures}});
            }
            for (const auto &[name, ms] : variantMs) timings.insert(name, ms / (zonePixels / 1e6));
            {
                const int defaultThreads = cv::getNumThreads();
                for (int threads : {1, 8}) {
                    cv::setNumThreads(threads);
                    const auto t0 = Clock::now();
                    const Planes out = candidateETiled(full, profile, CandidateE{1.0, 1.0, 2.0});
                    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                    timings.insert(QStringLiteral("E-full-image-%1-threads").arg(threads), ms / (master.total() / 1e6));
                    require(!out.y.empty(), "E failed on the full image");
                }
                cv::setNumThreads(defaultThreads);
                timings.insert("default_threads", defaultThreads);
            }
            timings.insert("E-profile-full-image", profileMs / (master.total() / 1e6));
            QJsonArray modelPlanes;
            for (const auto &p : model.planes)
                modelPlanes.append(QJsonObject{{"slope", p.slope}, {"intercept", p.intercept}, {"affine", p.affine},
                                               {"patches", p.patches}, {"min_signal", p.minimumSignal}, {"max_signal", p.maximumSignal}});
            scenes.append(QJsonObject{
                {"raw", QFileInfo(raw).fileName()}, {"raw_sha256", sha256(raw)},
                {"iso", loaded.metadata.iso}, {"camera", loaded.metadata.cameraModel},
                {"blind_noise_profile", blindProfile(full)},
                {"width", master.cols}, {"height", master.rows},
                {"input", "neutral linear Rec.2020 master before the denoiser (TestRecipes::neutral, renderWorkingImage)"},
                {"noise_model", modelPlanes}, {"zones", zoneRecords},
                {"correlation_factor", QJsonArray{correlation[0], correlation[1], correlation[2]}},
                {"ms_per_megapixel", timings}});
            std::cout << QFileInfo(raw).fileName().toStdString() << " : " << zones.size() << " zones" << std::endl;
        }
        QJsonObject root{
            {"study", "adaptive-neighbourhood denoising candidates (protocole-bruit-voisinages-adaptatifs)"},
            {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"zone", QJsonObject{{"inner", Inner}, {"margin", Margin}}},
            {"machine", QJsonObject{{"cpu_threads", QThread::idealThreadCount()}, {"os", QSysInfo::prettyProductName()},
                                    {"gpu", "none"}}},
            {"measures", "Haar HH MAD (Y and Co/Cg) and mid-band (Gaussian 1 - 4) MAD ratios output/input on the inner "
                         "zone; mid-band Y correlation; mean drifts; correlation of the removed |dY| with the smoothed "
                         "input gradient (high = structure removed). No clean reference: no MAE/PSNR."},
            {"scenes", scenes}};
        QFile f(output);
        require(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), "cannot create " + output.toStdString());
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
