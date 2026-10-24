// Demosaicing study: LibRaw's Bayer algorithms and their noise options, OpenCV's
// edge-aware demosaic and IPR's RCD (src/raw/Demosaic), the last two plugged into
// LibRaw through interpolate_bayer_cb, applied to the same unpacked RAW. For each
// variant:
//  - cost of dcraw_process (LibRaw is built without OpenMP: one core);
//  - noise left in flat zones and detail kept in textured zones, as ratios to
//    AHD, LibRaw's default and IPR's algorithm before RCD;
//  - reconstruction error on a synthetic mosaic: the visible area binned 2x2
//    (R, mean G, B in raw units) is the truth, re-mosaicked into the top-left
//    quarter of the same RAW and demosaicked again.
// Output stays in camera RGB with the camera white balance, linear, unrotated.
// Usage:
//   demosaicProbe OUTPUT.json CROPS_DIR RAW...
#include "Demosaic.h"

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

#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int Tile = 128;         // measured tiles, native pixels
constexpr int FlatTiles = 40;
constexpr int DetailTiles = 20;
constexpr int CropSize = 128;     // montage tiles, shown at 2x
constexpr int TruthBorder = 16;   // synthetic quarter: excluded border near the original data
constexpr double TruthBlur = 0.6; // optical low-pass applied to the binned truth, pixels

void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

struct Variant {
    const char *name;
    int quality;            // user_qual
    int medianPasses = 0;   // med_passes (3x3 median on R-G, B-G after interpolation)
    int fbdd = 0;           // fbdd_noiserd (0 off, 1 light, 2 full, before interpolation)
    int dcbIterations = 0;
    int dcbEnhance = 0;
    enum class Ours { None, OpenCvEdgeAware, Rcd } ours = Ours::None;
    Demosaic::ChromaMedian oursMedian = Demosaic::ChromaMedian::Off;   // IPR's median after its interpolation
    Demosaic::Green oursGreen = Demosaic::Green::Ratio;
};

const std::vector<Variant> &variants()
{
    static const std::vector<Variant> list{
        {"AHD", 3},   // production, first: reference of the ratios and of the tiles
        {"linear", 0},
        {"VNG", 1},
        {"PPG", 2},
        {"AHD-median1", 3, 1},
        {"AHD-median3", 3, 3},
        {"AHD-fbdd1", 3, 0, 1},
        {"AHD-fbdd2", 3, 0, 2},
        {"DCB", 4},
        {"DCB-i2-enhance", 4, 0, 0, 2, 1},
        {"DCB-fbdd1", 4, 0, 1},
        {"DHT", 11},
        {"AAHD", 12},
        {"OpenCV-EA", 0, 0, 0, 0, 0, Variant::Ours::OpenCvEdgeAware},
        {"RCD", 0, 0, 0, 0, 0, Variant::Ours::Rcd},
        {"RCD-median-flat", 0, 0, 0, 0, 0, Variant::Ours::Rcd, Demosaic::ChromaMedian::FlatOnly},
        {"RCD-own-direction", 0, 0, 0, 0, 0, Variant::Ours::Rcd, Demosaic::ChromaMedian::FlatOnly, Demosaic::Green::RatioOwnDirection},
        {"RCD-laplacian", 0, 0, 0, 0, 0, Variant::Ours::Rcd, Demosaic::ChromaMedian::FlatOnly, Demosaic::Green::Laplacian},
        {"RCD-laplacian-own", 0, 0, 0, 0, 0, Variant::Ours::Rcd, Demosaic::ChromaMedian::FlatOnly, Demosaic::Green::LaplacianOwnDirection},
        {"RCD-half-laplacian", 0, 0, 0, 0, 0, Variant::Ours::Rcd, Demosaic::ChromaMedian::FlatOnly, Demosaic::Green::HalfLaplacian},
    };
    return list;
}

// Variants of this run: AHD first (reference of the ratios), then the requested ones or all.
std::vector<Variant> selected;
// OpenCV threads of our interpolations: the first for the measures, the others for scaling.
std::vector<int> threadCounts{8};

