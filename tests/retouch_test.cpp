// Removing what should not be in a photo: the zones, their sources and what stays untouched.
#include "Retouch.h"

#include <QCoreApplication>
#include <QIODevice>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <limits>

using Retouch::Kind;
using Retouch::Operation;
using Retouch::Operations;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
        ++failures;
    }
}

// A slowly varying scene with a fine grain, the same recipe at any size.
cv::Mat grainy(const cv::Size &size, int seed)
{
    cv::Mat image(size, CV_32FC3);
    cv::RNG random{uint64_t(seed)};
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const float level = 0.3f + 0.2f * float(y) / float(size.height) + 0.1f * float(x) / float(size.width);
            const float grain = float(random.gaussian(0.006));
            image.at<cv::Vec3f>(y, x) = cv::Vec3f(level * 0.9f + grain, level + grain, level * 0.7f + grain);
        }
    }
    return image;
}

Operation dot(Kind kind, float u, float v, float size)
{
    Operation operation;
    operation.kind = kind;
    LocalAdjustments::BrushStroke stroke;
    stroke.size = size;
    stroke.feather = 0.3f;
    stroke.points.emplace_back(u, v);
    operation.strokes.push_back(stroke);
    return operation;
}

// What changed, outside and inside a zone.
double largestChangeOutside(const cv::Mat &before, const cv::Mat &after, const cv::Mat &zone)
{
    cv::Mat changes;
    cv::absdiff(before, after, changes);
    changes.setTo(cv::Scalar::all(0), zone > 0.0f);
    return cv::norm(changes, cv::NORM_INF);
}

void removingAnObject()
{
    const cv::Size size(800, 500);
    const cv::Mat clean = grainy(size, 5);
    cv::Mat scene = clean.clone();
    const cv::Rect spot(380, 230, 40, 40);
    scene(spot).setTo(cv::Scalar::all(0.95));
    Operations operations{dot(Kind::Remove, 0.5f, 0.5f, 0.05f)};
    const cv::Mat zone = Retouch::coverage(operations[0], size);
    check(!zone.empty() && zone.at<float>(250, 400) > 0.9f, "a brush covers its zone");

    cv::Mat result = scene.clone();
    check(Retouch::apply(result, operations) == 0, "a zone is removed");
    check(largestChangeOutside(scene, result, zone) == 0.0, "nothing changes outside the zone");
    const cv::Mat inside = result(spot);
    cv::Mat difference;
    cv::absdiff(inside, clean(spot), difference);
    check(cv::mean(difference)[1] < 0.03, "the object is gone, the scene continues");
    cv::Mat green, smooth, fine;
    cv::extractChannel(inside, green, 1);
    cv::GaussianBlur(green, smooth, cv::Size(), 2.0);
    cv::absdiff(green, smooth, fine);
    check(cv::mean(fine)[0] > 0.001, "and keeps some grain, it is not a flat patch");

    cv::Mat again = scene.clone();
    Retouch::apply(again, operations);
    check(cv::norm(again, result, cv::NORM_INF) == 0.0, "the same retouch gives the same pixels");

    // Off, or at no strength, it does nothing.
    Operations off = operations;
    off[0].visible = false;
    cv::Mat untouched = scene.clone();
    Retouch::apply(untouched, off);
    check(!Retouch::active(off) && cv::norm(untouched, scene, cv::NORM_INF) == 0.0, "turned off, it changes nothing");
    off[0].visible = true;
    off[0].opacity = 0.0f;
    check(!Retouch::active(off), "at no opacity it is not active");

    // Half way.
    Operations half = operations;
    half[0].opacity = 0.5f;
    cv::Mat partly = scene.clone();
    Retouch::apply(partly, half);
    const float centre = partly.at<cv::Vec3f>(250, 400)[1];
    check(centre > result.at<cv::Vec3f>(250, 400)[1] + 0.1f && centre < 0.95f - 0.1f, "opacity mixes the two");
}

