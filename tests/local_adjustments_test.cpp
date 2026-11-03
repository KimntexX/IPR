#include "LocalAdjustments.h"

#include <opencv2/imgproc.hpp>

#include <QByteArray>
#include <QIODevice>

#include <cmath>
#include <iostream>
#include <limits>

using namespace LocalAdjustments;

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

const cv::Size canvas(400, 300);

Shape linear(cv::Point2f start, cv::Point2f end)
{
    Shape shape;
    shape.kind = Kind::Linear;
    shape.start = start;
    shape.end = end;
    return shape;
}

Shape radial(cv::Point2f centre, float radiusX, float radiusY, float feather)
{
    Shape shape;
    shape.kind = Kind::Radial;
    shape.center = centre;
    shape.radiusX = radiusX;
    shape.radiusY = radiusY;
    shape.feather = feather;
    return shape;
}

Shape brush(std::vector<cv::Point2f> points, float size, float feather = 0.0f, float flow = 1.0f)
{
    Shape shape;
    shape.kind = Kind::Brush;
    BrushStroke stroke;
    stroke.points = std::move(points);
    stroke.size = size;
    stroke.feather = feather;
    stroke.flow = flow;
    shape.strokes.push_back(stroke);
    return shape;
}

Mask maskOf(const Shape &shape)
{
    Mask mask;
    mask.shapes.push_back(shape);
    return mask;
}

Mask valued(const Shape &shape, LocalValues::Parameter parameter, float value)
{
    Mask mask = maskOf(shape);
    mask.values[size_t(parameter)] = value;
    return mask;
}

Shape whole()
{
    return radial({0.5f, 0.5f}, 4.0f, 4.0f, 0.0f);
}

float at(const cv::Mat &map, float u, float v)
{
    return map.at<float>(int(v * float(map.rows)), int(u * float(map.cols)));
}

double worstDifference(const cv::Mat &a, const cv::Mat &b)
{
    cv::Mat difference;
    cv::absdiff(a, b, difference);
    double worst = 0.0;
    cv::minMaxLoc(difference, nullptr, &worst);
    return worst;
}

