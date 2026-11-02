#include "RecipeCodec.h"

#include <QCborArray>
#include <QCborValue>

#include <algorithm>
#include <climits>
#include <cmath>

namespace RecipeCodec {
namespace {
using EditRecipe::RawDevelopment;
using EditRecipe::Settings;
using EditRecipe::UserImageMetadata;

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
    {"developmentMode", &RawDevelopment::developmentMode},
};
const Field<RawDevelopment, bool> RawBools[] = {
    {"normalizationEnabled", &RawDevelopment::normalizationEnabled},
    {"lensProfileResolved", &RawDevelopment::lensProfileResolved},
    {"lensCorrectionEnabled", &RawDevelopment::lensCorrectionEnabled},
    {"chromaticAberrationEnabled", &RawDevelopment::chromaticAberrationEnabled},
};
const char *const CurveKeys[ToneCurve::ChannelCount] = {"luminance", "red", "green", "blue"};
constexpr qsizetype MaxCurvePoints = 64;
const QString RecipeRawKey = QStringLiteral("rawDevelopment");

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

// A value of another type than expected leaves the target unchanged.
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
}

bool bound(Settings &s)
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
    return true;
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
    const QCborValue pointColor = map.value(QStringLiteral("pointColor"));
    if (pointColor.isArray()) {
        const QCborArray points = pointColor.toArray();
        if (points.size() > qsizetype(PointColor::MaxPoints))
            return false;
        settings.pointColor = {};
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
    }
    const QCborMap curves = map.value(QStringLiteral("toneCurves")).toMap();
    for (int channel = 0; channel < ToneCurve::ChannelCount; ++channel) {
        const QCborValue stored = curves.value(QLatin1StringView(CurveKeys[channel]));
        if (!stored.isArray())
            continue;
        const QCborArray curve = stored.toArray();
        if (curve.size() > MaxCurvePoints)
            return false;
        QVector<ToneCurve::Point> &points = settings.toneCurves[size_t(channel)];
        points.clear();
        for (const QCborValue &entry : curve) {
            const QCborArray xy = entry.toArray();
            points.append({float(xy.at(0).toDouble(std::nan(""))), float(xy.at(1).toDouble(std::nan("")))});
        }
    }
    return bound(settings);
}

QCborMap encodeMetadata(const UserImageMetadata &metadata)
{
    QCborMap map;
    store(map, metadata, MetadataInts);
    store(map, metadata, MetadataStrings);
    store(map, metadata, MetadataBools);
    return map;
}

void decodeMetadata(const QCborMap &map, UserImageMetadata &metadata)
{
    load(map, metadata, MetadataInts);
    load(map, metadata, MetadataStrings);
    load(map, metadata, MetadataBools);
    metadata.rating = std::clamp(metadata.rating, 0, 5);
}

QCborMap encodeRaw(const RawDevelopment &raw)
{
    QCborMap map;
    store(map, raw, RawStrings);
    store(map, raw, RawBools);
    return map;
}

void decodeRaw(const QCborMap &map, RawDevelopment &raw)
{
    load(map, raw, RawStrings);
    load(map, raw, RawBools);
}

QCborMap encodeRecipe(const Settings &settings, const RawDevelopment &raw)
{
    QCborMap recipe = encodeSettings(settings);
    QCborMap development = encodeRaw(raw);
    development.remove(QStringLiteral("engineVersion"));
    recipe.insert(RecipeRawKey, development);
    return recipe;
}

bool decodeRecipe(const QCborMap &map, Settings &settings, RawDevelopment &raw)
{
    decodeRaw(map.value(RecipeRawKey).toMap(), raw);
    return decodeSettings(map, settings);
}

QCborMap settingsOf(QCborMap recipe)
{
    recipe.remove(RecipeRawKey);
    return recipe;
}
}
