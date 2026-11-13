#include "LocalAdjustments.h"
#include "Pixels.h"
#include "image_testing.h"
#include "Resize.h"

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

const ipr::Size canvas(400, 300);

Shape linear(ipr::Point2f start, ipr::Point2f end)
{
    Shape shape;
    shape.kind = Kind::Linear;
    shape.start = start;
    shape.end = end;
    return shape;
}

Shape radial(ipr::Point2f centre, float radiusX, float radiusY, float feather)
{
    Shape shape;
    shape.kind = Kind::Radial;
    shape.center = centre;
    shape.radiusX = radiusX;
    shape.radiusY = radiusY;
    shape.feather = feather;
    return shape;
}

Shape brush(std::vector<ipr::Point2f> points, float size, float feather = 0.0f, float flow = 1.0f)
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

float at(const ipr::Image &map, float u, float v)
{
    return imagetest::at<float>(map, int(v * float(map.height())), int(u * float(map.width())));
}

double worstDifference(const ipr::Image &a, const ipr::Image &b)
{
    return ipr::largestDifference(a, b);
}

void shapes()
{
    // A vertical gradient: whole before its start, nothing after its end, half in the middle.
    const ipr::Image gradient = coverage(maskOf(linear({0.5f, 0.25f}, {0.5f, 0.75f})), canvas);
    check((gradient.depth() == ipr::Depth::F32 && gradient.channels() == 1) && gradient.size() == canvas, "the coverage has the canvas's size");
    check(at(gradient, 0.5f, 0.1f) == 1.0f, "before the start: all of it");
    check(at(gradient, 0.5f, 0.9f) == 0.0f, "after the end: nothing");
    check(std::abs(at(gradient, 0.5f, 0.5f) - 0.5f) < 0.02f, "in the middle: half");
    check(at(gradient, 0.1f, 0.5f) == at(gradient, 0.9f, 0.5f), "the same along the gradient");
    const ipr::Image point = coverage(maskOf(linear({0.5f, 0.5f}, {0.5f, 0.5f})), canvas);
    check(at(point, 0.1f, 0.1f) == 1.0f, "a gradient of no length covers all, without dividing by zero");

    // A radial: inside at the centre, outside beyond its radius, along each axis's own radius.
    const ipr::Image ellipse = coverage(maskOf(radial({0.5f, 0.5f}, 0.25f, 0.25f, 0.0f)), canvas);
    check(at(ellipse, 0.5f, 0.5f) == 1.0f, "the centre is inside");
    check(at(ellipse, 0.7f, 0.5f) == 1.0f && at(ellipse, 0.8f, 0.5f) == 0.0f, "up to its radius across");
    check(at(ellipse, 0.5f, 0.7f) == 1.0f && at(ellipse, 0.5f, 0.8f) == 0.0f, "and down");
    const ipr::Image soft = coverage(maskOf(radial({0.5f, 0.5f}, 0.25f, 0.25f, 1.0f)), canvas);
    const float halfway = at(soft, 0.625f, 0.5f);
    check(imagetest::at<float>(soft, 150, 200) > 0.99f && halfway > 0.3f && halfway < 0.7f, "a feather fades from the centre");
    Shape turned = radial({0.5f, 0.5f}, 0.3f, 0.05f, 0.0f);
    turned.angle = 90.0f;
    const ipr::Image upright = coverage(maskOf(turned), canvas);
    // Turned a quarter: the long radius, 0.3 of the width, now stands along the height.
    check(at(upright, 0.5f, 0.85f) == 1.0f && at(upright, 0.75f, 0.5f) == 0.0f, "an angle turns the ellipse");
    const ipr::Image tiny = coverage(maskOf(radial({0.5f, 0.5f}, 0.0f, 0.0f, 0.5f)), canvas);
    check(ipr::allFinite(tiny), "a radius of zero stays finite");

    // A stroke: along its path, up to its radius, not beyond.
    const float size = 0.05f;   // 20 px on this canvas
    const ipr::Image stroke = coverage(maskOf(brush({{0.25f, 0.5f}, {0.75f, 0.5f}}, size)), canvas);
    check(at(stroke, 0.5f, 0.5f) == 1.0f, "on the path");
    check(imagetest::at<float>(stroke, 150 + 18, 200) == 1.0f && imagetest::at<float>(stroke, 150 + 22, 200) == 0.0f, "up to its radius");
    check(at(stroke, 0.1f, 0.5f) == 0.0f, "not past its end");
    const ipr::Image dab = coverage(maskOf(brush({{0.5f, 0.5f}}, size)), canvas);
    check(at(dab, 0.5f, 0.5f) == 1.0f && at(dab, 0.75f, 0.5f) == 0.0f, "a single point is a dab");
    const ipr::Image none = coverage(maskOf(brush({}, size)), canvas);
    check(ipr::countNonZero(none) == 0, "a stroke without points paints nothing");
    const ipr::Image off = coverage(maskOf(brush({{3.0f, 3.0f}}, size)), canvas);
    check(ipr::countNonZero(off) == 0, "a stroke off the canvas paints nothing");
    const ipr::Image flowed = coverage(maskOf(brush({{0.5f, 0.5f}}, size, 0.0f, 0.5f)), canvas);
    check(std::abs(at(flowed, 0.5f, 0.5f) - 0.5f) < 1e-5f, "the flow bounds a stroke");

    // A stroke over itself does not build up; a second stroke does.
    const ipr::Image back = coverage(maskOf(brush({{0.25f, 0.5f}, {0.75f, 0.5f}, {0.25f, 0.5f}}, size, 0.0f, 0.5f)), canvas);
    check(std::abs(at(back, 0.5f, 0.5f) - 0.5f) < 1e-5f, "a stroke over itself stays at its flow");
    Shape twice = brush({{0.5f, 0.5f}}, size, 0.0f, 0.5f);
    twice.strokes.push_back(twice.strokes.front());
    check(std::abs(at(coverage(maskOf(twice), canvas), 0.5f, 0.5f) - 0.75f) < 1e-5f, "two strokes build up as paint");

    // Inverted: the complement, all over.
    Shape inverse = linear({0.5f, 0.25f}, {0.5f, 0.75f});
    inverse.inverted = true;
    const ipr::Image flipped = coverage(maskOf(inverse), canvas);
    const ipr::Image sum = ipr::mapped(flipped, gradient, [](float x, float y) { return x + y; });
    check(worstDifference(sum, ipr::filled(canvas, ipr::Depth::F32, 1, 1)) < 1e-5, "an inverted shape is its complement");
    check(ipr::countNonZero(coverage(Mask{}, canvas)) == 0, "a mask without shapes covers nothing");
}

