#include "RawBlackLevelEstimator.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

using Levels = std::array<unsigned int, 4>;

RawBlackLevelEstimator::Input fixture(std::vector<std::uint16_t> &mosaic)
{
    constexpr int rawWidth = 20;
    constexpr int rawHeight = 20;
    mosaic.resize(rawWidth * rawHeight);
    constexpr Levels levels{{2048, 2050, 2052, 2054}};
    for (int y = 0; y < rawHeight; ++y) {
        for (int x = 0; x < rawWidth; ++x) {
            const bool active = x >= 6 && x < 14 && y >= 6 && y < 14;
            const int channel = (y % 2) * 2 + (x % 2);
            mosaic[y * rawWidth + x] = static_cast<std::uint16_t>(active ? 6000 : levels[channel]);
        }
    }

    RawBlackLevelEstimator::Input input;
    input.marginsAreMasked = true;
    input.mosaic = mosaic.data();
    input.rawWidth = rawWidth;
    input.rawHeight = rawHeight;
    input.leftMargin = 6;
    input.topMargin = 6;
    input.activeWidth = 8;
    input.activeHeight = 8;
    input.maximum = 16383;
    input.hasCfa = true;
    input.cfaChannels = {{0, 1, 2, 3}};
    return input;
}

}

int main()
{
    using namespace RawBlackLevelEstimator;
    std::vector<std::uint16_t> mosaic;
    const auto validInput = fixture(mosaic);
    const auto measured = measure(validInput);
    if (!check(measured.wasMeasured() && measured.levels == Levels{{2048, 2050, 2052, 2054}},
               "CFA-specific black levels should be measured from masked margins"))
        return 1;

    // Decision: the decoder keeps its black when it agrees, whatever the
    // format or make; a missing or deviating black is replaced.
    if (!check(decide(Levels{{2049, 2050, 2051, 2054}}, false, measured, 16383).source == Source::Decoder,
               "an agreeing decoder black must be kept (identical rendering)"))
        return 1;
    if (!check(decide(Levels{{0, 0, 0, 0}}, false, measured, 16383).source == Source::Measured,
               "a missing decoder black must be replaced (R6 Mark III CR3 case)"))
        return 1;
    const auto deviating = decide(Levels{{2048, 2050, 2052, 2200}}, false, measured, 16383);
    if (!check(deviating.source == Source::Measured && deviating.deviation == 146,
               "a deviating decoder black must be replaced, with its deviation"))
        return 1;
    if (!check(decide(Levels{{0, 0, 0, 0}}, true, measured, 16383).source == Source::Decoder,
               "a decoder spatial black pattern must be kept"))
        return 1;
    if (!check(tolerance(16383) == 16 && tolerance(4095) == 8, "tolerance follows the white level, at least 8 codes"))
        return 1;

    auto input = validInput;
    input.marginsAreMasked = false;
    if (!check(measure(input).status == Status::NotApplicable && decide(Levels{{0, 0, 0, 0}}, false, measure(input), 16383).source == Source::Decoder,
               "without declared optical black, margins are not read for other makes"))
        return 1;

    input = validInput;
    input.hasCfa = false;
    if (!check(measure(input).status == Status::NotApplicable, "non-CFA mosaics are not measured"))
        return 1;

    input = validInput;
    input.cfaChannels[3] = -1;
    if (!check(measure(input).status == Status::InvalidInput, "invalid CFA channel maps must be rejected"))
        return 1;

    input = validInput;
    input.activeWidth = input.rawWidth;
    input.leftMargin = 0;
    input.activeHeight = input.rawHeight;
    input.topMargin = 0;
    if (!check(measure(input).status == Status::InvalidInput, "images without masked margins must be rejected"))
        return 1;

    input = validInput;
    input.leftMargin = input.rawWidth;
    if (!check(measure(input).status == Status::InvalidInput,
               "margin bounds must be validated before calculating active area"))
        return 1;

    input = validInput;
    input.activeWidth = 18;
    input.leftMargin = 1;
    input.activeHeight = 18;
    input.topMargin = 1;
    if (!check(measure(input).status == Status::InsufficientSamples,
               "margins with too few samples for each CFA site must be rejected"))
        return 1;

    input = validInput;
    for (int y = 0; y < input.rawHeight; ++y)
        for (int x = 0; x < input.rawWidth; ++x)
            if (!(x >= input.leftMargin && x < input.leftMargin + input.activeWidth &&
                  y >= input.topMargin && y < input.topMargin + input.activeHeight))
                mosaic[y * input.rawWidth + x] = 200;
    if (!check(measure(input).status == Status::ImplausibleLevel,
               "black levels below two percent of white must be rejected"))
        return 1;
    if (!check(decide(Levels{{0, 0, 0, 0}}, false, measure(input), 16383).source == Source::Decoder,
               "an implausible measurement never replaces the decoder black"))
        return 1;

    std::vector<std::uint16_t> highBlackMosaic;
    input = fixture(highBlackMosaic);
    input.maximum = 7000;
    if (!check(measure(input).status == Status::ImplausibleLevel, "implausible measured black levels must be rejected"))
        return 1;

    // Padded stride and bright crop margins must not contaminate a declared
    // optical black area, for any make. Four CFA sites each get 96 samples here.
    std::vector<std::uint16_t> padded(40 * 32, 12000);
    RawBlackLevelEstimator::Input masked = validInput;
    masked.marginsAreMasked = false;
    masked.rawWidth = 32; masked.rawHeight = 32; masked.rowStrideSamples = 40;
    masked.leftMargin = 14; masked.topMargin = 4;
    masked.activeWidth = 16; masked.activeHeight = 24;
    masked.maskedAreas = {{0, 0, 12, 32}};
    masked.mosaic = padded.data();
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 12; ++x)
            padded[y * 40 + x] = 2048 + ((y % 2) * 2 + x % 2);
    const auto optical = measure(masked);
    if (!check(optical.wasMeasured() && optical.levels == Levels{{2048, 2049, 2050, 2051}},
               "declared optical black must exclude scene margins and respect stride")) return 1;
    masked.maskedAreas = {{31, 0, 12, 32}};
    if (!check(measure(masked).status == Status::InvalidInput, "invalid optical bounds must be rejected")) return 1;
    masked.maskedAreas = {{0, 0, 12, 32}}; masked.rowStrideSamples = 20;
    if (!check(measure(masked).status == Status::InvalidInput, "stride shorter than raw width must be rejected")) return 1;
    return 0;
}
