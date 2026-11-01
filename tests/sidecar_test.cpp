#include "Sidecar.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    const QString source = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    QFile file(source);
    if (!check(directory.isValid() && file.open(QIODevice::WriteOnly) && file.write("x") == 1, "source written"))
        return 1;
    file.close();

    // A file holding values no control can reach, as a damaged or hand-edited one would.
    EditRecipe::Settings out;
    out.brightness = 40.0f;
    out.contrast = 900;
    out.blur = -3;
    out.temperature = 100;
    out.tint = 400;
    out.grain = 250;
    out.denoiseLuminance = 300;
    out.sharpeningRadius = 9.0f;
    out.mixerHue[2] = -500;
    out.gradingHue[1] = 720;
    out.gradingSaturation[0] = -20;
    out.gradingBlending = 140;
    out.pointColor.count = 1;
    out.pointColor.points[0].source = {0.3, 0.5, 0.4};
    out.pointColor.points[0].hueShift = 180;
    out.pointColor.points[0].range = -10;
    out.toneCurves[ToneCurve::Red] = {{0.8f, 0.9f}, {1.4f, 1.0f}, {0.2f, -0.5f}, {0.2005f, 0.4f}, {0.5f, 2.0f}};
    EditRecipe::UserImageMetadata metadata;
    metadata.rating = 12;
    if (!check(Sidecar::write(source, out, metadata, {}), "sidecar written")) return 1;

    EditRecipe::Settings in;
    EditRecipe::UserImageMetadata inMetadata;
    if (!check(Sidecar::read(source, &in, &inMetadata), "an out-of-range sidecar is still read")) return 1;
    if (!check(in.brightness == 5.0f && in.contrast == 100 && in.blur == 0 && in.temperature == 2000 &&
               in.tint == 150 && in.grain == 100 && in.denoiseLuminance == 100 && in.sharpeningRadius == 3.0f,
               "global values are bounded as the controls bound them")) return 1;
    if (!check(in.mixerHue[2] == -100 && in.gradingHue[1] == 359 && in.gradingSaturation[0] == 0 &&
               in.gradingBlending == 100 && in.pointColor.points[0].hueShift == 100 &&
               in.pointColor.points[0].range == 0 && inMetadata.rating == 5,
               "colour and annotation values are bounded")) return 1;
    const QVector<ToneCurve::Point> &curve = in.toneCurves[ToneCurve::Red];
    if (!check(curve.size() == 3 && curve[0].x == 0.2f && curve[0].y == 0.0f && curve[1].x == 0.5f &&
               curve[1].y == 1.0f && curve[2].x == 0.8f,
               "curve points are sorted, kept in [0, 1] and merged when too close")) return 1;
    if (!check(in.toneCurves[ToneCurve::Green].isEmpty(), "an empty curve stays the identity")) return 1;

    // A value that is not a number makes the whole file unusable.
    out = {};
    out.brightness = std::numeric_limits<float>::quiet_NaN();
    if (!check(Sidecar::write(source, out, {}, {}), "second sidecar written")) return 1;
    in = {};
    if (!check(!Sidecar::read(source, &in, &inMetadata) && in.brightness == 0.0f,
               "a non-finite exposure rejects the file and leaves the settings untouched")) return 1;

    std::cout << "sidecar: ok\n";
    return 0;
}
