#include "CanonMakernotes.h"

#include <libraw/libraw.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
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

std::vector<std::uint16_t> fileInfo(std::uint16_t farLimit, std::uint16_t nearLimit, std::size_t count = 27)
{
    std::vector<std::uint16_t> values(count, 0);
    values[0] = std::uint16_t(count * 2);
    if (count > 21) {
        values[20] = farLimit;
        values[21] = nearLimit;
    }
    return values;
}

// The record as LibRaw's stream holds it, after `prefix` bytes, in the file's byte order.
std::vector<unsigned char> encoded(const std::vector<std::uint16_t> &values, bool little, std::size_t prefix)
{
    std::vector<unsigned char> bytes(prefix, 0xab);
    for (std::uint16_t value : values) {
        const unsigned char low = value & 0xff, high = value >> 8;
        bytes.push_back(little ? low : high);
        bytes.push_back(little ? high : low);
    }
    return bytes;
}

bool approx(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main()
{
    using namespace CanonMakernotes;

    // The 60D's usual record: from 25.21 m to infinity, the profile's farthest entry.
    const auto infinite = focusRangeFromFileInfo(fileInfo(0xffff, 2521));
    if (!check(infinite && infinite->farInfinite && approx(infinite->nearM, 25.21),
               "a range reaching infinity keeps its near limit")) return 1;
    if (!check(profileFocusDistanceM(infinite) == 0.0, "a range reaching infinity selects the farthest entry")) return 1;

    // A finite range, read on a 60D portrait (3.06 to 3.38 m): its middle.
    const auto finite = focusRangeFromFileInfo(fileInfo(338, 306));
    if (!check(finite && !finite->farInfinite && approx(finite->farM, 3.38) && approx(finite->nearM, 3.06),
               "a finite range is read in centimetres")) return 1;
    if (!check(approx(profileFocusDistanceM(finite), 3.22), "a finite range is used by its middle")) return 1;

    if (!check(!focusRangeFromFileInfo(fileInfo(0, 0)), "zeros hold no distance")) return 1;
    if (!check(!focusRangeFromFileInfo(fileInfo(300, 400)), "a near limit beyond the far one is refused")) return 1;
    if (!check(!focusRangeFromFileInfo(fileInfo(0xffff, 0xffff)), "a near limit at infinity is refused")) return 1;
    if (!check(!focusRangeFromFileInfo(fileInfo(0, 0, 21)), "a record without index 21 is refused")) return 1;
    if (!check(!focusRangeFromFileInfo({}), "an empty record is refused")) return 1;
    if (!check(profileFocusDistanceM(std::nullopt) == 0.0, "an unknown range selects the farthest entry")) return 1;

    // LibRaw's callback, on its own buffer stream positioned on the value, in both byte orders.
    const std::vector<std::uint16_t> record = fileInfo(880, 740, 91);
    for (const bool little : {true, false}) {
        std::vector<unsigned char> bytes = encoded(record, little, 16);
        LibRaw_buffer_datastream stream(bytes.data(), bytes.size());
        stream.seek(16, SEEK_SET);
        FileInfoCapture capture;
        readFileInfo(&capture, FileInfoTag, 3, int(record.size()), little ? 0x4949 : 0x4d4d, &stream, 0);
        if (!check(capture.values == record, "the callback reads FileInfo in the file's byte order")) return 1;
        if (!check(stream.tell() == 16, "the callback leaves the stream where LibRaw had it")) return 1;
        if (!check(approx(profileFocusDistanceM(focusRangeFromFileInfo(capture.values)), 8.10),
                   "the record read gives the middle of its range")) return 1;

        // A CR3 carries the record twice: the first one is kept.
        std::vector<unsigned char> other = encoded(fileInfo(100, 50, 91), little, 0);
        LibRaw_buffer_datastream second(other.data(), other.size());
        readFileInfo(&capture, FileInfoTag, 3, 91, little ? 0x4949 : 0x4d4d, &second, 0);
        if (!check(capture.values == record, "a second record does not replace the first")) return 1;
    }

    {
        std::vector<unsigned char> bytes = encoded(record, true, 0);
        LibRaw_buffer_datastream stream(bytes.data(), bytes.size());
        FileInfoCapture capture;
        readFileInfo(&capture, 0x0004, 3, int(record.size()), 0x4949, &stream, 0);
        readFileInfo(&capture, FileInfoTag, 4, int(record.size()), 0x4949, &stream, 0);
        readFileInfo(&capture, FileInfoTag, 3, 21, 0x4949, &stream, 0);
        if (!check(capture.values.empty(), "another tag, type or a short record is ignored")) return 1;
        readFileInfo(&capture, FileInfoTag, 3, int(record.size()) + 10, 0x4949, &stream, 0);
        if (!check(capture.values.empty(), "a record longer than the stream is ignored")) return 1;
        readFileInfo(nullptr, FileInfoTag, 3, int(record.size()), 0x4949, &stream, 0);
        readFileInfo(&capture, FileInfoTag, 3, int(record.size()), 0x4949, nullptr, 0);
        if (!check(capture.values.empty(), "a missing context or stream is ignored")) return 1;
    }

    std::cout << "Canon makernote tests passed\n";
    return 0;
}
