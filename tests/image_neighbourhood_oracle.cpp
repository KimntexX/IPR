// O3: the small filters of the image layer against OpenCV as the oracle: the largest difference and
// the time. The tolerance is fixed here, before any measure. The oracle is OpenCV's own code, IPP
// off; how far IPP lies from it is reported apart. Writes one JSON proof.
#include "Neighbourhood.h"
#include "OpenCvBridge.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ipr;

namespace {

// Below one step of 16 bits on a signal of white 1 (1 / 65535 is 1.53e-5).
constexpr double tolerance = 1e-5;

cv::Mat scene(int width, int height, int channels, int depth = CV_32F)
{
    cv::Mat mat(height, width, CV_32FC(channels));
    cv::RNG random(5);
    random.fill(mat, cv::RNG::UNIFORM, 0.0f, 1.0f);
    cv::Mat broad(height / 8 + 1, width / 8 + 1, CV_32FC(channels));
    random.fill(broad, cv::RNG::UNIFORM, 0.0f, 1.0f);
    cv::resize(broad, broad, mat.size(), 0, 0, cv::INTER_NEAREST);
    cv::Mat out = 0.2 * mat + 0.8 * broad;
    if (depth == CV_8U) {
        cv::Mat bytes;
        out.convertTo(bytes, CV_8U, 255.0);
        cv::threshold(bytes, bytes, 200, 255, cv::THRESH_BINARY);
        return bytes;
    }
    return out;
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
        std::cerr << "usage: iprImageNeighbourhoodOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    const int threads = 8;
    cv::setNumThreads(threads);
    struct Case
    {
        std::string name;
        int depth;
        std::function<void(const cv::Mat &, cv::Mat &)> openCv;
        std::function<void(const cv::Mat &, cv::Mat &)> mine;
    };
    const auto into = [](const std::function<void(const Image &, Image &)> &call) {
        return [call](const cv::Mat &in, cv::Mat &out) {
            out.create(in.size(), in.type());
            Image target = fromMutableMat(out);
            call(fromMat(in), target);
        };
    };
    const auto kernel = [](int side) { return cv::getStructuringElement(cv::MORPH_RECT, cv::Size(side, side)); };
    const std::vector<Case> cases = {
        {"sobel x", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::Sobel(i, o, CV_32F, 1, 0); },
         into([](const Image &i, Image &o) { sobelInto(i, o, true); })},
        {"sobel y", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::Sobel(i, o, CV_32F, 0, 1); },
         into([](const Image &i, Image &o) { sobelInto(i, o, false); })},
        {"sobel x / 8", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::Sobel(i, o, CV_32F, 1, 0, 3, 1.0 / 8.0); },
         into([](const Image &i, Image &o) { sobelInto(i, o, true, 1.0 / 8.0); })},
        {"sobel y / 8", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::Sobel(i, o, CV_32F, 0, 1, 3, 1.0 / 8.0); },
         into([](const Image &i, Image &o) { sobelInto(i, o, false, 1.0 / 8.0); })},
        {"laplacian", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::Laplacian(i, o, CV_32F); },
         into([](const Image &i, Image &o) { laplacianInto(i, o); })},
        {"central difference x", CV_32F,
         [](const cv::Mat &i, cv::Mat &o) {
             const cv::Mat dx = (cv::Mat_<float>(1, 3) << -0.5f, 0.f, 0.5f);
             cv::filter2D(i, o, CV_32F, dx, cv::Point(-1, -1), 0, cv::BORDER_REFLECT);
         },
         into([](const Image &i, Image &o) { centralDifferenceInto(i, o, true); })},
        {"central difference y", CV_32F,
         [](const cv::Mat &i, cv::Mat &o) {
             const cv::Mat dy = (cv::Mat_<float>(3, 1) << -0.5f, 0.f, 0.5f);
             cv::filter2D(i, o, CV_32F, dy, cv::Point(-1, -1), 0, cv::BORDER_REFLECT);
         },
         into([](const Image &i, Image &o) { centralDifferenceInto(i, o, false); })},
        {"median 3", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::medianBlur(i, o, 3); },
         into([](const Image &i, Image &o) { medianInto(i, o, 3); })},
        {"median 5", CV_32F, [](const cv::Mat &i, cv::Mat &o) { cv::medianBlur(i, o, 5); },
         into([](const Image &i, Image &o) { medianInto(i, o, 5); })},
        {"box 5 last pixel", CV_32F,
         [](const cv::Mat &i, cv::Mat &o) { cv::blur(i, o, cv::Size(5, 5), cv::Point(-1, -1), cv::BORDER_REPLICATE); },
         into([](const Image &i, Image &o) { boxMeanInto(i, o, 5, 5, BlurBorder::LastPixel); })},
        {"box 41 last pixel", CV_32F,
         [](const cv::Mat &i, cv::Mat &o) { cv::blur(i, o, cv::Size(41, 41), cv::Point(-1, -1), cv::BORDER_REPLICATE); },
         into([](const Image &i, Image &o) { boxMeanInto(i, o, 41, 41, BlurBorder::LastPixel); })},
        {"minimum 15", CV_32F, [kernel](const cv::Mat &i, cv::Mat &o) { cv::erode(i, o, kernel(15)); },
         into([](const Image &i, Image &o) { minimumInto(i, o, 15, 15, Beyond::Nothing); })},
        {"maximum 7", CV_32F, [kernel](const cv::Mat &i, cv::Mat &o) { cv::dilate(i, o, kernel(7)); },
         into([](const Image &i, Image &o) { maximumInto(i, o, 7, 7, Beyond::Nothing); })},
        {"minimum 9 bytes zero beyond", CV_8U,
         [kernel](const cv::Mat &i, cv::Mat &o) { cv::erode(i, o, kernel(9), cv::Point(-1, -1), 1, cv::BORDER_CONSTANT, cv::Scalar(0)); },
         into([](const Image &i, Image &o) { minimumInto(i, o, 9, 9, Beyond::Zero); })},
        {"maximum 13 bytes", CV_8U, [kernel](const cv::Mat &i, cv::Mat &o) { cv::dilate(i, o, kernel(13)); },
         into([](const Image &i, Image &o) { maximumInto(i, o, 13, 13, Beyond::Nothing); })},
    };
    const std::vector<cv::Size> sizes = {cv::Size(1, 1), cv::Size(2, 3), cv::Size(17, 9), cv::Size(640, 481), cv::Size(2048, 1365)};
    std::ostringstream report;
    bool within = true, first = true;
    for (const Case &c : cases)
        for (const cv::Size &size : sizes)
            for (const int channels : {1, 3}) {
                if (c.depth == CV_8U && channels != 1)
                    continue;
                const cv::Mat input = scene(size.width, size.height, channels, c.depth);
                cv::Mat oracle, withIpp, mine;
                cv::ipp::setUseIPP(true);
                const double openCvMs = medianOfThree([&] { c.openCv(input, withIpp); });
                cv::ipp::setUseIPP(false);
                c.openCv(input, oracle);
                cv::ipp::setUseIPP(true);
                const double myMs = medianOfThree([&] { c.mine(input, mine); });
                const double difference = largestGap(mine, oracle), ippGap = largestGap(withIpp, oracle);
                within = within && difference <= tolerance;
                if (size.width >= 640 || difference > tolerance)
                    std::cout << c.name << " " << size.width << "x" << size.height << " c" << channels << ": opencv " << openCvMs
                              << " ms, ipr " << myMs << " ms, largest difference " << difference << ", IPP apart " << ippGap << std::endl;
                report << (first ? "" : ",\n") << "  {\"filter\": \"" << c.name << "\", \"size\": [" << size.width << ", "
                       << size.height << "], \"channels\": " << channels << ", \"opencv_ms\": " << openCvMs << ", \"ipr_ms\": " << myMs
                       << ", \"largest_difference\": " << difference << ", \"ipp_from_opencv\": " << ippGap << "}";
                first = false;
            }
    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"small filters\",\n \"revision\": \"" << argv[1] << "\",\n \"threads\": " << threads << ",\n \"opencv\": \""
        << CV_VERSION << "\",\n \"oracle\": \"OpenCV's own code, IPP off; ipp_from_opencv is how far IPP lies from it\""
        << ",\n \"tolerance_fixed_before_measuring\": " << tolerance << ",\n \"within_tolerance\": " << (within ? "true" : "false")
        << ",\n \"timing\": \"median of three, on a machine busy with other work\",\n \"cases\": [\n" << report.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
