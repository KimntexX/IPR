#include "LineDetection.h"
#include "Straightening.h"
#include "Pixels.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

struct Buffer
{
    int width = 0, height = 0;
    std::vector<float> values;

    LineDetection::Image image() const { return {values.data(), width, height, width}; }
};

// The pattern on a straight canvas: facades of irregular widths and storeys, on a dark ground.
float pattern(double u, double v)
{
    static const double columns[] = {0.08, 0.17, 0.31, 0.38, 0.52, 0.66, 0.73, 0.88};
    static const double rows[] = {0.11, 0.26, 0.37, 0.55, 0.69, 0.84};
    int column = 0, row = 0;
    for (const double c : columns) column += u > c ? 1 : 0;
    for (const double r : rows) row += v > r ? 1 : 0;
    return 0.25f + 0.08f * float((column * 3 + row * 5) % 7);
}

// What a camera saw of the straight canvas, when applying these settings straightens it back.
Buffer photographed(int width, int height, const Geometry::Settings &truth, double noise = 0.0)
{
    Buffer out{width, height, std::vector<float>(size_t(width) * size_t(height))};
    const ipr::Matrix33d h = Geometry::homography(truth, {width, height});
    std::mt19937 random(11);
    std::normal_distribution<float> gaussian(0.0f, float(noise));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float sum = 0;
            for (const double sy : {0.25, 0.75})
                for (const double sx : {0.25, 0.75}) {
                    const ipr::Vec3d q = h * ipr::Vec3d((x + sx) / width, (y + sy) / height, 1.0);
                    sum += pattern(q[0] / q[2], q[1] / q[2]);
                }
            out.values[size_t(y) * size_t(width) + size_t(x)] = sum / 4.0f + (noise > 0.0 ? gaussian(random) : 0.0f);
        }
    return out;
}

double angleOf(const LineDetection::Segment &s)
{
    double angle = std::atan2(double(s.y1 - s.y0), double(s.x1 - s.x0)) * 180.0 / 3.14159265358979323846;
    while (angle > 90.0) angle -= 180.0;
    while (angle <= -90.0) angle += 180.0;
    return angle;
}

Buffer flat(int width, int height, float value)
{
    return {width, height, std::vector<float>(size_t(width) * size_t(height), value)};
}

void invalidImagesGiveNoLine()
{
    check(LineDetection::detect({}).empty(), "no pixels");
    const Buffer image = flat(64, 64, 0.5f);
    check(LineDetection::detect({image.values.data(), 8, 64, 64}).empty(), "too narrow");
    check(LineDetection::detect({image.values.data(), 64, 8, 64}).empty(), "too short");
    check(LineDetection::detect({image.values.data(), 64, 64, 10}).empty(), "stride below the width");
    check(LineDetection::detect(image.image()).empty(), "a flat image has no line");
}

void aStepEdgeIsFoundWhereItIs()
{
    Buffer image = flat(400, 300, 0.2f);
    for (int y = 0; y < 300; ++y)
        for (int x = 200; x < 400; ++x)
            image.values[size_t(y) * 400 + size_t(x)] = 0.7f;
    const auto segments = LineDetection::detect(image.image());
    check(segments.size() == 1, "one edge, one segment");
    check(std::abs(std::abs(angleOf(segments[0])) - 90.0) < 0.05, "vertical");
    // Between the pixels 199 and 200.
    check(std::abs(segments[0].x0 - 199.5f) < 0.2f && std::abs(segments[0].x1 - 199.5f) < 0.2f, "at its place");
    check(segments[0].length > 280.0f, "along the whole height");
}

void aBlurredEdgeIsStillAnEdge()
{
    // As a lens leaves it: a transition of some ten pixels, and an edge in the middle of a pixel.
    Buffer image = flat(800, 600, 0.0f);
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 800; ++x)
            image.values[size_t(y) * 800 + size_t(x)] = 0.2f + 0.5f * std::clamp((float(x) - 400.5f) / 10.0f + 0.5f, 0.0f, 1.0f);
    const auto segments = LineDetection::detect(image.image());
    check(segments.size() == 1, "one blurred edge, one segment");
    check(std::abs(segments[0].x0 - 400.5f) < 0.3f, "in its middle");
}

