#ifndef IPR_RAW_RAWDEVELOPMENT_H
#define IPR_RAW_RAWDEVELOPMENT_H

#include "Demosaic.h"
#include "RawInput.h"

#include <QString>
#include <opencv2/core.hpp>

#include <array>
#include <functional>

// IPR's development of a Bayer RAW, from a RawInput to linear camera RGB: black,
// white, white balance, demosaic. Policy A reproduces LibRaw's dcraw_process()
// exactly (parity mode of PLAN-SEPARATION-LIBRAW): black subtracted and clipped at
// 0, the white adjusted to the data unless IPR calibrated it, as-shot multipliers
// scaled so the smallest is 1, values truncated to the 16-bit grid, both greens
// merged, IPR's RCD and median, output on the 16-bit grid. Policy B keeps the
// values below the black through RCD and the median, without the 16-bit grid,
// and clips them at 0 only on output (mode B, under evaluation by the bench).
namespace RawDevelopment {

enum class Policy {
    LibRawParity,   // A
    Signed          // B
};

// Intermediate stages the comparison bench observes (interface I2).
enum class Stage {
    UnpackedCfa,            // sensor counts (CV_16UC1)
    BlackSubtractedCfa,     // counts minus black, signed, before the lower bound (CV_32FC1)
    NormalizedCfa,          // divided by the effective white (CV_32FC1), before the gains; A clips at 0 first
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
    Policy policy = Policy::LibRawParity;
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
    Policy policy = Policy::LibRawParity;
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
