#include "RawDevelopment.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RawDevelopment {
namespace {
// The colour read by RCD at a site: the second green of the cell is a green.
int demosaicColour(int colour) { return colour == RawInput::Green2 ? RawInput::Green : colour; }
} // namespace

Result develop(const RawInput::Input &input, const Options &options)
{
    Result result;
    if (!RawInput::valid(input)) {
        result.error = QStringLiteral("Entrée RAW non prise en charge par le développement IPR.");
        return result;
    }
    const int width = input.mosaic.cols, height = input.mosaic.rows;
    const auto &pattern = input.pattern;
    const auto colourAt = [&](int y, int x) { return pattern[size_t(y & 1)][size_t(x & 1)]; };
    Context context;
    context.origin = input.origin;
    context.black = input.black;
    context.white = input.white;
    const auto observe = [&](Stage stage, const cv::Mat &pixels, bool mosaic) {
        if (options.observer) options.observer(Capture{stage, pixels, mosaic, pattern, context});
    };
    observe(Stage::UnpackedCfa, input.mosaic, true);

    // Black per colour, then the lower bound at 0; the data maximum for the white.
    cv::Mat signedCounts(height, width, CV_32FC1);
    float dataMaximum = 0.f;
    for (int y = 0; y < height; ++y) {
        const auto *in = input.mosaic.ptr<std::uint16_t>(y);
        float *out = signedCounts.ptr<float>(y);
        for (int x = 0; x < width; ++x) {
            out[x] = float(in[x]) - float(input.black[size_t(colourAt(y, x))]);
            dataMaximum = std::max(dataMaximum, out[x]);
        }
    }
    observe(Stage::BlackSubtractedCfa, signedCounts, true);
    // The common part of the black (its smallest value) comes off the white; the white
    // drops to the data maximum when that lies just below it (LibRaw's adjustment).
    const double commonBlack = *std::min_element(input.black.cbegin(), input.black.cend());
    double divisor = input.white - commonBlack;
    if (options.adjustWhiteThreshold > 0.0 && dataMaximum > 0.f && dataMaximum < divisor &&
        dataMaximum > divisor * options.adjustWhiteThreshold)
        divisor = dataMaximum;
    context.divisor = divisor;
    context.white = divisor + commonBlack;
    cv::Mat clipped = cv::max(signedCounts, 0.f);
    if (options.observer) {
        const cv::Mat normalized = clipped / divisor;
        observe(Stage::NormalizedCfa, normalized, true);
    }

    // As-shot gains, the smallest 1; values truncated to the 16-bit grid, as LibRaw scales
    // them: gains, scales and products all in single precision (a scale computed in double
    // and rounded once is one ulp lower on some gains, one count lower on some values).
    const double smallest = *std::min_element(input.asShotMultipliers.cbegin(), input.asShotMultipliers.cend());
    std::array<float, 4> scale{};
    for (int c = 0; c < 4; ++c) {
        const float gain = float(input.asShotMultipliers[size_t(c)] / smallest);
        context.gains[size_t(c)] = gain;
        scale[size_t(c)] = gain * 65535.f / float(divisor);
    }
    result.gains = context.gains;
    cv::Mat mosaic(height, width, CV_32FC1);
    for (int y = 0; y < height; ++y) {
        const float *in = clipped.ptr<float>(y);
        float *out = mosaic.ptr<float>(y);
        for (int x = 0; x < width; ++x) {
            const float scaled = std::trunc(in[x] * scale[size_t(colourAt(y, x))]);
            out[x] = std::min(scaled, 65535.f) / 65535.f;
        }
    }
    clipped.release();
    signedCounts.release();
    observe(Stage::PreDemosaicCfa, mosaic, true);

    Demosaic::Pattern bayer{};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) bayer[size_t(y)][size_t(x)] = demosaicColour(pattern[size_t(y)][size_t(x)]);
    cv::Mat beforeMedian;
    if (!Demosaic::interpolate(mosaic, bayer, options.median, result.cameraRgb,
                               options.observer ? &beforeMedian : nullptr, options.cancelled)) {
        result.cameraRgb.release();
        result.error = QStringLiteral("Dématriçage annulé ou impossible.");
        return result;
    }
    mosaic.release();
    if (options.observer) observe(Stage::PostDemosaicCameraRgb, beforeMedian, false);
    // On the 16-bit grid, as LibRaw's image after the interpolation.
    result.cameraRgb.forEach<cv::Vec3f>([](cv::Vec3f &pixel, const int *) {
        for (int c = 0; c < 3; ++c)
            pixel[c] = float(std::clamp(std::lround(pixel[c] * 65535.f), 0L, 65535L)) / 65535.f;
    });
    observe(Stage::PostMedianCameraRgb, result.cameraRgb, false);
    return result;
}

cv::Mat orient(const cv::Mat &image, int orientation)
{
    // LibRaw's flip bits: 2 the rows, 1 the columns, then 4 transposes.
    cv::Mat out = image.clone();
    if (orientation & 2) cv::flip(out, out, 0);
    if (orientation & 1) cv::flip(out, out, 1);
    if (orientation & 4) cv::transpose(out, out);
    return out;
}

} // namespace RawDevelopment