void aSlowCurveIsNotALine()
{
    // A long arc whose sagitta is far beyond a pixel, drawn as a step.
    Buffer image = flat(1200, 400, 0.2f);
    for (int y = 0; y < 400; ++y)
        for (int x = 0; x < 1200; ++x) {
            const double u = (x - 600.0) / 600.0;
            if (y > 200.0 + 40.0 * u * u)
                image.values[size_t(y) * 1200 + size_t(x)] = 0.7f;
        }
    for (const auto &segment : LineDetection::detect(image.image()))
        check(segment.length < 400.0f, "an arc is not one long line");
}

void aThinLineIsOneSegment()
{
    Buffer image = flat(400, 300, 0.2f);
    for (int x = 20; x < 380; ++x)
        image.values[150 * 400 + size_t(x)] = 0.8f;
    const auto segments = LineDetection::detect(image.image());
    check(segments.size() == 1, "both sides of a line are one segment");
    check(std::abs(angleOf(segments[0])) < 0.1, "horizontal");
    check(std::abs(segments[0].y0 - 150.0f) < 0.2f, "on the line");
}

void aLineBehindAnObstacleIsOneLine()
{
    // A horizon with a dark mast across it.
    Buffer image = flat(1000, 600, 0.2f);
    for (int y = 300; y < 600; ++y)
        for (int x = 0; x < 1000; ++x)
            image.values[size_t(y) * 1000 + size_t(x)] = 0.6f;
    for (int y = 0; y < 600; ++y)
        for (int x = 450; x < 550; ++x)
            image.values[size_t(y) * 1000 + size_t(x)] = 0.4f;
    const auto segments = LineDetection::detect(image.image());
    check(!segments.empty(), "found");
    check(std::abs(angleOf(segments[0])) < 0.1, "level");
    check(segments[0].length > 950.0f, "across the mast");

    // Two parallel lines at different heights stay two.
    Buffer shifted = flat(1000, 600, 0.2f);
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 1000; ++x)
            if ((x < 450 && y >= 300) || (x >= 550 && y >= 320))
                shifted.values[size_t(y) * 1000 + size_t(x)] = 0.6f;
    for (const auto &segment : LineDetection::detect(shifted.image()))
        check(segment.length < 500.0f, "not merged across a step");
}

void noiseAloneDrawsNoLongLine()
{
    Buffer image = flat(800, 600, 0.3f);
    std::mt19937 random(3);
    std::normal_distribution<float> gaussian(0.0f, 0.03f);
    for (float &value : image.values) value += gaussian(random);
    check(LineDetection::detect(image.image()).empty(), "noise is not a line");
}

void aCircleIsNotALine()
{
    Buffer image = flat(600, 600, 0.2f);
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 600; ++x)
            if (std::hypot(x - 300.0, y - 300.0) < 200.0)
                image.values[size_t(y) * 600 + size_t(x)] = 0.8f;
    for (const auto &segment : LineDetection::detect(image.image()))
        check(segment.length < 110.0f, "no long segment on a circle");
}

void nonFinitePixelsDoNotBreakIt()
{
    Buffer image = photographed(400, 300, {});
    image.values[1000] = std::numeric_limits<float>::quiet_NaN();
    image.values[2000] = std::numeric_limits<float>::infinity();
    const auto segments = LineDetection::detect(image.image());
    check(!segments.empty(), "lines remain");
    for (const auto &segment : segments)
        check(std::isfinite(segment.x0 + segment.y0 + segment.x1 + segment.y1 + segment.contrast), "finite result");
}

void aLargeImageGivesItsOwnCoordinates()
{
    Buffer image = flat(4000, 3000, 0.2f);
    for (int y = 0; y < 3000; ++y)
        for (int x = 1000; x < 4000; ++x)
            image.values[size_t(y) * 4000 + size_t(x)] = 0.7f;
    const auto segments = LineDetection::detect(image.image());
    check(!segments.empty(), "found after the reduction");
    check(std::abs(segments[0].x0 - 999.5f) < 1.5f, "in the input's pixels");
}

void aStrideIsFollowed()
{
    const Buffer tight = photographed(300, 200, {});
    Buffer wide{300, 200, std::vector<float>(size_t(340) * 200, 9.0f)};
    for (int y = 0; y < 200; ++y)
        std::copy_n(tight.values.begin() + y * 300, 300, wide.values.begin() + y * 340);
    const auto a = LineDetection::detect(tight.image());
    const auto b = LineDetection::detect({wide.values.data(), 300, 200, 340});
    check(a.size() == b.size() && !a.empty(), "same lines");
    for (size_t i = 0; i < a.size(); ++i)
        check(a[i].x0 == b[i].x0 && a[i].y1 == b[i].y1, "same place");
}

