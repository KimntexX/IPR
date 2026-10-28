#ifndef IPR_RAW_RAWDEVELOPMENT_H
#define IPR_RAW_RAWDEVELOPMENT_H

#include "Demosaic.h"
#include "RawInput.h"

#include <QString>
#include <opencv2/core.hpp>

#include <array>
#include <functional>

// IPR's development of a Bayer RAW, from a RawInput to linear camera RGB: black,
// white, white balance, demosaic. It reproduces LibRaw's dcraw_process() exactly
// (parity mode A of PLAN-SEPARATION-LIBRAW): black subtracted and clipped at 0,
// the white adjusted to the data unless IPR calibrated it, as-shot multipliers
// scaled so the smallest is 1, values truncated to the 16-bit grid, both greens
// merged, IPR's RCD and median, output on the 16-bit grid.
namespace RawDevelopment {

// Intermediate stages the comparison bench observes (interface I2).
enum class Stage {
    UnpackedCfa,            // sensor counts (CV_16UC1)
    BlackSubtractedCfa,     // counts minus black, signed, before the lower bound (CV_32FC1)
    NormalizedCfa,          // after the lower bound, divided by the effective white (CV_32FC1), before the gains
    PreDemosaicCfa,         // the mosaic RCD reads (CV_32FC1, white 1)
    PostDemosaicCameraRgb,  // RCD before the median (CV_32FC3)
    PostMedianCameraRgb     // after the median, the development's output (CV_32FC3)
};

struct Context
{
    cv::Point origin;                // the captures' (0, 0) in the sensor frame (RawInput::Input::origin)
    std::array<double, 4> black{};   // per RawInput::Colour, counts
    double white = 0.0;              // effective white, counts (after the data adjustment)
    double divisor = 0.0;            // counts dividing the black-subtracted values
    std::array<double, 4> gains{};   // white-balance gains applied, smallest 1
    const char *policy = "A: black clipped at 0, gains truncated to the 16-bit grid, output on the 16-bit grid";
};

struct Capture
{
    Stage stage;
    const cv::Mat &pixels;                               // whole image, sensor orientation, valid during the call
    bool mosaic;                                         // CFA (pattern below) or RGB
    const std::array<std::array<int, 2>, 2> &pattern;    // RawInput::Colour of each 2x2 site
    const Context &context;
};

struct Options
{
    // LibRaw lowers the white to the data maximum when it lies above this share of
    // it; 0 keeps the white as given (IPR's calibration sets it).
    double adjustWhiteThreshold = 0.75;
    Demosaic::ChromaMedian median = Demosaic::OpeningMedian;
    std::function<bool()> cancelled;
    // The comparison bench's observer: called once per stage, after it, on the
    // developing thread; an exception fails the development. Empty in the app.
    std::function<void(const Capture &)> observer;
};

struct Result
{
    cv::Mat cameraRgb;                 // CV_32FC3, white 1, sensor orientation; empty on failure
    std::array<double, 4> gains{};     // white-balance gains applied (smallest 1)
    QString error;
};

Result develop(const RawInput::Input &input, const Options &options);

// The image turned as the decoder's orientation code (RawInput::Input::orientation).
cv::Mat orient(const cv::Mat &image, int orientation);

} // namespace RawDevelopment

#endif // IPR_RAW_RAWDEVELOPMENT_H