void ranges()
{
    // A ramp of tones from black to white, across.
    ipr::Image rgb = ipr::Image::create(canvas, ipr::Depth::F32, 3);
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x) {
            const float linearValue = std::pow(float(x) / float(rgb.width() - 1), 2.2f);
            imagetest::at<ipr::Vec3f>(rgb, y, x) = ipr::Vec3f(linearValue, linearValue, linearValue);
        }
    const ipr::Image luminance = rangeLuminance(rgb);
    check((luminance.depth() == ipr::Depth::F32 && luminance.channels() == 1) && luminance.size() == canvas, "a luminance of the same size");
    check(at(luminance, 0.0f, 0.5f) < 0.01f && imagetest::at<float>(luminance, 150, 399) > 0.99f, "from 0 to 1");
    check(std::abs(at(luminance, 0.5f, 0.5f) - 0.5f) < 0.03f, "encoded as the eye reads it");
    ipr::Image extended = imagetest::filledWith({2, 2}, {-0.5, -0.5, -0.5});
    imagetest::at<ipr::Vec3f>(extended, 0, 0) = ipr::Vec3f(9.f, 9.f, 9.f);
    const ipr::Image bounded = rangeLuminance(extended);
    check(std::abs(imagetest::at<float>(bounded, 0, 0) - 1.0f) < 1e-5f && imagetest::at<float>(bounded, 1, 1) == 0.0f, "bounded at both ends");

    Shape shadows;
    shadows.kind = Kind::LuminanceRange;
    shadows.rangeLow = 0.0f;
    shadows.rangeHigh = 0.3f;
    shadows.rangeFeather = 0.1f;
    const ipr::Image dark = coverage(maskOf(shadows), canvas, luminance);
    check(at(dark, 0.1f, 0.5f) == 1.0f, "inside its tones: all of it");
    check(at(dark, 0.7f, 0.5f) == 0.0f, "beyond them: nothing");
    const float fading = at(dark, 0.35f, 0.5f);
    check(fading > 0.0f && fading < 1.0f, "fading between");
    check(ipr::countNonZero(coverage(maskOf(shadows), canvas)) == 0, "without an image, a range covers nothing");
    check(ipr::countNonZero(coverage(maskOf(shadows), canvas, ipr::filled({10, 10}, ipr::Depth::F32, 1, 0))) == 0, "nor with one of another size");

    // With a zone: the tones of that zone.
    Mask inZone = maskOf(linear({0.5f, 0.45f}, {0.5f, 0.55f}));
    Shape intersect = shadows;
    intersect.combine = Combine::Intersect;
    inZone.shapes.push_back(intersect);
    const ipr::Image both = coverage(inZone, canvas, luminance);
    check(at(both, 0.1f, 0.1f) == 1.0f && at(both, 0.1f, 0.9f) == 0.0f && at(both, 0.8f, 0.1f) == 0.0f, "an intersection takes the tones of a zone");

    Masks masks{valued(shadows, LocalValues::Exposure, 1.0f)};
    check(usesLuminance(masks) && !usesLuminance({valued(whole(), LocalValues::Exposure, 1.0f)}), "a range says it reads the image");
    check(at(valueMaps(masks, canvas, luminance)[LocalValues::Exposure], 0.1f, 0.5f) == 1.0f, "and gives its value there");
    masks[0].shapes[0].rangeLow = 0.8f;
    masks[0].shapes[0].rangeHigh = 0.2f;
    check(bound(masks) && masks[0].shapes[0].rangeHigh == 0.8f, "a range keeps its ends in order");
    masks[0].shapes[0].rangeFeather = std::numeric_limits<float>::quiet_NaN();
    check(!bound(masks), "and a number as its fade");
}