void itIsDeterministic()
{
    const Buffer image = photographed(600, 400, {}, 0.005);
    const auto a = LineDetection::detect(image.image()), b = LineDetection::detect(image.image());
    check(a.size() == b.size(), "same count");
    for (size_t i = 0; i < a.size(); ++i)
        check(a[i].x0 == b[i].x0 && a[i].x1 == b[i].x1 && a[i].y0 == b[i].y0 && a[i].y1 == b[i].y1, "same segments");
}

Straightening::Proposal proposed(const Buffer &image, Straightening::Mode mode, const Geometry::Settings &base = {})
{
    return Straightening::propose(LineDetection::detect(image.image()), {image.width, image.height}, base, mode);
}

void linesAreReadOnLinearImages()
{
    check(Straightening::linesOf({}).empty(), "nothing to read");
    check(Straightening::linesOf(ipr::filled({64, 64}, ipr::Depth::U8, 3, 0)).empty(), "another type");
    check(Straightening::linesOf(ipr::filled({64, 64}, ipr::Depth::F32, 1, 0)).empty(), "one channel");

    for (const int channels : {3, 4}) {
        ipr::Image image = ipr::filled({400, 300}, ipr::Depth::F32, channels, 0.04);
        ipr::Image lower = image.mutableView(ipr::Recti(0, 150, 400, 150));
        ipr::fill(lower, 0.5);
        const auto lines = Straightening::linesOf(image);
        check(lines.size() == 1 && std::abs(angleOf(lines[0])) < 0.1, "a level edge");
    }
    // A part of an image has its own rows.
    ipr::Image whole = ipr::filled({600, 400}, ipr::Depth::F32, 3, 0.04);
    ipr::Image lower = whole.mutableView(ipr::Recti(0, 200, 600, 200));
    ipr::fill(lower, 0.5);
    const auto lines = Straightening::linesOf(whole.view(ipr::Recti(100, 50, 400, 300)));
    check(lines.size() == 1 && std::abs(lines[0].y0 - 149.5f) < 0.3f, "at its place in the part");
    // Below the black, nothing breaks.
    ipr::Image negative = ipr::filled({100, 100}, ipr::Depth::F32, 3, -0.1);
    check(Straightening::linesOf(negative).empty(), "signed values");
}

void levelFindsAKnownRotation()
{
    for (const float truth : {-8.0f, -2.5f, -0.4f, 0.0f, 0.7f, 3.0f, 10.0f}) {
        Geometry::Settings settings;
        settings.rotation = truth;
        for (const double noise : {0.0, 0.01}) {
            const auto result = proposed(photographed(1200, 800, settings, noise), Straightening::Mode::Level);
            check(result.valid(), "a rotated grid is understood");
            check(std::abs(result.rotation - truth) < 0.1f, "rotation within a tenth of a degree");
            check(result.vertical == 0 && result.horizontal == 0, "level moves nothing else");
            check(result.misalignmentAfter <= result.misalignmentBefore + 1e-3f, "better or equal");
            check(result.supportingLines >= 5, "several lines agree");
        }
    }
}

void verticalFindsAKnownKeystone()
{
    for (const int truth : {-40, -15, 20, 50}) {
        Geometry::Settings settings;
        settings.vertical = truth;
        settings.rotation = 1.5f;
        const auto result = proposed(photographed(1200, 800, settings, 0.005), Straightening::Mode::Vertical);
        check(result.valid(), "a keystone is understood");
        check(std::abs(result.vertical - truth) <= 3, "vertical within three steps");
        check(std::abs(result.rotation - 1.5f) < 0.5f, "rotation with it");
        check(result.horizontal == 0, "the horizontals stay");
    }
}

void fullFindsBothPerspectives()
{
    Geometry::Settings settings;
    settings.vertical = 30;
    settings.horizontal = -25;
    const auto result = proposed(photographed(1200, 800, settings, 0.005), Straightening::Mode::Full);
    check(result.valid(), "understood");
    check(std::abs(result.vertical - 30) <= 4, "vertical");
    check(std::abs(result.horizontal + 25) <= 4, "horizontal");
}