void shapes()
{
    // A vertical gradient: whole before its start, nothing after its end, half in the middle.
    const cv::Mat gradient = coverage(maskOf(linear({0.5f, 0.25f}, {0.5f, 0.75f})), canvas);
    check(gradient.type() == CV_32FC1 && gradient.size() == canvas, "the coverage has the canvas's size");
    check(at(gradient, 0.5f, 0.1f) == 1.0f, "before the start: all of it");
    check(at(gradient, 0.5f, 0.9f) == 0.0f, "after the end: nothing");
    check(std::abs(at(gradient, 0.5f, 0.5f) - 0.5f) < 0.02f, "in the middle: half");
    check(at(gradient, 0.1f, 0.5f) == at(gradient, 0.9f, 0.5f), "the same along the gradient");
    const cv::Mat point = coverage(maskOf(linear({0.5f, 0.5f}, {0.5f, 0.5f})), canvas);
    check(at(point, 0.1f, 0.1f) == 1.0f, "a gradient of no length covers all, without dividing by zero");

    // A radial: inside at the centre, outside beyond its radius, along each axis's own radius.
    const cv::Mat ellipse = coverage(maskOf(radial({0.5f, 0.5f}, 0.25f, 0.25f, 0.0f)), canvas);
    check(at(ellipse, 0.5f, 0.5f) == 1.0f, "the centre is inside");
    check(at(ellipse, 0.7f, 0.5f) == 1.0f && at(ellipse, 0.8f, 0.5f) == 0.0f, "up to its radius across");
    check(at(ellipse, 0.5f, 0.7f) == 1.0f && at(ellipse, 0.5f, 0.8f) == 0.0f, "and down");
    const cv::Mat soft = coverage(maskOf(radial({0.5f, 0.5f}, 0.25f, 0.25f, 1.0f)), canvas);
    const float halfway = at(soft, 0.625f, 0.5f);
    check(soft.at<float>(150, 200) > 0.99f && halfway > 0.3f && halfway < 0.7f, "a feather fades from the centre");
    Shape turned = radial({0.5f, 0.5f}, 0.3f, 0.05f, 0.0f);
    turned.angle = 90.0f;
    const cv::Mat upright = coverage(maskOf(turned), canvas);
    // Turned a quarter: the long radius, 0.3 of the width, now stands along the height.
    check(at(upright, 0.5f, 0.85f) == 1.0f && at(upright, 0.75f, 0.5f) == 0.0f, "an angle turns the ellipse");
    const cv::Mat tiny = coverage(maskOf(radial({0.5f, 0.5f}, 0.0f, 0.0f, 0.5f)), canvas);
    check(cv::checkRange(tiny), "a radius of zero stays finite");

    // A stroke: along its path, up to its radius, not beyond.
    const float size = 0.05f;   // 20 px on this canvas
    const cv::Mat stroke = coverage(maskOf(brush({{0.25f, 0.5f}, {0.75f, 0.5f}}, size)), canvas);
    check(at(stroke, 0.5f, 0.5f) == 1.0f, "on the path");
    check(stroke.at<float>(150 + 18, 200) == 1.0f && stroke.at<float>(150 + 22, 200) == 0.0f, "up to its radius");
    check(at(stroke, 0.1f, 0.5f) == 0.0f, "not past its end");
    const cv::Mat dab = coverage(maskOf(brush({{0.5f, 0.5f}}, size)), canvas);
    check(at(dab, 0.5f, 0.5f) == 1.0f && at(dab, 0.75f, 0.5f) == 0.0f, "a single point is a dab");
    const cv::Mat none = coverage(maskOf(brush({}, size)), canvas);
    check(cv::countNonZero(none) == 0, "a stroke without points paints nothing");
    const cv::Mat off = coverage(maskOf(brush({{3.0f, 3.0f}}, size)), canvas);
    check(cv::countNonZero(off) == 0, "a stroke off the canvas paints nothing");
    const cv::Mat flowed = coverage(maskOf(brush({{0.5f, 0.5f}}, size, 0.0f, 0.5f)), canvas);
    check(std::abs(at(flowed, 0.5f, 0.5f) - 0.5f) < 1e-5f, "the flow bounds a stroke");

    // A stroke over itself does not build up; a second stroke does.
    const cv::Mat back = coverage(maskOf(brush({{0.25f, 0.5f}, {0.75f, 0.5f}, {0.25f, 0.5f}}, size, 0.0f, 0.5f)), canvas);
    check(std::abs(at(back, 0.5f, 0.5f) - 0.5f) < 1e-5f, "a stroke over itself stays at its flow");
    Shape twice = brush({{0.5f, 0.5f}}, size, 0.0f, 0.5f);
    twice.strokes.push_back(twice.strokes.front());
    check(std::abs(at(coverage(maskOf(twice), canvas), 0.5f, 0.5f) - 0.75f) < 1e-5f, "two strokes build up as paint");

    // Inverted: the complement, all over.
    Shape inverse = linear({0.5f, 0.25f}, {0.5f, 0.75f});
    inverse.inverted = true;
    const cv::Mat flipped = coverage(maskOf(inverse), canvas);
    cv::Mat sum = flipped + gradient;
    check(worstDifference(sum, cv::Mat(canvas, CV_32FC1, cv::Scalar(1))) < 1e-5, "an inverted shape is its complement");
    check(cv::countNonZero(coverage(Mask{}, canvas)) == 0, "a mask without shapes covers nothing");
}

