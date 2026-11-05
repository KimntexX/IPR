// O3: resizing of the image layer against OpenCV as the oracle: the largest difference and the
// time, on the sizes IPR uses. The tolerance is fixed here, before any measure. The oracle is
// OpenCV's own code: Intel IPP, which OpenCV may call instead, departs from it, and that gap is
// reported apart. Writes one JSON proof.
#include "OpenCvBridge.h"
#include "Resize.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
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

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: iprImageResizeOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    const int threads = 8;
    cv::setNumThreads(threads);
    struct Case { int width, height, channels, toWidth, toHeight; Interpolation interpolation; double scale = 0.0; };
    const Interpolation linear = Interpolation::Linear, area = Interpolation::Area;
    const std::vector<Case> list = {
        // Small and odd sizes, both ways, both modes.
        {1, 1, 3, 5, 3, linear}, {3, 2, 1, 1, 1, area}, {17, 5, 3, 6, 2, area}, {17, 5, 3, 40, 13, linear},
        {17, 5, 4, 40, 13, area}, {64, 64, 1, 23, 91, area}, {64, 64, 2, 23, 91, linear},
        // The preview proxy, the reductions of dehaze and local contrast, and back up.
        {6960, 4640, 4, 2048, 1365, area}, {6959, 4639, 3, 2048, 1365, area}, {6960, 4640, 1, 3480, 2320, area},
        {6959, 4639, 1, 1739, 1159, area}, {1739, 1159, 1, 6959, 4639, linear}, {580, 386, 3, 6960, 4640, linear},
        {2048, 1365, 2, 6960, 4640, linear},
        // Given as scales, as the panorama and the stack ask.
        {5202, 3464, 3, 0, 0, area, 0.25}, {5202, 3464, 3, 0, 0, area, 0.3333}, {1301, 866, 3, 0, 0, linear, 1.7},
    };
    std::ostringstream cases;
    bool within = true;
    bool first = true;
    for (const Case &c : list) {
        const cv::Mat input = scene(c.width, c.height, c.channels);
        const Image image = fromMat(input);
        const int flag = c.interpolation == area ? cv::INTER_AREA : cv::INTER_LINEAR;
        cv::Mat oracle, withIpp;
        Image ours;
        double openCv = 0.0, mine = 0.0;
        const auto openCvResize = [&](cv::Mat &out) {
            if (c.scale > 0.0)
                cv::resize(input, out, cv::Size(), c.scale, c.scale, flag);
            else
                cv::resize(input, out, cv::Size(c.toWidth, c.toHeight), 0, 0, flag);
        };
        // Timed as IPR ran it, IPP allowed; compared with OpenCV's own code.
        cv::ipp::setUseIPP(true);
        openCv = medianOfThree([&] { openCvResize(withIpp); });
        cv::ipp::setUseIPP(false);
        openCvResize(oracle);
        cv::ipp::setUseIPP(true);
        if (c.scale > 0.0)
            mine = medianOfThree([&] { ours = resizedBy(image, c.scale, c.scale, c.interpolation, threads); });
        else
            mine = medianOfThree([&] { ours = resized(image, {c.toWidth, c.toHeight}, c.interpolation, threads); });
        double ippGap = 0.0;
        if (withIpp.size() == oracle.size()) {
            const cv::Mat apart = cv::abs(withIpp - oracle);
            cv::minMaxLoc(apart.reshape(1), nullptr, &ippGap);
        }
        double difference = 1e9;
        const bool sameSize = ours.width() == oracle.cols && ours.height() == oracle.rows;
        if (sameSize) {
            const cv::Mat gap = cv::abs(asMat(ours) - oracle);
            cv::minMaxLoc(gap.reshape(1), nullptr, &difference);
        }
        within = within && sameSize && difference <= tolerance;
        char line[256];
        std::snprintf(line, sizeof(line), "%dx%d c%d -> %dx%d %s: opencv %.1f ms, ipr %.1f ms (x%.2f), largest difference %.3g, IPP apart %.3g%s",
                      c.width, c.height, c.channels, oracle.cols, oracle.rows, c.interpolation == area ? "area" : "linear",
                      openCv, mine, mine / std::max(openCv, 1e-3), difference, ippGap, sameSize ? "" : " SIZE DIFFERS");
        std::cout << line << std::endl;
        cases << (first ? "" : ",\n") << "  {\"from\": [" << c.width << ", " << c.height << "], \"channels\": " << c.channels
              << ", \"to\": [" << oracle.cols << ", " << oracle.rows << "], \"interpolation\": \""
              << (c.interpolation == area ? "area" : "linear") << "\", \"scale_given\": " << c.scale
              << ", \"opencv_ms\": " << openCv << ", \"ipr_ms\": " << mine << ", \"same_size\": " << (sameSize ? "true" : "false")
              << ", \"largest_difference\": " << difference << ", \"ipp_from_opencv\": " << ippGap << "}";
        first = false;
    }
    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"resize\",\n \"revision\": \"" << argv[1] << "\",\n \"threads\": " << threads
        << ",\n \"opencv\": \"" << CV_VERSION << "\",\n \"oracle\": \"OpenCV's own code, IPP off; ipp_from_opencv is how far IPP, which IPR used, lies from it\",\n \"tolerance_fixed_before_measuring\": " << tolerance
        << ",\n \"within_tolerance\": " << (within ? "true" : "false")
        << ",\n \"timing\": \"median of three, on a machine busy with other work\",\n \"cases\": [\n" << cases.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