// OpenCV edge-aware demosaic in place of LibRaw's interpolation. At this step the
// image is white-balanced, one colour per pixel (second green folded into 1).
void openCvEdgeAware(void *context)
{
    auto *raw = static_cast<LibRaw *>(context);
    auto &d = raw->imgdata;
    const int w = d.sizes.iwidth, h = d.sizes.iheight;
    require(d.image && w > 1 && h > 1, "interpolation callback without image");
    const auto colour = [&](int r, int c) { const int k = raw->FC(r, c); return k == 3 ? 1 : k; };
    cv::Mat cfa(h, w, CV_16U);
    for (int y = 0; y < h; ++y) {
        auto *p = cfa.ptr<ushort>(y);
        for (int x = 0; x < w; ++x) p[x] = d.image[size_t(y) * w + x][colour(y, x)];
    }
    // OpenCV names a Bayer pattern by its second row: RGGB is BayerBG.
    const int a = colour(0, 0), b = colour(0, 1);
    const int code = a == 0 ? cv::COLOR_BayerBG2RGB_EA
                   : a == 2 ? cv::COLOR_BayerRG2RGB_EA
                   : b == 0 ? cv::COLOR_BayerGB2RGB_EA
                            : cv::COLOR_BayerGR2RGB_EA;
    cv::Mat rgb;
    cv::demosaicing(cfa, rgb, code);
    for (int y = 0; y < h; ++y) {
        const auto *p = rgb.ptr<cv::Vec<ushort, 3>>(y);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) d.image[size_t(y) * w + x][c] = p[x][c];
    }
}


// --- IPR's production interpolation (src/raw/Demosaic) ---------------------------
Demosaic::ChromaMedian oursMedian = Demosaic::ChromaMedian::Off;   // set by process() for the callback
Demosaic::Green oursGreen = Demosaic::Green::Ratio;
double oursMs = 0;                 // time of our interpolation, measured inside the callback

void rcdInterpolate(void *context)
{
    auto *raw = static_cast<LibRaw *>(context);
    auto &d = raw->imgdata;
    Demosaic::Pattern pattern{};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const int k = raw->FC(y, x);
            pattern[size_t(y)][size_t(x)] = k == 3 ? 1 : k;   // second green folded
        }
    const auto t0 = Clock::now();
    Demosaic::interpolate(d.image, d.sizes.iwidth, d.sizes.iheight, pattern, oursMedian, oursGreen, {});
    oursMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
// LibRaw keeps its callbacks protected: a subclass sets the interpolation step.
class StudyRaw : public LibRaw
{
public:
    void setBayerInterpolation(process_step_callback cb) { callbacks.interpolate_bayer_cb = cb; }
};

struct ImageDeleter {
    void operator()(libraw_processed_image_t *p) const { LibRaw::dcraw_clear_mem(p); }
};

// Demosaics with one variant; returns linear camera RGB in [0, 1] (CV_32FC3) and the cost.
cv::Mat process(StudyRaw &raw, const Variant &v, double *ms)
{
    auto &p = raw.imgdata.params;
    p.user_qual = v.quality;
    p.med_passes = v.medianPasses;
    p.fbdd_noiserd = v.fbdd;
    p.dcb_iterations = v.dcbIterations;
    p.dcb_enhance_fl = v.dcbEnhance;
    oursMedian = v.oursMedian;
    oursGreen = v.oursGreen;
    oursMs = 0;
    raw.setBayerInterpolation(v.ours == Variant::Ours::OpenCvEdgeAware ? openCvEdgeAware
                              : v.ours == Variant::Ours::Rcd           ? rcdInterpolate
                                                                       : nullptr);
    const auto t0 = Clock::now();
    const int result = raw.dcraw_process();
    *ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    raw.setBayerInterpolation(nullptr);
    require(result == LIBRAW_SUCCESS, std::string("dcraw_process: ") + libraw_strerror(result));
    int error = 0;
    std::unique_ptr<libraw_processed_image_t, ImageDeleter> image(raw.dcraw_make_mem_image(&error));
    require(image && error == LIBRAW_SUCCESS && image->colors == 3 && image->bits == 16, "dcraw_make_mem_image failed");
    const cv::Mat wrapped(image->height, image->width, CV_16UC3, image->data);
    cv::Mat out;
    wrapped.convertTo(out, CV_32FC3, 1.0 / 65535.0);
    return out;
}

struct Planes { cv::Mat y, co, cg; };

