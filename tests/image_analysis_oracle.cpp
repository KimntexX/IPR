// O3: the analysis of the image layer against OpenCV as the oracle: template matching, phase
// correlation, Hann window, eigenvectors, Cholesky, nearest rotation. Tolerances fixed here, before
// any measure; IPP off. Writes one JSON proof.
#include "Analysis.h"
#include "OpenCvBridge.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ipr;

namespace {

cv::Mat texture(int width, int height, int seed, double sigma = 1.5)
{
    cv::Mat mat(height, width, CV_32F);
    cv::RNG random{static_cast<uint64>(seed)};
    random.fill(mat, cv::RNG::UNIFORM, 0.0f, 1.0f);
    cv::Mat smooth;
    cv::GaussianBlur(mat, smooth, cv::Size(0, 0), sigma);
    return smooth;
}

template <typename Call>
double milliseconds(const Call &call)
{
    const auto start = std::chrono::steady_clock::now();
    call();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double largestGap(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type())
        return 1e9;
    cv::Mat x, y;
    a.convertTo(x, CV_64F);
    b.convertTo(y, CV_64F);
    const cv::Mat apart = cv::abs(x - y);
    double gap = 0.0;
    cv::minMaxLoc(apart.reshape(1), nullptr, &gap);
    return gap;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: iprImageAnalysisOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    cv::ipp::setUseIPP(false);
    cv::setNumThreads(8);
    std::ostringstream report;
    bool within = true, first = true;
    const auto record = [&](const std::string &name, double difference, double tolerance, double openCvMs, double myMs) {
        within = within && difference <= tolerance;
        std::cout << name << ": largest difference " << difference << " (tolerance " << tolerance << "), opencv " << openCvMs
                  << " ms, ipr " << myMs << " ms" << std::endl;
        report << (first ? "" : ",\n") << "  {\"case\": \"" << name << "\", \"largest_difference\": " << difference
               << ", \"tolerance\": " << tolerance << ", \"opencv_ms\": " << openCvMs << ", \"ipr_ms\": " << myMs << "}";
        first = false;
    };

    // Template matching: scores from -1 to 1. OpenCV correlates in floats through a DFT; a score
    // apart by 1e-4 does not move a peak.
    for (const auto &[side, search] : std::vector<std::pair<int, int>>{{33, 65}, {65, 97}, {25, 41}, {9, 15}}) {
        const cv::Mat image = texture(search, search, side), block = image(cv::Rect((search - side) / 2 + 2, (search - side) / 2 - 1, side, side)).clone();
        cv::Mat theirs, mine(search - side + 1, search - side + 1, CV_32F);
        const double openCvMs = milliseconds([&] { cv::matchTemplate(image, block, theirs, cv::TM_CCOEFF_NORMED); });
        Image target = fromMutableMat(mine);
        const double myMs = milliseconds([&] { matchTemplateInto(fromMat(image), fromMat(block), target); });
        record("match template " + std::to_string(side) + " in " + std::to_string(search), largestGap(mine, theirs), 1e-4, openCvMs, myMs);
        cv::Point theirPeak, myPeak;
        cv::minMaxLoc(theirs, nullptr, nullptr, nullptr, &theirPeak);
        cv::minMaxLoc(mine, nullptr, nullptr, nullptr, &myPeak);
        record("its peak " + std::to_string(side), theirPeak == myPeak ? 0.0 : 1.0, 0.0, 0, 0);
    }

    // Hann window.
    for (const cv::Size size : {cv::Size(2, 2), cv::Size(7, 5), cv::Size(384, 256)}) {
        cv::Mat theirs, mine(size, CV_32F);
        cv::createHanningWindow(theirs, size, CV_32F);
        Image target = fromMutableMat(mine);
        hannWindowInto(target);
        record("hann " + std::to_string(size.width) + "x" + std::to_string(size.height), largestGap(mine, theirs), 0.0, 0, 0);
    }

    // Phase correlation of a texture against itself moved by a known shift, on sizes with odd factors.
    // OpenCV transforms floats in floats: on a smooth texture the high frequencies are below their
    // precision, and its result moves by tenths of a pixel from its own computation in doubles. The
    // 1e-3 first fixed against its floats could not hold (0.01 to 0.04 measured on the smooth
    // texture, 2026-11-05); hence two measures: against OpenCV in doubles on a sharp texture, whose
    // frequencies all stand above that noise, and against OpenCV in floats beyond its own gap
    // between floats and doubles.
    const auto apart = [](const Point<double> &p, const cv::Point2d &q) { return std::max(std::abs(p.x - q.x), std::abs(p.y - q.y)); };
    for (const double sigma : {0.7, 1.5})
        for (const auto &[size, shift] : std::vector<std::pair<cv::Size, cv::Point2d>>{
                 {cv::Size(384, 256), {7.3, -4.6}}, {cv::Size(375, 250), {-12.25, 3.5}}, {cv::Size(243, 160), {0.4, 0.0}}}) {
            const cv::Mat a = texture(size.width + 40, size.height + 40, 3, sigma);
            cv::Mat moved;
            const cv::Matx23d m(1, 0, shift.x, 0, 1, shift.y);
            cv::warpAffine(a, moved, m, a.size(), cv::INTER_CUBIC, cv::BORDER_REFLECT);
            const cv::Rect inner(20, 20, size.width, size.height);
            const cv::Mat first = a(inner).clone(), second = moved(inner).clone();
            cv::Mat window;
            cv::createHanningWindow(window, size, CV_32F);
            cv::Point2d theirs;
            const double openCvMs = milliseconds([&] { theirs = cv::phaseCorrelate(first, second, window); });
            Point<double> mine;
            const double myMs = milliseconds([&] { mine = phaseCorrelation(fromMat(first), fromMat(second), fromMat(window)); });
            cv::Mat first64, second64, window64;
            first.convertTo(first64, CV_64F);
            second.convertTo(second64, CV_64F);
            window.convertTo(window64, CV_64F);
            const cv::Point2d doubles = cv::phaseCorrelate(first64, second64, window64);
            const std::string name = "phase correlation " + std::to_string(size.width) + "x" + std::to_string(size.height) +
                                     (sigma < 1.0 ? ", sharp" : ", smooth");
            const double ownGap = std::max(std::abs(doubles.x - theirs.x), std::abs(doubles.y - theirs.y));
            if (sigma < 1.0)
                record(name + ", against opencv in doubles", apart(mine, doubles), 1e-3, openCvMs, myMs);
            record(name + ", against opencv in floats beyond its own float/double gap of " + std::to_string(ownGap),
                   std::max(0.0, apart(mine, theirs) - ownGap), 1e-3, openCvMs, myMs);
        }

    // Small linear algebra.
    cv::RNG random(17);
    for (const int n : {3, 9}) {
        cv::Mat a(n, n, CV_64F);
        random.fill(a, cv::RNG::UNIFORM, -1.0, 1.0);
        const cv::Mat symmetric = a.t() * a + cv::Mat::eye(n, n, CV_64F) * 0.1;
        cv::Mat values, vectors;
        cv::eigen(symmetric, values, vectors);
        std::vector<double> flat(symmetric.begin<double>(), symmetric.end<double>()), myValues, myVectors;
        symmetricEigen(flat, n, myValues, myVectors);
        double valueGap = 0.0, vectorGap = 0.0;
        for (int i = 0; i < n; ++i) {
            valueGap = std::max(valueGap, std::abs(values.at<double>(i) - myValues[size_t(i)]));
            double dot = 0.0;
            for (int k = 0; k < n; ++k)
                dot += vectors.at<double>(i, k) * myVectors[size_t(i * n + k)];
            vectorGap = std::max(vectorGap, 1.0 - std::abs(dot));
        }
        record("eigenvalues " + std::to_string(n), valueGap, 1e-9, 0, 0);
        record("eigenvectors " + std::to_string(n) + ", up to sign", vectorGap, 1e-9, 0, 0);
        cv::Mat b(n, 1, CV_64F), x;
        random.fill(b, cv::RNG::UNIFORM, -1.0, 1.0);
        cv::solve(symmetric, b, x, cv::DECOMP_CHOLESKY);
        std::vector<double> myX;
        solveCholesky(flat, std::vector<double>(b.begin<double>(), b.end<double>()), n, myX);
        double solveGap = 0.0;
        for (int i = 0; i < n; ++i)
            solveGap = std::max(solveGap, std::abs(x.at<double>(i) - myX[size_t(i)]));
        record("cholesky " + std::to_string(n), solveGap, 1e-9, 0, 0);
    }
    // Singular values and the vector of the smallest, as the homography fit reads them.
    for (const int rows : {8, 18, 60, 400}) {
        cv::Mat system(rows, 9, CV_64F);
        random.fill(system, cv::RNG::UNIFORM, -1.0, 1.0);
        cv::Mat w, u, vt;
        const double openCvMs = milliseconds([&] { cv::SVD::compute(system, w, u, vt, cv::SVD::FULL_UV); });
        cv::Mat myW, myVt;
        const double myMs = milliseconds([&] { singularValues(system, myW, myVt); });
        double valueGap = 0.0;
        for (int i = 0; i < w.rows; ++i)
            valueGap = std::max(valueGap, std::abs(w.at<double>(i) - myW.at<double>(i)));
        double dot = 0.0;
        for (int k = 0; k < 9; ++k)
            dot += vt.at<double>(8, k) * myVt.at<double>(8, k);
        record("singular values " + std::to_string(rows) + " x 9", valueGap, 1e-9, openCvMs, myMs);
        record("null vector " + std::to_string(rows) + " x 9, up to sign", 1.0 - std::abs(dot), 1e-9, 0, 0);
    }
    for (int trial = 0; trial < 5; ++trial) {
        cv::Matx33d m;
        for (int i = 0; i < 9; ++i)
            m.val[i] = random.uniform(-1.0, 1.0);
        cv::Matx33d u, vt;
        cv::Matx31d w;
        cv::SVD::compute(m, w, u, vt);
        cv::Matx33d r = u * vt;
        if (cv::determinant(r) < 0.0) {
            for (int i = 0; i < 3; ++i)
                u(i, 2) = -u(i, 2);
            r = u * vt;
        }
        const Matrix33d mine = nearestRotation(fromCv(m));
        double gap = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                gap = std::max(gap, std::abs(r(i, j) - mine(size_t(i), size_t(j))));
        record("nearest rotation " + std::to_string(trial), gap, 1e-9, 0, 0);
        record("determinant " + std::to_string(trial), std::abs(cv::determinant(m) - determinant(fromCv(m))), 1e-12, 0, 0);
    }

    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"analysis\",\n \"revision\": \"" << argv[1] << "\",\n \"opencv\": \"" << CV_VERSION
        << "\",\n \"oracle\": \"OpenCV's own code, IPP off\",\n \"within_tolerance\": " << (within ? "true" : "false")
        << ",\n \"timing\": \"one run, on a machine busy with other work\",\n \"cases\": [\n" << report.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
