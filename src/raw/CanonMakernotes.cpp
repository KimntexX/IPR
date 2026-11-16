#include "CanonMakernotes.h"

#include <libraw/libraw.h>

#include <cstdio>
#include <utility>

namespace CanonMakernotes {
namespace {

constexpr int ShortType = 3;          // TIFF int16u
constexpr int FarIndex = 20;
constexpr int NearIndex = 21;
constexpr std::uint16_t Infinity = 0xffff;
constexpr unsigned int LittleEndian = 0x4949;   // "II"
constexpr int MaximumCount = 1024;              // FileInfo holds a few dozen entries

} // namespace

std::optional<FocusRange> focusRangeFromFileInfo(const std::vector<std::uint16_t> &values)
{
    if (values.size() <= std::size_t(NearIndex))
        return std::nullopt;
    const std::uint16_t farLimit = values[FarIndex];
    const std::uint16_t nearLimit = values[NearIndex];
    if (nearLimit == 0 || nearLimit == Infinity || (farLimit != Infinity && (farLimit == 0 || nearLimit > farLimit)))
        return std::nullopt;
    FocusRange range;
    range.nearM = nearLimit / 100.0;
    range.farInfinite = farLimit == Infinity;
    range.farM = range.farInfinite ? 0.0 : farLimit / 100.0;
    return range;
}

double profileFocusDistanceM(const std::optional<FocusRange> &range)
{
    if (!range || range->farInfinite)
        return 0.0;
    return 0.5 * (range->nearM + range->farM);
}

void readFileInfo(void *context, int tag, int type, int count, unsigned int order,
                  void *stream, long long)
{
    auto *capture = static_cast<FileInfoCapture *>(context);
    auto *input = static_cast<LibRaw_abstract_datastream *>(stream);
    if (!capture || !input || tag != FileInfoTag || type != ShortType ||
        count <= NearIndex || count > MaximumCount || !capture->values.empty())
        return;
    std::vector<std::uint16_t> values(static_cast<std::size_t>(count));
    const INT64 position = input->tell();
    const int itemsRead = input->read(values.data(), sizeof(std::uint16_t), count);
    // LibRaw reads on from its own position after the callback.
    input->seek(position, SEEK_SET);
    if (itemsRead != count)
        return;
    const bool little = order == LittleEndian;
    for (std::uint16_t &value : values) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
        value = little ? std::uint16_t(bytes[0] | (bytes[1] << 8))
                       : std::uint16_t((bytes[0] << 8) | bytes[1]);
    }
    capture->values = std::move(values);
}

} // namespace CanonMakernotes
