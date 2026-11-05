#include "PanoramaFeatures.h"

#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace PanoramaFeatures {
namespace {

constexpr int grid = 8;                  // samples of a descriptor per side
constexpr float spacing = 5.0f;          // between two samples, pixels of the level
constexpr double sampleBlur = 2.0;       // so that a sample stands for its whole spacing
constexpr double orientationBlur = 4.5;
constexpr float flatDeviation = 1e-3f;   // a patch below this has nothing to describe
constexpr double harrisBlur = 1.5;
constexpr float harrisK = 0.04f;
constexpr float harrisQuality = 0.0005f; // of the strongest corner

inline float bilinear(const cv::Mat &image, float x, float y)
{
    const int x0 = int(x), y0 = int(y);
    const float dx = x - float(x0), dy = y - float(y0);
    const float *top = image.ptr<float>(y0), *bottom = image.ptr<float>(y0 + 1);
    return (1.0f - dy) * ((1.0f - dx) * top[x0] + dx * top[x0 + 1]) + dy * ((1.0f - dx) * bottom[x0] + dx * bottom[x0 + 1]);
}

// Harris corners, the strongest first, none too close to a stronger one so that a busy part
// of the image does not take them all.
std::vector<cv::Point2f> cornersOf(const cv::Mat &level, int wanted)
{
    cv::Mat gradientX, gradientY;
    gradientX = ipr::sobel(level, true);
    gradientY = ipr::sobel(level, false);
    cv::Mat xx, yy, xy;
    xx = ipr::multiplied(gradientX, gradientX);
    yy = ipr::multiplied(gradientY, gradientY);
    xy = ipr::multiplied(gradientX, gradientY);
    xx = ipr::gaussianBlurred(xx, harrisBlur, ipr::BlurBorder::Mirror);
    yy = ipr::gaussianBlurred(yy, harrisBlur, ipr::BlurBorder::Mirror);
    xy = ipr::gaussianBlurred(xy, harrisBlur, ipr::BlurBorder::Mirror);

    cv::Mat response(level.size(), CV_32FC1);
    float strongest = 0.0f;
    for (int y = 0; y < level.rows; ++y) {
        const float *a = xx.ptr<float>(y), *b = yy.ptr<float>(y), *c = xy.ptr<float>(y);
        float *out = response.ptr<float>(y);
        for (int x = 0; x < level.cols; ++x) {
            const float trace = a[x] + b[x];
            out[x] = a[x] * b[x] - c[x] * c[x] - harrisK * trace * trace;
            strongest = std::max(strongest, out[x]);
        }
    }
    std::vector<cv::Point2f> corners;
    if (strongest <= 0.0f)
        return corners;

    struct Candidate
    {
        float strength;
        int x;
        int y;
    };
    std::vector<Candidate> candidates;
    const float weakest = strongest * harrisQuality;
    for (int y = 1; y < level.rows - 1; ++y) {
        const float *above = response.ptr<float>(y - 1), *row = response.ptr<float>(y), *below = response.ptr<float>(y + 1);
        for (int x = 1; x < level.cols - 1; ++x) {
            const float value = row[x];
            if (value < weakest || value < row[x - 1] || value <= row[x + 1] || value < above[x - 1] || value < above[x] ||
                value < above[x + 1] || value <= below[x - 1] || value <= below[x] || value <= below[x + 1])
                continue;
            candidates.push_back({value, x, y});
        }
    }
    // Equal strengths keep the order of the rows, so that the result does not depend on the sort.
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.strength > b.strength; });

    const int spacingCells = std::max(4, std::max(level.cols, level.rows) / 200);
    const int gridWidth = level.cols / spacingCells + 1, gridHeight = level.rows / spacingCells + 1;
    std::vector<char> taken(size_t(gridWidth) * size_t(gridHeight), 0);
    for (const Candidate &candidate : candidates) {
        const int cellX = candidate.x / spacingCells, cellY = candidate.y / spacingCells;
        char &slot = taken[size_t(cellY) * size_t(gridWidth) + size_t(cellX)];
        if (slot)
            continue;
        slot = 1;
        corners.emplace_back(float(candidate.x), float(candidate.y));
        if (int(corners.size()) >= wanted)
            break;
    }
    return corners;
}

