#ifndef IPR_DETECT_LINEDETECTION_H
#define IPR_DETECT_LINEDETECTION_H

#include <cstddef>
#include <vector>

// The straight lines of an image, without a network or an image library: pixels of a strong
// gradient grow into regions of one direction, and a region long, thin and dense enough is a
// segment. Deterministic. Standard C++ only, on any buffer of floats.
namespace LineDetection {

struct Image
{
    const float *pixels = nullptr;  // perceptual luminance: a step weighs the same in shadows and lights
    int width = 0;
    int height = 0;
    std::ptrdiff_t stride = 0;      // in floats, from one row to the next
};

struct Segment
{
    // Pixel indices of the input (a pixel's centre is its index), sub-pixel.
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float length = 0;
    float contrast = 0;   // mean gradient along it, per pixel of the reduced image
};

struct Options
{
    int workingSide = 1600;             // the image is reduced by a whole factor to at most this
    float minimumLengthShare = 0.04f;   // of the reduced image's long side
    float noiseFactor = 3.0f;           // a strong gradient, in medians of the gradients
    float minimumGradient = 0.004f;     // however flat the image
    float directionTolerance = 22.5f;   // degrees, between a pixel and its region
    float maximumWander = 0.8f;         // pixels of the reduced image, RMS of its middle from a straight line
    float maximumWidth = 4.0f;          // pixels of the reduced image, RMS across: an edge may be blurred
    float mergeAngle = 1.5f;            // degrees, between two parts of one line
    float mergeDistance = 4.0f;         // pixels of the reduced image, across: both sides of a thin line
    float mergeGapShare = 0.03f;        // of the long side, along
    // A line behind an obstacle, such as a horizon behind a mast: farther, but better aligned.
    float farGapShare = 0.35f;          // of the long side, along
    float farAngle = 0.5f;              // degrees
    float farDistance = 1.5f;           // pixels of the reduced image, across
    int maximumSegments = 400;          // the longest are kept
};

// Longest first. Empty on an invalid or unusable image: null pixels, a side under 16 pixels, a
// stride under the width. A non-finite pixel counts as no information, and draws no line.
std::vector<Segment> detect(const Image &image, const Options &options = {});

} // namespace LineDetection

#endif // IPR_DETECT_LINEDETECTION_H
