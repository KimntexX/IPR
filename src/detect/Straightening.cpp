#include "Straightening.h"
#include "Types.h"

#include <algorithm>
#include <cmath>

namespace Straightening {

namespace {

constexpr double pi = 3.14159265358979323846;

enum class Axis { Horizontal, Vertical };

struct Line
{
    ipr::Point2d from, to;   // normalized source
    double length = 0;      // in source pixels
    Axis axis = Axis::Horizontal;
    double tilt = 0;        // degrees from its axis, with the base's geometry
};

// Degrees from the axis, clockwise positive, of a segment of the canvas (y goes down).
double deviationOf(double dx, double dy, Axis axis)
{
    double angle = std::atan2(dy, dx) * 180.0 / pi;
    if (axis == Axis::Vertical)
        angle -= 90.0;
    while (angle > 90.0) angle -= 180.0;
    while (angle <= -90.0) angle += 180.0;
    return angle;
}

// What a geometry leaves of each line's direction, on the canvas's pixels.
class Evaluator
{
public:
    Evaluator(const std::vector<Line> &lines, const ipr::Size &source, const Geometry::Settings &base)
        : m_lines(lines), m_source(source), m_settings(base)
    {
    }

    std::vector<double> deviations(float rotation, int vertical, int horizontal)
    {
        m_settings.rotation = rotation;
        m_settings.vertical = vertical;
        m_settings.horizontal = horizontal;
        const ipr::Matrix33d h = Geometry::homography(m_settings, m_source);
        const ipr::Size canvas = Geometry::canvasSize(m_settings, m_source);
        std::vector<double> out;
        out.reserve(m_lines.size());
        for (const Line &line : m_lines) {
            const ipr::Point2d a = mapped(h, line.from), b = mapped(h, line.to);
            out.push_back(deviationOf((b.x - a.x) * canvas.width, (b.y - a.y) * canvas.height, line.axis));
        }
        return out;
    }

private:
    static ipr::Point2d mapped(const ipr::Matrix33d &h, const ipr::Point2d &p)
    {
        const ipr::Vec3d q = h * ipr::Vec3d(p.x, p.y, 1.0);
        return {q[0] / q[2], q[1] / q[2]};
    }

