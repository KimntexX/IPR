// O3: remap and warpPerspective of the image layer against OpenCV as the oracle, and the pixels
// moved by Arrange.h: the largest difference and the time. The tolerance is fixed here, before any measure. The oracle is OpenCV's
// own code, IPP off; how far IPP lies from it is reported apart. Writes one JSON proof.
#include "OpenCvBridge.h"
#include "Arrange.h"
#include "Warp.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ipr;

namespace {

// Below one step of 16 bits on a signal of white 1 (1 / 65535 is 1.53e-5).
constexpr double tolerance = 1e-5;

cv::Mat scene(int width, int height, int channels)
{
    cv::Mat mat(height, width, CV_32FC(channels));
    cv::RNG random(5);
    random.fill(mat, cv::RNG::UNIFORM, 0.0f, 1.0f);
    cv::Mat broad(height / 8 + 1, width / 8 + 1, CV_32FC(channels));
    random.fill(broad, cv::RNG::UNIFORM, 0.0f, 1.0f);
    cv::resize(broad, broad, mat.size(), 0, 0, cv::INTER_NEAREST);
    return 0.2 * mat + 0.8 * broad;
}

template <typename Call>
double milliseconds(const Call &call)
{
    const auto start = std::chrono::steady_clock::now();
    call();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

template <typename Call>
double medianOfThree(const Call &call)
{
    std::vector<double> times{milliseconds(call), milliseconds(call), milliseconds(call)};
    std::sort(times.begin(), times.end());
    return times[1];
}

double largestGap(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type())
        return 1e9;
    double gap = 0.0;
    const cv::Mat apart = cv::abs(a - b);
    cv::minMaxLoc(apart.reshape(1), nullptr, &gap);
    return gap;
}

// A rotation of a few degrees with some perspective, sending the output into and partly beyond the image.
cv::Matx33d homography(int width, int height, double degrees, double tilt)
{
    const double a = degrees * 3.14159265358979323846 / 180.0;
    const cv::Matx33d centre(1, 0, -width / 2.0, 0, 1, -height / 2.0, 0, 0, 1);
    const cv::Matx33d back(1, 0, width / 2.0, 0, 1, height / 2.0, 0, 0, 1);
    const cv::Matx33d turn(std::cos(a) * 1.07, -std::sin(a), 3.3, std::sin(a), std::cos(a) * 1.07, -2.1, tilt / width, 0.3 * tilt / height, 1);
    return back * turn * centre;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: iprImageWarpOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    const int threads = 8;
    cv::setNumThreads(threads);
    struct Case { std::string kind; int width, height, channels; Resampling resampling; Outside outside; double degrees, tilt; };
    const Resampling linear = Resampling::Linear, cubic = Resampling::Cubic, lanczos = Resampling::Lanczos4;
    const Outside last = Outside::LastPixel, zero = Outside::Zero;
    const std::vector<Case> list = {
        {"remap", 7, 5, 3, linear, last, 3, 0}, {"remap", 7, 5, 3, cubic, zero, 3, 0}, {"remap", 31, 17, 1, lanczos, last, -5, 0},
        {"remap", 640, 480, 3, linear, last, 2, 0}, {"remap", 640, 480, 3, cubic, last, 2, 0}, {"remap", 640, 480, 4, lanczos, zero, -3, 0},
        {"remap", 2048, 1365, 3, lanczos, last, 1.5, 0}, {"remap", 5202, 3464, 3, lanczos, last, 0.7, 0},
        {"perspective", 9, 6, 3, cubic, zero, 4, 0.2}, {"perspective", 640, 480, 3, linear, last, 3, 0.15},
        {"perspective", 640, 480, 4, cubic, zero, -6, 0.3}, {"perspective", 2048, 1365, 3, cubic, last, 2, 0.1},
        {"perspective", 2048, 1365, 1, cubic, zero, 2, 0.1}, {"perspective", 6960, 4640, 3, lanczos, last, 1.2, 0.08},
        {"perspective", 5202, 3464, 3, lanczos, last, -0.4, 0.02},
    };
    std::ostringstream cases;
    bool within = true;
    bool first = true;
    for (const Case &c : list) {
        const cv::Mat input = scene(c.width, c.height, c.channels);
        const Image image = fromMat(input);
        const int flag = c.resampling == linear ? cv::INTER_LINEAR : c.resampling == cubic ? cv::INTER_CUBIC : cv::INTER_LANCZOS4;
        const int border = c.outside == last ? cv::BORDER_REPLICATE : cv::BORDER_CONSTANT;
        const cv::Matx33d m = homography(c.width, c.height, c.degrees, c.tilt);
        cv::Mat mapX(c.height, c.width, CV_32FC1), mapY(c.height, c.width, CV_32FC1);
        if (c.kind == "remap")
            for (int y = 0; y < c.height; ++y)
                for (int x = 0; x < c.width; ++x) {
                    const cv::Vec3d p = m * cv::Vec3d(x, y, 1);
                    mapX.at<float>(y, x) = float(p[0] / p[2] + 0.37 * std::sin(y * 0.05));
                    mapY.at<float>(y, x) = float(p[1] / p[2] + 0.29 * std::cos(x * 0.07));
                }
        cv::Mat oracle, withIpp;
        const auto openCv = [&](cv::Mat &out) {
            if (c.kind == "remap")
                cv::remap(input, out, mapX, mapY, flag, border, cv::Scalar::all(0));
            else
                cv::warpPerspective(input, out, cv::Mat(m), input.size(), flag | cv::WARP_INVERSE_MAP, border, cv::Scalar::all(0));
        };
        cv::ipp::setUseIPP(true);
        const double openCvMs = medianOfThree([&] { openCv(withIpp); });
        cv::ipp::setUseIPP(false);
        openCv(oracle);
        cv::ipp::setUseIPP(true);
        cv::Mat mine(input.size(), input.type());
        Image target = fromMutableMat(mine);
        const double mineMs = medianOfThree([&] {
            if (c.kind == "remap")
                remapInto(image, target, fromMat(mapX), fromMat(mapY), c.resampling, c.outside, threads);
            else
                warpPerspectiveInto(image, target, fromCv(m), c.resampling, c.outside, threads);
        });
        const double difference = largestGap(mine, oracle), ippGap = largestGap(withIpp, oracle);
        within = within && difference <= tolerance;
        const char *names[] = {"linear", "cubic", "lanczos4"};
        const char *resampling = names[c.resampling == linear ? 0 : c.resampling == cubic ? 1 : 2];
        char line[256];
        std::snprintf(line, sizeof(line), "%s %dx%d c%d %s %s: opencv %.1f ms, ipr %.1f ms (x%.2f), largest difference %.3g, IPP apart %.3g",
                      c.kind.c_str(), c.width, c.height, c.channels, resampling, c.outside == last ? "last pixel" : "zero",
                      openCvMs, mineMs, mineMs / std::max(openCvMs, 1e-3), difference, ippGap);
        std::cout << line << std::endl;
        cases << (first ? "" : ",\n") << "  {\"kind\": \"" << c.kind << "\", \"size\": [" << c.width << ", " << c.height
              << "], \"channels\": " << c.channels << ", \"resampling\": \"" << resampling << "\", \"outside\": \""
              << (c.outside == last ? "last pixel" : "zero") << "\", \"opencv_ms\": " << openCvMs << ", \"ipr_ms\": " << mineMs
              << ", \"largest_difference\": " << difference << ", \"ipp_from_opencv\": " << ippGap << "}";
        first = false;
    }
    // Pixels moved, and the pyramid: these must agree to the bit, or within the tolerance for the pyramid.
    std::ostringstream arranged;
    bool firstArranged = true;
    for (const cv::Size size : {cv::Size(1, 1), cv::Size(7, 4), cv::Size(640, 481), cv::Size(6960, 4640)})
        for (const int channels : {1, 3}) {
            const cv::Mat input = scene(size.width, size.height, channels);
            const auto compare = [&](const std::string &what, const cv::Mat &theirs, const cv::Mat &mine, double ms, double myMs) {
                const double difference = largestGap(mine, theirs);
                within = within && difference <= (what == "pyramid" ? tolerance : 0.0);
                arranged << (firstArranged ? "" : ",\n") << "  {\"operation\": \"" << what << "\", \"size\": [" << size.width
                         << ", " << size.height << "], \"channels\": " << channels << ", \"opencv_ms\": " << ms
                         << ", \"ipr_ms\": " << myMs << ", \"largest_difference\": " << difference << "}";
                firstArranged = false;
                std::cout << what << " " << size.width << "x" << size.height << " c" << channels << ": opencv " << ms << " ms, ipr "
                          << myMs << " ms, largest difference " << difference << std::endl;
            };
            cv::Mat theirs, mine;
            cv::ipp::setUseIPP(false);
            for (const int turns : {1, 2, 3}) {
                const int code = turns == 1 ? cv::ROTATE_90_CLOCKWISE : turns == 2 ? cv::ROTATE_180 : cv::ROTATE_90_COUNTERCLOCKWISE;
                const double ms = medianOfThree([&] { cv::rotate(input, theirs, code); });
                const double myMs = medianOfThree([&] { mine = turned(input, turns); });
                compare(turns == 1 ? "quarter turn" : turns == 2 ? "half turn" : "three quarters", theirs, mine, ms, myMs);
            }
            double ms = medianOfThree([&] { cv::flip(input, theirs, -1); });
            double myMs = medianOfThree([&] { mine = mirrored(input, true, true); });
            compare("both mirrors", theirs, mine, ms, myMs);
            ms = medianOfThree([&] { cv::transpose(input, theirs); });
            myMs = medianOfThree([&] { mine = transposed(input); });
            compare("transpose", theirs, mine, ms, myMs);
            if (size.width > 4 && size.height > 3) {
                ms = medianOfThree([&] { cv::copyMakeBorder(input, theirs, 3, 2, 4, 1, cv::BORDER_REFLECT_101); });
                myMs = medianOfThree([&] { mine = padded(input, 3, 2, 4, 1); });
                compare("pad", theirs, mine, ms, myMs);
            }
            if (size.width > 20 && size.height > 20) {
                const cv::Rect region(size.width / 3, 0, size.width / 3, size.height / 2);
                ms = medianOfThree([&] { cv::copyMakeBorder(input(region), theirs, 6, 6, 6, 6, cv::BORDER_REFLECT_101); });
                myMs = medianOfThree([&] { mine = paddedRegion(input, fromCv(region), 6, 6, 6, 6); });
                compare("pad region", theirs, mine, ms, myMs);
            }
            ms = medianOfThree([&] { cv::pyrDown(input, theirs); });
            myMs = medianOfThree([&] { mine = pyramidDown(input); });
            compare("pyramid", theirs, mine, ms, myMs);
            cv::ipp::setUseIPP(true);
        }
    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"remap and warpPerspective\",\n \"revision\": \"" << argv[1] << "\",\n \"threads\": " << threads
        << ",\n \"opencv\": \"" << CV_VERSION << "\",\n \"oracle\": \"OpenCV's own code, IPP off; ipp_from_opencv is how far IPP lies from it\""
        << ",\n \"tolerance_fixed_before_measuring\": " << tolerance << ",\n \"within_tolerance\": " << (within ? "true" : "false")
        << ",\n \"timing\": \"median of three, on a machine busy with other work\",\n \"cases\": [\n" << cases.str() << "\n ],\n \"arranged\": [\n" << arranged.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
