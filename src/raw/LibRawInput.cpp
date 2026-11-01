#include "LibRawInput.h"

#ifdef IPR_WITH_LIBRAW
#include <libraw/libraw.h>
#endif

namespace LibRawInput {

RawInput::Input read(const LibRaw &raw)
{
    RawInput::Input input;
#ifdef IPR_WITH_LIBRAW
    const auto &d = raw.imgdata;
    if (!d.rawdata.raw_image || d.idata.filters < 1000)   // no Bayer mosaic (X-Trans, sRAW, linear DNG)
        return input;
    const auto &s = d.sizes;
    const cv::Mat sensor(int(s.raw_height), int(s.raw_width), CV_16UC1, d.rawdata.raw_image, size_t(s.raw_pitch));
    input.origin = cv::Point(int(s.left_margin), int(s.top_margin));
    input.mosaic = sensor(cv::Rect(input.origin, cv::Size(int(s.width), int(s.height)))).clone();
    // Colour of each site of the visible 2x2 cell, as LibRaw's FC(): 3 is the cell's second green.
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            input.pattern[size_t(y)][size_t(x)] = int(d.idata.filters >> ((((y << 1) & 14) | (x & 1)) << 1) & 3);
    const auto &c = d.color;
    for (int colour = 0; colour < 4; ++colour) {
        input.black[size_t(colour)] = double(c.black) + double(c.cblack[colour]);
        input.asShotMultipliers[size_t(colour)] = double(c.cam_mul[colour]);
        input.daylightMultipliers[size_t(colour)] = double(c.pre_mul[colour]);
    }
    // A second-green multiplier left at 0 means "same as the first green".
    for (auto *multipliers : {&input.asShotMultipliers, &input.daylightMultipliers})
        if (!((*multipliers)[RawInput::Green2] > 0.0)) (*multipliers)[RawInput::Green2] = (*multipliers)[RawInput::Green];
    input.spatialBlack = c.cblack[4] > 0 && c.cblack[5] > 0;
    input.white = double(c.maximum);
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            input.cameraToSrgb(r, k) = double(c.rgb_cam[r][k]);
    input.orientation = s.flip;
    input.make = QString::fromLocal8Bit(d.idata.make).trimmed();
    input.model = QString::fromLocal8Bit(d.idata.model).trimmed();
    input.iso = double(d.other.iso_speed);
#else
    Q_UNUSED(raw);
#endif
    return input;
}

} // namespace LibRawInput
