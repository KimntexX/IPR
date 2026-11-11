#ifndef IPR_RAW_RAWDEVELOPMENT_H
#define IPR_RAW_RAWDEVELOPMENT_H

#include "Demosaic.h"
#include "Image.h"
#include "RawInput.h"
#include "Types.h"

#include <QString>

#include <array>
#include <functional>

// IPR's development of a Bayer RAW, from a RawInput to linear camera RGB: black,
// white, white balance, demosaic. Policy A reproduces LibRaw's dcraw_process()
// exactly (parity mode of PLAN-SEPARATION-LIBRAW): black subtracted and clipped at
// 0, the white adjusted to the data unless IPR calibrated it, as-shot multipliers
// scaled so the smallest is 1, values truncated to the 16-bit grid, both greens
// merged, IPR's RCD and median (interpolated values bounded at 0), output on the
// 16-bit grid. Policy B keeps the values below the black through RCD, the median
// and the output, without the 16-bit grid (mode B, under evaluation by the bench).
namespace RawDevelopment {

enum class Policy {
    LibRawParity,   // A
    Signed          // B
};

// Intermediate stages the comparison bench observes (interface I2).
enum class Stage {
    UnpackedCfa,            // sensor counts (U16)
    BlackSubtractedCfa,     // counts minus black, signed, before the lower bound (F32)
    NormalizedCfa,          // divided by the effective white (F32), before the gains; A clips at 0 first
    PreDemosaicCfa,         // the mosaic RCD reads (F32, white 1)
    PostDemosaicCameraRgb,  // RCD before the median (F32, three channels)
    PreOutputCameraRgb,     // after the median, before A's output bound and grid; B: the output (F32, three channels)
    PostMedianCameraRgb     // the development's output (F32, three channels); A: counts k as float(k) * float(1 / 65535)
};

struct Context
{
    ipr::Point2i origin;                // the captures' (0, 0) in the sensor frame (RawInput::Input::origin)
    std::array<double, 4> black{};   // per RawInput::Colour, counts
    double white = 0.0;              // effective white, counts (after the data adjustment)
    double divisor = 0.0;            // counts dividing the black-subtracted values
    std::array<double, 4> gains{};   // white-balance gains applied, smallest 1
    Policy policy = Policy::LibRawParity;
};

struct Capture
{
    Stage stage;
    const ipr::Image &pixels;                            // whole image, sensor orientation, valid during the call
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
    // The CFA denoising stage (empty in the default opening): called once on the
    // black-subtracted mosaic, signed and in counts, before any bound and before the
    // white balance, with the 2x2 colour pattern. It rewrites the mosaic in place;
    // false fails the development with its message (a stage that cannot run keeps the
    // mosaic and returns true). Runs on the developing thread.
    std::function<bool(ipr::Image &mosaic, const std::array<std::array<int, 2>, 2> &pattern, QString *error)> cfaStage;
    // The comparison bench's observer: called once per stage, after it, on the
    // developing thread; an exception fails the development. Empty in the app.
    std::function<void(const Capture &)> observer;
};

struct Result
{
    ipr::Image cameraRgb;              // F32, three channels, white 1, sensor orientation; empty on failure
    std::array<double, 4> gains{};     // white-balance gains applied (smallest 1)
    QString error;
};

Result develop(const RawInput::Input &input, const Options &options);

// The image turned as the decoder's orientation code (RawInput::Input::orientation): the
// same image for code 0, new pixels otherwise.
ipr::Image orient(ipr::Image image, int orientation);

} // namespace RawDevelopment

#endif // IPR_RAW_RAWDEVELOPMENT_H
