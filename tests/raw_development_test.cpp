#include "RawDevelopment.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

// A uniform RGGB mosaic with LibRaw's labelling of the second green, levels of a
// Canon CR2 (black 2048, white 11222) and its as-shot multipliers.
RawInput::Input sample(int width = 48, int height = 40)
{
    RawInput::Input input;
    input.pattern = {{{RawInput::Red, RawInput::Green}, {RawInput::Green2, RawInput::Blue}}};
    input.black = {2048, 2048, 2048, 2048};
    input.white = 11222;
    input.asShotMultipliers = {2076, 1024, 1611, 1024};
    input.origin = cv::Point(36, 12);
    input.mosaic = cv::Mat(height, width, CV_16UC1);
    const std::uint16_t counts[4] = {3000, 5000, 3838, 5000};   // per RawInput::Colour
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            input.mosaic.at<std::uint16_t>(y, x) = counts[input.pattern[size_t(y & 1)][size_t(x & 1)]];
    return input;
}

RawDevelopment::Options calibrated()
{
    RawDevelopment::Options options;
    options.adjustWhiteThreshold = 0.0;
    options.median = Demosaic::ChromaMedian::Off;
    return options;
}

float counts(const cv::Mat &mosaic, int y, int x) { return mosaic.at<float>(y, x) * 65535.f; }

bool same(const cv::Mat &a, const cv::Mat &b) { return a.size() == b.size() && cv::norm(a, b, cv::NORM_INF) == 0; }
} // namespace

