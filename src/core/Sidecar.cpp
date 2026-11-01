#include "Sidecar.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <cmath>

namespace Sidecar {
namespace {
using EditRecipe::RawDevelopment;
using EditRecipe::Settings;
using EditRecipe::UserImageMetadata;

constexpr quint32 BinaryMagic = 0x49505231u;   // "IPR1", the former binary sidecars
constexpr qint64 MaxBytes = 16 * 1024 * 1024;

// The state of the photo a sidecar belongs to.
struct Identity
{
    qint64 size = -1;
    qint64 modified = -1;
};

Identity identityOf(const QFileInfo &source)
{
    return {source.size(), source.lastModified().toMSecsSinceEpoch()};
}

// Values within the ranges of the editor's controls, so a damaged or hand-edited
// file cannot reach the render outside them. Non-finite values reject the file.
bool bound(Settings &s, UserImageMetadata &metadata)
{
    if (!std::isfinite(s.brightness) || !std::isfinite(s.sharpeningRadius))
        return false;
    const auto within = [](int &value, int low, int high) { value = std::clamp(value, low, high); };
    s.brightness = std::clamp(s.brightness, -5.0f, 5.0f);
    within(s.contrast, -100, 100);
    within(s.blur, 0, 20);
    for (int *value : {&s.shadows, &s.blacks, &s.highlights, &s.whites, &s.vibrance, &s.saturation,
                       &s.texture, &s.clarity, &s.dehaze})
        within(*value, -100, 100);
    within(s.temperature, 2000, 50000);
    within(s.tint, -150, 150);
    within(s.grain, 0, 100);
    within(s.denoiseLuminance, 0, 100);
    within(s.denoiseChrominance, 0, 100);
    within(s.sharpeningAmount, 0, 200);
    s.sharpeningRadius = std::clamp(s.sharpeningRadius, 0.3f, 3.0f);
    within(s.sharpeningMasking, 0, 100);
    for (auto *band : {&s.mixerHue, &s.mixerSaturation, &s.mixerLuminance})
        for (int &value : *band) within(value, -100, 100);
    for (int &luminance : s.gradingLuminance) within(luminance, -100, 100);
    for (int &hue : s.gradingHue) within(hue, 0, 359);
    for (int &saturation : s.gradingSaturation) within(saturation, 0, 100);
    within(s.gradingBlending, 0, 100);
    within(s.gradingBalance, -100, 100);
    for (size_t index = 0; index < s.pointColor.count; ++index) {
        PointColor::Point &point = s.pointColor.points[index];
        if (!std::isfinite(point.source[0]) || !std::isfinite(point.source[1]) || !std::isfinite(point.source[2]))
            return false;
        for (int *value : {&point.hueShift, &point.saturationShift, &point.luminanceShift, &point.variance})
            within(*value, -100, 100);
        within(point.range, 0, 100);
    }
    for (QVector<ToneCurve::Point> &curve : s.toneCurves)
        if (!curve.isEmpty()) curve = ToneCurve::normalized(std::move(curve));   // empty is the identity too
    within(metadata.rating, 0, 5);
    return true;
}

// ---- Current format: named CBOR fields, each with its default when absent.

template <typename Owner, typename Value>
struct Field
{
    const char *key;
    Value Owner::*member;
};

const Field<Settings, int> SettingInts[] = {
    {"contrast", &Settings::contrast}, {"blur", &Settings::blur}, {"shadows", &Settings::shadows},
    {"blacks", &Settings::blacks}, {"highlights", &Settings::highlights}, {"whites", &Settings::whites},
    {"temperature", &Settings::temperature}, {"tint", &Settings::tint}, {"vibrance", &Settings::vibrance},
    {"saturation", &Settings::saturation}, {"texture", &Settings::texture}, {"clarity", &Settings::clarity},
    {"dehaze", &Settings::dehaze}, {"grain", &Settings::grain},
    {"denoiseLuminance", &Settings::denoiseLuminance}, {"denoiseChrominance", &Settings::denoiseChrominance},
    {"sharpeningAmount", &Settings::sharpeningAmount}, {"sharpeningMasking", &Settings::sharpeningMasking},
    {"gradingBlending", &Settings::gradingBlending}, {"gradingBalance", &Settings::gradingBalance},
};
const Field<Settings, float> SettingFloats[] = {
    {"brightness", &Settings::brightness}, {"sharpeningRadius", &Settings::sharpeningRadius},
};
const Field<Settings, bool> SettingBools[] = {
    {"grayscale", &Settings::grayscale}, {"automaticWhiteBalance", &Settings::automaticWhiteBalance},
};
const Field<Settings, std::array<int, ColorMixer::BandCount>> SettingBands[] = {
    {"mixerHue", &Settings::mixerHue}, {"mixerSaturation", &Settings::mixerSaturation},
    {"mixerLuminance", &Settings::mixerLuminance},
};
const Field<Settings, std::array<int, 4>> SettingWheels[] = {
    {"gradingHue", &Settings::gradingHue}, {"gradingSaturation", &Settings::gradingSaturation},
    {"gradingLuminance", &Settings::gradingLuminance},
};
const Field<UserImageMetadata, int> MetadataInts[] = {{"rating", &UserImageMetadata::rating}};
const Field<UserImageMetadata, QString> MetadataStrings[] = {{"colorLabel", &UserImageMetadata::colorLabel}};
const Field<UserImageMetadata, bool> MetadataBools[] = {
    {"flagged", &UserImageMetadata::flagged}, {"ratingSet", &UserImageMetadata::ratingSet},
    {"colorLabelSet", &UserImageMetadata::colorLabelSet},
};
const Field<RawDevelopment, QString> RawStrings[] = {
    {"profile", &RawDevelopment::profile}, {"profileSha256", &RawDevelopment::profileSha256},
    {"manifest", &RawDevelopment::manifest}, {"manifestSha256", &RawDevelopment::manifestSha256},
    {"engineVersion", &RawDevelopment::engineVersion}, {"lensProfile", &RawDevelopment::lensProfile},
    {"lensProfileSha256", &RawDevelopment::lensProfileSha256},
};
const Field<RawDevelopment, bool> RawBools[] = {
    {"normalizationEnabled", &RawDevelopment::normalizationEnabled},
    {"lensProfileResolved", &RawDevelopment::lensProfileResolved},
    {"lensCorrectionEnabled", &RawDevelopment::lensCorrectionEnabled},
    {"chromaticAberrationEnabled", &RawDevelopment::chromaticAberrationEnabled},
};
const char *const CurveKeys[ToneCurve::ChannelCount] = {"luminance", "red", "green", "blue"};

QCborValue encode(int value) { return qint64(value); }
QCborValue encode(float value) { return double(value); }
QCborValue encode(bool value) { return value; }
QCborValue encode(const QString &value) { return value; }
template <size_t N>
QCborValue encode(const std::array<int, N> &values)
{
    QCborArray array;
    for (int value : values) array.append(qint64(value));
    return array;
}

// A value of another type than expected leaves the default in place.
void decode(const QCborValue &value, int &target)
{
    if (value.isInteger())
        target = int(std::clamp<qint64>(value.toInteger(), INT_MIN, INT_MAX));
    else if (value.isDouble() && std::isfinite(value.toDouble()))
        target = int(std::clamp<double>(std::round(value.toDouble()), INT_MIN, INT_MAX));
}
void decode(const QCborValue &value, float &target)
{
    if (value.isDouble() || value.isInteger()) target = float(value.toDouble());
}
void decode(const QCborValue &value, bool &target)
{
    if (value.isBool()) target = value.toBool();
}
void decode(const QCborValue &value, QString &target)
{
    if (value.isString()) target = value.toString();
}
template <size_t N>
void decode(const QCborValue &value, std::array<int, N> &targets)
{
    const QCborArray array = value.toArray();
    for (qsizetype index = 0; index < std::min<qsizetype>(array.size(), qsizetype(N)); ++index)
        decode(array.at(index), targets[size_t(index)]);
}

template <typename Owner, typename Value, size_t N>
void store(QCborMap &map, const Owner &owner, const Field<Owner, Value> (&fields)[N])
{
    for (const auto &field : fields)
        map.insert(QLatin1StringView(field.key), encode(owner.*field.member));
}

template <typename Owner, typename Value, size_t N>
void load(const QCborMap &map, Owner &owner, const Field<Owner, Value> (&fields)[N])
{
    for (const auto &field : fields)
        decode(map.value(QLatin1StringView(field.key)), owner.*field.member);
}

QCborMap encodeSettings(const Settings &settings)
{
    QCborMap map;
    store(map, settings, SettingFloats);
    store(map, settings, SettingInts);
    store(map, settings, SettingBools);
    store(map, settings, SettingBands);
    store(map, settings, SettingWheels);
    QCborArray points;
    for (size_t index = 0; index < std::min(settings.pointColor.count, PointColor::MaxPoints); ++index) {
        const PointColor::Point &point = settings.pointColor.points[index];
        points.append(QCborMap{
            {QStringLiteral("source"), QCborArray{point.source[0], point.source[1], point.source[2]}},
            {QStringLiteral("hue"), point.hueShift}, {QStringLiteral("saturation"), point.saturationShift},
            {QStringLiteral("luminance"), point.luminanceShift}, {QStringLiteral("variance"), point.variance},
            {QStringLiteral("range"), point.range}});
    }
    map.insert(QStringLiteral("pointColor"), points);
    QCborMap curves;
    for (int channel = 0; channel < ToneCurve::ChannelCount; ++channel) {
        QCborArray curve;
        for (const ToneCurve::Point &point : settings.toneCurves[size_t(channel)])
            curve.append(QCborArray{double(point.x), double(point.y)});
        curves.insert(QLatin1StringView(CurveKeys[channel]), curve);
    }
    map.insert(QStringLiteral("toneCurves"), curves);
    return map;
}

bool decodeSettings(const QCborMap &map, Settings &settings)
{
    load(map, settings, SettingFloats);
    load(map, settings, SettingInts);
    load(map, settings, SettingBools);
    load(map, settings, SettingBands);
    load(map, settings, SettingWheels);
    const QCborArray points = map.value(QStringLiteral("pointColor")).toArray();
    if (points.size() > qsizetype(PointColor::MaxPoints))
        return false;
    settings.pointColor.count = size_t(points.size());
    for (qsizetype index = 0; index < points.size(); ++index) {
        const QCborMap entry = points.at(index).toMap();
        PointColor::Point &point = settings.pointColor.points[size_t(index)];
        const QCborArray source = entry.value(QStringLiteral("source")).toArray();
        if (source.size() != 3)
            return false;
        for (int component = 0; component < 3; ++component)
            point.source[component] = source.at(component).toDouble(std::nan(""));
        decode(entry.value(QStringLiteral("hue")), point.hueShift);
        decode(entry.value(QStringLiteral("saturation")), point.saturationShift);
        decode(entry.value(QStringLiteral("luminance")), point.luminanceShift);
        decode(entry.value(QStringLiteral("variance")), point.variance);
        decode(entry.value(QStringLiteral("range")), point.range);
    }
    const QCborMap curves = map.value(QStringLiteral("toneCurves")).toMap();
    for (int channel = 0; channel < ToneCurve::ChannelCount; ++channel) {
        const QCborArray curve = curves.value(QLatin1StringView(CurveKeys[channel])).toArray();
        if (curve.size() > 64)
            return false;
        for (const QCborValue &entry : curve) {
            const QCborArray xy = entry.toArray();
            settings.toneCurves[size_t(channel)].append(
                {float(xy.at(0).toDouble(std::nan(""))), float(xy.at(1).toDouble(std::nan("")))});
        }
    }
    return true;
}

std::optional<Document> readCbor(const QByteArray &bytes, const Identity &photo)
{
    QCborParserError error;
    QCborValue value = QCborValue::fromCbor(bytes, &error);
    if (error.error != QCborError::NoError)
        return std::nullopt;
    if (value.isTag() && value.tag() == QCborTag(QCborKnownTags::Signature))
        value = value.taggedValue();
    const QCborMap map = value.toMap();
    const QCborMap header = map.value(QStringLiteral("ipr")).toMap();
    const QCborMap identity = header.value(QStringLiteral("photo")).toMap();
    const qint64 format = header.value(QStringLiteral("format")).toInteger(-1);
    if (format < 1 || identity.value(QStringLiteral("size")).toInteger(-2) != photo.size ||
        identity.value(QStringLiteral("modified")).toInteger(-2) != photo.modified)
        return std::nullopt;
    Document document;
    document.format = format > FormatVersion ? Format::Newer
        : format < FormatVersion ? Format::Older : Format::Current;
    if (!decodeSettings(map.value(QStringLiteral("settings")).toMap(), document.settings))
        return std::nullopt;
    const QCborMap metadata = map.value(QStringLiteral("metadata")).toMap();
    load(metadata, document.metadata, MetadataInts);
    load(metadata, document.metadata, MetadataStrings);
    load(metadata, document.metadata, MetadataBools);
    const QCborMap raw = map.value(QStringLiteral("raw")).toMap();
    load(raw, document.raw, RawStrings);
    load(raw, document.raw, RawBools);
    return document;
}

// ---- Former binary format (versions 1 to 9), converted on reading.

void readInt(QDataStream &stream, int &target)
{
    qint32 value = 0;
    stream >> value;
    target = value;
}

std::optional<Document> readBinary(const QByteArray &bytes, const Identity &photo)
{
    QDataStream stream(bytes);
    stream.setVersion(QDataStream::Qt_6_4);
    quint32 magic = 0;
    quint16 version = 0;
    qint64 sourceSize = -1;
    qint64 sourceModified = -1;
    stream >> magic >> version >> sourceSize >> sourceModified;
    if (magic != BinaryMagic || version < 1 || version > 9 || sourceSize != photo.size ||
        sourceModified != photo.modified)
        return std::nullopt;

    Document document;
    document.format = Format::Older;
    Settings &loaded = document.settings;
    stream >> loaded.brightness;
    readInt(stream, loaded.contrast); readInt(stream, loaded.blur); stream >> loaded.grayscale;
    readInt(stream, loaded.shadows); readInt(stream, loaded.blacks); readInt(stream, loaded.highlights);
    readInt(stream, loaded.whites); readInt(stream, loaded.temperature); readInt(stream, loaded.tint);
    readInt(stream, loaded.vibrance); readInt(stream, loaded.saturation); readInt(stream, loaded.texture);
    readInt(stream, loaded.clarity); readInt(stream, loaded.dehaze); readInt(stream, loaded.grain);
    if (version >= 2) {
        readInt(stream, loaded.denoiseLuminance);
        readInt(stream, loaded.denoiseChrominance);
        readInt(stream, loaded.sharpeningAmount);
        stream >> loaded.sharpeningRadius;
        readInt(stream, loaded.sharpeningMasking);
    }
    const size_t storedMixerBands = version >= 4 ? 8 : (version == 3 ? 7 : 6);
    for (size_t storedIndex = 0; storedIndex < storedMixerBands; ++storedIndex) {
        // Preserve technical band identity: the last band of versions 1-3 was
        // magenta at 300 degrees, not violet. Newly inserted bands start neutral.
        const size_t oldIndex = version >= 3 || storedIndex == 0
            ? storedIndex : storedIndex + 1;
        const size_t index = version < 4 && oldIndex == 6 ? 7 : oldIndex;
        readInt(stream, loaded.mixerHue[index]); readInt(stream, loaded.mixerSaturation[index]);
        readInt(stream, loaded.mixerLuminance[index]);
    }
    for (size_t index = 0; index < 3; ++index) {   // the zones; the global wheel follows in v8
        readInt(stream, loaded.gradingHue[index]); readInt(stream, loaded.gradingSaturation[index]);
    }
    stream >> loaded.automaticWhiteBalance;
    for (QVector<ToneCurve::Point> &curve : loaded.toneCurves) {
        qint32 count = 0;
        stream >> count;
        if (count < 0 || count > 64)
            return std::nullopt;
        for (qint32 index = 0; index < count; ++index) {
            ToneCurve::Point point;
            stream >> point.x >> point.y;
            curve.append(point);
        }
    }
    UserImageMetadata &metadata = document.metadata;
    stream >> metadata.rating >> metadata.colorLabel >> metadata.flagged
           >> metadata.ratingSet >> metadata.colorLabelSet;
    RawDevelopment &raw = document.raw;
    if (version >= 5) {
        stream >> raw.profile >> raw.profileSha256 >> raw.manifest
               >> raw.manifestSha256 >> raw.normalizationEnabled >> raw.engineVersion;
    }
    if (version >= 6) {
        stream >> raw.lensProfile >> raw.lensProfileSha256 >> raw.lensProfileResolved
               >> raw.lensCorrectionEnabled >> raw.chromaticAberrationEnabled;
    }
    if (version >= 7) {
        readInt(stream, loaded.gradingBlending);
        readInt(stream, loaded.gradingBalance);
    }
    if (version >= 8) {
        readInt(stream, loaded.gradingHue[3]);
        readInt(stream, loaded.gradingSaturation[3]);
        for (int &luminance : loaded.gradingLuminance) readInt(stream, luminance);
    }
    if (version >= 9) {
        qint32 count = 0;
        stream >> count;
        if (count < 0 || count > qint32(PointColor::MaxPoints))
            return std::nullopt;
        loaded.pointColor.count = size_t(count);
        for (qint32 index = 0; index < count; ++index) {
            PointColor::Point &point = loaded.pointColor.points[size_t(index)];
            stream >> point.source[0] >> point.source[1] >> point.source[2];
            readInt(stream, point.hueShift); readInt(stream, point.saturationShift);
            readInt(stream, point.luminanceShift); readInt(stream, point.variance); readInt(stream, point.range);
        }
    }
    if (stream.status() != QDataStream::Ok)
        return std::nullopt;
    return document;
}

std::optional<QByteArray> readSidecarBytes(const QString &sidecar)
{
    QFile file(sidecar);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxBytes)
        return std::nullopt;
    return file.readAll();
}

std::optional<Document> parse(const QByteArray &bytes, const Identity &photo)
{
    QDataStream magic(bytes);
    quint32 first = 0;
    magic >> first;
    return first == BinaryMagic ? readBinary(bytes, photo) : readCbor(bytes, photo);
}
}

