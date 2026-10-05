#include "RawBlackLevelEstimator.h"

#include <algorithm>
#include <vector>

namespace RawBlackLevelEstimator {

Result estimate(const Input &input)
{
    Result result;
    if (input.extension.compare(QStringLiteral("cr3"), Qt::CaseInsensitive) != 0 ||
        !input.cameraMake.contains(QStringLiteral("Canon"), Qt::CaseInsensitive) ||
        input.globalBlack != 0 || input.rawGlobalBlack != 0 ||
        input.patternBlack != 0 || input.rawPatternBlack != 0 ||
        input.maximum <= 0 || !input.mosaic || !input.hasCfa) {
        result.status = Status::NotApplicable;
        return result;
    }

    for (int channel = 0; channel < 4; ++channel) {
        if (input.channelBlack[channel] >
                static_cast<unsigned int>(input.maximum / 100) ||
            input.rawChannelBlack[channel] >
                static_cast<unsigned int>(input.maximum / 100)) {
            result.status = Status::NotApplicable;
            return result;
        }
    }

    if (input.rawWidth <= 0 || input.rawHeight <= 0 ||
        input.activeWidth <= 0 || input.activeHeight <= 0 ||
        input.leftMargin < 0 || input.topMargin < 0 ||
        input.leftMargin > input.rawWidth ||
        input.topMargin > input.rawHeight ||
        input.activeWidth > input.rawWidth - input.leftMargin ||
        input.activeHeight > input.rawHeight - input.topMargin ||
        (input.leftMargin == 0 && input.topMargin == 0 &&
         input.activeWidth == input.rawWidth &&
         input.activeHeight == input.rawHeight)) {
        result.status = Status::InvalidInput;
        return result;
    }
    const int right = input.leftMargin + input.activeWidth;
    const int bottom = input.topMargin + input.activeHeight;

    for (int channel : input.cfaChannels) {
        if (channel < 0 || channel >= 4) {
            result.status = Status::InvalidInput;
            return result;
        }
    }

    std::array<std::vector<std::uint32_t>, 4> histograms;
    for (auto &histogram : histograms)
        histogram.resize(65536);

    const auto addSample = [&](int x, int y) {
        const int channel = input.cfaChannels[(y % 2) * 2 + (x % 2)];
        const std::uint16_t value =
            input.mosaic[static_cast<std::size_t>(y) * input.rawWidth + x];
        ++histograms[channel][value];
        ++result.sampleCounts[channel];
    };

    for (int y = 0; y < input.rawHeight; ++y) {
        for (int x = 0; x < input.leftMargin; ++x)
            addSample(x, y);
        for (int x = right; x < input.rawWidth; ++x)
            addSample(x, y);
    }
    for (int y = 0; y < input.topMargin; ++y) {
        for (int x = input.leftMargin; x < right; ++x)
            addSample(x, y);
    }
    for (int y = bottom; y < input.rawHeight; ++y) {
        for (int x = input.leftMargin; x < right; ++x)
            addSample(x, y);
    }

    for (int channel = 0; channel < 4; ++channel) {
        if (result.sampleCounts[channel] < 64) {
            result.status = Status::InsufficientSamples;
            return result;
        }

        const std::uint64_t medianIndex =
            (result.sampleCounts[channel] - 1) / 2;
        std::uint64_t cumulative = 0;
        for (std::size_t value = 0; value < histograms[channel].size(); ++value) {
            cumulative += histograms[channel][value];
            if (cumulative > medianIndex) {
                result.levels[channel] = static_cast<unsigned int>(value);
                break;
            }
        }
        if (result.levels[channel] < input.maximum / 50 ||
            result.levels[channel] > input.maximum / 4) {
            result.status = Status::ImplausibleLevel;
            return result;
        }
    }

    result.status = Status::Estimated;
    return result;
}

QString statusName(Status status)
{
    switch (status) {
    case Status::Estimated: return QStringLiteral("estimated");
    case Status::NotApplicable: return QStringLiteral("not_applicable");
    case Status::InvalidInput: return QStringLiteral("invalid_input");
    case Status::InsufficientSamples:
        return QStringLiteral("insufficient_samples");
    case Status::ImplausibleLevel:
        return QStringLiteral("implausible_level");
    }
    return QStringLiteral("unknown");
}

}
