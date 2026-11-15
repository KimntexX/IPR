#include "LineDetection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace LineDetection {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int gradientBins = 1024;
constexpr double piecePixels = 8.0;
constexpr int maximumPieces = 512;

struct Plane
{
    int width = 0;
    int height = 0;
    std::vector<float> values;

    float at(int x, int y) const { return values[size_t(y) * size_t(width) + size_t(x)]; }
};

// The average of each block; a non-finite pixel does not count, and a block without any is 0.
Plane reduced(const Image &image, int factor)
{
    Plane out;
    out.width = image.width / factor;
    out.height = image.height / factor;
    out.values.resize(size_t(out.width) * size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            float sum = 0;
            int count = 0;
            for (int j = 0; j < factor; ++j) {
                const float *row = image.pixels + std::ptrdiff_t(y * factor + j) * image.stride + x * factor;
                for (int i = 0; i < factor; ++i)
                    if (std::isfinite(row[i])) {
                        sum += row[i];
                        ++count;
                    }
            }
            out.values[size_t(y) * size_t(out.width) + size_t(x)] = count ? sum / float(count) : 0.0f;
        }
    return out;
}

// A binomial filter of five taps on both axes; the borders repeat.
Plane smoothed(const Plane &in)
{
    static constexpr std::array<float, 5> taps{1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f};
    Plane across = in, out = in;
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x) {
            float sum = 0;
            for (int k = -2; k <= 2; ++k)
                sum += taps[size_t(k + 2)] * in.at(std::clamp(x + k, 0, in.width - 1), y);
            across.values[size_t(y) * size_t(in.width) + size_t(x)] = sum;
        }
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x) {
            float sum = 0;
            for (int k = -2; k <= 2; ++k)
                sum += taps[size_t(k + 2)] * across.at(x, std::clamp(y + k, 0, in.height - 1));
            out.values[size_t(y) * size_t(in.width) + size_t(x)] = sum;
        }
    return out;
}

struct Gradients
{
    std::vector<float> magnitude;
    std::vector<float> doubledCos;   // of twice the edge's direction: a direction has no sign
    std::vector<float> doubledSin;
    float largest = 0;
    float median = 0;
};

Gradients gradientsOf(const Plane &plane)
{
    const size_t count = plane.values.size();
    Gradients out;
    out.magnitude.assign(count, 0.0f);
    out.doubledCos.assign(count, 0.0f);
    out.doubledSin.assign(count, 0.0f);
    std::vector<float> strengths;
    strengths.reserve(count);
    // The borders stay at zero: the smoothing leans on repeated pixels there.
    for (int y = 2; y < plane.height - 2; ++y)
        for (int x = 2; x < plane.width - 2; ++x) {
            const float dx = 0.5f * (plane.at(x + 1, y) - plane.at(x - 1, y));
            const float dy = 0.5f * (plane.at(x, y + 1) - plane.at(x, y - 1));
            const float square = dx * dx + dy * dy;
            const float magnitude = std::sqrt(square);
            strengths.push_back(magnitude);
            if (square <= 0.0f)
                continue;
            const size_t index = size_t(y) * size_t(plane.width) + size_t(x);
            out.magnitude[index] = magnitude;
            // The edge runs across its gradient: twice its angle is the gradient's plus a half turn.
            out.doubledCos[index] = (dy * dy - dx * dx) / square;
            out.doubledSin[index] = -2.0f * dx * dy / square;
            out.largest = std::max(out.largest, magnitude);
        }
    if (!strengths.empty()) {
        const auto middle = strengths.begin() + std::ptrdiff_t(strengths.size() / 2);
        std::nth_element(strengths.begin(), middle, strengths.end());
        out.median = *middle;
    }
    return out;
}

// Strongest first, in a fixed order, the seeds of the regions.
std::vector<std::uint32_t> seedsOf(const Gradients &gradients, float threshold)
{
    std::array<std::uint32_t, gradientBins + 1> starts{};
    const auto binOf = [&](float magnitude) {
        const int bin = int(double(magnitude) / double(gradients.largest) * (gradientBins - 1));
        return gradientBins - 1 - std::clamp(bin, 0, gradientBins - 1);
    };
    for (const float magnitude : gradients.magnitude)
        if (magnitude > threshold)
            ++starts[size_t(binOf(magnitude)) + 1];
    std::partial_sum(starts.begin(), starts.end(), starts.begin());
    std::vector<std::uint32_t> seeds(starts.back());
    for (size_t index = 0; index < gradients.magnitude.size(); ++index) {
        const float magnitude = gradients.magnitude[index];
        if (magnitude > threshold)
            seeds[starts[size_t(binOf(magnitude))]++] = std::uint32_t(index);
    }
    return seeds;
}

// A straight line through a region, in pixels of the reduced image.
struct Fitted
{
    double centreX = 0, centreY = 0;
    double directionX = 1, directionY = 0;
    double from = 0, to = 0;     // along the direction, from the centre
    double width = 0;            // RMS distance across
    double wander = 0;           // RMS distance of the region's middle, taken piece by piece
    double contrast = 0;
};

