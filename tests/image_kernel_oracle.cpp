// O3: a kernel of the image layer against OpenCV as the oracle: the largest difference and the time,
// on sizes up to a whole photo. The tolerance is fixed here, before any measure. Writes one JSON proof.
#include "Filters.h"
#include "OpenCvBridge.h"
#include "Parallel.h"

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
    // Texture, plus broad edges: noise alone would hide what a large sigma does.
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
        std::cerr << "usage: iprImageKernelOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    const int threads = 8;
    cv::setNumThreads(threads);
    std::ostringstream cases;
    bool within = true;
    bool first = true;
    struct Case { int width, height, channels; double sigma; };
    const std::vector<Case> list = {
        {1, 1, 3, 2.0}, {3, 2, 1, 5.0}, {17, 5, 3, 3.0}, {64, 64, 1, 0.5}, {64, 64, 4, 8.0}, {640, 480, 3, 1.0}, {640, 480, 3, 40.0},
        {2048, 1365, 3, 1.0}, {2048, 1365, 3, 3.0}, {2048, 1365, 3, 10.0}, {2048, 1365, 3, 30.0}, {2048, 1365, 4, 3.0},
        {6960, 4640, 3, 1.0}, {6960, 4640, 3, 3.0}, {6960, 4640, 3, 10.0}, {6960, 4640, 3, 30.0},
    };
    for (const Case &c : list) {
        const cv::Mat input = scene(c.width, c.height, c.channels);
        const Image image = fromMat(input);
        cv::Mat oracle;
        const double openCv = medianOfThree([&] { cv::GaussianBlur(input, oracle, cv::Size(0, 0), c.sigma, c.sigma, cv::BORDER_REFLECT_101); });
        Image ours;
        const double mine = medianOfThree([&] { ours = gaussianBlur(image, c.sigma, threads); });
        const double one = c.width >= 640 ? milliseconds([&] { (void)gaussianBlur(image, c.sigma, 1); }) : 0.0;
        double difference = 0;
        const cv::Mat gap = cv::abs(asMat(ours) - oracle);
        cv::minMaxLoc(gap.reshape(1), nullptr, &difference);
        within = within && difference <= tolerance;
        char line[256];
        std::snprintf(line, sizeof(line), "%dx%d c%d sigma %.1f: opencv %.1f ms, ipr %.1f ms (x%.2f), ipr alone %.1f ms, largest difference %.2e",
                      c.width, c.height, c.channels, c.sigma, openCv, mine, mine / std::max(openCv, 1e-3), one, difference);
        std::cout << line << std::endl;
        cases << (first ? "" : ",\n") << "  {\"width\": " << c.width << ", \"height\": " << c.height << ", \"channels\": " << c.channels
              << ", \"sigma\": " << c.sigma << ", \"opencv_ms\": " << openCv << ", \"ipr_ms\": " << mine << ", \"ipr_one_thread_ms\": " << one
              << ", \"largest_difference\": " << difference << "}";
        first = false;
    }
    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"gaussianBlur\",\n \"revision\": \"" << argv[1] << "\",\n \"threads\": " << threads
        << ",\n \"opencv\": \"" << CV_VERSION << "\",\n \"tolerance_fixed_before_measuring\": " << tolerance
        << ",\n \"within_tolerance\": " << (within ? "true" : "false") << ",\n \"timing\": \"median of three, on a machine busy with other calculations\""
        << ",\n \"cases\": [\n" << cases.str() << "\n ]\n}\n";
    std::cout << (within ? "within tolerance\n" : "BEYOND TOLERANCE\n");
    return within ? 0 : 1;
}