Planes split(const cv::Mat &rgb)
{
    Planes p{cv::Mat(rgb.size(), CV_32F), cv::Mat(rgb.size(), CV_32F), cv::Mat(rgb.size(), CV_32F)};
    for (int y = 0; y < rgb.rows; ++y) {
        const auto *s = rgb.ptr<cv::Vec3f>(y);
        auto *py = p.y.ptr<float>(y), *pco = p.co.ptr<float>(y), *pcg = p.cg.ptr<float>(y);
        for (int x = 0; x < rgb.cols; ++x) {
            py[x] = 0.25f * (s[x][0] + 2.f * s[x][1] + s[x][2]);
            pco[x] = s[x][0] - s[x][2];
            pcg[x] = s[x][1] - 0.5f * (s[x][0] + s[x][2]);
        }
    }
    return p;
}

double madSigma(const cv::Mat &m)
{
    std::vector<float> v(m.begin<float>(), m.end<float>());
    for (float &x : v) x = std::abs(x);
    const auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    return *mid / 0.6745;
}

// Robust sigma of the diagonal Haar coefficient (HH), the finest noise band;
// constant 2x2 cells (clipped or empty) are skipped.
double haarSigma(const cv::Mat &plane)
{
    std::vector<float> values;
    for (int y = 0; y + 1 < plane.rows; y += 2) {
        const float *a = plane.ptr<float>(y), *b = plane.ptr<float>(y + 1);
        for (int x = 0; x + 1 < plane.cols; x += 2) {
            if (a[x] == a[x + 1] && a[x] == b[x] && a[x] == b[x + 1]) continue;
            values.push_back(0.5f * (a[x] - a[x + 1] - b[x] + b[x + 1]));
        }
    }
    if (values.empty()) return 0;
    const auto mid = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), mid, values.end());
    const float centre = *mid;
    for (float &v : values) v = std::abs(v - centre);
    std::nth_element(values.begin(), mid, values.end());
    return *mid / 0.6745;
}

double rms(const cv::Mat &m) { return std::sqrt(cv::mean(m.mul(m))[0]); }

cv::Mat blur(const cv::Mat &m, double sigma)
{
    cv::Mat out;
    cv::GaussianBlur(m, out, cv::Size(0, 0), sigma, sigma, cv::BORDER_REFLECT);
    return out;
}

struct TileMeasure {
    double lumaHh, chromaHh, lumaMid, chromaMid, lumaFine, chromaFine;
};

TileMeasure measureTile(const cv::Mat &rgb, const cv::Rect &r)
{
    const Planes p = split(rgb(r));
    const cv::Mat g1y = blur(p.y, 1), g4y = blur(p.y, 4);
    const cv::Mat g1co = blur(p.co, 1), g4co = blur(p.co, 4), g1cg = blur(p.cg, 1), g4cg = blur(p.cg, 4);
    TileMeasure m;
    m.lumaHh = haarSigma(p.y);
    m.chromaHh = 0.5 * (haarSigma(p.co) + haarSigma(p.cg));
    m.lumaMid = madSigma(g1y - g4y);
    m.chromaMid = 0.5 * (madSigma(g1co - g4co) + madSigma(g1cg - g4cg));
    m.lumaFine = rms(p.y - g1y);
    m.chromaFine = 0.5 * (rms(p.co - g1co) + rms(p.cg - g1cg));
    return m;
}

// Flat and textured tiles chosen once on the AHD output, then reused for every variant.
void chooseTiles(const cv::Mat &rgb, std::vector<cv::Rect> *flat, std::vector<cv::Rect> *detail)
{
    struct Candidate { cv::Rect r; double flatness, texture; };
    std::vector<Candidate> c;
    for (int y = Tile; y + 2 * Tile <= rgb.rows; y += Tile)
        for (int x = Tile; x + 2 * Tile <= rgb.cols; x += Tile) {
            const cv::Rect r(x, y, Tile, Tile);
            double minV, maxV;
            cv::minMaxLoc(rgb(r).reshape(1), &minV, &maxV);
            const Planes p = split(rgb(r));
            const double mean = cv::mean(p.y)[0];
            if (maxV > 0.95 || mean < 0.003) continue;
            cv::Scalar m, s;
            cv::meanStdDev(blur(p.y, 3), m, s);
            const double noise = std::max(1e-6, double(haarSigma(p.y)));
            c.push_back({r, s[0] / mean, s[0] / noise});
        }
    std::sort(c.begin(), c.end(), [](const Candidate &a, const Candidate &b) { return a.flatness < b.flatness; });
    for (size_t i = 0; i < c.size() && int(flat->size()) < FlatTiles; ++i) flat->push_back(c[i].r);
    std::sort(c.begin(), c.end(), [](const Candidate &a, const Candidate &b) { return a.texture > b.texture; });
    for (size_t i = 0; i < c.size() && int(detail->size()) < DetailTiles; ++i) detail->push_back(c[i].r);
}