Fitted fitted(const std::vector<std::uint32_t> &region, const Gradients &gradients, int width)
{
    double weight = 0, sumX = 0, sumY = 0;
    for (const std::uint32_t index : region) {
        const double w = gradients.magnitude[index];
        weight += w;
        sumX += w * double(index % std::uint32_t(width));
        sumY += w * double(index / std::uint32_t(width));
    }
    Fitted line;
    line.centreX = sumX / weight;
    line.centreY = sumY / weight;
    line.contrast = weight / double(region.size());
    double xx = 0, xy = 0, yy = 0;
    for (const std::uint32_t index : region) {
        const double w = gradients.magnitude[index];
        const double x = double(index % std::uint32_t(width)) - line.centreX;
        const double y = double(index / std::uint32_t(width)) - line.centreY;
        xx += w * x * x;
        xy += w * x * y;
        yy += w * y * y;
    }
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    line.directionX = std::cos(angle);
    line.directionY = std::sin(angle);
    const auto alongOf = [&](std::uint32_t index) {
        return (double(index % std::uint32_t(width)) - line.centreX) * line.directionX +
               (double(index / std::uint32_t(width)) - line.centreY) * line.directionY;
    };
    const auto offOf = [&](std::uint32_t index) {
        return (double(index / std::uint32_t(width)) - line.centreY) * line.directionX -
               (double(index % std::uint32_t(width)) - line.centreX) * line.directionY;
    };
    line.from = 1e300;
    line.to = -1e300;
    double across = 0;
    for (const std::uint32_t index : region) {
        const double along = alongOf(index), off = offOf(index);
        line.from = std::min(line.from, along);
        line.to = std::max(line.to, along);
        across += gradients.magnitude[index] * off * off;
    }
    line.width = std::sqrt(across / weight);

    // The blur widens an edge without bending it: straightness is read on the middle of each piece.
    const size_t pieces = size_t(std::clamp((line.to - line.from) / piecePixels, 1.0, double(maximumPieces)));
    std::array<double, maximumPieces> pieceWeight{}, pieceOff{};
    const double pieceLength = std::max(line.to - line.from, 1.0) / double(pieces);
    for (const std::uint32_t index : region) {
        const size_t piece = std::min(pieces - 1, size_t((alongOf(index) - line.from) / pieceLength));
        const double w = gradients.magnitude[index];
        pieceWeight[piece] += w;
        pieceOff[piece] += w * offOf(index);
    }
    double wander = 0;
    for (size_t piece = 0; piece < pieces; ++piece)
        if (pieceWeight[piece] > 0.0) {
            const double middle = pieceOff[piece] / pieceWeight[piece];
            wander += pieceWeight[piece] * middle * middle;
        }
    line.wander = std::sqrt(wander / weight);
    return line;
}

struct Part
{
    double x0, y0, x1, y1;
    double contrast;

    double length() const { return std::hypot(x1 - x0, y1 - y0); }
};

// Whether two parts lie on one line; if so, the first takes the second in.
bool merged(Part &into, const Part &part, double cosAngle, double distance, double gap)
{
    const double length = into.length();
    const double ux = (into.x1 - into.x0) / length, uy = (into.y1 - into.y0) / length;
    const double partLength = part.length();
    const double dot = (part.x1 - part.x0) / partLength * ux + (part.y1 - part.y0) / partLength * uy;
    if (std::abs(dot) < cosAngle)
        return false;
    const auto off = [&](double x, double y) { return (y - into.y0) * ux - (x - into.x0) * uy; };
    const double off0 = off(part.x0, part.y0), off1 = off(part.x1, part.y1);
    if (std::abs(off0) > distance || std::abs(off1) > distance)
        return false;
    const auto along = [&](double x, double y) { return (x - into.x0) * ux + (y - into.y0) * uy; };
    const double a = along(part.x0, part.y0), b = along(part.x1, part.y1);
    const double partFrom = std::min(a, b), partTo = std::max(a, b);
    if (partFrom > length + gap || partTo < -gap)
        return false;
    const double from = std::min(0.0, partFrom), to = std::max(length, partTo);
    const double total = length + partLength;
    into.contrast = (into.contrast * length + part.contrast * partLength) / total;
    // Between the two, by length: the two sides of a thin line give its middle.
    const double shift = (off0 + off1) / 2.0 * partLength / total;
    const double x0 = into.x0 - shift * uy, y0 = into.y0 + shift * ux;
    into.x0 = x0 + from * ux;
    into.y0 = y0 + from * uy;
    into.x1 = x0 + to * ux;
    into.y1 = y0 + to * uy;
    return true;
}

} // namespace

