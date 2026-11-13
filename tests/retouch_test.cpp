// Removing what should not be in a photo: the zones, their sources and what stays untouched.
#include "Retouch.h"
#include <random>
#include "image_testing.h"
#include "Filters.h"
#include "Resize.h"
#include "Pixels.h"

#include <QCoreApplication>
#include <QIODevice>
#include <QVariantMap>

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
ipr::Image grainy(const ipr::Size &size, int seed)
{
    ipr::Image image = ipr::Image::create(size, ipr::Depth::F32, 3);
    std::mt19937 random(static_cast<unsigned>(seed));
    std::normal_distribution<float> gaussian(0.f, 0.006f);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const float level = 0.3f + 0.2f * float(y) / float(size.height) + 0.1f * float(x) / float(size.width);
            const float grain = gaussian(random);
            imagetest::at<ipr::Vec3f>(image, y, x) = ipr::Vec3f(level * 0.9f + grain, level + grain, level * 0.7f + grain);
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
double largestChangeOutside(const ipr::Image &before, const ipr::Image &after, const ipr::Image &zone)
{
    double largest = 0.0;
    for (int y = 0; y < before.height(); ++y)
        for (int x = 0; x < before.width(); ++x)
            if (!(imagetest::at<float>(zone, y, x) > 0.0f))
                for (int c = 0; c < 3; ++c)
                    largest = std::max(largest, std::abs(double(imagetest::at<ipr::Vec3f>(before, y, x)[c]) -
                                                         double(imagetest::at<ipr::Vec3f>(after, y, x)[c])));
    return largest;
}

void removingAnObject()
{
    const ipr::Size size(800, 500);
    const ipr::Image clean = grainy(size, 5);
    ipr::Image scene = clean.clone();
    const ipr::Recti spot(380, 230, 40, 40);
    {
        ipr::Image object = scene.mutableView(spot);
        ipr::fill(object, 0.95);
    }
    Operations operations{dot(Kind::Remove, 0.5f, 0.5f, 0.05f)};
    const ipr::Image zone = Retouch::coverage(operations[0], size);
    check(!zone.empty() && imagetest::at<float>(zone, 250, 400) > 0.9f, "a brush covers its zone");

    ipr::Image result = scene.clone();
    check(Retouch::apply(result, operations) == 0, "a zone is removed");
    check(largestChangeOutside(scene, result, zone) == 0.0, "nothing changes outside the zone");
    const ipr::Image inside = result.copy(spot);
    check(ipr::meanOf(imagetest::absoluteDifference(inside, clean.copy(spot)))[1] < 0.03, "the object is gone, the scene continues");
    const ipr::Image green = ipr::channel(inside, 1);
    const ipr::Image fine = imagetest::absoluteDifference(green, ipr::gaussianBlurred(green, 2.0));
    check(ipr::meanOf(fine)[0] > 0.001, "and keeps some grain, it is not a flat patch");

    ipr::Image again = scene.clone();
    Retouch::apply(again, operations);
    check(ipr::largestDifference(again, result) == 0.0, "the same retouch gives the same pixels");

    // Off, or at no strength, it does nothing.
    Operations off = operations;
    off[0].visible = false;
    ipr::Image untouched = scene.clone();
    Retouch::apply(untouched, off);
    check(!Retouch::active(off) && ipr::largestDifference(untouched, scene) == 0.0, "turned off, it changes nothing");
    off[0].visible = true;
    off[0].opacity = 0.0f;
    check(!Retouch::active(off), "at no opacity it is not active");

    // Half way.
    Operations half = operations;
    half[0].opacity = 0.5f;
    ipr::Image partly = scene.clone();
    Retouch::apply(partly, half);
    const float centre = imagetest::at<ipr::Vec3f>(partly, 250, 400)[1];
    check(centre > imagetest::at<ipr::Vec3f>(result, 250, 400)[1] + 0.1f && centre < 0.95f - 0.1f, "opacity mixes the two");
}

void previewAndExportAgree()
{
    // The same scene at two sizes: the preview's zone and the export's look alike.
    const ipr::Image large = grainy(ipr::Size(4096, 2048), 9);
    ipr::Image largeScene = large.clone();
    {
        ipr::Image object = largeScene.mutableView(ipr::Recti(1900, 950, 300, 150));
        ipr::fill(object, 0.9);
    }
    const ipr::Image smallScene = ipr::resized(largeScene, ipr::Size(2048, 1024), ipr::Interpolation::Area);
    Operations operations{dot(Kind::Remove, 0.5f, 0.5f, 0.06f)};
    ipr::Image largeResult = largeScene.clone(), smallResult = smallScene.clone();
    check(Retouch::apply(largeResult, operations) == 0 && Retouch::apply(smallResult, operations) == 0, "both sizes are retouched");
    const ipr::Image reduced = ipr::resized(largeResult, smallResult.size(), ipr::Interpolation::Area);
    const ipr::Recti area(960, 490, 128, 44);
    check(ipr::meanOf(imagetest::absoluteDifference(reduced.copy(area), smallResult.copy(area)))[1] < 0.02,
          "the export finds what the preview shows");
}

void cloneAndHeal()
{
    const ipr::Size size(600, 400);
    ipr::Image scene = grainy(size, 13);
    const ipr::Image before = scene.clone();
    Operation clone = dot(Kind::Clone, 0.25f, 0.5f, 0.03f);
    clone.source = ipr::Point2f(0.5f, 0.0f);
    ipr::Image result = scene.clone();
    check(Retouch::apply(result, {clone}) == 0, "a clone is done");
    const ipr::Point2i centre(150, 200), from(450, 200);
    check(ipr::length(imagetest::at<ipr::Vec3f>(result, centre) - imagetest::at<ipr::Vec3f>(before, from)) < 1e-5, "a clone copies its source as it is");

    // A heal takes the texture of a brighter place, at the level of the zone.
    ipr::Image stepped = scene.clone();
    imagetest::addTo(stepped, ipr::Recti(300, 0, 300, 400), {0.2f, 0.2f, 0.2f});
    Operation heal = dot(Kind::Heal, 0.25f, 0.5f, 0.03f);
    heal.source = ipr::Point2f(0.5f, 0.0f);
    ipr::Image healed = stepped.clone();
    check(Retouch::apply(healed, {heal}) == 0, "a heal is done");
    const float level = imagetest::at<ipr::Vec3f>(healed, centre)[1];
    check(std::abs(level - imagetest::at<ipr::Vec3f>(before, centre)[1]) < 0.03f, "a heal comes to the zone's level");
    Operation copied = heal;
    copied.kind = Kind::Clone;
    ipr::Image plain = stepped.clone();
    Retouch::apply(plain, {copied});
    check(imagetest::at<ipr::Vec3f>(plain, centre)[1] > level + 0.1f, "where a clone keeps the source's");

    // No source, or one outside the image: nothing happens, and it says so.
    Operation nowhere = dot(Kind::Clone, 0.25f, 0.5f, 0.03f);
    ipr::Image same = scene.clone();
    check(Retouch::apply(same, {nowhere}) == 1 && ipr::largestDifference(same, scene) == 0.0, "a clone without a source does nothing");
    nowhere.source = ipr::Point2f(-0.9f, 0.0f);
    check(Retouch::apply(same, {nowhere}) == 1 && ipr::largestDifference(same, scene) == 0.0, "nor with one outside the image");
}

void aSourceIsSuggested()
{
    // Left half dark, right half bright: a zone in the dark half is healed from the dark half.
    ipr::Image scene = grainy(ipr::Size(800, 400), 17);
    imagetest::addTo(scene, ipr::Recti(400, 0, 400, 400), {0.3f, 0.3f, 0.3f});
    Operation operation = dot(Kind::Heal, 0.25f, 0.5f, 0.03f);
    const ipr::Point2f source = Retouch::suggestSource(scene, operation);
    check(source != ipr::Point2f(0.0f, 0.0f), "a source is found");
    const float toX = 0.25f + source.x;
    check(toX > 0.02f && toX < 0.48f, "where the scene is like the zone's surroundings");
    const float toY = 0.5f + source.y;
    check(toY > 0.0f && toY < 1.0f, "inside the image");
    Operation none;
    check(Retouch::suggestSource(scene, none) == ipr::Point2f(0.0f, 0.0f), "no zone, no source");
    check(Retouch::suggestSource(ipr::Image(), operation) == ipr::Point2f(0.0f, 0.0f), "no image, no source");

    // Nothing else to take from: it says so rather than picking at random.
    Operation everything = dot(Kind::Heal, 0.5f, 0.5f, 1.0f);
    check(Retouch::suggestSource(scene, everything) == ipr::Point2f(0.0f, 0.0f), "a zone over everything has no source");
}

void emptyZonesAreRespected()
{
    // A transform leaves exactly 0 in the corners: it is neither a source nor retouched.
    ipr::Image scene = grainy(ipr::Size(600, 400), 21);
    {
        ipr::Image corner = scene.mutableView(ipr::Recti(0, 0, 200, 200));
        ipr::fill(corner, 0.0);
    }
    const ipr::Image before = scene.clone();
    Operations remove{dot(Kind::Remove, 0.33f, 0.5f, 0.06f)};
    ipr::Image result = scene.clone();
    Retouch::apply(result, remove);
    check(imagetest::largestAbsolute(result.copy(ipr::Recti(0, 0, 100, 100))) == 0, "an empty pixel stays empty");
    double darkest = 1.0;
    const ipr::Image zone = Retouch::coverage(remove[0], result.size());
    for (int y = 0; y < result.height(); ++y)
        for (int x = 0; x < result.width(); ++x)
            if (imagetest::at<float>(zone, y, x) > 0.5f && !(x < 210 && y < 210))
                darkest = std::min(darkest, double(imagetest::at<ipr::Vec3f>(result, y, x)[1]));
    check(darkest > 0.1, "and no black is brought into the zone");
}

void tooMuchIsRefused()
{
    ipr::Image scene = grainy(ipr::Size(300, 200), 23);
    const ipr::Image before = scene.clone();
    Operations all{dot(Kind::Remove, 0.5f, 0.5f, 1.0f)};
    check(Retouch::apply(scene, all) == 1, "a zone over everything cannot be invented, and it says so");
    check(ipr::largestDifference(scene, before) == 0.0, "the image stays as it is");
    ipr::Image none;
    check(Retouch::apply(none, all) == 0, "no image, nothing to do");
}

void boundsAndNames()
{
    Operations operations{dot(Kind::Heal, 0.5f, 0.5f, 5.0f)};
    operations[0].opacity = 3.0f;
    operations[0].source = ipr::Point2f(4.0f, -4.0f);
    check(Retouch::bound(operations), "values out of range are brought back");
    check(operations[0].opacity == 1.0f && operations[0].source == ipr::Point2f(1.0f, -1.0f) && operations[0].strokes[0].size == 1.0f, "to their range");
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

void geometryFollowing()
{
    const ipr::Size size(400,300);
    auto scene = ipr::filled(size, ipr::Depth::F32, 3, .3);
    auto object = scene.mutableView({112,112,16,16});
    ipr::fill(object, .95);
    // An identifiable source patch, away from the target and the borders.
    for (int y=190; y<230; ++y)
        for (int x=240; x<280; ++x) imagetest::at<ipr::Vec3f>(scene,y,x) = {.65f,.12f,.08f};
    for (const auto kind : {Kind::Remove,Kind::Heal,Kind::Clone}) {
        auto operation = dot(kind,.3f,.4f,.055f);
        operation.source = {.35f,.3f};
        auto reference = scene.clone();
        check(Retouch::apply(reference,{operation})==0, "reference correction succeeds");
        for (const int mode : {0,1,2,3,4}) {
            Geometry::Settings geometry;
            if (mode==0) geometry.rotation=19;
            if (mode==1) geometry.quarterTurns=1;
            if (mode==2) geometry.flipHorizontal=true;
            if (mode==3) geometry.vertical=20;
            if (mode==4) { geometry.aspect=15; geometry.horizontal=12; }
            auto changed = Geometry::warpRgb(scene.clone(),geometry,Geometry::Quality::Preview);
            const auto before=changed.clone();
            const auto expected=Geometry::warpRgb(reference.clone(),geometry,Geometry::Quality::Preview);
            check(Retouch::applyOnCanvas(changed,{operation},geometry,size)==0,"anchored correction succeeds");
            const auto target=Geometry::toCanvas(geometry,size,{.3,.4});
            const int x=int(target->x*changed.width()), y=int(target->y*changed.height());
            const auto got=imagetest::at<ipr::Vec3f>(changed,y,x), want=imagetest::at<ipr::Vec3f>(expected,y,x);
            check(std::abs(got[0]-want[0])<.04f && std::abs(got[1]-want[1])<.04f &&
                  std::abs(got[2]-want[2])<.04f, "target and sampled source follow the photo");
            const auto zone=Retouch::canvasCoverage(operation,geometry,size);
            check(largestChangeOutside(before,changed,zone)==0,"pixels outside the projected correction are unchanged");
        }
    }
    auto operation=dot(Kind::Clone,.3f,.4f,.04f);
    operation.source={.35f,.3f}; operation.frame.rotation=17;
    const auto targetInFrame=Geometry::toCanvas(operation.frame,size,{.3,.4});
    const auto sourceInFrame=Geometry::toCanvas(operation.frame,size,{.65,.7});
    operation.strokes[0].points[0]={float(targetInFrame->x),float(targetInFrame->y)};
    operation.source={float(sourceInFrame->x-targetInFrame->x),float(sourceInFrame->y-targetInFrame->y)};
    auto reset=scene.clone();
    check(Retouch::applyOnCanvas(reset,{operation},{},size)==0 && imagetest::at<ipr::Vec3f>(reset,120,120)[1]<.2f,
          "a correction created after rotation remains attached when geometry resets");
    Operations bad{operation};bad[0].frame.rotation=std::numeric_limits<float>::quiet_NaN();
    check(!Retouch::bound(bad),"nonfinite frame refused");
    auto missingSource = ipr::filled(size,ipr::Depth::F32,3,.3);
    auto empty = missingSource.mutableView({220,170,80,80}); ipr::fill(empty,0);
    const auto unchanged = missingSource.clone();
    auto copy = dot(Kind::Clone,.3f,.4f,.04f); copy.source={.35f,.3f};
    check(Retouch::applyOnCanvas(missingSource,{copy},{},size)==1 &&
          ipr::largestDifference(missingSource,unchanged)==0,"unavailable source is refused without a black patch");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    geometryFollowing();
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
