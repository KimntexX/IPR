#ifndef IPR_RAW_RAWBLACKLEVELESTIMATOR_H
#define IPR_RAW_RAWBLACKLEVELESTIMATOR_H

#include <QString>

#include <array>
#include <cstdint>
#include <vector>

namespace RawBlackLevelEstimator {

enum class Status
{
    Estimated,
    NotApplicable,
    InvalidInput,
    InsufficientSamples,
    ImplausibleLevel
};

struct Input
{
    struct MaskedArea { int left, top, width, height; };
    QString extension;
    QString cameraMake;
    const std::uint16_t *mosaic = nullptr;
    int rawWidth = 0;
    int rawHeight = 0;
    int rowStrideSamples = 0; // 0 retains packed-width compatibility.
    std::vector<MaskedArea> maskedAreas; // Explicit optical black, not crop margins.
    int leftMargin = 0;
    int topMargin = 0;
    int activeWidth = 0;
    int activeHeight = 0;
    int maximum = 0;
    bool hasCfa = false;
    std::array<int, 4> cfaChannels{{-1, -1, -1, -1}};
    int globalBlack = 0;
    int rawGlobalBlack = 0;
    std::array<unsigned int, 4> channelBlack{{0, 0, 0, 0}};
    std::array<unsigned int, 4> rawChannelBlack{{0, 0, 0, 0}};
    unsigned int patternBlack = 0;
    unsigned int rawPatternBlack = 0;
};

struct Result
{
    Status status = Status::NotApplicable;
    std::array<unsigned int, 4> levels{{0, 0, 0, 0}};
    std::array<std::uint64_t, 4> sampleCounts{{0, 0, 0, 0}};

    bool wasEstimated() const { return status == Status::Estimated; }
};

Result estimate(const Input &input);
QString statusName(Status status);

}

#endif // IPR_RAW_RAWBLACKLEVELESTIMATOR_H
