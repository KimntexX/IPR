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

RawBlackLevelEstimator::Input fixture(std::vector<std::uint16_t> &mosaic)
{
    constexpr int rawWidth = 20;
    constexpr int rawHeight = 20;
    mosaic.resize(rawWidth * rawHeight);
    constexpr std::array<unsigned int, 4> levels{{2048, 2050, 2052, 2054}};
    for (int y = 0; y < rawHeight; ++y) {
        for (int x = 0; x < rawWidth; ++x) {
            const bool active = x >= 6 && x < 14 && y >= 6 && y < 14;
            const int channel = (y % 2) * 2 + (x % 2);
            mosaic[y * rawWidth + x] =
                static_cast<std::uint16_t>(active ? 6000 : levels[channel]);
        }
    }

    RawBlackLevelEstimator::Input input;
    input.extension = QStringLiteral("cr3");
    input.cameraMake = QStringLiteral("Canon");
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
    std::vector<std::uint16_t> mosaic;
    const auto validInput = fixture(mosaic);
    const auto estimated = RawBlackLevelEstimator::estimate(validInput);
    if (!check(estimated.wasEstimated() &&
                   estimated.levels == std::array<unsigned int, 4>{{2048, 2050, 2052, 2054}},
               "CFA-specific black levels should be estimated from masked margins"))
        return 1;

    auto input = validInput;
    input.extension = QStringLiteral("dng");
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "DNG files must not use the Canon CR3 fallback"))
        return 1;

    input = validInput;
    input.extension = QStringLiteral("cr2");
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "CR2 files must not use the Canon CR3 fallback"))
        return 1;

    input = validInput;
    input.cameraMake = QStringLiteral("Nikon");
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "non-Canon files must not use the Canon fallback"))
        return 1;

    input = validInput;
    input.patternBlack = 1;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "existing repeated black-level metadata must prevent estimation"))
        return 1;

    input = validInput;
    input.rawGlobalBlack = 1;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "existing raw black-level metadata must prevent estimation"))
        return 1;

    input = validInput;
    input.channelBlack[2] = 200;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::NotApplicable,
               "existing per-channel black-level metadata must not be overwritten"))
        return 1;

    input = validInput;
    input.cfaChannels[3] = -1;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::InvalidInput,
               "invalid CFA channel maps must be rejected"))
        return 1;

    input = validInput;
    input.activeWidth = input.rawWidth;
    input.leftMargin = 0;
    input.activeHeight = input.rawHeight;
    input.topMargin = 0;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::InvalidInput,
               "images without masked margins must be rejected"))
        return 1;

    input = validInput;
    input.leftMargin = input.rawWidth;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::InvalidInput,
               "margin bounds must be validated before calculating active area"))
        return 1;

    input = validInput;
    input.activeWidth = 18;
    input.leftMargin = 1;
    input.activeHeight = 18;
    input.topMargin = 1;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::InsufficientSamples,
               "margins with too few samples for each CFA site must be rejected"))
        return 1;

    input = validInput;
    for (int y = 0; y < input.rawHeight; ++y) {
        for (int x = 0; x < input.rawWidth; ++x) {
            if (!(x >= input.leftMargin &&
                  x < input.leftMargin + input.activeWidth &&
                  y >= input.topMargin &&
                  y < input.topMargin + input.activeHeight))
                mosaic[y * input.rawWidth + x] = 200;
        }
    }
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::ImplausibleLevel,
               "black levels below two percent of white must be rejected"))
        return 1;

    std::vector<std::uint16_t> highBlackMosaic;
    input = fixture(highBlackMosaic);
    input.maximum = 7000;
    if (!check(RawBlackLevelEstimator::estimate(input).status ==
                   RawBlackLevelEstimator::Status::ImplausibleLevel,
               "implausible estimated black levels must be rejected"))
        return 1;

    // Padded stride and bright crop margins must not contaminate a declared
    // optical black area. Four CFA sites each get 96 samples here.
    std::vector<std::uint16_t> padded(40 * 32, 12000);
    RawBlackLevelEstimator::Input masked = validInput;
    masked.rawWidth = 32; masked.rawHeight = 32; masked.rowStrideSamples = 40;
    masked.leftMargin = 14; masked.topMargin = 4;
    masked.activeWidth = 16; masked.activeHeight = 24;
    masked.maskedAreas = {{0, 0, 12, 32}};
    masked.mosaic = padded.data();
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 12; ++x)
            padded[y * 40 + x] = 2048 + ((y % 2) * 2 + x % 2);
    const auto optical = RawBlackLevelEstimator::estimate(masked);
    if (!check(optical.wasEstimated() && optical.levels == std::array<unsigned int, 4>{{2048,2049,2050,2051}},
               "declared optical black must exclude scene margins and respect stride")) return 1;
    masked.maskedAreas = {{31, 0, 12, 32}};
    if (!check(RawBlackLevelEstimator::estimate(masked).status == RawBlackLevelEstimator::Status::InvalidInput,
               "invalid optical bounds must be rejected")) return 1;
    masked.maskedAreas = {{0, 0, 12, 32}}; masked.rowStrideSamples = 20;
    if (!check(RawBlackLevelEstimator::estimate(masked).status == RawBlackLevelEstimator::Status::InvalidInput,
               "stride shorter than raw width must be rejected")) return 1;
    return 0;
}
