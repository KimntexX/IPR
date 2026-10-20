#ifndef IPR_RAW_RAWBLACKLEVELESTIMATOR_H
#define IPR_RAW_RAWBLACKLEVELESTIMATOR_H

#include <QString>

#include <array>
#include <cstdint>
#include <vector>

// Black level of a RAW mosaic, read by IPR on every file that has optical
// black pixels: measured per CFA channel (median), then compared with the
// decoder's own value. LibRaw's default is not always right (Canon R6 Mark
// III CR3: zero or inconsistent while the masked pixels carry a pedestal);
// the measurement replaces it only when it is missing or deviates, so files
// it reads correctly keep exactly the same rendering.
namespace RawBlackLevelEstimator {

enum class Status
{
    Measured,
    NotApplicable,       // no CFA mosaic, or no optical black to read
    InvalidInput,
    InsufficientSamples,
    ImplausibleLevel
};

struct Input
{
    struct MaskedArea { int left, top, width, height; };
    const std::uint16_t *mosaic = nullptr;
    int rawWidth = 0;
    int rawHeight = 0;
    int rowStrideSamples = 0; // 0 retains packed-width compatibility.
    std::vector<MaskedArea> maskedAreas; // Declared optical black, not crop margins.
    // Without a declared area, read the crop margins instead (historical Canon
    // fallback: not every margin is masked, so other makes never use it).
    bool marginsAreMasked = false;
    int leftMargin = 0;
    int topMargin = 0;
    int activeWidth = 0;
    int activeHeight = 0;
    int maximum = 0;
    bool hasCfa = false;
    std::array<int, 4> cfaChannels{{-1, -1, -1, -1}};
};

struct Result
{
    Status status = Status::NotApplicable;
    std::array<unsigned int, 4> levels{{0, 0, 0, 0}};
    std::array<std::uint64_t, 4> sampleCounts{{0, 0, 0, 0}};

    bool wasMeasured() const { return status == Status::Measured; }
};

Result measure(const Input &input);

enum class Source { Decoder, Measured };
struct Decision
{
    Source source = Source::Decoder;
    unsigned int deviation = 0; // largest |decoder - measured| over the channels
};

// Largest decoder/measurement difference still taken as agreement, in codes.
unsigned int tolerance(int maximum);

// `decoder`: black per CFA channel reported by the decoder (common + channel).
// The measurement wins when the decoder's black deviates beyond tolerance(); a
// decoder spatial black pattern, which a per-channel median cannot represent,
// and a failed measurement always keep the decoder's value.
Decision decide(const std::array<unsigned int, 4> &decoder, bool decoderPattern,
                const Result &measured, int maximum);

QString statusName(Status status);

}

#endif // IPR_RAW_RAWBLACKLEVELESTIMATOR_H
