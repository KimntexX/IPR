#include "ColorAdjustments.h"
#include "PerceptualColour.h"
#include "PointColor.h"
#include "Sidecar.h"
#include "WorkingColorSpace.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

namespace {
constexpr double Pi = 3.14159265358979323846;

bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

double hueDifference(double hue, double reference)
{
    double d = std::fmod(hue - reference + 3.0 * Pi, 2.0 * Pi) - Pi;
    return d;
}

// The orange source of 6V1A0327, as Lightroom wrote it.
const PointColor::Hsv Orange{0.548111, 0.502314, 0.161747};

PointColor::Point orange()
{
    PointColor::Point point;
    point.source = Orange;
    return point;
}

PointColor::Parameters single(const PointColor::Point &point)
{
    PointColor::Parameters parameters;
    parameters.points[0] = point;
    parameters.count = 1;
    return parameters;
}

// The HSV a colour reaches.
PointColor::Hsv adjusted(const PointColor::Hsv &hsv, const PointColor::Parameters &parameters)
{
    cv::Vec3d rgb = PointColor::fromHsv(hsv);
    PointColor::adjust(rgb, parameters);
    return PointColor::toHsv(rgb);
}

bool conversions()
{
    bool ok = true;
    for (const PointColor::Hsv &hsv : {Orange, PointColor::Hsv{3.908962, 0.586369, 0.432142},
                                       PointColor::Hsv{0.0691, 0.689713, 0.208665}, PointColor::Hsv{5.9, 0.3, 0.8}}) {
        const PointColor::Hsv back = PointColor::toHsv(PointColor::fromHsv(hsv));
        ok &= check(std::abs(hueDifference(back[0], hsv[0])) < 1e-9 && std::abs(back[1] - hsv[1]) < 1e-9 &&
                    std::abs(back[2] - hsv[2]) < 1e-9, "HSV round trips through the working space");
    }
    const PointColor::Hsv grey = PointColor::toHsv({0.3, 0.3, 0.3});
    // Rounded published matrices: D65 white lands on D50 white within about 1e-4.
    ok &= check(grey[1] < 1e-3 && std::abs(grey[2] - 0.3) < 1e-3, "a grey has no saturation and keeps its value");
    return ok;
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
    const cv::Vec3f blue(cv::Vec3d(PointColor::fromHsv({4.0, 0.6, 0.5})));
    ok &= check(PointColor::apply(blue, parameters) == blue, "a colour outside the range is not even converted");
    return ok;
}

bool neighbourhood()
{
    const PointColor::Point point = orange();
    bool ok = check(std::abs(PointColor::weight(Orange, point) - 1.0) < 1e-9, "weight 1 at the source");
    const double near = PointColor::weight({Orange[0] + 0.3, Orange[1], Orange[2]}, point);
    const double far = PointColor::weight({Orange[0] + 0.6, Orange[1], Orange[2]}, point);
    ok &= check(near > far && far >= 0.0 && near < 1.0, "weight falls with the hue distance");
    ok &= check(PointColor::weight({Orange[0] + Pi, Orange[1], Orange[2]}, point) == 0.0,
                "opposite hue is outside the range");
    ok &= check(PointColor::weight({Orange[0], 0.001, Orange[2]}, point) < 0.02, "near greys fade out");
    ok &= check(PointColor::weight({Orange[0], Orange[1], 0.8}, point) < PointColor::weight({Orange[0], Orange[1], 0.25}, point),
                "weight falls with the value distance");
    PointColor::Point narrow = point, wide = point;
    narrow.range = 0;
    wide.range = 100;
    const PointColor::Hsv neighbour{Orange[0] + 0.35, Orange[1] - 0.15, 0.25};
    ok &= check(PointColor::weight(neighbour, narrow) < PointColor::weight(neighbour, point) &&
                PointColor::weight(neighbour, point) < PointColor::weight(neighbour, wide),
                "the range widens the neighbourhood");
    return ok;
}

bool shifts()
{
    PointColor::Point point = orange();
    point.hueShift = 100;
    PointColor::Hsv result = adjusted(Orange, single(point));
    bool ok = check(std::abs(hueDifference(result[0], Orange[0]) - 0.6104) < 1e-6, "hue +100 turns the source by 0.61 rad");
    ok &= check(std::abs(result[1] - Orange[1]) < 1e-9 && std::abs(result[2] - Orange[2]) < 1e-9,
                "the hue shift keeps saturation and value");
    point = orange();
    point.saturationShift = -100;
    result = adjusted(Orange, single(point));
    ok &= check(std::abs(result[1] / Orange[1] - 0.526) < 0.001 && std::abs(result[2] - Orange[2]) < 1e-9,
                "saturation -100 keeps 52.6 % of the saturation, at the same value, as Lightroom");
    point.saturationShift = 100;
    ok &= check(adjusted(Orange, single(point))[1] > Orange[1] * 1.8, "saturation +100 adds saturation");
    point = orange();
    point.luminanceShift = 100;
    result = adjusted(Orange, single(point));
    ok &= check(result[2] > Orange[2] * 2.1 && std::abs(result[1] - Orange[1]) < 1e-9, "luminance +100 raises the value");
    point.luminanceShift = -100;
    ok &= check(adjusted(Orange, single(point))[2] < Orange[2] * 0.55, "luminance -100 lowers the value");
    return ok;
}

bool variance()
{
    PointColor::Point point = orange();
    const PointColor::Hsv neighbour{Orange[0] + 0.1, Orange[1], Orange[2]};
    point.variance = -100;
    const double gathered = hueDifference(adjusted(neighbour, single(point))[0], Orange[0]);
    point.variance = 100;
    const double spread = hueDifference(adjusted(neighbour, single(point))[0], Orange[0]);
    bool ok = check(gathered > 0.0 && gathered < 0.05, "negative variance gathers the hues toward the source");
    ok &= check(spread > 0.15, "positive variance spreads the hues");
    const PointColor::Hsv lighter{Orange[0], Orange[1], 0.25};
    point.variance = 100;
    ok &= check(adjusted(lighter, single(point))[2] > 0.25, "positive variance spreads the values too");
    point.variance = -100;
    ok &= check(adjusted(lighter, single(point))[2] < 0.25, "negative variance gathers the values");
    ok &= check(std::abs(hueDifference(adjusted(Orange, single(point))[0], Orange[0])) < 1e-9,
                "the source itself does not move");
    return ok;
}

bool overlap()
{
    PointColor::Point point = orange();
    point.hueShift = 100;
    const double once = hueDifference(adjusted(Orange, single(point))[0], Orange[0]);
    PointColor::Parameters twice = single(point);
    twice.points[1] = point;
    twice.count = 2;
    const double shared = hueDifference(adjusted(Orange, twice)[0], Orange[0]);
    bool ok = check(shared <= once + 1e-9 && shared > once * 0.5, "two identical points share their reach");
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
    const PointColor::Hsv clamped = adjusted(Orange, single(point));
    point.hueShift = 100;
    point.saturationShift = -100;
    point.range = 100;
    bool ok = check(cv::norm(clamped - adjusted(Orange, single(point))) < 1e-12, "values are clamped");
    PointColor::Parameters many = single(orange());
    many.count = 1000;
    many.points[0].hueShift = 100;
    const PointColor::Hsv result = adjusted(Orange, many);
    ok &= check(std::isfinite(result[0]) && std::isfinite(result[1]) && std::isfinite(result[2]),
                "a count above the maximum is bounded");
    cv::Vec3d black(0.0, 0.0, 0.0);
    PointColor::adjust(black, single(point));
    ok &= check(black == cv::Vec3d(0.0, 0.0, 0.0), "black stays black");
    return ok;
}

bool perceptualStage()
{
    PerceptualColour::Parameters parameters;
    parameters.pointColor = single(orange());
    parameters.pointColor.points[0].saturationShift = -100;
    cv::Mat image(1, 2, CV_32FC3);
    image.at<cv::Vec3f>(0, 0) = cv::Vec3f(PointColor::fromHsv(Orange));
    image.at<cv::Vec3f>(0, 1) = cv::Vec3f(0.3f, 0.3f, 0.3f);
    PerceptualColour::apply(image, parameters);
    const PointColor::Hsv out = PointColor::toHsv(cv::Vec3d(image.at<cv::Vec3f>(0, 0)));
    bool ok = check(std::abs(out[1] / Orange[1] - 0.526) < 0.002 && std::abs(out[2] / Orange[2] - 1.0) < 1e-4,
                    "the perceptual stage applies the point colour");
    ok &= check(image.at<cv::Vec3f>(0, 1) == cv::Vec3f(0.3f, 0.3f, 0.3f), "the stage leaves greys");
    return ok;
}

bool overlay()
{
    PerceptualColour::Parameters parameters;
    parameters.pointColor = single(orange());
    cv::Mat image(1, 2, CV_32FC3);
    image.at<cv::Vec3f>(0, 0) = cv::Vec3f(PointColor::fromHsv(Orange));
    image.at<cv::Vec3f>(0, 1) = cv::Vec3f(PointColor::fromHsv({4.0, 0.6, 0.5}));
    const cv::Mat weights = PerceptualColour::pointWeights(image, parameters, 0);
    bool ok = check(!weights.empty() && std::abs(weights.at<float>(0, 0) - 1.0f) < 1e-4f &&
                    weights.at<float>(0, 1) == 0.0f, "the overlay weighs the source 1 and a far colour 0");
    ok &= check(PerceptualColour::pointWeights(image, parameters, 1).empty() &&
                PerceptualColour::pointWeights(image, parameters, -1).empty(), "an invalid point gives no weights");
    cv::Mat rgba(1, 2, CV_32FC4);
    rgba.at<cv::Vec4f>(0, 0) = {0.6f, 0.3f, 0.1f, 1.0f};
    rgba.at<cv::Vec4f>(0, 1) = {0.6f, 0.3f, 0.1f, 0.5f};
    cv::Mat w(1, 2, CV_32FC1);
    w.at<float>(0, 0) = 1.0f;
    w.at<float>(0, 1) = 0.0f;
    ColorAdjustments::showWeights(rgba, w);
    const cv::Vec4f kept = rgba.at<cv::Vec4f>(0, 0), grey = rgba.at<cv::Vec4f>(0, 1);
    ok &= check(cv::norm(kept - cv::Vec4f(0.6f, 0.3f, 0.1f, 1.0f)) < 1e-6 && grey[0] == grey[1] && grey[1] == grey[2] &&
                grey[3] == 0.5f, "weight 1 keeps the colour, weight 0 shows its luminance, alpha kept");
    return ok;
}

bool sidecar()
{
    QTemporaryDir directory;
    const QString source = directory.filePath(QStringLiteral("photo.png"));
    QFile file(source);
    if (!directory.isValid() || !file.open(QIODevice::WriteOnly) || file.write("x") != 1) return false;
    file.close();
    EditRecipe::Settings settings;
    settings.pointColor = single(orange());
    settings.pointColor.points[0].hueShift = -37;
    settings.pointColor.points[0].variance = 44;
    settings.pointColor.points[0].range = 66;
    settings.pointColor.points[1].source = {3.908962, 0.586369, 0.432142};
    settings.pointColor.points[1].luminanceShift = 12;
    settings.pointColor.count = 2;
    EditRecipe::Settings loaded;
    EditRecipe::UserImageMetadata metadata;
    bool ok = check(Sidecar::write(source, settings, {}, {}) && Sidecar::read(source, &loaded, &metadata),
                    "the sidecar is written and read");
    const PointColor::Point &first = loaded.pointColor.points[0], &second = loaded.pointColor.points[1];
    ok &= check(loaded.pointColor.count == 2 && first.source == Orange && first.hueShift == -37 &&
                first.variance == 44 && first.range == 66 &&
                second.source == PointColor::Hsv(3.908962, 0.586369, 0.432142) &&
                second.luminanceShift == 12 && second.range == 50,
                "the points round trip through the sidecar");

    // A point count beyond the maximum makes the sidecar invalid.
    QFile saved(Sidecar::path(source));
    if (!saved.open(QIODevice::ReadWrite)) return false;
    QByteArray bytes = saved.readAll();
    const qsizetype at = bytes.size() - 2 * (3 * 8 + 5 * 4) - 4;
    QDataStream patch(&bytes, QIODevice::WriteOnly);
    patch.setVersion(QDataStream::Qt_6_4);
    patch.device()->seek(at);
    patch << qint32(9);
    saved.seek(0);
    saved.write(bytes);
    saved.close();
    ok &= check(!Sidecar::read(source, &loaded, &metadata), "too many points are refused");
    return ok;
}
} // namespace

int main()
{
    bool ok = conversions();
    ok &= identity();
    ok &= neighbourhood();
    ok &= shifts();
    ok &= variance();
    ok &= overlap();
    ok &= robustness();
    ok &= perceptualStage();
    ok &= overlay();
    ok &= sidecar();
    if (ok) std::cout << "pointColorTests passed\n";
    return ok ? 0 : 1;
}
