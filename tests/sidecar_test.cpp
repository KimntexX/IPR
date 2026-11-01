#include "RecipeCodec.h"
#include "Sidecar.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// The sidecar as a CBOR map, to alter it as another IPR version or a damaged file would.
QCborMap documentOf(const QString &source)
{
    const QCborValue value = QCborValue::fromCbor(readBytes(Sidecar::path(source)));
    return (value.isTag() ? value.taggedValue() : value).toMap();
}

bool writeDocument(const QString &source, const QCborMap &map)
{
    return writeBytes(Sidecar::path(source), QCborValue(QCborKnownTags::Signature, map).toCbor());
}

// A version 9 sidecar of the former binary format, as IPR alpha wrote it.
bool writeVersion9(const QString &source)
{
    QFile file(Sidecar::path(source));
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_4);
    const QFileInfo info(source);
    stream << quint32(0x49505231u) << quint16(9) << qint64(info.size())
           << qint64(info.lastModified().toMSecsSinceEpoch());
    stream << float(0.75f) << qint32(20) << qint32(0) << false;
    for (int index = 0; index < 12; ++index)   // shadows ... grain; temperature is the fifth
        stream << qint32(index == 4 ? 6100 : index == 0 ? -15 : 0);
    stream << qint32(12) << qint32(34) << qint32(56) << float(1.5f) << qint32(78);
    for (int band = 0; band < 8; ++band)
        stream << qint32(band) << qint32(-band) << qint32(2 * band);
    for (int zone = 0; zone < 3; ++zone)
        stream << qint32(200 + zone) << qint32(4 + zone);
    stream << true;
    stream << qint32(2) << float(0) << float(0.1f) << float(1) << float(0.9f);
    for (int curve = 1; curve < 4; ++curve)
        stream << qint32(0);
    stream << int(3) << QStringLiteral("green") << true << true << true;
    stream << QStringLiteral("profile.dcp") << QStringLiteral("p256") << QStringLiteral("manifest.json")
           << QStringLiteral("m256") << true << QStringLiteral("engine-7");
    stream << QStringLiteral("lens.lcp") << QStringLiteral("l256") << true << false << true;
    stream << qint32(80) << qint32(-30);
    stream << qint32(90) << qint32(10) << qint32(1) << qint32(2) << qint32(3) << qint32(4);
    stream << qint32(1) << 0.3 << 0.5 << 0.4 << qint32(-20) << qint32(10) << qint32(5) << qint32(0) << qint32(60);
    return stream.status() == QDataStream::Ok;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    const QString source = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    if (!check(directory.isValid() && writeBytes(source, "x"), "source written"))
        return 1;

    // Every value round-trips through the current format.
    Sidecar::Document out;
    out.settings.brightness = 1.25f;
    out.settings.contrast = -12;
    out.settings.grayscale = true;
    out.settings.temperature = 4800;
    out.settings.sharpeningRadius = 1.7f;
    out.settings.mixerSaturation[7] = -40;
    out.settings.gradingLuminance[3] = 15;
    out.settings.gradingBlending = 70;
    out.settings.automaticWhiteBalance = true;
    out.settings.pointColor.count = 1;
    out.settings.pointColor.points[0].source = {3.908962, 0.586369, 0.432142};
    out.settings.pointColor.points[0].luminanceShift = 12;
    out.settings.toneCurves[ToneCurve::Blue] = {{0.0f, 0.1f}, {0.5f, 0.6f}, {1.0f, 0.95f}};
    out.metadata.rating = 4;
    out.metadata.colorLabel = QStringLiteral("red");
    out.metadata.ratingSet = true;
    out.raw.profile = QStringLiteral("Canon EOS R6m3 Camera Neutral.dcp");
    out.raw.lensProfileResolved = true;
    out.raw.chromaticAberrationEnabled = false;
    if (!check(Sidecar::write(source, out), "sidecar written")) return 1;
    const QByteArray written = readBytes(Sidecar::path(source));
    if (!check(written.startsWith("\xd9\xd9\xf7"), "the sidecar is a self-described CBOR document")) return 1;
    {
        const auto in = Sidecar::read(source);
        if (!check(in && in->format == Sidecar::Format::Current, "the current format is read as current")) return 1;
        const auto &s = in->settings;
        if (!check(s.brightness == 1.25f && s.contrast == -12 && s.grayscale && s.temperature == 4800 &&
                   s.sharpeningRadius == 1.7f && s.mixerSaturation[7] == -40 && s.gradingLuminance[3] == 15 &&
                   s.gradingBlending == 70 && s.automaticWhiteBalance && s.pointColor.count == 1 &&
                   s.pointColor.points[0].source == PointColor::Hsv(3.908962, 0.586369, 0.432142) &&
                   s.pointColor.points[0].luminanceShift == 12 && s.pointColor.points[0].range == 50 &&
                   s.toneCurves[ToneCurve::Blue].size() == 3 && s.toneCurves[ToneCurve::Blue][1].y == 0.6f &&
                   s.toneCurves[ToneCurve::Red].isEmpty(),
                   "settings round-trip")) return 1;
        if (!check(in->metadata.rating == 4 && in->metadata.colorLabel == QStringLiteral("red") &&
                   in->metadata.ratingSet && !in->metadata.colorLabelSet,
                   "user metadata round-trips")) return 1;
        if (!check(in->raw.profile == out.raw.profile && in->raw.lensProfileResolved &&
                   in->raw.lensCorrectionEnabled && !in->raw.chromaticAberrationEnabled,
                   "the RAW development round-trips")) return 1;
    }

    // The edit history is saved with the recipe; a damaged history is dropped, the recipe still read.
    {
        Sidecar::Document edited = out;
        edited.history.reset(RecipeCodec::encodeSettings({}));
        edited.history.record(RecipeCodec::encodeSettings(out.settings));
        edited.history.addSnapshot(QStringLiteral("Warm"), 7);
        const auto in = Sidecar::write(source, edited) ? Sidecar::read(source) : std::nullopt;
        if (!check(in && in->history.size() == 2 && in->history.position() == 1 &&
                   in->history.snapshots().size() == 1 && in->history.snapshots()[0].name == QStringLiteral("Warm"),
                   "the history round-trips")) return 1;
        QCborMap map = documentOf(source);
        if (!check(map.value(QStringLiteral("history")).isByteArray(), "the history is stored compressed")) return 1;
        map.insert(QStringLiteral("history"), QByteArray("not a history"));
        if (!check(writeDocument(source, map), "damaged history written")) return 1;
        const auto damaged = Sidecar::read(source);
        if (!check(damaged && damaged->history.isEmpty() && damaged->settings.temperature == 4800,
                   "a damaged history is dropped and the recipe kept")) return 1;
        if (!check(Sidecar::write(source, out), "sidecar rewritten without history")) return 1;
    }

    // A section or field this IPR does not know is skipped; a missing field takes its default.
    {
        QCborMap map = documentOf(source);
        map.insert(QStringLiteral("masks"), QCborMap{{QStringLiteral("count"), 3}});
        QCborMap settings = map.value(QStringLiteral("settings")).toMap();
        settings.insert(QStringLiteral("futureSlider"), 42);
        settings.remove(QStringLiteral("contrast"));
        map.insert(QStringLiteral("settings"), settings);
        if (!check(writeDocument(source, map), "extended sidecar written")) return 1;
        const auto in = Sidecar::read(source);
        if (!check(in && in->format == Sidecar::Format::Current && in->settings.contrast == 0 &&
                   in->settings.temperature == 4800,
                   "unknown sections and fields are skipped, missing ones take their default")) return 1;
    }

    // A file written by a newer IPR opens read-only: it is read, never overwritten.
    {
        QCborMap map = documentOf(source);
        QCborMap header = map.value(QStringLiteral("ipr")).toMap();
        header.insert(QStringLiteral("format"), Sidecar::FormatVersion + 1);
        map.insert(QStringLiteral("ipr"), header);
        if (!check(writeDocument(source, map), "newer sidecar written")) return 1;
        const QByteArray newer = readBytes(Sidecar::path(source));
        const auto in = Sidecar::read(source);
        if (!check(in && in->format == Sidecar::Format::Newer && in->settings.temperature == 4800,
                   "a newer sidecar is read and reported as newer")) return 1;
        if (!check(!Sidecar::write(source, out) && readBytes(Sidecar::path(source)) == newer,
                   "a newer sidecar is never overwritten")) return 1;
    }

    // A former binary sidecar is converted in memory, then rewritten at the current format.
    {
        if (!check(writeVersion9(source), "version 9 sidecar written")) return 1;
        const auto in = Sidecar::read(source);
        if (!check(in && in->format == Sidecar::Format::Older, "a binary sidecar is read as older")) return 1;
        const auto &s = in->settings;
        if (!check(s.brightness == 0.75f && s.contrast == 20 && s.shadows == -15 && s.temperature == 6100 &&
                   s.denoiseLuminance == 12 && s.sharpeningRadius == 1.5f && s.mixerLuminance[7] == 14 &&
                   s.gradingHue[2] == 202 && s.gradingHue[3] == 90 && s.gradingLuminance[3] == 4 &&
                   s.gradingBlending == 80 && s.gradingBalance == -30 && s.automaticWhiteBalance &&
                   s.toneCurves[ToneCurve::Luminance].size() == 2 && s.pointColor.count == 1 &&
                   s.pointColor.points[0].hueShift == -20 && s.pointColor.points[0].range == 60,
                   "version 9 settings are converted")) return 1;
        if (!check(in->metadata.rating == 3 && in->metadata.colorLabel == QStringLiteral("green") &&
                   in->raw.profile == QStringLiteral("profile.dcp") && in->raw.engineVersion == QStringLiteral("engine-7") &&
                   in->raw.lensProfileResolved && !in->raw.lensCorrectionEnabled,
                   "version 9 metadata and RAW development are converted")) return 1;
        Sidecar::Document upgraded = *in;
        if (!check(Sidecar::write(source, upgraded), "the converted sidecar is written")) return 1;
        const auto again = Sidecar::read(source);
        if (!check(again && again->format == Sidecar::Format::Current && again->settings.temperature == 6100 &&
                   again->raw.engineVersion == QStringLiteral("engine-7"),
                   "the first save upgrades the file to the current format")) return 1;
    }

    // A file holding values no control can reach, as a damaged or hand-edited one would.
    out = {};
    out.settings.brightness = 40.0f;
    out.settings.contrast = 900;
    out.settings.blur = -3;
    out.settings.temperature = 100;
    out.settings.tint = 400;
    out.settings.grain = 250;
    out.settings.denoiseLuminance = 300;
    out.settings.sharpeningRadius = 9.0f;
    out.settings.mixerHue[2] = -500;
    out.settings.gradingHue[1] = 720;
    out.settings.gradingSaturation[0] = -20;
    out.settings.gradingBlending = 140;
    out.settings.pointColor.count = 1;
    out.settings.pointColor.points[0].source = {0.3, 0.5, 0.4};
    out.settings.pointColor.points[0].hueShift = 180;
    out.settings.pointColor.points[0].range = -10;
    out.settings.toneCurves[ToneCurve::Red] = {{0.8f, 0.9f}, {1.4f, 1.0f}, {0.2f, -0.5f}, {0.2005f, 0.4f}, {0.5f, 2.0f}};
    out.metadata.rating = 12;
    if (!check(Sidecar::write(source, out), "out-of-range sidecar written")) return 1;
    {
        const auto in = Sidecar::read(source);
        if (!check(bool(in), "an out-of-range sidecar is still read")) return 1;
        const auto &s = in->settings;
        if (!check(s.brightness == 5.0f && s.contrast == 100 && s.blur == 0 && s.temperature == 2000 &&
                   s.tint == 150 && s.grain == 100 && s.denoiseLuminance == 100 && s.sharpeningRadius == 3.0f,
                   "global values are bounded as the controls bound them")) return 1;
        if (!check(s.mixerHue[2] == -100 && s.gradingHue[1] == 359 && s.gradingSaturation[0] == 0 &&
                   s.gradingBlending == 100 && s.pointColor.points[0].hueShift == 100 &&
                   s.pointColor.points[0].range == 0 && in->metadata.rating == 5,
                   "colour and annotation values are bounded")) return 1;
        const QVector<ToneCurve::Point> &curve = s.toneCurves[ToneCurve::Red];
        if (!check(curve.size() == 3 && curve[0].x == 0.2f && curve[0].y == 0.0f && curve[1].x == 0.5f &&
                   curve[1].y == 1.0f && curve[2].x == 0.8f,
                   "curve points are sorted, kept in [0, 1] and merged when too close")) return 1;
        if (!check(s.toneCurves[ToneCurve::Green].isEmpty(), "an empty curve stays the identity")) return 1;
    }

    // A value that is not a number, too many points or a damaged document make the file unusable.
    out = {};
    out.settings.brightness = std::numeric_limits<float>::quiet_NaN();
    if (!check(Sidecar::write(source, out), "non-finite sidecar written")) return 1;
    if (!check(!Sidecar::read(source), "a non-finite exposure rejects the file")) return 1;
    {
        out = {};
        if (!check(Sidecar::write(source, out), "plain sidecar written")) return 1;
        QCborMap map = documentOf(source);
        QCborMap settings = map.value(QStringLiteral("settings")).toMap();
        QCborArray points;
        for (size_t index = 0; index <= PointColor::MaxPoints; ++index)
            points.append(QCborMap{{QStringLiteral("source"), QCborArray{0.1, 0.5, 0.5}}});
        settings.insert(QStringLiteral("pointColor"), points);
        map.insert(QStringLiteral("settings"), settings);
        if (!check(writeDocument(source, map) && !Sidecar::read(source), "too many colour points are refused"))
            return 1;
    }
    if (!check(writeBytes(Sidecar::path(source), written.left(written.size() / 2)) && !Sidecar::read(source),
               "a truncated document is refused")) return 1;

    // A sidecar belongs to one state of its photo.
    if (!check(Sidecar::write(source, {}) && writeBytes(source, "changed") && !Sidecar::read(source),
               "a sidecar of another version of the photo is ignored")) return 1;
    if (!check(!Sidecar::read(QDir(directory.path()).filePath(QStringLiteral("absent.png"))),
               "a photo without a sidecar has no document")) return 1;

    std::cout << "sidecar: ok\n";
    return 0;
}