    const std::vector<Line> &m_lines;
    ipr::Size m_source;
    Geometry::Settings m_settings;
};

// The direction the long lines share: the peak of a histogram softened over the agreement, each
// line weighing its length twice over, as many short ones are often the texture of something
// else; then the weighted median of what lies around it.
struct Shared
{
    double direction = 0;
    double agreeingLength = 0;
    double longest = 0;
};

Shared sharedDirection(const std::vector<Line> &lines, const std::vector<double> &deviations, double limit, double agreement)
{
    constexpr double step = 0.1;
    const int bins = int(std::ceil(2.0 * limit / step)) + 1;
    std::vector<double> histogram(size_t(bins), 0.0);
    const int reach = std::max(1, int(std::round(agreement / step)));
    for (size_t i = 0; i < lines.size(); ++i) {
        if (std::abs(deviations[i]) > limit)
            continue;
        const int centre = int(std::round((deviations[i] + limit) / step));
        for (int k = std::max(0, centre - reach); k <= std::min(bins - 1, centre + reach); ++k)
            histogram[size_t(k)] += lines[i].length * lines[i].length * (1.0 - double(std::abs(k - centre)) / double(reach + 1));
    }
    const int peak = int(std::max_element(histogram.begin(), histogram.end()) - histogram.begin());
    const double around = double(peak) * step - limit;

    std::vector<std::pair<double, double>> near;
    for (size_t i = 0; i < lines.size(); ++i)
        if (std::abs(deviations[i] - around) <= agreement)
            near.emplace_back(deviations[i], lines[i].length);
    Shared shared;
    shared.direction = around;
    for (const auto &entry : near) {
        shared.agreeingLength += entry.second;
        shared.longest = std::max(shared.longest, entry.second);
    }
    if (near.empty())
        return shared;
    std::sort(near.begin(), near.end());
    double walked = 0;
    for (const auto &entry : near) {
        walked += entry.second;
        if (walked >= shared.agreeingLength / 2.0) {
            shared.direction = entry.first;
            break;
        }
    }
    return shared;
}

// How far from their axis the lines remain, each capped: a line that belongs to something else
// weighs as little as it can.
double misalignment(const std::vector<Line> &lines, const std::vector<double> &deviations, double cap)
{
    double sum = 0, weight = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        sum += lines[i].length * std::min(std::abs(deviations[i]), cap);
        weight += lines[i].length;
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

// The lines near one axis, on the current rotation and perspectives.
std::vector<Line> onAxis(const std::vector<Line> &lines, Axis axis)
{
    std::vector<Line> out;
    std::copy_if(lines.begin(), lines.end(), std::back_inserter(out), [axis](const Line &l) { return l.axis == axis; });
    return out;
}

// Whether the lines that agree are many, long and apart enough, across their direction, for a
// convergence to mean something.
bool perspectiveSupported(const std::vector<Line> &lines, const std::vector<double> &deviations, Axis axis,
                          double longSide, const Options &options)
{
    int count = 0;
    double length = 0, low = 1.0, high = 0.0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (std::abs(deviations[i]) > double(options.agreement))
            continue;
        ++count;
        length += lines[i].length;
        const double middle = axis == Axis::Vertical ? (lines[i].from.x + lines[i].to.x) / 2.0
                                                     : (lines[i].from.y + lines[i].to.y) / 2.0;
        low = std::min(low, middle);
        high = std::max(high, middle);
    }
    return count >= options.minimumPerspectiveLines &&
           length >= double(options.minimumPerspectiveLengthShare) * longSide &&
           high - low >= double(options.minimumPerspectiveSpread);
}

struct PerspectiveSearch
{
    int value = 0;
    float rotation = 0;
};

// The perspective that best brings these lines to their axis, with the rotation that goes with
// it; the current value when none removes enough of what is left.
PerspectiveSearch searchPerspective(const std::vector<Line> &lines, const ipr::Size &source, const Geometry::Settings &base,
                                    Axis axis, float rotation, int vertical, int horizontal, const Options &options)
{
    Evaluator evaluator(lines, source, base);
    const double cap = 2.0 * double(options.agreement);
    const auto evaluate = [&](int value) {
        const int v = axis == Axis::Vertical ? value : vertical;
        const int h = axis == Axis::Horizontal ? value : horizontal;
        // The rotation that suits this perspective: the shared direction goes back to the axis.
        float tried = rotation;
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<double> deviations = evaluator.deviations(tried, v, h);
            const Shared shared = sharedDirection(lines, deviations, double(options.perspectiveTilt) + 25.0, double(options.agreement));
            tried -= float(shared.direction);
        }
        return std::pair<double, float>(misalignment(lines, evaluator.deviations(tried, v, h), cap), tried);
    };
    const int current = axis == Axis::Vertical ? vertical : horizontal;
    const auto start = evaluate(current);
    double best = start.first;
    PerspectiveSearch found{current, start.second};
    const auto consider = [&](int value) {
        const auto tried = evaluate(value);
        if (tried.first < best) {
            best = tried.first;
            found = {value, tried.second};
        }
    };
    for (int value = -100; value <= 100; value += 5)
        consider(value);
    const int coarse = found.value;
    for (int value = std::max(-100, coarse - 4); value <= std::min(100, coarse + 4); ++value)
        consider(value);
    const double removed = start.first > 0.0 ? 1.0 - best / start.first : 0.0;
    const int v = axis == Axis::Vertical ? found.value : vertical;
    const int h = axis == Axis::Horizontal ? found.value : horizontal;
    if (removed < double(options.minimumPerspectiveGain) ||
        !perspectiveSupported(lines, evaluator.deviations(found.rotation, v, h), axis,
                              double(std::max(source.width, source.height)), options))
        return {current, rotation};
    return found;
}

} // namespace

std::vector<LineDetection::Segment> linesOf(const cv::Mat &image)
{
    if (image.empty() || (image.type() != CV_32FC3 && image.type() != CV_32FC4))
        return {};
    const int channels = image.channels();
    cv::Mat luminance(image.size(), CV_32FC1);
    for (int y = 0; y < image.rows; ++y) {
        const float *in = image.ptr<float>(y);
        float *out = luminance.ptr<float>(y);
        for (int x = 0; x < image.cols; ++x, in += channels)
            out[x] = std::sqrt(std::max(0.0f, 0.2627f * in[0] + 0.678f * in[1] + 0.0593f * in[2]));
    }
    return LineDetection::detect({luminance.ptr<float>(), luminance.cols, luminance.rows, std::ptrdiff_t(luminance.step1())});
}

