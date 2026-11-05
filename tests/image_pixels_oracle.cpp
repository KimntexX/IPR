// O3: pixel arithmetic of the image layer against OpenCV as the oracle: the largest difference and
// the time. The tolerance is fixed here, before any measure; reductions are compared in double.
// The oracle is OpenCV's own code, IPP off. Writes one JSON proof.
#include "OpenCvBridge.h"

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ipr;

namespace {

constexpr double tolerance = 1e-5;

cv::Mat scene(int width, int height, int channels, int seed)
{
    cv::Mat mat(height, width, CV_32FC(channels));
    cv::RNG random{static_cast<uint64>(seed)};
    random.fill(mat, cv::RNG::UNIFORM, -0.5f, 1.5f);
    return mat;
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
        std::cerr << "usage: iprImagePixelsOracle <revision> <new json>\n";
        return 2;
    }
    if (std::ifstream(argv[2]).good()) {
        std::cerr << "The proof exists\n";
        return 2;
    }
    cv::ipp::setUseIPP(false);
    cv::setNumThreads(8);
    const cv::Mat a3 = scene(2048, 1365, 3, 1), b3 = scene(2048, 1365, 3, 2), a1 = scene(2048, 1365, 1, 3), b1 = scene(2048, 1365, 1, 4);
    const cv::Mat a4 = scene(2048, 1365, 4, 5);
    cv::Mat bytesA, bytesB;
    a1.convertTo(bytesA, CV_8U, 120.0, 60.0);
    b1.convertTo(bytesB, CV_8U, 140.0, 30.0);
    const cv::Mat positive = cv::abs(a1) + 0.01f;
    struct Case { std::string name; std::function<cv::Mat()> openCv, mine; };
    const std::vector<Case> cases = {
        {"multiply", [&] { cv::Mat o; cv::multiply(a3, b3, o); return o; }, [&] { return multiplied(a3, b3); }},
        {"multiply by a scalar", [&] { cv::Mat o; cv::multiply(a3, cv::Scalar(0.91, 1.07, 1.3), o); return o; },
         [&] { return multiplied(a3, std::vector<double>{0.91, 1.07, 1.3}); }},
        {"divide", [&] { cv::Mat o; cv::divide(a1, positive, o); return o; }, [&] { return divided(a1, positive); }},
        {"add weighted", [&] { cv::Mat o; cv::addWeighted(a1, 0.3, b1, 0.7, 0.0, o); return o; }, [&] { return blended(a1, 0.3, b1, 0.7, 0.0); }},
        {"scale add", [&] { cv::Mat o; cv::scaleAdd(a1, 0.37, b1, o); return o; }, [&] { return addScaled(a1, 0.37, b1); }},
        {"subtract from a scalar", [&] { cv::Mat o; cv::subtract(cv::Scalar(1.0), a1, o); return o; }, [&] { return subtractedFrom(1.0, a1); }},
        {"magnitude", [&] { cv::Mat o; cv::magnitude(a1, b1, o); return o; }, [&] { return magnitude(a1, b1); }},
        {"square root", [&] { cv::Mat o; cv::sqrt(positive, o); return o; }, [&] { return squareRoot(positive); }},
        {"max with a bound", [&] { cv::Mat o; cv::max(a3, 0.2f, o); return o; }, [&] { return atLeast(a3, 0.2f); }},
        {"max per channel", [&] { cv::Mat o; cv::max(a3, cv::Scalar(-0.1, 0.0, 0.3), o); return o; },
         [&] { return atLeast(a3, std::vector<float>{-0.1f, 0.0f, 0.3f}); }},
        {"min with a bound", [&] { cv::Mat o; cv::min(a1, 1.0f, o); return o; }, [&] { return atMost(a1, 1.0f); }},
        {"transform", [&] { cv::Mat o; cv::transform(a3, o, cv::Matx33f(1.2f, 0, 0, 0, 0.9f, 0, 0, 0, 1.4f)); return o; },
         [&] { return transformed(a3, Matrix33f(1.2f, 0, 0, 0, 0.9f, 0, 0, 0, 1.4f)); }},
        {"rgba to rgb", [&] { cv::Mat o; cv::cvtColor(a4, o, cv::COLOR_RGBA2RGB); return o; }, [&] { return withoutAlpha(a4); }},
        {"extract channel", [&] { cv::Mat o; cv::extractChannel(a4, o, 3); return o; }, [&] { return channel(a4, 3); }},
        {"split and merge", [&] { std::vector<cv::Mat> p; cv::split(a3, p); cv::Mat o; cv::merge(p, o); return o; },
         [&] { return merged(splitChannels(a3)); }},
        {"bitwise or", [&] { cv::Mat o; cv::bitwise_or(bytesA, bytesB, o); return o; }, [&] { return bitwiseOr(bytesA, bytesB); }},
        {"bitwise and", [&] { cv::Mat o; cv::bitwise_and(bytesA, bytesB, o); return o; }, [&] { return bitwiseAnd(bytesA, bytesB); }},
        {"bitwise not", [&] { cv::Mat o; cv::bitwise_not(bytesA, o); return o; }, [&] { return bitwiseNot(bytesA); }},
    };
    std::ostringstream report;
    bool within = true, first = true;
    for (const Case &c : cases) {
        cv::Mat theirs, mine;
        const double openCvMs = milliseconds([&] { theirs = c.openCv(); });
        const double myMs = milliseconds([&] { mine = c.mine(); });
        const double difference = largestGap(mine, theirs);
        within = within && difference <= tolerance;
        std::cout << c.name << ": opencv " << openCvMs << " ms, ipr " << myMs << " ms, largest difference " << difference << std::endl;
        report << (first ? "" : ",\n") << "  {\"operation\": \"" << c.name << "\", \"opencv_ms\": " << openCvMs << ", \"ipr_ms\": " << myMs
               << ", \"largest_difference\": " << difference << "}";
        first = false;
    }
    // Reductions.
    const auto reduction = [&](const std::string &name, double theirs, double mine) {
        const double difference = std::abs(theirs - mine) / std::max(1.0, std::abs(theirs));
        within = within && difference <= 1e-9;
        std::cout << name << ": opencv " << theirs << ", ipr " << mine << ", relative difference " << difference << std::endl;
        report << ",\n  {\"reduction\": \"" << name << "\", \"opencv\": " << theirs << ", \"ipr\": " << mine
               << ", \"relative_difference\": " << difference << "}";
    };
    double low = 0.0, high = 0.0;
    cv::Point lowAt, highAt;
    cv::minMaxLoc(a1, &low, &high, &lowAt, &highAt);
    const MatExtremes found = extremesOf(a1);
    reduction("smallest", low, found.smallest);
    reduction("largest", high, found.largest);
    reduction("smallest place", lowAt.y * a1.cols + lowAt.x, found.smallestAt.y * a1.cols + found.smallestAt.x);
    reduction("largest place", highAt.y * a1.cols + highAt.x, found.largestAt.y * a1.cols + found.largestAt.x);
    reduction("count not zero", double(cv::countNonZero(bytesA - 100)), double(countNonZero(cv::Mat(bytesA - 100))));
    cv::Scalar mean, deviation;
    cv::meanStdDev(a3, mean, deviation);
    std::vector<double> myMean, myDeviation;
    meanAndDeviationOf(a3, myMean, myDeviation);
    for (int c = 0; c < 3; ++c) {
        reduction("mean " + std::to_string(c), mean[c], myMean[size_t(c)]);
        reduction("deviation " + std::to_string(c), deviation[c], myDeviation[size_t(c)]);
    }
    std::ofstream out(argv[2]);
    out << "{\n \"kernel\": \"pixel arithmetic\",\n \"revision\": \"" << argv[1] << "\",\n \"opencv\": \"" << CV_VERSION
        << "\",\n \"oracle\": \"OpenCV's own code, IPP off\",\n \"tolerance_fixed_before_measuring\": " << tolerance
        << ",\n \"reduction_relative_tolerance\": 1e-9,\n \"within_tolerance\": " << (within ? "true" : "false")
        << ",\n \"timing\": \"one run, on a machine busy with other work\",\n \"cases\": [\n" << report.str() << "\n ]\n}\n";
    return within ? 0 : 1;
}