void resolutions()
{
    // The same zone at every resolution: a preview is the export in small.
    const Mask mask = maskOf(radial({0.4f, 0.6f}, 0.2f, 0.3f, 0.6f));
    const cv::Mat large = coverage(mask, cv::Size(1600, 1200));
    cv::Mat reduced;
    cv::resize(large, reduced, canvas, 0, 0, cv::INTER_AREA);
    const double worst = worstDifference(reduced, coverage(mask, canvas));
    if (worst > 0.01)
        std::cerr << "radial worst " << worst << '\n';
    check(worst <= 0.01, "a radial is the same at both resolutions");

    const Mask strokes = maskOf(brush({{0.2f, 0.2f}, {0.6f, 0.7f}, {0.8f, 0.3f}}, 0.06f, 0.5f));
    cv::resize(coverage(strokes, cv::Size(1600, 1200)), reduced, canvas, 0, 0, cv::INTER_AREA);
    const double worstStroke = worstDifference(reduced, coverage(strokes, canvas));
    // A soft edge falls by up to 0.125 per pixel here: an average over 4 x 4 samples differs a little.
    if (worstStroke > 0.03)
        std::cerr << "stroke worst " << worstStroke << '\n';
    check(worstStroke <= 0.03, "so is a stroke");
    const Mask ramp = maskOf(linear({0.1f, 0.2f}, {0.9f, 0.7f}));
    cv::resize(coverage(ramp, cv::Size(1600, 1200)), reduced, canvas, 0, 0, cv::INTER_AREA);
    check(worstDifference(reduced, coverage(ramp, canvas)) <= 0.01, "and a gradient");
}

void combinations()
{
    Shape left = linear({0.45f, 0.5f}, {0.55f, 0.5f});
    Shape right = left;
    right.inverted = true;
    right.combine = Combine::Add;

    Mask both = maskOf(left);
    both.shapes.push_back(right);
    check(at(coverage(both, canvas), 0.1f, 0.5f) == 1.0f && at(coverage(both, canvas), 0.9f, 0.5f) == 1.0f, "an addition covers both");

    Mask without = maskOf(whole());
    Shape cut = left;
    cut.combine = Combine::Subtract;
    without.shapes.push_back(cut);
    const cv::Mat removed = coverage(without, canvas);
    check(at(removed, 0.1f, 0.5f) == 0.0f && at(removed, 0.9f, 0.5f) == 1.0f, "a subtraction removes its zone");

    Mask shared = maskOf(radial({0.5f, 0.5f}, 0.3f, 0.3f, 0.0f));
    Shape half = left;
    half.combine = Combine::Intersect;
    shared.shapes.push_back(half);
    const cv::Mat crossed = coverage(shared, canvas);
    check(at(crossed, 0.3f, 0.5f) == 1.0f && at(crossed, 0.7f, 0.5f) == 0.0f && at(crossed, 0.1f, 0.5f) == 0.0f, "an intersection keeps what both cover");

    Mask startsCut = maskOf(cut);
    check(cv::countNonZero(coverage(startsCut, canvas)) == 0, "subtracting from nothing leaves nothing");
}

