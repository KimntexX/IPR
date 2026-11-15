#ifndef IPR_RAW_CANONMAKERNOTES_H
#define IPR_RAW_CANONMAKERNOTES_H

#include <cstdint>
#include <optional>
#include <vector>

// Canon makernote fields LibRaw does not expose. The focus distance chooses the
// lens profile (LCP) entry: without it every Canon photo took the farthest one.
// Layout from the public Canon tag tables (FileInfo, makernote tag 0x0093, a
// count-prefixed int16u array): index 20 is the far limit of the focus range,
// index 21 the near one, in centimetres, 65535 meaning infinity. Read on the
// EOS 60D (CR2), R6 Mark III and R7 (CR3), whose ShotInfo copy is empty or
// in the other order on some bodies.
namespace CanonMakernotes {

constexpr int FileInfoTag = 0x0093;

struct FocusRange
{
    double nearM = 0.0;
    double farM = 0.0;       // meaningless when farInfinite
    bool farInfinite = false;
};

// The focus range of a FileInfo record, in host order. Empty when the record is
// too short or holds no distance (zeros, or a near limit beyond the far one).
std::optional<FocusRange> focusRangeFromFileInfo(const std::vector<std::uint16_t> &values);

// The distance a lens profile entry is chosen by: the middle of a finite range;
// 0 when unknown or reaching infinity, which selects the profile's farthest entry.
double profileFocusDistanceM(const std::optional<FocusRange> &range);

// What LibRaw's makernotes callback collected: the first FileInfo record seen.
struct FileInfoCapture
{
    std::vector<std::uint16_t> values;
};

// LibRaw makernotes callback (LibRaw::set_makernotes_handler); `context` is a
// FileInfoCapture and `stream` LibRaw's datastream, positioned on the value.
// The tag numbers belong to the maker: use the result only for a Canon file.
void readFileInfo(void *context, int tag, int type, int count, unsigned int order,
                  void *stream, long long base);

} // namespace CanonMakernotes

#endif // IPR_RAW_CANONMAKERNOTES_H