Proposal propose(const std::vector<LineDetection::Segment> &segments, const ipr::Size &source,
                 const Geometry::Settings &base, Mode mode, const Options &options)
{
    Proposal proposal;
    if (source.width <= 0 || source.height <= 0 || !(options.agreement > 0.0f) || !(options.levelTilt > 0.0f))
        return proposal;

    // The directions are read on a canvas without any correction of its own: turns and mirrors only.
    Geometry::Settings frame = base;
    frame.rotation = 0;
    frame.vertical = 0;
    frame.horizontal = 0;
    const double tiltLimit = mode == Mode::Level ? double(options.levelTilt) : double(options.perspectiveTilt);

    const double longSide = std::max(source.width, source.height);
    std::vector<Line> lines;
    lines.reserve(segments.size());
    const ipr::Matrix33d h = Geometry::homography(frame, source);
    const ipr::Size canvas = Geometry::canvasSize(frame, source);
    const auto onCanvas = [&h](const ipr::Point2d &p) {
        const ipr::Vec3d q = h * ipr::Vec3d(p.x, p.y, 1.0);
        return ipr::Point2d(q[0] / q[2], q[1] / q[2]);
    };
    for (const LineDetection::Segment &segment : segments) {
        if (!std::isfinite(segment.x0) || !std::isfinite(segment.y0) || !std::isfinite(segment.x1) || !std::isfinite(segment.y1))
            continue;
        Line line;
        line.from = {(double(segment.x0) + 0.5) / source.width, (double(segment.y0) + 0.5) / source.height};
        line.to = {(double(segment.x1) + 0.5) / source.width, (double(segment.y1) + 0.5) / source.height};
        line.length = std::hypot(double(segment.x1 - segment.x0), double(segment.y1 - segment.y0));
        if (line.length < double(options.minimumVoteShare) * longSide || line.length <= 0.0)
            continue;
        const ipr::Point2d a = onCanvas(line.from), b = onCanvas(line.to);
        const double dx = (b.x - a.x) * canvas.width, dy = (b.y - a.y) * canvas.height;
        const double fromHorizontal = deviationOf(dx, dy, Axis::Horizontal);
        const double fromVertical = deviationOf(dx, dy, Axis::Vertical);
        line.axis = std::abs(fromVertical) < std::abs(fromHorizontal) ? Axis::Vertical : Axis::Horizontal;
        line.tilt = line.axis == Axis::Vertical ? fromVertical : fromHorizontal;
        if (std::abs(line.tilt) <= tiltLimit)
            lines.push_back(line);
    }

    const double needed = double(options.minimumLengthShare) * longSide;
    double total = 0;
    for (const Line &line : lines)
        total += line.length;
    if (lines.empty() || total < needed) {
        proposal.refusal = Refusal::NotEnoughLines;
        return proposal;
    }

    Evaluator evaluator(lines, source, frame);
    const std::vector<double> initial = evaluator.deviations(0.0f, 0, 0);
    const double cap = 2.0 * double(options.agreement);
    const Shared shared = sharedDirection(lines, initial, tiltLimit, double(options.agreement));
    if (shared.agreeingLength < needed || shared.agreeingLength < double(options.minimumAgreementShare) * total) {
        proposal.refusal = Refusal::NoAgreement;
        return proposal;
    }
    // A photo is rarely far from level by accident: the further, the more lines must say so.
    const double neededForAngle = needed * (1.0 + std::abs(shared.direction) / double(options.degreesPerEvidence));
    if (shared.agreeingLength < neededForAngle || shared.longest < double(options.minimumLongestShare) * longSide) {
        proposal.refusal = Refusal::NotEnoughEvidence;
        return proposal;
    }

    // The canvas turns clockwise by the rotation: a line leaning clockwise needs the opposite.
    float rotation = -float(shared.direction);
    int vertical = 0, horizontal = 0;
    if (mode != Mode::Level) {
        const std::vector<Line> upright = onAxis(lines, Axis::Vertical);
        if (int(upright.size()) >= options.minimumPerspectiveLines) {
            const PerspectiveSearch found = searchPerspective(upright, source, frame, Axis::Vertical, rotation, 0, 0, options);
            vertical = found.value;
            rotation = found.rotation;
        }
    }
    if (mode == Mode::Full) {
        const std::vector<Line> level = onAxis(lines, Axis::Horizontal);
        if (int(level.size()) >= options.minimumPerspectiveLines) {
            const PerspectiveSearch found = searchPerspective(level, source, frame, Axis::Horizontal, rotation, vertical, 0, options);
            horizontal = found.value;
            // The verticals keep the last word on the rotation when they had one.
            if (vertical == 0)
                rotation = found.rotation;
        }
    }

    const std::vector<double> after = evaluator.deviations(rotation, vertical, horizontal);
    proposal.refusal = Refusal::None;
    proposal.rotation = std::clamp(rotation, -45.0f, 45.0f);
    proposal.vertical = vertical;
    proposal.horizontal = horizontal;
    proposal.misalignmentBefore = float(misalignment(lines, initial, cap));
    proposal.misalignmentAfter = float(misalignment(lines, after, cap));
    for (const double deviation : after)
        if (std::abs(deviation) <= double(options.agreement))
            ++proposal.supportingLines;
    return proposal;
}

Geometry::Settings applied(const Geometry::Settings &base, const Proposal &proposal)
{
    Geometry::Settings settings = base;
    if (proposal.valid()) {
        settings.rotation = proposal.rotation;
        settings.vertical = proposal.vertical;
        settings.horizontal = proposal.horizontal;
    }
    return settings;
}

} // namespace Straightening