double median(std::vector<double> v)
{
    if (v.empty()) return std::nan("");
    const auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

// 8-bit display of a tile: the same exposure for every variant, sRGB transfer.
QImage display(const cv::Mat &rgb, const cv::Rect &r, double exposure)
{
    QImage out(2 * r.width, 2 * r.height, QImage::Format_RGB888);
    for (int y = 0; y < r.height; ++y) {
        const auto *s = rgb.ptr<cv::Vec3f>(r.y + y) + r.x;
        for (int x = 0; x < r.width; ++x) {
            int v[3];
            for (int c = 0; c < 3; ++c) {
                const double l = std::clamp(s[x][c] * exposure, 0.0, 1.0);
                const double e = l <= 0.0031308 ? 12.92 * l : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
                v[c] = int(std::lround(255 * e));
            }
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) out.setPixel(2 * x + dx, 2 * y + dy, qRgb(v[0], v[1], v[2]));
        }
    }
    return out;
}

// Truth for the synthetic mosaic: the visible area binned 2x2, raw units with black.
cv::Mat binnedTruth(LibRaw &raw)
{
    const auto &s = raw.imgdata.sizes;
    const ushort *mosaic = raw.imgdata.rawdata.raw_image;
    require(mosaic && raw.imgdata.idata.filters >= 1000, "not a Bayer RAW");
    const int pitch = int(s.raw_pitch / sizeof(ushort));
    cv::Mat truth(s.height / 2, s.width / 2, CV_32FC3);
    for (int y = 0; y < truth.rows; ++y)
        for (int x = 0; x < truth.cols; ++x) {
            cv::Vec3f v(0, 0, 0);
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int r = 2 * y + dy, c = 2 * x + dx;
                    const int k = raw.COLOR(r, c);
                    const float value = mosaic[size_t(r + s.top_margin) * pitch + c + s.left_margin];
                    v[k == 3 ? 1 : k] += k == 0 || k == 2 ? value : 0.5f * value;
                }
            truth.at<cv::Vec3f>(y, x) = v;
        }
    return truth;
}

void remosaic(LibRaw &raw, const cv::Mat &truth)
{
    const auto &s = raw.imgdata.sizes;
    ushort *mosaic = raw.imgdata.rawdata.raw_image;
    const int pitch = int(s.raw_pitch / sizeof(ushort));
    for (int r = 0; r < truth.rows; ++r)
        for (int c = 0; c < truth.cols; ++c) {
            const int k = raw.COLOR(r, c);
            const float v = truth.at<cv::Vec3f>(r, c)[k == 3 ? 1 : k];
            mosaic[size_t(r + s.top_margin) * pitch + c + s.left_margin] = ushort(std::clamp(std::lround(v), 0L, 65535L));
        }
}

// Per-channel gain and offset mapping the truth to the processed output (black,
// white balance and scaling), fitted once on AHD and reused for every variant.
struct Fit { cv::Vec3d gain, offset; };

Fit fitTruth(const cv::Mat &truth, const cv::Mat &out, const cv::Mat &valid)
{
    Fit f;
    for (int c = 0; c < 3; ++c) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
        for (int y = 0; y < truth.rows; y += 3)
            for (int x = 0; x < truth.cols; x += 3) {
                const double t = truth.at<cv::Vec3f>(y, x)[c], o = out.at<cv::Vec3f>(y, x)[c];
                if (!valid.at<uchar>(y, x) || o <= 0.0) continue;
                sx += t; sy += o; sxx += t * t; sxy += t * o; n += 1;
            }
        require(n > 1000, "synthetic fit without samples");
        f.gain[c] = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        f.offset[c] = (sy - f.gain[c] * sx) / n;
    }
    return f;
}