void previewAndExportAgree()
{
    // The same scene at two sizes: the preview's zone and the export's look alike.
    const cv::Mat large = grainy(cv::Size(4096, 2048), 9);
    cv::Mat largeScene = large.clone();
    cv::Mat smallScene;
    largeScene(cv::Rect(1900, 950, 300, 150)).setTo(cv::Scalar::all(0.9));
    cv::resize(largeScene, smallScene, cv::Size(2048, 1024), 0, 0, cv::INTER_AREA);
    Operations operations{dot(Kind::Remove, 0.5f, 0.5f, 0.06f)};
    cv::Mat largeResult = largeScene.clone(), smallResult = smallScene.clone();
    check(Retouch::apply(largeResult, operations) == 0 && Retouch::apply(smallResult, operations) == 0, "both sizes are retouched");
    cv::Mat reduced, difference;
    cv::resize(largeResult, reduced, smallResult.size(), 0, 0, cv::INTER_AREA);
    cv::absdiff(reduced, smallResult, difference);
    check(cv::mean(difference(cv::Rect(960, 490, 128, 44)))[1] < 0.02, "the export finds what the preview shows");
}

void cloneAndHeal()
{
    const cv::Size size(600, 400);
    cv::Mat scene = grainy(size, 13);
    const cv::Mat before = scene.clone();
    Operation clone = dot(Kind::Clone, 0.25f, 0.5f, 0.03f);
    clone.source = cv::Point2f(0.5f, 0.0f);
    cv::Mat result = scene.clone();
    check(Retouch::apply(result, {clone}) == 0, "a clone is done");
    const cv::Point centre(150, 200), from(450, 200);
    check(cv::norm(result.at<cv::Vec3f>(centre) - before.at<cv::Vec3f>(from)) < 1e-5, "a clone copies its source as it is");

    // A heal takes the texture of a brighter place, at the level of the zone.
    cv::Mat stepped = scene.clone();
    stepped(cv::Rect(300, 0, 300, 400)) += cv::Scalar::all(0.2);
    Operation heal = dot(Kind::Heal, 0.25f, 0.5f, 0.03f);
    heal.source = cv::Point2f(0.5f, 0.0f);
    cv::Mat healed = stepped.clone();
    check(Retouch::apply(healed, {heal}) == 0, "a heal is done");
    const float level = healed.at<cv::Vec3f>(centre)[1];
    check(std::abs(level - before.at<cv::Vec3f>(centre)[1]) < 0.03f, "a heal comes to the zone's level");
    Operation copied = heal;
    copied.kind = Kind::Clone;
    cv::Mat plain = stepped.clone();
    Retouch::apply(plain, {copied});
    check(plain.at<cv::Vec3f>(centre)[1] > level + 0.1f, "where a clone keeps the source's");

    // No source, or one outside the image: nothing happens, and it says so.
    Operation nowhere = dot(Kind::Clone, 0.25f, 0.5f, 0.03f);
    cv::Mat same = scene.clone();
    check(Retouch::apply(same, {nowhere}) == 1 && cv::norm(same, scene, cv::NORM_INF) == 0.0, "a clone without a source does nothing");
    nowhere.source = cv::Point2f(-0.9f, 0.0f);
    check(Retouch::apply(same, {nowhere}) == 1 && cv::norm(same, scene, cv::NORM_INF) == 0.0, "nor with one outside the image");
}

void aSourceIsSuggested()
{
    // Left half dark, right half bright: a zone in the dark half is healed from the dark half.
    cv::Mat scene = grainy(cv::Size(800, 400), 17);
    scene(cv::Rect(400, 0, 400, 400)) += cv::Scalar::all(0.3);
    Operation operation = dot(Kind::Heal, 0.25f, 0.5f, 0.03f);
    const cv::Point2f source = Retouch::suggestSource(scene, operation);
    check(source != cv::Point2f(0.0f, 0.0f), "a source is found");
    const float toX = 0.25f + source.x;
    check(toX > 0.02f && toX < 0.48f, "where the scene is like the zone's surroundings");
    const float toY = 0.5f + source.y;
    check(toY > 0.0f && toY < 1.0f, "inside the image");
    Operation none;
    check(Retouch::suggestSource(scene, none) == cv::Point2f(0.0f, 0.0f), "no zone, no source");
    check(Retouch::suggestSource(cv::Mat(), operation) == cv::Point2f(0.0f, 0.0f), "no image, no source");

    // Nothing else to take from: it says so rather than picking at random.
    Operation everything = dot(Kind::Heal, 0.5f, 0.5f, 1.0f);
    check(Retouch::suggestSource(scene, everything) == cv::Point2f(0.0f, 0.0f), "a zone over everything has no source");
}

