#include "PerceptualColour.h"
#include "PointColor.h"
#include "WorkingColorSpace.h"

#include <cmath>
#include <iostream>

namespace {
constexpr double DegreesPerRadian = 180.0 / 3.14159265358979323846;

bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

cv::Vec3d lch(double lightness, double chroma, double hueDegrees)
{
    const double h = hueDegrees / DegreesPerRadian;
    return {lightness, chroma * std::cos(h), chroma * std::sin(h)};
}

double chromaOf(const cv::Vec3d &lab) { return std::hypot(lab[1], lab[2]); }
double hueOf(const cv::Vec3d &lab) { return std::atan2(lab[2], lab[1]) * DegreesPerRadian; }

// One point picked on an orange.
PointColor::Parameters single(const PointColor::Point &point)
{
    PointColor::Parameters parameters;
    parameters.points[0] = point;
    parameters.count = 1;
    return parameters;
}

const cv::Vec3d Orange = lch(0.65, 0.12, 50.0);

PointColor::Point orange()
{
    PointColor::Point point;
    point.source = Orange;
    return point;
}

cv::Vec3d adjusted(cv::Vec3d lab, const PointColor::Parameters &parameters)
{
    PointColor::adjust(lab, parameters);
    return lab;
}

bool identity()
{
    bool ok = check(!PointColor::active(PointColor::Parameters{}), "no point is inactive");
    PointColor::Parameters parameters = single(orange());
    ok &= check(!PointColor::active(parameters), "a point without shift is inactive");
    parameters.points[1].hueShift = 50;   // beyond count
    ok &= check(!PointColor::active(parameters), "points beyond the count are ignored");
    const cv::Vec3f rgb(0.3f, 0.2f, 0.1f);
    ok &= check(PointColor::apply(rgb, parameters) == rgb, "inactive settings leave the colour unchanged");
    parameters.points[0].hueShift = 100;
    const cv::Vec3f grey(0.4f, 0.4f, 0.4f);
    ok &= check(PointColor::apply(grey, parameters) == grey, "exact greys are unchanged");
    return ok;
}

bool neighbourhood()
{
    const PointColor::Point point = orange();
    bool ok = check(std::abs(PointColor::weight(Orange, point) - 1.0) < 1e-9, "weight 1 at the source");
    const double near = PointColor::weight(lch(0.65, 0.12, 58.0), point);
    const double far = PointColor::weight(lch(0.65, 0.12, 75.0), point);
    ok &= check(near > far && far >= 0.0 && near < 1.0, "weight falls with the hue distance");
    ok &= check(PointColor::weight(lch(0.65, 0.12, 230.0), point) == 0.0, "opposite hue is outside the range");
    ok &= check(PointColor::weight(lch(0.65, 0.12, 50.0) * 1e-3, point) == 0.0, "near greys are outside");
    ok &= check(PointColor::weight(lch(0.25, 0.12, 50.0), point) < PointColor::weight(lch(0.6, 0.12, 50.0), point),
                "weight falls with the lightness distance");
    PointColor::Point narrow = point, wide = point;
    narrow.range = 0;
    wide.range = 100;
    const cv::Vec3d neighbour = lch(0.6, 0.09, 65.0);
    ok &= check(PointColor::weight(neighbour, narrow) < PointColor::weight(neighbour, point) &&
                PointColor::weight(neighbour, point) < PointColor::weight(neighbour, wide),
                "the range widens the neighbourhood");
    return ok;
}

bool shifts()
{
    PointColor::Point point = orange();
    point.hueShift = 100;
    const cv::Vec3d rotated = adjusted(Orange, single(point));
    bool ok = check(hueOf(rotated) > 50.0 + 1.0, "a positive hue shift turns the source hue up");
    ok &= check(std::abs(chromaOf(rotated) - chromaOf(Orange)) < 1e-9, "the hue shift keeps the chroma");
    const cv::Vec3d blue = lch(0.5, 0.1, 250.0);
    ok &= check(cv::norm(adjusted(blue, single(point)) - blue) < 1e-12, "a colour outside the range is unchanged");

    point = orange();
    point.saturationShift = -100;
    ok &= check(chromaOf(adjusted(Orange, single(point))) < 1e-9, "saturation -100 makes the source grey");
    point.saturationShift = 100;
    ok &= check(chromaOf(adjusted(Orange, single(point))) > chromaOf(Orange) * 1.5, "saturation +100 adds chroma");

    point = orange();
    point.luminanceShift = 100;
    ok &= check(adjusted(Orange, single(point))[0] > Orange[0], "luminance +100 lightens the source");
    point.luminanceShift = -100;
    ok &= check(adjusted(Orange, single(point))[0] < Orange[0], "luminance -100 darkens the source");
    return ok;
}

bool variance()
{
    PointColor::Point point = orange();
    const cv::Vec3d neighbour = lch(0.65, 0.12, 60.0);
    point.variance = -100;
    const double gathered = hueOf(adjusted(neighbour, single(point)));
    point.variance = 100;
    const double spread = hueOf(adjusted(neighbour, single(point)));
    bool ok = check(gathered < 60.0 && gathered > 50.0, "negative variance gathers the hues toward the source");
    ok &= check(spread > 60.0, "positive variance spreads the hues");
    ok &= check(std::abs(hueOf(adjusted(Orange, single(point))) - 50.0) < 1e-9, "the source itself does not move");
    return ok;
}

bool overlap()
{
    PointColor::Point point = orange();
    point.hueShift = 100;
    const double once = hueOf(adjusted(Orange, single(point))) - 50.0;
    PointColor::Parameters twice = single(point);
    twice.points[1] = point;
    twice.count = 2;
    const double shared = hueOf(adjusted(Orange, twice)) - 50.0;
    bool ok = check(shared <= once + 1e-9 && shared > once * 0.5, "two identical points share their reach");

    // The order matters: the second point sees the colour the first one produced.
    PointColor::Point lighten = orange(), turn = orange();
    lighten.luminanceShift = 100;
    turn.hueShift = 100;
    PointColor::Parameters first, second;
    first.points[0] = lighten;
    first.points[1] = turn;
    second.points[0] = turn;
    second.points[1] = lighten;
    first.count = second.count = 2;
    ok &= check(cv::norm(adjusted(Orange, first) - adjusted(Orange, second)) > 1e-6, "the points act in order");
    return ok;
}

bool robustness()
{
    PointColor::Point point = orange();
    point.hueShift = 250;
    point.saturationShift = -400;
    point.range = 300;
    const cv::Vec3d clamped = adjusted(Orange, single(point));
    point.hueShift = 100;
    point.saturationShift = -100;
    point.range = 100;
    bool ok = check(cv::norm(clamped - adjusted(Orange, single(point))) < 1e-12, "values are clamped");
    PointColor::Parameters many = single(orange());
    many.count = 1000;
    many.points[0].hueShift = 100;
    const cv::Vec3d result = adjusted(Orange, many);
    ok &= check(std::isfinite(result[0]) && std::isfinite(result[1]), "a count above the maximum is bounded");
    const cv::Vec3d black = adjusted({0.0, 0.0, 0.0}, single(point));
    ok &= check(black == cv::Vec3d(0.0, 0.0, 0.0), "black stays black");
    return ok;
}

bool perceptualStage()
{
    PerceptualColour::Parameters parameters;
    parameters.pointColor = single(orange());
    parameters.pointColor.points[0].saturationShift = -100;
    const cv::Vec3d sourceRgb = WorkingColor::oklabToWorking(Orange);
    cv::Mat image(1, 2, CV_32FC3);
    image.at<cv::Vec3f>(0, 0) = cv::Vec3f(sourceRgb);
    image.at<cv::Vec3f>(0, 1) = cv::Vec3f(0.3f, 0.3f, 0.3f);
    PerceptualColour::apply(image, parameters);
    const cv::Vec3d out = WorkingColor::workingToOklab(cv::Vec3d(image.at<cv::Vec3f>(0, 0)));
    bool ok = check(chromaOf(out) < 1e-4, "the perceptual stage applies the point colour");
    ok &= check(image.at<cv::Vec3f>(0, 1) == cv::Vec3f(0.3f, 0.3f, 0.3f), "the stage leaves greys");
    return ok;
}
} // namespace

int main()
{
    bool ok = identity();
    ok &= neighbourhood();
    ok &= shifts();
    ok &= variance();
    ok &= overlap();
    ok &= robustness();
    ok &= perceptualStage();
    if (ok) std::cout << "pointColorTests passed\n";
    return ok ? 0 : 1;
}