void resolutions()
{
    // The same zone at every resolution: a preview is the export in small.
    const Mask mask = maskOf(radial({0.4f, 0.6f}, 0.2f, 0.3f, 0.6f));
    const ipr::Image large = coverage(mask, ipr::Size(1600, 1200));
    ipr::Image reduced;
    reduced = ipr::resized(large, canvas, ipr::Interpolation::Area);
    const double worst = worstDifference(reduced, coverage(mask, canvas));
    if (worst > 0.01)
        std::cerr << "radial worst " << worst << '\n';
    check(worst <= 0.01, "a radial is the same at both resolutions");

    const Mask strokes = maskOf(brush({{0.2f, 0.2f}, {0.6f, 0.7f}, {0.8f, 0.3f}}, 0.06f, 0.5f));
    reduced = ipr::resized(coverage(strokes, ipr::Size(1600, 1200)), canvas, ipr::Interpolation::Area);
    const double worstStroke = worstDifference(reduced, coverage(strokes, canvas));
    // A soft edge falls by up to 0.125 per pixel here: an average over 4 x 4 samples differs a little.
    if (worstStroke > 0.03)
        std::cerr << "stroke worst " << worstStroke << '\n';
    check(worstStroke <= 0.03, "so is a stroke");
    const Mask ramp = maskOf(linear({0.1f, 0.2f}, {0.9f, 0.7f}));
    reduced = ipr::resized(coverage(ramp, ipr::Size(1600, 1200)), canvas, ipr::Interpolation::Area);
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
    const ipr::Image removed = coverage(without, canvas);
    check(at(removed, 0.1f, 0.5f) == 0.0f && at(removed, 0.9f, 0.5f) == 1.0f, "a subtraction removes its zone");

    Mask shared = maskOf(radial({0.5f, 0.5f}, 0.3f, 0.3f, 0.0f));
    Shape half = left;
    half.combine = Combine::Intersect;
    shared.shapes.push_back(half);
    const ipr::Image crossed = coverage(shared, canvas);
    check(at(crossed, 0.3f, 0.5f) == 1.0f && at(crossed, 0.7f, 0.5f) == 0.0f && at(crossed, 0.1f, 0.5f) == 0.0f, "an intersection keeps what both cover");

    Mask startsCut = maskOf(cut);
    check(ipr::countNonZero(coverage(startsCut, canvas)) == 0, "subtracting from nothing leaves nothing");
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
    for (const ipr::Image &map : empty.values)
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
    check((full[Exposure].depth() == ipr::Depth::F32 && full[Exposure].channels() == 1) && at(full[Exposure], 0.5f, 0.5f) == 1.0f && at(full[Exposure], 0.01f, 0.01f) == 1.0f, "the value all over");
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
    const ipr::Image expected = ipr::mapped(coverage(a, canvas), coverage(b, canvas), [](float x, float y) { return x * 1.0f - y * 0.5f; });
    check(worstDifference(abc[Exposure], expected) < 1e-5, "masks add up");
    check(worstDifference(abc[Contrast], ipr::mapped(coverage(b, canvas), [](float v) { return v * 20.0f; })) < 1e-4, "each setting by itself");

    // Two opposite values cancel out where both cover.
    Mask plus = valued(whole(), Exposure, 1.0f), minus = valued(whole(), Exposure, -1.0f);
    check(ipr::countNonZero(valueMaps({plus, minus}, canvas)[Exposure]) == 0, "opposite values cancel");
    check(valueMaps(masks, ipr::Size())[Exposure].empty(), "an empty canvas gives nothing");
}