// Errors on the valid pixels only (clipped channels excluded): every algorithm
// fails the same way where the white balance pushes a channel past white.
QJsonObject syntheticError(const cv::Mat &truthOut, const cv::Mat &out, const cv::Mat &edges, const cv::Mat &valid)
{
    const cv::Rect inner(TruthBorder, TruthBorder, truthOut.cols - 2 * TruthBorder, truthOut.rows - 2 * TruthBorder);
    const Planes t = split(truthOut(inner)), o = split(out(inner));
    const cv::Mat dy = o.y - t.y, dco = o.co - t.co, dcg = o.cg - t.cg;
    const cv::Mat diff = out(inner) - truthOut(inner);
    const cv::Mat ok = valid(inner), edge = edges(inner) & ok, flat = (edges(inner) == 0) & ok;
    const double white = cv::mean(t.y, ok)[0] * 4.0;   // scale: four times the mean luma
    const auto masked = [&](const cv::Mat &m, const cv::Mat &mask) {
        return std::sqrt(cv::mean(m.mul(m), mask)[0]) / white;
    };
    const cv::Scalar squared = cv::mean(diff.mul(diff), ok);
    QJsonObject r;
    r.insert("cpsnr_db", 20.0 * std::log10(white / std::sqrt((squared[0] + squared[1] + squared[2]) / 3.0)));
    r.insert("luma_rmse", masked(dy, ok));
    r.insert("chroma_rmse", std::hypot(masked(dco, ok), masked(dcg, ok)));
    r.insert("luma_edge_rmse", masked(dy, edge));
    r.insert("chroma_edge_rmse", std::hypot(masked(dco, edge), masked(dcg, edge)));
    r.insert("chroma_flat_rmse", std::hypot(masked(dco, flat), masked(dcg, flat)));
    return r;
}