void emptyZonesAreRespected()
{
    // A transform leaves exactly 0 in the corners: it is neither a source nor retouched.
    cv::Mat scene = grainy(cv::Size(600, 400), 21);
    scene(cv::Rect(0, 0, 200, 200)).setTo(cv::Scalar::all(0));
    const cv::Mat before = scene.clone();
    Operations remove{dot(Kind::Remove, 0.33f, 0.5f, 0.06f)};
    cv::Mat result = scene.clone();
    Retouch::apply(result, remove);
    const cv::Mat empty = result(cv::Rect(0, 0, 100, 100));
    check(cv::countNonZero(empty.reshape(1)) == 0, "an empty pixel stays empty");
    double darkest = 1.0;
    const cv::Mat zone = Retouch::coverage(remove[0], result.size());
    cv::Mat green;
    cv::extractChannel(result, green, 1);
    cv::Mat inZone = zone > 0.5f;
    inZone(cv::Rect(0, 0, 210, 210)).setTo(0);
    cv::minMaxLoc(green, &darkest, nullptr, nullptr, nullptr, inZone);
    check(darkest > 0.1, "and no black is brought into the zone");
}

void tooMuchIsRefused()
{
    cv::Mat scene = grainy(cv::Size(300, 200), 23);
    const cv::Mat before = scene.clone();
    Operations all{dot(Kind::Remove, 0.5f, 0.5f, 1.0f)};
    check(Retouch::apply(scene, all) == 1, "a zone over everything cannot be invented, and it says so");
    check(cv::norm(scene, before, cv::NORM_INF) == 0.0, "the image stays as it is");
    cv::Mat none;
    check(Retouch::apply(none, all) == 0, "no image, nothing to do");
}

void boundsAndNames()
{
    Operations operations{dot(Kind::Heal, 0.5f, 0.5f, 5.0f)};
    operations[0].opacity = 3.0f;
    operations[0].source = cv::Point2f(4.0f, -4.0f);
    check(Retouch::bound(operations), "values out of range are brought back");
    check(operations[0].opacity == 1.0f && operations[0].source == cv::Point2f(1.0f, -1.0f) && operations[0].strokes[0].size == 1.0f, "to their range");
    operations[0].opacity = std::numeric_limits<float>::quiet_NaN();
    check(!Retouch::bound(operations), "a value that is not a number is refused");
    Kind kind = Kind::Remove;
    check(Retouch::kindNamed(QStringLiteral("clone"), kind) && kind == Kind::Clone && !Retouch::kindNamed(QStringLiteral("nope"), kind), "kinds have names");

    QVariantList list = Retouch::describe({dot(Kind::Clone, 0.2f, 0.4f, 0.03f)});
    const QVariantMap entry = list.at(0).toMap();
    check(entry.value(QStringLiteral("kind")).toString() == QStringLiteral("clone") && entry.value(QStringLiteral("strokes")).toInt() == 1, "the panel reads a retouch");
    check(std::abs(entry.value(QStringLiteral("centerX")).toDouble() - 0.2) < 1e-6, "and its place");

    // The key follows what changes the result.
    const auto keyOf = [](const Operations &value) {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        Retouch::appendKey(stream, value);
        return bytes;
    };
    Operations one{dot(Kind::Remove, 0.5f, 0.5f, 0.05f)};
    check(keyOf({}).isEmpty(), "without a retouch the key is unchanged");
    Operations off = one;
    off[0].visible = false;
    check(keyOf(off).isEmpty(), "nor with one turned off");
    Operations moved = one;
    moved[0].strokes[0].points[0].x = 0.6f;
    check(keyOf(one) != keyOf(moved), "a moved zone changes it");
    Operations other = one;
    other[0].kind = Kind::Clone;
    check(keyOf(one) != keyOf(other), "and so does another kind");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    removingAnObject();
    previewAndExportAgree();
    cloneAndHeal();
    aSourceIsSuggested();
    emptyZonesAreRespected();
    tooMuchIsRefused();
    boundsAndNames();
    if (failures == 0)
        std::cout << "retouch: ok\n";
    return failures == 0 ? 0 : 1;
}