void source()
{
    const ipr::Size size(300, 200);
    // A map whose value is its normalized u, to read where each pixel came from.
    Geometry::Settings straight;
    ipr::Image map = ipr::Image::create(size, ipr::Depth::F32, 1);
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            imagetest::at<float>(map, y, x) = (float(x) + 0.5f) / float(map.width());
    check(LocalAdjustments::toSource(map, straight, size).block() == map.block(), "without a transform it is the same map");
    check(LocalAdjustments::toSource(ipr::Image(), straight, size).empty(), "an empty map stays empty");

    // A mirror: the canvas's left is the source's right.
    Geometry::Settings mirrored;
    mirrored.flipHorizontal = true;
    const ipr::Image flipped = LocalAdjustments::toSource(map, mirrored, size);
    check(flipped.size() == size && std::abs(imagetest::at<float>(flipped, 100, 30) - (1.0f - (30.5f / 300.0f))) < 0.005f, "a mirror reads the other side");

    // A quarter turn swaps the sides, and the map comes back to the source's.
    Geometry::Settings turned;
    turned.quarterTurns = 1;
    const ipr::Size canvasOfTurn = Geometry::canvasSize(turned, size);
    ipr::Image turnedMap = ipr::Image::create(canvasOfTurn, ipr::Depth::F32, 1);
    for (int y = 0; y < turnedMap.height(); ++y)
        for (int x = 0; x < turnedMap.width(); ++x)
            imagetest::at<float>(turnedMap, y, x) = (float(y) + 0.5f) / float(turnedMap.height());
    const ipr::Image back = LocalAdjustments::toSource(turnedMap, turned, size);
    check(back.size() == size, "a turned map comes back at the source's size");
    // A clockwise turn sends the source's left to the canvas's top.
    check(std::abs(imagetest::at<float>(back, 100, 30) - 30.5f / 300.0f) < 0.01f, "with each pixel where it belongs");

    // A landmark: a rotated canvas's value is read at the mapped source position.
    Geometry::Settings rotated;
    rotated.rotation = 8.0f;
    const ipr::Point2d sourcePoint(0.3, 0.4);
    const auto canvasPoint = Geometry::toCanvas(rotated, size, sourcePoint);
    check(canvasPoint.has_value(), "the point has its place on the canvas");
    if (canvasPoint) {
        ipr::Image ramp = ipr::Image::create(size, ipr::Depth::F32, 1);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x)
                imagetest::at<float>(ramp, y, x) = (float(x) + 0.5f) / float(ramp.width());
        const ipr::Image read = LocalAdjustments::toSource(ramp, rotated, size);
        const float found = imagetest::at<float>(read, int(0.4 * 200), int(0.3 * 300));
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

void cones()
{
    Shape shape;
    shape.kind = Kind::Triangle;
    shape.feather = 0;
    const auto triangle = coverage(maskOf(shape), canvas);
    check(at(triangle, .5f, .5f) == 1, "triangle interior");
    check(at(triangle, .65f, .5f) == 0 && at(triangle, .5f, .8f) == 0, "cone sides and end are bounded");
    check(at(triangle, .54f, .3f) == 0, "triangle begins at a point");
    shape.kind = Kind::Trapezoid;
    const auto trapezoid = coverage(maskOf(shape), canvas);
    check(at(trapezoid, .54f, .3f) == 1, "trapezoid has a finite near width");
    shape.startWidth = 0;
    check(worstDifference(triangle, coverage(maskOf(shape), canvas)) == 0, "zero near width becomes a triangle");
    shape.kind = Kind::Triangle;
    shape.feather = .5f;
    const auto soft = coverage(maskOf(shape), canvas);
    check(at(soft, .58f, .5f) > 0 && at(soft, .58f, .5f) < 1, "cone feather is progressive");
    const auto large = coverage(maskOf(shape), {800, 600});
    check(std::abs(at(soft,.58f,.5f)-at(large,.58f,.5f)) < .03, "cone feather follows resolution");
    shape.inverted = true;
    const auto inverse = coverage(maskOf(shape), canvas);
    check(std::abs(at(soft,.58f,.5f)+at(inverse,.58f,.5f)-1) < 1e-6f, "cone inversion");
    shape.inverted = false;
    shape.start = {.25f,.5f}; shape.end = {.75f,.5f}; shape.feather = 0;
    check(at(coverage(maskOf(shape),canvas),.5f,.6f) == 1, "rotated cone");
    shape.end = shape.start;
    const auto empty = coverage(maskOf(shape),canvas);
    check(ipr::extremesOf(empty).largest == 0, "coincident endpoints produce an empty mask");
    Masks masks{maskOf(shape)};
    masks[0].shapes[0].endWidth = std::numeric_limits<float>::quiet_NaN();
    check(!bound(masks), "invalid cone width rejected");
    masks = {valued(Shape{}, LocalValues::Exposure, 1)};
    masks[0].shapes[0].kind = Kind::Trapezoid;
    QByteArray before, after;
    QDataStream first(&before, QIODevice::WriteOnly);
    appendKey(first, masks);
    masks[0].shapes[0].endWidth += .1f;
    QDataStream second(&after, QIODevice::WriteOnly);
    appendKey(second, masks);
    check(before != after, "cone width invalidates mask cache");
    Shape spot;
    spot.kind = Kind::Triangle;
    spot.diffusion = 1;
    spot.feather = .5f;
    const auto beam = coverage(maskOf(spot), canvas);
    check(std::abs(at(beam,.5f,.3f) - .9f) < .01f && std::abs(at(beam,.5f,.5f) - .5f) < .01f &&
          std::abs(at(beam,.5f,.7f) - .1f) < .01f, "spot follows a linear axial falloff despite side feather");
    // An intermediate contour must widen along straight sides, rather than
    // closing into a rounded lobe as the axial strength falls.
    Shape widening = spot;
    widening.diffusion = .7f;
    const auto wedge = coverage(maskOf(widening), canvas);
    for (const int y : {105, 135, 165}) {
        const float along = (float(y) - 74.5f) / 150.0f;
        const float expectedHalfWidth = 80.0f * along * .75f;
        int count = 0;
        for (int x = 0; x < canvas.width; ++x)
            count += imagetest::at<float>(wedge, y, x) >= .5f;
        std::cout << "cone-contour triangle y=" << y << " halfWidth=" << float(count) * .5f
                  << " expected=" << expectedHalfWidth << '\n';
        check(std::abs(float(count) * .5f - expectedHalfWidth) <= 1,
              "half-coverage width grows linearly towards the base");
    }
    widening.kind = Kind::Trapezoid;
    const auto broad = coverage(maskOf(widening), canvas);
    for (const int y : {135, 165}) {
        const float along = (float(y) - 74.5f) / 150.0f;
        const float expectedHalfWidth = (24.0f + 56.0f * along) * .75f;
        int count = 0;
        for (int x = 0; x < canvas.width; ++x)
            count += imagetest::at<float>(broad, y, x) >= .5f;
        std::cout << "cone-contour trapezoid y=" << y << " halfWidth=" << float(count) * .5f
                  << " expected=" << expectedHalfWidth << '\n';
        check(std::abs(float(count) * .5f - expectedHalfWidth) <= 1,
              "trapezoid keeps straight widening intensity contours");
    }
    float previous = 1;
    for (int y = 90; y < 225; ++y) {
        const float value = imagetest::at<float>(beam, y, 200);
        check(std::isfinite(value) && value >= 0 && value <= previous + 1e-6f, "axial falloff is finite bounded and monotone");
        previous = value;
    }
    const auto bigBeam = coverage(maskOf(spot), {800, 600});
    check(std::abs(at(beam,.5f,.4f)-at(bigBeam,.5f,.4f)) < .03f, "spot follows resolution");
    spot.start = {.25f,.5f}; spot.end = {.75f,.5f};
    const auto horizontal = coverage(maskOf(spot), canvas);
    check(at(horizontal,.3f,.5f) > at(horizontal,.7f,.5f), "falloff follows rotated beam axis");
    spot.inverted = true;
    check(std::abs(at(horizontal,.4f,.5f)+at(coverage(maskOf(spot),canvas),.4f,.5f)-1) < 1e-6f,
          "inversion applies to the full diffused beam");
    masks = {valued(spot, LocalValues::Exposure, 1)};
    before.clear(); after.clear();
    QDataStream third(&before, QIODevice::WriteOnly);
    appendKey(third, masks);
    masks[0].shapes[0].diffusion = .5f;
    QDataStream fourth(&after, QIODevice::WriteOnly);
    appendKey(fourth, masks);
    check(before != after, "diffusion invalidates cache");
    masks[0].shapes[0].diffusion = 2;
    check(bound(masks) && masks[0].shapes[0].diffusion == 1, "diffusion bounded");
    masks[0].shapes[0].diffusion = std::numeric_limits<float>::quiet_NaN();
    check(!bound(masks), "nonfinite diffusion refused");
}

int main()
{
    cones();
    shapes();
    ranges();
    resolutions();
    combinations();
    maps();
    source();
    bounds();
    if (passed)
        std::cout << "local adjustments: ok\n";
    return passed ? 0 : 1;
}
