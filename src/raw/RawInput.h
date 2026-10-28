#ifndef IPR_RAW_RAWINPUT_H
#define IPR_RAW_RAWINPUT_H

#include <QString>
#include <opencv2/core.hpp>

#include <array>

// What IPR's development needs of a Bayer RAW, whichever decoder read it: the
// visible mosaic in sensor counts and the levels and colours that go with it.
// Filled from LibRaw after unpack() today (LibRawInput), by IPR's own decoders
// later; the development (black, scaling, white balance, demosaic) is IPR's.
// Shared plan: PLAN-SEPARATION-LIBRAW.md, interface I1.
namespace RawInput {

// Colour of a mosaic site. The second green of the 2x2 cell is kept apart: its
// black level and noise may differ from the first green's.
enum Colour { Red = 0, Green = 1, Blue = 2, Green2 = 3 };

struct Input
{
    cv::Mat mosaic;                                // CV_16UC1, visible area only, sensor counts
    std::array<std::array<int, 2>, 2> pattern{};  // Colour of each site of the 2x2 cell, row then column
    std::array<double, 4> black{};                 // per Colour, counts (IPR's optical black reading applied)
    bool spatialBlack = false;                     // the decoder declares a black varying across the cell grid
    double white = 0.0;                            // counts (IPR's calibration applied when it applies)
    std::array<double, 4> asShotMultipliers{};     // camera white balance, per Colour
    std::array<double, 4> daylightMultipliers{};   // decoder's daylight balance, per Colour
    cv::Matx33d cameraToSrgb = cv::Matx33d::eye(); // linear camera RGB to linear sRGB, for files without profile
    int orientation = 0;                           // decoder flip code: 0 none, 3 180°, 5 90° anticlockwise, 6 90° clockwise
    QString make;
    QString model;
    double iso = 0.0;
};

// True when IPR's development can take the input: a non-empty 16-bit mosaic, a
// Bayer pattern with one red, one blue and two greens, the white above every
// black, no spatial black, positive as-shot multipliers. Otherwise the decoder's
// own development is used.
bool valid(const Input &input);

} // namespace RawInput

#endif // IPR_RAW_RAWINPUT_H