void maps()
{
    using LocalValues::Exposure;
    using LocalValues::Saturation;
    using LocalValues::Contrast;
    // No mask, no shape, no value, hidden or at zero: nothing, and the modules' global paths.
    Masks masks;
    check(!active(masks), "no masks do nothing");
    LocalValues::Maps empty = valueMaps(masks, canvas);
    for (const cv::Mat &map : empty.values)
        check(map.empty(), "and leave every map empty");
    masks.push_back(valued(whole(), Exposure, 1.0f));
    check(active(masks), "a mask with a value acts");
    Masks hidden = masks;
    hidden[0].visible = false;
    check(!active(hidden), "a hidden mask does not");
    Masks transparent = masks;
    transparent[0].opacity = 0.0f;
    check(!active(transparent), "nor one of no opacity");
    Masks shapeless = masks;
    shapeless[0].shapes.clear();
    check(!active(shapeless), "nor one without a shape");
    Masks valueless = masks;
    valueless[0].values = {};
    check(!active(valueless), "nor one without a value");

    // Opacity 100 %: the value itself over a whole zone. The other settings stay empty.
    LocalValues::Maps full = valueMaps(masks, canvas);
    check(full[Exposure].type() == CV_32FC1 && at(full[Exposure], 0.5f, 0.5f) == 1.0f && at(full[Exposure], 0.01f, 0.01f) == 1.0f, "the value all over");
    check(full[Contrast].empty() && full[Saturation].empty(), "only its own setting has a map");
    Masks half = masks;
    half[0].opacity = 0.25f;
    check(at(valueMaps(half, canvas)[Exposure], 0.5f, 0.5f) == 0.25f, "the opacity weighs it");

    // Masks add up, and their order changes nothing.
    Mask a = valued(linear({0.5f, 0.2f}, {0.5f, 0.8f}), Exposure, 1.0f);
    Mask b = valued(radial({0.5f, 0.5f}, 0.3f, 0.3f, 0.5f), Exposure, -0.5f);
    b.values[size_t(Contrast)] = 20.0f;
    Mask c = valued(brush({{0.3f, 0.3f}, {0.7f, 0.6f}}, 0.1f, 0.5f), Saturation, 40.0f);
    LocalValues::Maps abc = valueMaps({a, b, c}, canvas);
    LocalValues::Maps cba = valueMaps({c, b, a}, canvas);
    for (size_t i = 0; i < abc.values.size(); ++i) {
        check(abc.values[i].empty() == cba.values[i].empty(), "the same maps in any order");
        if (!abc.values[i].empty())
            check(worstDifference(abc.values[i], cba.values[i]) < 1e-5, "with the same values");
    }
    cv::Mat expected = coverage(a, canvas) * 1.0 - coverage(b, canvas) * 0.5;
    check(worstDifference(abc[Exposure], expected) < 1e-5, "masks add up");
    check(worstDifference(abc[Contrast], coverage(b, canvas) * 20.0) < 1e-4, "each setting by itself");

    // Two opposite values cancel out where both cover.
    Mask plus = valued(whole(), Exposure, 1.0f), minus = valued(whole(), Exposure, -1.0f);
    check(cv::countNonZero(valueMaps({plus, minus}, canvas)[Exposure]) == 0, "opposite values cancel");
    check(valueMaps(masks, cv::Size())[Exposure].empty(), "an empty canvas gives nothing");
}

void source()
{
    const cv::Size size(300, 200);
    // A map whose value is its normalized u, to read where each pixel came from.
    Geometry::Settings straight;
    cv::Mat map(size, CV_32FC1);
    for (int y = 0; y < map.rows; ++y)
        for (int x = 0; x < map.cols; ++x)
            map.at<float>(y, x) = (float(x) + 0.5f) / float(map.cols);
    check(LocalAdjustments::toSource(map, straight, size).data == map.data, "without a transform it is the same map");
    check(LocalAdjustments::toSource(cv::Mat(), straight, size).empty(), "an empty map stays empty");

    // A mirror: the canvas's left is the source's right.
    Geometry::Settings mirrored;
    mirrored.flipHorizontal = true;
    const cv::Mat flipped = LocalAdjustments::toSource(map, mirrored, size);
    check(flipped.size() == size && std::abs(flipped.at<float>(100, 30) - (1.0f - (30.5f / 300.0f))) < 0.005f, "a mirror reads the other side");

    // A quarter turn swaps the sides, and the map comes back to the source's.
    Geometry::Settings turned;
    turned.quarterTurns = 1;
    const cv::Size canvasOfTurn = Geometry::canvasSize(turned, size);
    cv::Mat turnedMap(canvasOfTurn, CV_32FC1);
    for (int y = 0; y < turnedMap.rows; ++y)
        for (int x = 0; x < turnedMap.cols; ++x)
            turnedMap.at<float>(y, x) = (float(y) + 0.5f) / float(turnedMap.rows);
    const cv::Mat back = LocalAdjustments::toSource(turnedMap, turned, size);
    check(back.size() == size, "a turned map comes back at the source's size");
    // A clockwise turn sends the source's left to the canvas's top.
    check(std::abs(back.at<float>(100, 30) - 30.5f / 300.0f) < 0.01f, "with each pixel where it belongs");

    // A landmark: a rotated canvas's value is read at the mapped source position.
    Geometry::Settings rotated;
    rotated.rotation = 8.0f;
    const cv::Point2d sourcePoint(0.3, 0.4);
    const auto canvasPoint = Geometry::toCanvas(rotated, size, sourcePoint);
    check(canvasPoint.has_value(), "the point has its place on the canvas");
    if (canvasPoint) {
        cv::Mat ramp(size, CV_32FC1);
        for (int y = 0; y < ramp.rows; ++y)
            for (int x = 0; x < ramp.cols; ++x)
                ramp.at<float>(y, x) = (float(x) + 0.5f) / float(ramp.cols);
        const cv::Mat read = LocalAdjustments::toSource(ramp, rotated, size);
        const float found = read.at<float>(int(0.4 * 200), int(0.3 * 300));
        if (std::abs(double(found) - canvasPoint->x) > 0.01)
            std::cerr << "landmark " << found << " vs " << canvasPoint->x << '\n';
        check(std::abs(double(found) - canvasPoint->x) <= 0.01, "the same place as the geometry says");
    }
}