QString sha256(const QString &path)
{
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "cannot read " + path.toStdString());
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QJsonObject study(const QString &path, const QDir &crops)
{
    StudyRaw raw;
    require(raw.open_file(QDir::toNativeSeparators(path).toStdWString().c_str()) == LIBRAW_SUCCESS, "cannot open " + path.toStdString());
    require(raw.unpack() == LIBRAW_SUCCESS, "cannot unpack " + path.toStdString());
    auto &p = raw.imgdata.params;
    p.output_color = 0;   // camera RGB: isolates the demosaic from the colour matrix
    p.use_camera_wb = 1;
    p.use_camera_matrix = 0;
    p.no_auto_bright = 1;
    p.output_bps = 16;
    p.gamm[0] = p.gamm[1] = 1.0;
    p.user_flip = 0;

    QJsonObject scene;
    scene.insert("raw", QFileInfo(path).fileName());
    scene.insert("raw_sha256", sha256(path));
    scene.insert("camera", QString::fromLocal8Bit(raw.imgdata.idata.model).trimmed());
    scene.insert("iso", int(std::lround(raw.imgdata.other.iso_speed)));
    scene.insert("width", raw.imgdata.sizes.width);
    scene.insert("height", raw.imgdata.sizes.height);
    const double megapixels = raw.imgdata.sizes.width * double(raw.imgdata.sizes.height) / 1e6;

    std::vector<cv::Rect> flat, detail;
    std::vector<TileMeasure> ahdFlat, ahdDetail;
    std::vector<QImage> montageTiles;
    double exposureFlat = 1, exposureDetail = 1;
    QJsonObject results;
    cv::Mat ahdFirst;
    for (const Variant &v : selected) {
        double ms = 0;
        const cv::Mat out = process(raw, v, &ms);
        const double ours = oursMs;
        if (flat.empty()) {
            ahdFirst = out;
            chooseTiles(out, &flat, &detail);
            require(!flat.empty() && !detail.empty(), "no measurable tile");
            exposureFlat = 0.25 / std::max(1e-4, cv::mean(split(out(flat[0])).y)[0]);
            exposureDetail = 0.25 / std::max(1e-4, cv::mean(split(out(detail[0])).y)[0]);
        }
        std::vector<double> lumaHh, chromaHh, lumaMid, chromaMid, lumaFine, chromaFine;
        std::vector<TileMeasure> flatM, detailM;
        for (const auto &r : flat) flatM.push_back(measureTile(out, r));
        for (const auto &r : detail) detailM.push_back(measureTile(out, r));
        if (ahdFlat.empty()) { ahdFlat = flatM; ahdDetail = detailM; }
        for (size_t i = 0; i < flat.size(); ++i) {
            lumaHh.push_back(flatM[i].lumaHh / ahdFlat[i].lumaHh);
            chromaHh.push_back(flatM[i].chromaHh / ahdFlat[i].chromaHh);
            lumaMid.push_back(flatM[i].lumaMid / ahdFlat[i].lumaMid);
            chromaMid.push_back(flatM[i].chromaMid / ahdFlat[i].chromaMid);
        }
        for (size_t i = 0; i < detail.size(); ++i) {
            lumaFine.push_back(detailM[i].lumaFine / ahdDetail[i].lumaFine);
            chromaFine.push_back(detailM[i].chromaFine / ahdDetail[i].chromaFine);
        }
        QJsonObject m;
        m.insert("ms_per_megapixel", ms / megapixels);
        if (v.ours != Variant::Ours::None) {
            m.insert("ours_ms_per_megapixel", ours / megapixels);
            // scaling: our interpolation again at each further thread count
            QJsonObject byThreads;
            byThreads.insert(QString::number(threadCounts.front()), ours / megapixels);
            for (size_t t = 1; t < threadCounts.size(); ++t) {
                cv::setNumThreads(threadCounts[t]);
                double unused = 0;
                process(raw, v, &unused);
                byThreads.insert(QString::number(threadCounts[t]), oursMs / megapixels);
            }
            cv::setNumThreads(threadCounts.front());
            m.insert("ours_ms_per_megapixel_by_threads", byThreads);
        }
        m.insert("flat_luma_hh_ratio", median(lumaHh));
        m.insert("flat_chroma_hh_ratio", median(chromaHh));
        m.insert("flat_luma_mid_ratio", median(lumaMid));
        m.insert("flat_chroma_mid_ratio", median(chromaMid));
        m.insert("detail_luma_fine_ratio", median(lumaFine));
        m.insert("detail_chroma_fine_ratio", median(chromaFine));
        results.insert(v.name, m);
        QImage pair(2 * CropSize, 4 * CropSize, QImage::Format_RGB888);
        {
            const cv::Rect fr(flat[0].x, flat[0].y, CropSize, CropSize), dr(detail[0].x, detail[0].y, CropSize, CropSize);
            const QImage a = display(out, fr, exposureFlat), b = display(out, dr, exposureDetail);
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x) {
                    pair.setPixel(x, y, a.pixel(x, y));
                    pair.setPixel(x, y + a.height(), b.pixel(x, y));
                }
        }
        montageTiles.push_back(pair);
    }
    // repeatability: the first AHD run and a last one must match
    {
        double ms = 0;
        const cv::Mat again = process(raw, selected.front(), &ms);
        scene.insert("repeatable", cv::norm(again, ahdFirst, cv::NORM_INF) == 0);
    }
    QJsonObject ahdAbsolute;
    std::vector<double> lh, ch, cm;
    for (const auto &t : ahdFlat) { lh.push_back(t.lumaHh); ch.push_back(t.chromaHh); cm.push_back(t.chromaMid); }
    ahdAbsolute.insert("flat_luma_hh", median(lh));
    ahdAbsolute.insert("flat_chroma_hh", median(ch));
    ahdAbsolute.insert("flat_chroma_mid", median(cm));
    scene.insert("ahd_absolute", ahdAbsolute);
    scene.insert("flat_tiles", int(flat.size()));
    scene.insert("detail_tiles", int(detail.size()));

    // Synthetic mosaic in the top-left quarter. The binned truth is blurred a little
    // (optical low-pass of a real sensor): unfiltered, every channel holds detail up
    // to Nyquist and all algorithms alias alike.
    cv::Mat truth = binnedTruth(raw);
    cv::GaussianBlur(truth, truth, cv::Size(0, 0), TruthBlur, TruthBlur, cv::BORDER_REFLECT);
    remosaic(raw, truth);
    QJsonObject synthetic;
    Fit fit;
    cv::Mat truthOut, edges, valid;
    for (const Variant &v : selected) {
        double ms = 0;
        const cv::Mat out = process(raw, v, &ms)(cv::Rect(0, 0, truth.cols, truth.rows)).clone();
        if (truthOut.empty()) {
            // valid: no channel near white within 3 pixels, on the reference output
            std::vector<cv::Mat> channels;
            cv::split(out, channels);
            cv::Mat high = cv::max(cv::max(channels[0], channels[1]), channels[2]) > 0.9f;
            cv::dilate(high, high, cv::Mat::ones(7, 7, CV_8U));
            valid = high == 0;
            fit = fitTruth(truth, out, valid);
            truthOut.create(truth.size(), CV_32FC3);
            for (int y = 0; y < truth.rows; ++y)
                for (int x = 0; x < truth.cols; ++x)
                    for (int c = 0; c < 3; ++c)
                        truthOut.at<cv::Vec3f>(y, x)[c] = float(fit.gain[c] * truth.at<cv::Vec3f>(y, x)[c] + fit.offset[c]);
            // edges: top 10 % of the truth luma gradient
            const Planes t = split(truthOut);
            cv::Mat gx, gy, mag;
            cv::Sobel(t.y, gx, CV_32F, 1, 0);
            cv::Sobel(t.y, gy, CV_32F, 0, 1);
            cv::magnitude(gx, gy, mag);
            std::vector<float> values(mag.begin<float>(), mag.end<float>());
            const auto q = values.begin() + values.size() * 9 / 10;
            std::nth_element(values.begin(), q, values.end());
            edges = mag > *q;
        }
        synthetic.insert(v.name, syntheticError(truthOut, out, edges, valid));
    }
    QJsonObject gains;
    gains.insert("gain", QJsonArray{fit.gain[0], fit.gain[1], fit.gain[2]});
    gains.insert("offset", QJsonArray{fit.offset[0], fit.offset[1], fit.offset[2]});
    synthetic.insert("fit", gains);
    synthetic.insert("truth_blur_sigma", TruthBlur);
    synthetic.insert("valid_fraction", cv::countNonZero(valid) / double(valid.total()));
    scene.insert("variants", results);
    scene.insert("synthetic", synthetic);

    QImage montage(int(montageTiles.size()) * 2 * CropSize, 4 * CropSize, QImage::Format_RGB888);
    montage.fill(Qt::black);
    for (size_t i = 0; i < montageTiles.size(); ++i)
        for (int y = 0; y < montageTiles[i].height(); ++y)
            for (int x = 0; x < montageTiles[i].width(); ++x)
                montage.setPixel(int(i) * 2 * CropSize + x, y, montageTiles[i].pixel(x, y));
    const QString crop = QFileInfo(path).completeBaseName() + "-demosaic.png";
    require(montage.save(crops.filePath(crop)), "cannot write crop");
    scene.insert("crop", crop);
    return scene;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = QCoreApplication::arguments();
    // options: --threads N[,M...] (default 8, the performance baseline), --variants A,B,...
    QStringList wanted;
    for (int i = 1; i + 1 < args.size();) {
        if (args[i] == "--threads") {
            threadCounts.clear();
            for (const QString &t : args[i + 1].split(',')) threadCounts.push_back(t.toInt());
            args.remove(i, 2);
        }
        else if (args[i] == "--variants") { wanted = args[i + 1].split(','); args.remove(i, 2); }
        else ++i;
    }
    if (args.size() < 4 || threadCounts.empty() ||
        std::any_of(threadCounts.begin(), threadCounts.end(), [](int t) { return t < 1; })) {
        std::cerr << "usage: demosaicProbe [--threads N[,M...]] [--variants A,B] OUTPUT.json CROPS_DIR RAW...\n";
        return 2;
    }
    cv::setNumThreads(threadCounts.front());
    selected.push_back(variants().front());
    for (const auto &v : variants())
        if (&v != &variants().front() && (wanted.isEmpty() || wanted.contains(v.name))) selected.push_back(v);
    if (!wanted.isEmpty() && int(selected.size()) != 1 + int(wanted.size() - wanted.count(variants().front().name))) {
        std::cerr << "unknown variant in --variants\n";
        return 2;
    }
    try {
        QDir crops(args[2]);
        require(crops.mkpath("."), "cannot create crops directory");
        QJsonArray scenes;
        for (int i = 3; i < args.size(); ++i) {
            std::cerr << args[i].toStdString() << "\n";
            scenes.append(study(args[i], crops));
        }
        QJsonArray order;
        for (const auto &v : selected) order.append(v.name);
        QJsonObject record;
        record.insert("probe", "demosaicProbe");
        record.insert("libraw_version", QString::fromLatin1(LibRaw::version()));
        record.insert("libraw_openmp", false);
        record.insert("opencv_threads", cv::getNumThreads());
        record.insert("machine", QSysInfo::prettyProductName());
        record.insert("generated_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        record.insert("variant_order", order);
        record.insert("crop_layout", "per variant, 2x: flat tile on top, detail tile below; camera RGB, no matrix");
        record.insert("scenes", scenes);
        QFile out(args[1]);
        require(out.open(QIODevice::WriteOnly), "cannot write output");
        out.write(QJsonDocument(record).toJson(QJsonDocument::Indented));
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