void aStraightImageStaysAsItIs()
{
    const auto result = proposed(photographed(1200, 800, {}, 0.005), Straightening::Mode::Full);
    check(result.valid(), "understood");
    check(std::abs(result.rotation) < 0.1f, "no rotation");
    check(result.vertical == 0 && result.horizontal == 0, "no perspective");
}

void withoutLinesItRefuses()
{
    const Buffer image = flat(800, 600, 0.3f);
    const auto none = proposed(image, Straightening::Mode::Level);
    check(!none.valid() && none.refusal == Straightening::Refusal::NotEnoughLines, "nothing to read");
    check(none.rotation == 0.0f, "a refusal proposes nothing");

    LineDetection::Segment diagonal{0, 0, 500, 500, 0, 1};
    const auto far = Straightening::propose({diagonal}, {800, 600}, {}, Straightening::Mode::Level);
    check(far.refusal == Straightening::Refusal::NotEnoughLines, "a diagonal levels nothing");

    const auto invalid = Straightening::propose({}, {0, 0}, {}, Straightening::Mode::Level);
    check(invalid.refusal == Straightening::Refusal::InvalidInput, "no source");
}

void linesThatDisagreeAreRefused()
{
    std::vector<LineDetection::Segment> fan;
    for (const float tilt : {-11.0f, -7.0f, -3.0f, 1.0f, 5.0f, 9.0f}) {
        const float radians = tilt * 3.14159265f / 180.0f;
        fan.push_back({100, 300, 100 + 120 * std::cos(radians), 300 + 120 * std::sin(radians), 0, 1});
    }
    const auto result = Straightening::propose(fan, {800, 600}, {}, Straightening::Mode::Level);
    check(result.refusal == Straightening::Refusal::NoAgreement, "a fan of lines shares no direction");
}

void aFewOutliersDoNotMoveTheResult()
{
    std::vector<LineDetection::Segment> lines;
    const float radians = 2.0f * 3.14159265f / 180.0f;
    for (int i = 0; i < 6; ++i)
        lines.push_back({50, 60.0f + 80.0f * i, 50 + 500 * std::cos(radians), 60.0f + 80.0f * i + 500 * std::sin(radians), 0, 1});
    lines.push_back({50, 50, 300, 50 + 300 * std::tan(9.0f * 3.14159265f / 180.0f), 0, 1});
    const auto result = Straightening::propose(lines, {800, 600}, {}, Straightening::Mode::Level);
    check(result.valid() && std::abs(result.rotation + 2.0f) < 0.1f, "the slanted line is left out");
}

void aLargeCorrectionNeedsMoreEvidence()
{
    // The same two lines justify a small rotation and not a large one.
    const auto leaning = [](float degrees) {
        const float radians = degrees * 3.14159265f / 180.0f;
        std::vector<LineDetection::Segment> lines;
        for (int i = 0; i < 2; ++i)
            lines.push_back({100, 100.0f + 300.0f * i, 100 + 250 * std::cos(radians), 100.0f + 300.0f * i + 250 * std::sin(radians), 0, 1});
        return lines;
    };
    const auto small = Straightening::propose(leaning(1.0f), {800, 600}, {}, Straightening::Mode::Level);
    check(small.valid() && std::abs(small.rotation + 1.0f) < 0.1f, "two long lines level a small tilt");
    const auto large = Straightening::propose(leaning(9.0f), {800, 600}, {}, Straightening::Mode::Level);
    check(large.refusal == Straightening::Refusal::NotEnoughEvidence, "not a large one");
    check(large.rotation == 0.0f, "a refusal proposes nothing");
}

void manyShortLinesDoNotOutweighALongOne()
{
    // A horizon, level, against the texture of something leaning.
    std::vector<LineDetection::Segment> lines{{50, 300, 750, 300, 0, 1}};
    const float radians = 6.0f * 3.14159265f / 180.0f;
    for (int i = 0; i < 12; ++i) {
        const float y = 50.0f + 20.0f * float(i);
        lines.push_back({100, y, 100 + 45 * std::cos(radians), y + 45 * std::sin(radians), 0, 1});
    }
    const auto result = Straightening::propose(lines, {800, 600}, {}, Straightening::Mode::Level);
    check(result.valid() && std::abs(result.rotation) < 0.1f, "the horizon decides");
}