void bounds()
{
    Masks masks{valued(whole(), LocalValues::Exposure, 50.0f)};
    masks[0].opacity = 3.0f;
    masks[0].shapes[0].feather = -1.0f;
    masks[0].shapes.push_back(brush({{9.0f, -9.0f}}, 7.0f, 4.0f, 4.0f));
    check(bound(masks), "out of range is clamped, not refused");
    check(masks[0].opacity == 1.0f && masks[0].values[0] == 5.0f && masks[0].shapes[0].feather == 0.0f, "to its limits");
    const BrushStroke &stroke = masks[0].shapes[1].strokes[0];
    check(stroke.size == 1.0f && stroke.feather == 1.0f && stroke.flow == 1.0f && stroke.points[0].x == 5.0f && stroke.points[0].y == -4.0f, "strokes too");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinite = std::numeric_limits<float>::infinity();
    Masks bad{valued(whole(), LocalValues::Exposure, nan)};
    check(!bound(bad), "a value that is no number is refused");
    bad = {maskOf(whole())};
    bad[0].opacity = infinite;
    check(!bound(bad), "so is an opacity");
    bad = {maskOf(radial({nan, 0.5f}, 0.2f, 0.2f, 0.5f))};
    check(!bound(bad), "a centre");
    bad = {maskOf(brush({{0.5f, nan}}, 0.1f))};
    check(!bound(bad), "a stroke's point");
    bad = {maskOf(linear({0.5f, 0.5f}, {infinite, 0.5f}))};
    check(!bound(bad), "a gradient's end");
    Masks many(65);
    check(!bound(many), "more masks than a recipe keeps");

    Masks copy = masks;
    check(copy == masks, "equal masks compare equal");
    copy[0].shapes[1].strokes[0].points[0].x = 1.0f;
    check(!(copy == masks), "a stroke's point tells them apart");

    const auto keyOf = [](const Masks &value) {
        QByteArray key;
        QDataStream stream(&key, QIODevice::WriteOnly);
        appendKey(stream, value);
        return key;
    };
    check(keyOf({}).isEmpty(), "no masks add nothing to a key: renders without any keep theirs");
    check(keyOf(masks) != keyOf(copy), "a change makes another key");
    Masks renamed = masks;
    renamed[0].name = QStringLiteral("Ciel");
    check(keyOf(masks) == keyOf(renamed), "a name changes no pixel");
}

} // namespace

int main()
{
    shapes();
    resolutions();
    combinations();
    maps();
    source();
    bounds();
    if (passed)
        std::cout << "local adjustments: ok\n";
    return passed ? 0 : 1;
}