int main()
{
    bool ok = true;
    {
        RawInput::Input input = sample();
        input.spatialBlack = true;
        const auto result = RawDevelopment::develop(input, calibrated());
        ok &= check(result.cameraRgb.empty() && !result.error.isEmpty(), "an input IPR cannot develop is refused");
    }
    {
        // Values read from LibRaw's dcraw_process() on IMG_4173.CR2: the scale is taken in
        // single precision, the scaled count truncated, the top clipped to 65535.
        RawInput::Input input = sample(8, 8);
        const std::uint16_t blue[4] = {3838, 7003, 7527, 7900};
        for (int i = 0; i < 4; ++i) input.mosaic.at<std::uint16_t>(1, 1 + 2 * i) = blue[i];
        input.mosaic.at<std::uint16_t>(3, 1) = 1000;   // below the black
        std::vector<RawDevelopment::Stage> stages;
        cv::Mat signedCounts, normalized, preDemosaic;
        double divisor = 0.0;
        cv::Point origin;
        auto options = calibrated();
        options.observer = [&](const RawDevelopment::Capture &c) {
            stages.push_back(c.stage);
            if (c.stage == RawDevelopment::Stage::BlackSubtractedCfa) signedCounts = c.pixels.clone();
            if (c.stage == RawDevelopment::Stage::NormalizedCfa) normalized = c.pixels.clone();
            if (c.stage == RawDevelopment::Stage::PreDemosaicCfa) {
                preDemosaic = c.pixels.clone();
                divisor = c.context.divisor;
                origin = c.context.origin;
            }
        };
        const auto result = RawDevelopment::develop(input, options);
        ok &= check(result.error.isEmpty() && !result.cameraRgb.empty(), "the development completes");
        ok &= check(stages == std::vector<RawDevelopment::Stage>{
                                  RawDevelopment::Stage::UnpackedCfa, RawDevelopment::Stage::BlackSubtractedCfa,
                                  RawDevelopment::Stage::NormalizedCfa, RawDevelopment::Stage::PreDemosaicCfa,
                                  RawDevelopment::Stage::PostDemosaicCameraRgb, RawDevelopment::Stage::PostMedianCameraRgb},
                    "each stage is observed once, in order");
        ok &= check(std::abs(result.gains[2] - 1611.0 / 1024.0) < 1e-12 && result.gains[1] == 1.0,
                    "the gains are the multipliers over the smallest");
        ok &= check(divisor == 9174.0, "the calibrated white keeps the divisor white - black");
        ok &= check(origin == cv::Point(36, 12), "the captures carry the visible area's origin");
        ok &= check(counts(preDemosaic, 1, 1) == 20117.f && counts(preDemosaic, 1, 3) == 55687.f &&
                        counts(preDemosaic, 1, 5) == 61576.f && counts(preDemosaic, 1, 7) == 65535.f,
                    "the scaled counts match LibRaw's, the top clipped");
        ok &= check(signedCounts.at<float>(3, 1) == -1048.f && normalized.at<float>(3, 1) == 0.f &&
                        counts(preDemosaic, 3, 1) == 0.f,
                    "a count below the black is kept signed, then clipped at 0");
        ok &= check(std::abs(normalized.at<float>(0, 0) - 952.f / 9174.f) < 1e-7f, "the normalized mosaic has no gain");
    }
    {
        // LibRaw lowers the white to the data maximum when it lies above the threshold's share.
        RawInput::Input input = sample();
        input.mosaic.at<std::uint16_t>(0, 1) = 10000;
        double divisor = 0.0, white = 0.0;
        auto options = calibrated();
        options.adjustWhiteThreshold = 0.75;
        options.observer = [&](const RawDevelopment::Capture &c) {
            divisor = c.context.divisor;
            white = c.context.white;
        };
        RawDevelopment::develop(input, options);
        ok &= check(divisor == 7952.0 && white == 10000.0, "the white drops to the data maximum");
        input.mosaic.at<std::uint16_t>(0, 1) = 5000;
        RawDevelopment::develop(input, options);
        ok &= check(divisor == 9174.0, "a data maximum below the threshold keeps the white");
    }
    {
        // A uniform mosaic develops into a uniform camera colour on the 16-bit grid.
        const auto result = RawDevelopment::develop(sample(), calibrated());
        const float expected[3] = {std::trunc(952.f * (2076.f / 1024.f * 65535.f / 9174.f)),
                                   std::trunc(2952.f * (65535.f / 9174.f)),
                                   std::trunc(1790.f * (1611.f / 1024.f * 65535.f / 9174.f))};
        double worst = 0.0;
        bool onGrid = true;
        for (int y = 0; y < result.cameraRgb.rows; ++y)
            for (int x = 0; x < result.cameraRgb.cols; ++x)
                for (int c = 0; c < 3; ++c) {
                    const float value = result.cameraRgb.at<cv::Vec3f>(y, x)[c] * 65535.f;
                    worst = std::max(worst, double(std::abs(value - expected[c])));
                    onGrid &= std::abs(value - std::round(value)) < 1e-2f;
                }
        ok &= check(result.cameraRgb.type() == CV_32FC3 && result.cameraRgb.size() == cv::Size(48, 40),
                    "the output is camera RGB at the mosaic's size");
        ok &= check(worst <= 1.0 && onGrid, "a uniform colour is reproduced on the 16-bit grid");
    }
    {
        auto options = calibrated();
        options.cancelled = [] { return true; };
        const auto result = RawDevelopment::develop(sample(), options);
        ok &= check(result.cameraRgb.empty() && !result.error.isEmpty(), "a cancelled development reports it");
    }
    {
        const cv::Mat image = (cv::Mat_<float>(2, 3) << 1, 2, 3, 4, 5, 6);
        ok &= check(same(RawDevelopment::orient(image, 0), image), "code 0 keeps the image");
        ok &= check(same(RawDevelopment::orient(image, 3), cv::Mat((cv::Mat_<float>(2, 3) << 6, 5, 4, 3, 2, 1))),
                    "code 3 turns by 180 degrees");
        ok &= check(same(RawDevelopment::orient(image, 6), cv::Mat((cv::Mat_<float>(3, 2) << 4, 1, 5, 2, 6, 3))),
                    "code 6 turns clockwise");
        ok &= check(same(RawDevelopment::orient(image, 5), cv::Mat((cv::Mat_<float>(3, 2) << 3, 6, 2, 5, 1, 4))),
                    "code 5 turns anticlockwise");
    }
    if (ok) std::cout << "rawDevelopmentTests passed\n";
    return ok ? 0 : 1;
}
