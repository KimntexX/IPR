#include "RawDevelopment.h"

#include "Arrange.h"
#include "Parallel.h"
#include "Pixels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace RawDevelopment {
namespace {
// A count of the 16-bit grid as a value, white 1: the product OpenCV's u16 to f32
// conversion gives, which the profile stage has always received.
constexpr float GridStep = float(1.0 / 65535.0);

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
    const int width = input.mosaic.width(), height = input.mosaic.height();
    const auto &pattern = input.pattern;
    const auto colourAt = [&](int y, int x) { return pattern[size_t(y & 1)][size_t(x & 1)]; };
    Context context;
    context.origin = input.origin;
    context.black = input.black;
    context.white = input.white;
    context.policy = options.policy;
    const bool parity = options.policy == Policy::LibRawParity;
    const auto observe = [&](Stage stage, const ipr::Image &pixels, bool mosaic) {
        if (options.observer) options.observer(Capture{stage, pixels, mosaic, pattern, context});
    };
    observe(Stage::UnpackedCfa, input.mosaic, true);

    // Black per colour; the data maximum for the white. One buffer carries the mosaic
    // through to the demosaic.
    ipr::Image mosaic = ipr::Image::create(input.mosaic.size(), ipr::Depth::F32, 1);
    float dataMaximum = 0.f;
    for (int y = 0; y < height; ++y) {
        const auto *in = input.mosaic.row<std::uint16_t>(y);
        float *out = mosaic.mutableRow<float>(y);
        for (int x = 0; x < width; ++x) {
            out[x] = float(in[x]) - float(input.black[size_t(colourAt(y, x))]);
            dataMaximum = std::max(dataMaximum, out[x]);
        }
    }
    observe(Stage::BlackSubtractedCfa, mosaic, true);
    // The common part of the black (its smallest value) comes off the white; the white
    // drops to the data maximum when that lies just below it (LibRaw's adjustment).
    const double commonBlack = *std::min_element(input.black.cbegin(), input.black.cend());
    double divisor = input.white - commonBlack;
    if (options.adjustWhiteThreshold > 0.0 && dataMaximum > 0.f && dataMaximum < divisor &&
        dataMaximum > divisor * options.adjustWhiteThreshold)
        divisor = dataMaximum;
    context.divisor = divisor;
    context.white = divisor + commonBlack;
    if (parity) mosaic = ipr::atLeast(mosaic, 0.f);   // A: the lower bound at 0
    if (options.observer) {
        const float inverse = float(1.0 / divisor);   // as OpenCV divided a Mat by a number
        const ipr::Image normalized = ipr::mapped(mosaic, [inverse](float v) { return v * inverse; });
        observe(Stage::NormalizedCfa, normalized, true);
    }

    // As-shot gains, the smallest 1. A: values truncated to the 16-bit grid, as LibRaw
    // scales them: gains, scales and products all in single precision (a scale computed
    // in double and rounded once is one ulp lower on some gains, one count lower on some
    // values). B: no grid. Both clip at the white.
    const double smallest = *std::min_element(input.asShotMultipliers.cbegin(), input.asShotMultipliers.cend());
    std::array<float, 4> scale{};
    for (int c = 0; c < 4; ++c) {
        const float gain = float(input.asShotMultipliers[size_t(c)] / smallest);
        context.gains[size_t(c)] = gain;
        scale[size_t(c)] = parity ? gain * 65535.f / float(divisor) : float(double(gain) / divisor);
    }
    result.gains = context.gains;
    for (int y = 0; y < height; ++y) {
        float *row = mosaic.mutableRow<float>(y);
        for (int x = 0; x < width; ++x) {
            const float scaled = row[x] * scale[size_t(colourAt(y, x))];
            row[x] = parity ? std::min(std::trunc(scaled), 65535.f) / 65535.f : std::min(scaled, 1.f);
        }
    }
    observe(Stage::PreDemosaicCfa, mosaic, true);

    Demosaic::Pattern bayer{};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) bayer[size_t(y)][size_t(x)] = demosaicColour(pattern[size_t(y)][size_t(x)]);
    ipr::Image beforeMedian;
    const Demosaic::Range range = parity ? Demosaic::Range::NonNegative : Demosaic::Range::Signed;
    if (!Demosaic::interpolate(mosaic, bayer, options.median, range, result.cameraRgb,
                               options.observer ? &beforeMedian : nullptr, options.cancelled)) {
        result.cameraRgb = {};
        result.error = QStringLiteral("Dématriçage annulé ou impossible.");
        return result;
    }
    mosaic = {};
    if (options.observer) observe(Stage::PostDemosaicCameraRgb, beforeMedian, false);
    observe(Stage::PreOutputCameraRgb, result.cameraRgb, false);
    // A: on the 16-bit grid, as LibRaw's image after the interpolation. B: unbounded,
    // the values below the black reach the profile stage.
    if (parity)
        ipr::parallelRows(result.cameraRgb.height(), [&](int first, int last) {
            for (int y = first; y < last; ++y) {
                ipr::Vec3f *row = result.cameraRgb.mutableRow<ipr::Vec3f>(y);
                for (int x = 0; x < result.cameraRgb.width(); ++x)
                    for (int c = 0; c < 3; ++c)
                        row[x][c] = float(std::clamp(std::lround(row[x][c] * 65535.f), 0L, 65535L)) * GridStep;
            }
        });
    observe(Stage::PostMedianCameraRgb, result.cameraRgb, false);
    return result;
}

ipr::Image orient(ipr::Image image, int orientation)
{
    // LibRaw's flip bits: 2 the rows, 1 the columns, then 4 transposes.
    if (orientation & 3) image = ipr::mirrored(image, (orientation & 1) != 0, (orientation & 2) != 0);
    if (orientation & 4) image = ipr::transposed(image);
    return image;
}

} // namespace RawDevelopment
