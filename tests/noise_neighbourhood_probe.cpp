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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        require(argc >= 4, "usage: noiseNeighbourhoodProbe OUTPUT.json CROPS_DIR RAW...");
        const QString output = QString::fromLocal8Bit(argv[1]);
        const QString cropsDir = QString::fromLocal8Bit(argv[2]);
        require(!QFileInfo::exists(output), "output exists, nothing overwritten");
        require(QDir().mkpath(cropsDir), "cannot create the crops directory");
        QJsonArray scenes;
        for (int a = 3; a < argc; ++a) {
            const QString raw = QString::fromLocal8Bit(argv[a]);
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
                const Planes &production100 = results[1].second;
                results.push_back({{"D-confidence", "D", {{"from", "C-h2-1"}, {"n0", 4}, {"tau", 2}, {"weights", "neff, 0.7 production, 0.3 median"}}},
                                   timed("D-confidence", [&] { return fuse(in, cForFusion.out, cForFusion.effectiveNeighbours, production100, true); })});
                results.push_back({{"D-fixed", "D", {{"from", "C-h2-1"}, {"weights", "1/3 each, no fallback"}}},
                                   timed("D-fixed", [&] { return fuse(in, cForFusion.out, cForFusion.effectiveNeighbours, production100, false); })});
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
            QJsonArray modelPlanes;
            for (const auto &p : model.planes)
                modelPlanes.append(QJsonObject{{"slope", p.slope}, {"intercept", p.intercept}, {"affine", p.affine},
                                               {"patches", p.patches}, {"min_signal", p.minimumSignal}, {"max_signal", p.maximumSignal}});
            scenes.append(QJsonObject{
                {"raw", QFileInfo(raw).fileName()}, {"raw_sha256", sha256(raw)},
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