QString path(const QString &fileName)
{
    return fileName + QStringLiteral(".ipr");
}

std::optional<Document> read(const QString &fileName)
{
    const QFileInfo sourceInfo(fileName);
    if (!sourceInfo.exists())
        return std::nullopt;
    const std::optional<QByteArray> bytes = readSidecarBytes(path(fileName));
    if (!bytes)
        return std::nullopt;
    std::optional<Document> document = parse(*bytes, identityOf(sourceInfo));
    if (!document || !bound(document->settings, document->metadata))
        return std::nullopt;
    return document;
}

bool write(const QString &fileName, const Document &document)
{
    const QFileInfo sourceInfo(fileName);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return false;
    const Identity photo = identityOf(sourceInfo);
    // A newer IPR's file keeps what this version cannot represent: never overwritten.
    if (const std::optional<QByteArray> existing = readSidecarBytes(path(fileName))) {
        const std::optional<Document> previous = parse(*existing, photo);
        if (previous && previous->format == Format::Newer)
            return false;
    }
    QCborMap metadata, raw;
    store(metadata, document.metadata, MetadataInts);
    store(metadata, document.metadata, MetadataStrings);
    store(metadata, document.metadata, MetadataBools);
    store(raw, document.raw, RawStrings);
    store(raw, document.raw, RawBools);
    const QCborMap map{
        {QStringLiteral("ipr"), QCborMap{
            {QStringLiteral("format"), FormatVersion},
            {QStringLiteral("writer"), QCoreApplication::applicationVersion()},
            {QStringLiteral("photo"), QCborMap{{QStringLiteral("size"), photo.size},
                                               {QStringLiteral("modified"), photo.modified}}}}},
        {QStringLiteral("settings"), encodeSettings(document.settings)},
        {QStringLiteral("metadata"), metadata},
        {QStringLiteral("raw"), raw}};
    QSaveFile file(path(fileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QCborValue(QCborKnownTags::Signature, map).toCbor();
    return file.write(bytes) == bytes.size() && file.commit();
}
}
