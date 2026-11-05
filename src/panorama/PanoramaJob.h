#ifndef IPR_PANORAMA_PANORAMAJOB_H
#define IPR_PANORAMA_PANORAMAJOB_H

#include "PanoramaGeometry.h"
#include "Types.h"

#include <QString>
#include <opencv2/core.hpp>

#include <functional>
#include <vector>

// One panorama from a series taken from the same place: one row, horizontal or vertical, or
// several rows, in any order (plans M3 and M4 of docs/architecture/empilement-et-panorama.md).
// Each frame is decoded once and kept in a scratch folder the job owns; the geometry works on
// reduced copies, and the canvas is written by bands, so that the memory held does not grow
// with the frame count. The sources are only read, through the provider.
namespace PanoramaJob {

struct Frame
{
    cv::Mat cameraRgb;     // CV_32FC3, linear, white 1, the same balance intended across the series
    double focalMm = 0.0;  // 0 when unknown
    double sensorWidthMm = 0.0;        // long side, 0 when unknown
    bool lensGeometryCorrected = false; // a lens profile has removed the distortion
    QString error;
};

// What becomes of the canvas's empty corners.
enum class Edges {
    Crop,  // a rectangle full of real pixels, low in the view
    Keep,  // the whole canvas, its corners empty
    Fill   // the whole canvas, its corners invented from the rest of the image
};

struct Request
{
    int frameCount = 0;
    // Called once per index, one at a time, on the job's thread.
    std::function<Frame(int index)> frame;
    PanoramaGeometry::Projection projection = PanoramaGeometry::Projection::Automatic;
    Edges edges = Edges::Crop;
    QString scratchRoot;                    // the system's temporary folder when empty
    quint64 memoryBudgetBytes = quint64(1) << 30; // of the bands held at once
    quint64 minimumFreeDiskBytes = quint64(2) << 30;
    double maximumMegapixels = 400.0;       // of the canvas: beyond it the job refuses and says so
    // Asked once the canvas's size is known; a non-empty answer stops the job with it.
    std::function<QString(quint64 bytes)> reserveCanvas;
    std::function<bool()> cancelled;
    std::function<void(int done, int total)> progress;
};

struct FrameReport
{
    int index = 0;
    bool used = false;
    QString exclusion;                     // why it was left out, empty when used
    int features = 0;
    ipr::Vec3d gains{1.0, 1.0, 1.0};
    ipr::Recti area;                         // of the canvas
};

struct PairReport
{
    int a = 0;
    int b = 0;
    int points = 0;
    double residual = 0.0;                 // RMS, pixels
};

struct Result
{
    bool success = false;
    bool cancelled = false;
    QString error;
    cv::Mat cameraRgb;                     // CV_32FC3
    ipr::Size frameSize;
    ipr::Size canvas;
    ipr::Recti kept;                         // of the canvas
    double coveredShare = 0.0;             // of the canvas that holds pixels
    double filledShare = 0.0;              // of the result that is invented
    std::vector<FrameReport> frames;
    std::vector<PairReport> pairs;
    int usedFrames = 0;
    PanoramaGeometry::Shape shape;
    double focal = 0.0;                    // pixels
    double distortion = 0.0;               // radial, at the frame's corner
    double residual = 0.0;                 // RMS over all pairs, pixels
    double worstPair = 0.0;
    int seamCellPixels = 0;                // canvas pixels per side of a seam's cell
    int bandRows = 0;
    quint64 scratchBytes = 0;              // written to the scratch folder
};

Result run(const Request &request);

} // namespace PanoramaJob

#endif // IPR_PANORAMA_PANORAMAJOB_H