void detectOnLevel(const cv::Mat &level, float toImage, const Options &options, std::vector<Feature> &features)
{
    const std::vector<cv::Point2f> corners = cornersOf(level, options.cornersPerLevel);

    cv::Mat sampled, smooth, gradientX, gradientY;
    sampled = ipr::gaussianBlurred(level, sampleBlur, ipr::BlurBorder::Mirror);
    smooth = ipr::gaussianBlurred(level, orientationBlur, ipr::BlurBorder::Mirror);
    gradientX = ipr::sobel(smooth, true);
    gradientY = ipr::sobel(smooth, false);

    // The patch can turn in any direction: its corners stay inside with this margin.
    const float reach = spacing * float(grid) * 0.5f * std::sqrt(2.0f) + 2.0f;
    for (const cv::Point2f &corner : corners) {
        if (corner.x < reach || corner.y < reach || corner.x >= float(level.cols) - reach || corner.y >= float(level.rows) - reach)
            continue;
        const int cx = int(corner.x + 0.5f), cy = int(corner.y + 0.5f);
        const float gx = gradientX.at<float>(cy, cx), gy = gradientY.at<float>(cy, cx);
        const float length = std::hypot(gx, gy);
        if (length <= 0.0f)
            continue;
        const float cosine = gx / length, sine = gy / length;
        Feature feature;
        float mean = 0.0f;
        for (int j = 0; j < grid; ++j) {
            for (int i = 0; i < grid; ++i) {
                const float u = (float(i) - float(grid - 1) * 0.5f) * spacing;
                const float v = (float(j) - float(grid - 1) * 0.5f) * spacing;
                const float value = bilinear(sampled, corner.x + cosine * u - sine * v, corner.y + sine * u + cosine * v);
                feature.descriptor[size_t(j * grid + i)] = value;
                mean += value;
            }
        }
        mean /= float(grid * grid);
        float energy = 0.0f;
        for (float &value : feature.descriptor) {
            value -= mean;
            energy += value * value;
        }
        const float deviation = std::sqrt(energy / float(grid * grid));
        if (deviation < flatDeviation)
            continue;
        for (float &value : feature.descriptor)
            value /= deviation;
        feature.position = corner * toImage;
        features.push_back(feature);
    }
}

// For each of from, the index of its closest in to, -1 unless clearly the closest.
std::vector<int> closest(const std::vector<Feature> &from, const std::vector<Feature> &to, double ratio)
{
    std::vector<int> best(from.size(), -1);
    const float squaredRatio = float(ratio * ratio);
    cv::parallel_for_(cv::Range(0, int(from.size())), [&](const cv::Range &range) {
        for (int i = range.start; i < range.end; ++i) {
            const float *a = from[size_t(i)].descriptor.data();
            float first = std::numeric_limits<float>::max(), second = first;
            int found = -1;
            for (size_t j = 0; j < to.size(); ++j) {
                const float *b = to[j].descriptor.data();
                float distance = 0.0f;
                for (int k = 0; k < grid * grid; ++k) {
                    const float difference = a[k] - b[k];
                    distance += difference * difference;
                }
                if (distance < first) {
                    second = first;
                    first = distance;
                    found = int(j);
                } else if (distance < second) {
                    second = distance;
                }
            }
            if (found >= 0 && first < squaredRatio * second)
                best[size_t(i)] = found;
        }
    });
    return best;
}

} // namespace

std::vector<Feature> detect(const cv::Mat &image, const Options &options)
{
    std::vector<Feature> features;
    if (image.empty() || image.type() != CV_32FC1)
        return features;
    // An undefined pixel must not become an undefined place.
    cv::Mat level = image.clone();
    ipr::replaceNaN(level, 0.0f);
    float toImage = 1.0f;
    for (int index = 0; index < options.levels && std::min(level.cols, level.rows) > grid * int(spacing) * 2; ++index) {
        detectOnLevel(level, toImage, options, features);
        level = ipr::pyramidDown(level);
        toImage *= 2.0f;
    }
    return features;
}

std::vector<Correspondence> match(const std::vector<Feature> &a, const std::vector<Feature> &b, const Options &options)
{
    std::vector<Correspondence> matches;
    if (a.empty() || b.empty())
        return matches;
    const std::vector<int> forward = closest(a, b, options.ratio);
    const std::vector<int> backward = closest(b, a, options.ratio);
    for (size_t i = 0; i < a.size(); ++i) {
        const int j = forward[i];
        if (j >= 0 && backward[size_t(j)] == int(i))
            matches.push_back({a[i].position, b[size_t(j)].position});
    }
    return matches;
}

} // namespace PanoramaFeatures