void wavesDoNotOutvoteAHorizon()
{
    // Many short lines, all leaning the same way, far more length than the horizon's.
    std::vector<LineDetection::Segment> lines{{0, 280, 300, 280, 0, 1}};
    const float radians = 1.0f * 3.14159265f / 180.0f;
    for (int i = 0; i < 80; ++i) {
        const float x = float(i % 8) * 90.0f, y = 320.0f + 3.0f * float(i);
        lines.push_back({x, y, x + 80 * std::cos(radians), y + 80 * std::sin(radians), 0, 1});
    }
    const auto result = Straightening::propose(lines, {800, 600}, {}, Straightening::Mode::Level);
    check(result.valid() && std::abs(result.rotation) < 0.05f, "the horizon decides, not the waves");

    // Short lines alone say nothing.
    lines.erase(lines.begin());
    const auto waves = Straightening::propose(lines, {800, 600}, {}, Straightening::Mode::Level);
    check(waves.refusal == Straightening::Refusal::NotEnoughLines, "waves alone level nothing");
}

void aPerspectiveNeedsManyLongLines()
{
    // Five lines that converge, but short: not enough to turn a whole photo.
    std::vector<LineDetection::Segment> few;
    for (int i = 0; i < 5; ++i) {
        const float x = 100.0f + 150.0f * float(i), lean = (x - 400.0f) * 0.05f;
        few.push_back({x, 300, x + lean * 0.1f, 340, 0, 1});
    }
    few.push_back({50, 300, 750, 300, 0, 1});
    const auto result = Straightening::propose(few, {800, 600}, {}, Straightening::Mode::Vertical);
    check(result.valid() && result.vertical == 0, "short lines do not make a keystone");
}

void aPerspectiveNeedsLinesApart()
{
    // Three near-verticals close together: their angles alone do not make a keystone.
    std::vector<LineDetection::Segment> close;
    for (int i = 0; i < 3; ++i) {
        const float x = 400.0f + float(i) * 10.0f, lean = float(i) * 4.0f;
        close.push_back({x, 100, x + lean, 500, 0, 1});
    }
    const auto result = Straightening::propose(close, {800, 600}, {}, Straightening::Mode::Vertical);
    check(result.vertical == 0, "no keystone from lines side by side");
}

void theTurnsOfTheBaseStay()
{
    Geometry::Settings truth;
    truth.quarterTurns = 1;
    truth.rotation = 3.0f;
    Geometry::Settings base;
    base.quarterTurns = 1;
    base.scale = 120;
    const auto result = proposed(photographed(800, 1200, truth, 0.005), Straightening::Mode::Level, base);
    check(result.valid() && std::abs(result.rotation - 3.0f) < 0.15f, "read on the turned canvas");
    const Geometry::Settings applied = Straightening::applied(base, result);
    check(applied.quarterTurns == 1 && applied.scale == 120, "the base stays");
    check(applied.rotation == result.rotation, "the proposal is in");

    Straightening::Proposal refused;
    refused.rotation = 4.0f;
    check(Straightening::applied(base, refused) == base, "a refusal changes nothing");
}

} // namespace

int main()
{
    try {
        invalidImagesGiveNoLine();
        aStepEdgeIsFoundWhereItIs();
        aBlurredEdgeIsStillAnEdge();
        aSlowCurveIsNotALine();
        aThinLineIsOneSegment();
        aLineBehindAnObstacleIsOneLine();
        noiseAloneDrawsNoLongLine();
        aCircleIsNotALine();
        nonFinitePixelsDoNotBreakIt();
        aLargeImageGivesItsOwnCoordinates();
        aStrideIsFollowed();
        itIsDeterministic();
        linesAreReadOnLinearImages();
        levelFindsAKnownRotation();
        verticalFindsAKnownKeystone();
        fullFindsBothPerspectives();
        aStraightImageStaysAsItIs();
        withoutLinesItRefuses();
        linesThatDisagreeAreRefused();
        aFewOutliersDoNotMoveTheResult();
        aLargeCorrectionNeedsMoreEvidence();
        manyShortLinesDoNotOutweighALongOne();
        wavesDoNotOutvoteAHorizon();
        aPerspectiveNeedsManyLongLines();
        aPerspectiveNeedsLinesApart();
        theTurnsOfTheBaseStay();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "line detection: all passed\n";
    return 0;
}
