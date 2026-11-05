#ifndef IPR_PANORAMA_PANORAMABLEND_H
#define IPR_PANORAMA_PANORAMABLEND_H

#include "Types.h"

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

// Joining the warped frames, on a reduced copy of the canvas (one cell per scale * scale
// pixels). Where the seams run is decided there; each frame then contributes its fine detail
// up to its seam, and its slow variations over a wide transition, so that a difference of
// vignetting or of light does not show as a step and the detail does not double.
namespace PanoramaBlend {

// A frame on the reduced canvas.
struct View
{
    ipr::Recti area;       // cells of the reduced canvas
    cv::Mat colour;      // CV_32FC3, the mean of each cell's valid pixels
    cv::Mat whole;       // CV_8UC1, 255 where all of the cell's pixels are valid
    cv::Mat any;         // CV_8UC1, 255 where one of them is
    ipr::Vec3d gains{1.0, 1.0, 1.0};
};

// One gain per view and channel, from the overlaps: the differences of level between views
// are shared out, with a weak pull towards 1 so that the series keeps its exposure.
void levelGains(std::vector<View> &views);

// The frame each cell comes from, CV_16SC1, -1 where none. Each view in turn takes its share
// by a minimal cut through the overlap with what is already there, along the smallest
// visible difference. Empty when cancelled.
cv::Mat seams(const std::vector<View> &views, const ipr::Size &canvas, const std::function<bool()> &cancelled = {});

// An upright rectangle full of the mask, as low as it can stand without giving up much of the
// largest area: a series leans on the bottom of its view, so a crop takes from the top.
ipr::Recti lowRectangle(const cv::Mat &mask);

// What a frame brings to the canvas.
struct Layer
{
    ipr::Recti area;       // cells of the reduced canvas
    cv::Mat low;         // CV_32FC3: slow variations, gain included
    cv::Mat wide;        // CV_32FC1: weight of the slow variations
    cv::Mat narrow;      // CV_32FC1: weight of the detail
    ipr::Vec3f gains{1.0f, 1.0f, 1.0f};
};

// sigmas in cells.
Layer prepareLayer(const View &view, const cv::Mat &labels, int label, double narrowSigma, double wideSigma);

// One frame's pixels over a part of the canvas.
struct Pixels
{
    const Layer *layer = nullptr;
    ipr::Recti area;       // canvas pixels
    cv::Mat colour;      // CV_32FC3
    cv::Mat valid;       // CV_8UC1
};

// Fills target (CV_32FC3, the size of band) from the frames that cover band, a part of the
// canvas in pixels. scale: pixels per cell. A pixel no frame covers is 0.
void blendBand(const std::vector<Pixels> &pixels, int scale, const ipr::Recti &band, cv::Mat &target);

} // namespace PanoramaBlend

#endif // IPR_PANORAMA_PANORAMABLEND_H