std::vector<Segment> detect(const Image &image, const Options &options)
{
    constexpr int minimumSide = 16;
    if (!image.pixels || image.width < minimumSide || image.height < minimumSide || image.stride < image.width ||
        options.workingSide < minimumSide)
        return {};
    const int longSide = std::max(image.width, image.height);
    const int factor = std::max(1, (longSide + options.workingSide - 1) / options.workingSide);
    if (image.width / factor < minimumSide || image.height / factor < minimumSide)
        return {};

    const Plane plane = smoothed(reduced(image, factor));
    const Gradients gradients = gradientsOf(plane);
    if (gradients.largest <= 0.0f)
        return {};
    const float threshold = std::max(options.minimumGradient, options.noiseFactor * gradients.median);

    const double minimumLength = double(options.minimumLengthShare) * std::max(plane.width, plane.height);
    const double cosTolerance = std::cos(double(options.directionTolerance) * pi / 180.0);
    // Twice the angle: within the tolerance of a direction means within twice of its double.
    const double cosDoubled = std::cos(2.0 * double(options.directionTolerance) * pi / 180.0);

    std::vector<std::uint8_t> used(plane.values.size(), 0);
    std::vector<Part> parts;
    std::vector<std::uint32_t> region;
    const std::uint32_t width = std::uint32_t(plane.width);
    for (const std::uint32_t seed : seedsOf(gradients, threshold)) {
        if (used[seed])
            continue;
        region.assign(1, seed);
        used[seed] = 1;
        double sumCos = gradients.doubledCos[seed], sumSin = gradients.doubledSin[seed];
        for (size_t next = 0; next < region.size(); ++next) {
            const std::uint32_t at = region[next];
            const int x = int(at % width), y = int(at / width);
            const double norm = std::hypot(sumCos, sumSin);
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const int nx = x + i, ny = y + j;
                    if ((i == 0 && j == 0) || nx < 0 || ny < 0 || nx >= plane.width || ny >= plane.height)
                        continue;
                    const std::uint32_t neighbour = std::uint32_t(ny) * width + std::uint32_t(nx);
                    if (used[neighbour] || gradients.magnitude[neighbour] <= threshold)
                        continue;
                    const double agreement = norm > 0.0 ? (double(gradients.doubledCos[neighbour]) * sumCos +
                                                           double(gradients.doubledSin[neighbour]) * sumSin) / norm
                                                        : 1.0;
                    if (agreement < cosDoubled)
                        continue;
                    used[neighbour] = 1;
                    region.push_back(neighbour);
                    sumCos += gradients.doubledCos[neighbour];
                    sumSin += gradients.doubledSin[neighbour];
                }
        }
        if (double(region.size()) < minimumLength)
            continue;
        const Fitted line = fitted(region, gradients, plane.width);
        if (line.to - line.from < minimumLength || line.wander > double(options.maximumWander) ||
            line.width > double(options.maximumWidth))
            continue;
        // A band of parallel edges grows across its direction: its shape must follow its pixels.
        const double edgeAngle = 0.5 * std::atan2(sumSin, sumCos);
        const double follows = std::abs(line.directionX * std::cos(edgeAngle) + line.directionY * std::sin(edgeAngle));
        if (follows < cosTolerance)
            continue;
        parts.push_back({line.centreX + line.from * line.directionX, line.centreY + line.from * line.directionY,
                         line.centreX + line.to * line.directionX, line.centreY + line.to * line.directionY,
                         line.contrast});
    }

    const auto longerFirst = [](const Part &a, const Part &b) {
        const double la = a.length(), lb = b.length();
        if (la != lb)
            return la > lb;
        return std::array<double, 4>{a.x0, a.y0, a.x1, a.y1} < std::array<double, 4>{b.x0, b.y0, b.x1, b.y1};
    };
    std::sort(parts.begin(), parts.end(), longerFirst);
    const size_t bounded = size_t(std::max(options.maximumSegments, 1)) * 4;
    if (parts.size() > bounded)
        parts.resize(bounded);

    // The longest take in the others: the longer a line, the better it says where it goes on.
    const double side = std::max(plane.width, plane.height);
    const double cosMerge = std::cos(double(options.mergeAngle) * pi / 180.0);
    const double cosFar = std::cos(double(options.farAngle) * pi / 180.0);
    const double gap = double(options.mergeGapShare) * side, farGap = double(options.farGapShare) * side;
    std::vector<Part> lines;
    for (const Part &part : parts) {
        bool taken = false;
        for (Part &line : lines)
            if (merged(line, part, cosMerge, double(options.mergeDistance), gap) ||
                merged(line, part, cosFar, double(options.farDistance), farGap)) {
                taken = true;
                break;
            }
        if (!taken)
            lines.push_back(part);
    }
    std::sort(lines.begin(), lines.end(), longerFirst);
    if (lines.size() > size_t(std::max(options.maximumSegments, 1)))
        lines.resize(size_t(std::max(options.maximumSegments, 1)));

    // Back to the input's pixels: a reduced pixel's centre lies in the middle of its block.
    const auto toInput = [factor](double value) { return float((value + 0.5) * factor - 0.5); };
    std::vector<Segment> segments;
    segments.reserve(lines.size());
    for (const Part &line : lines) {
        Segment segment{toInput(line.x0), toInput(line.y0), toInput(line.x1), toInput(line.y1), 0.0f, float(line.contrast)};
        segment.length = std::hypot(segment.x1 - segment.x0, segment.y1 - segment.y0);
        segments.push_back(segment);
    }
    return segments;
}

} // namespace LineDetection
