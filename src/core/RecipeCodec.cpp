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
const Field<Geometry::Settings, int> GeometryInts[] = {
    {"quarterTurns", &Geometry::Settings::quarterTurns}, {"vertical", &Geometry::Settings::vertical},
    {"horizontal", &Geometry::Settings::horizontal}, {"aspect", &Geometry::Settings::aspect},
    {"scale", &Geometry::Settings::scale}, {"offsetX", &Geometry::Settings::offsetX},
    {"offsetY", &Geometry::Settings::offsetY},
};
const Field<Geometry::Settings, float> GeometryFloats[] = {{"rotation", &Geometry::Settings::rotation}};
const Field<Geometry::Settings, double> GeometryDoubles[] = {
    {"cropX", &Geometry::Settings::cropX}, {"cropY", &Geometry::Settings::cropY},
    {"cropWidth", &Geometry::Settings::cropWidth}, {"cropHeight", &Geometry::Settings::cropHeight},
    {"cropRatio", &Geometry::Settings::cropRatio},
};
const Field<Geometry::Settings, bool> GeometryBools[] = {
    {"flipHorizontal", &Geometry::Settings::flipHorizontal}, {"flipVertical", &Geometry::Settings::flipVertical},
    {"constrain", &Geometry::Settings::constrain},
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
QCborValue encode(double value) { return value; }
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
void decode(const QCborValue &value, double &target)
{
    if (value.isDouble() || value.isInteger()) target = value.toDouble();
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

namespace {
QCborArray pointArray(const cv::Point2f &point)
{
    return QCborArray{double(point.x), double(point.y)};
}

bool readPoint(const QCborValue &value, cv::Point2f &point)
{
    const QCborArray array = value.toArray();
    if (array.size() != 2 || !array.at(0).isDouble() || !array.at(1).isDouble())
        return false;
    point = {float(array.at(0).toDouble()), float(array.at(1).toDouble())};
    return true;
}

QCborArray encodeMasks(const LocalAdjustments::Masks &masks)
{
    QCborArray list;
    for (const LocalAdjustments::Mask &mask : masks) {
        // Only the values set: a setting added later reads as zero in an older file.
        QCborMap values;
        for (size_t i = 0; i < mask.values.size(); ++i)
            if (mask.values[i] != 0.0f)
                values.insert(LocalAdjustments::parameterName(int(i)), double(mask.values[i]));
        QCborArray shapes;
        for (const LocalAdjustments::Shape &shape : mask.shapes) {
            QCborArray strokes;
            for (const LocalAdjustments::BrushStroke &stroke : shape.strokes) {
                QCborArray points;
                for (const cv::Point2f &point : stroke.points) {
                    points.append(double(point.x));
                    points.append(double(point.y));
                }
                strokes.append(QCborMap{{QStringLiteral("size"), double(stroke.size)}, {QStringLiteral("feather"), double(stroke.feather)},
                                        {QStringLiteral("flow"), double(stroke.flow)}, {QStringLiteral("points"), points}});
            }
            shapes.append(QCborMap{
                {QStringLiteral("kind"), LocalAdjustments::kindName(shape.kind)}, {QStringLiteral("combine"), LocalAdjustments::combineName(shape.combine)},
                {QStringLiteral("inverted"), shape.inverted}, {QStringLiteral("start"), pointArray(shape.start)},
                {QStringLiteral("end"), pointArray(shape.end)}, {QStringLiteral("center"), pointArray(shape.center)},
                {QStringLiteral("radiusX"), double(shape.radiusX)}, {QStringLiteral("radiusY"), double(shape.radiusY)},
                {QStringLiteral("angle"), double(shape.angle)}, {QStringLiteral("feather"), double(shape.feather)},
                {QStringLiteral("strokes"), strokes}});
        }
        list.append(QCborMap{{QStringLiteral("name"), mask.name}, {QStringLiteral("visible"), mask.visible},
                             {QStringLiteral("opacity"), double(mask.opacity)}, {QStringLiteral("values"), values},
                             {QStringLiteral("shapes"), shapes}});
    }
    return list;
}

// False on what cannot be a mask; a value of an unknown setting, from a later version, is skipped.
bool decodeMasks(const QCborArray &list, LocalAdjustments::Masks &masks)
{
    masks.clear();
    for (const QCborValue &entry : list) {
        if (!entry.isMap())
            return false;
        const QCborMap map = entry.toMap();
        LocalAdjustments::Mask mask;
        mask.name = map.value(QStringLiteral("name")).toString();
        mask.visible = map.value(QStringLiteral("visible")).toBool(true);
        mask.opacity = float(map.value(QStringLiteral("opacity")).toDouble(1.0));
        const QCborMap values = map.value(QStringLiteral("values")).toMap();
        for (auto value = values.cbegin(); value != values.cend(); ++value) {
            const int parameter = LocalAdjustments::parameterNamed(value.key().toString());
            if (parameter >= 0)
                mask.values[size_t(parameter)] = float(value.value().toDouble());
        }
        for (const QCborValue &shapeEntry : map.value(QStringLiteral("shapes")).toArray()) {
            if (!shapeEntry.isMap())
                return false;
            const QCborMap fields = shapeEntry.toMap();
            LocalAdjustments::Shape shape;
            if (!LocalAdjustments::kindNamed(fields.value(QStringLiteral("kind")).toString(), shape.kind) ||
                !LocalAdjustments::combineNamed(fields.value(QStringLiteral("combine")).toString(), shape.combine) ||
                !readPoint(fields.value(QStringLiteral("start")), shape.start) ||
                !readPoint(fields.value(QStringLiteral("end")), shape.end) ||
                !readPoint(fields.value(QStringLiteral("center")), shape.center))
                return false;
            shape.inverted = fields.value(QStringLiteral("inverted")).toBool();
            shape.radiusX = float(fields.value(QStringLiteral("radiusX")).toDouble());
            shape.radiusY = float(fields.value(QStringLiteral("radiusY")).toDouble());
            shape.angle = float(fields.value(QStringLiteral("angle")).toDouble());
            shape.feather = float(fields.value(QStringLiteral("feather")).toDouble());
            for (const QCborValue &strokeEntry : fields.value(QStringLiteral("strokes")).toArray()) {
                const QCborMap strokeFields = strokeEntry.toMap();
                LocalAdjustments::BrushStroke stroke;
                stroke.size = float(strokeFields.value(QStringLiteral("size")).toDouble());
                stroke.feather = float(strokeFields.value(QStringLiteral("feather")).toDouble());
                stroke.flow = float(strokeFields.value(QStringLiteral("flow")).toDouble());
                const QCborArray points = strokeFields.value(QStringLiteral("points")).toArray();
                if (points.size() % 2 != 0)
                    return false;
                for (qsizetype i = 0; i + 1 < points.size(); i += 2)
                    stroke.points.emplace_back(float(points.at(i).toDouble()), float(points.at(i + 1).toDouble()));
                shape.strokes.push_back(std::move(stroke));
            }
            mask.shapes.push_back(std::move(shape));
        }
        masks.push_back(std::move(mask));
    }
    return true;
}
}

bool bound(Settings &s)
{
    if (!std::isfinite(s.brightness) || !std::isfinite(s.sharpeningRadius) || !Geometry::bound(s.geometry) ||
        !LocalAdjustments::bound(s.localMasks))
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
    QCborMap geometry;
    store(geometry, settings.geometry, GeometryInts);
    store(geometry, settings.geometry, GeometryFloats);
    store(geometry, settings.geometry, GeometryDoubles);
    store(geometry, settings.geometry, GeometryBools);
    map.insert(QStringLiteral("geometry"), geometry);
    // Absent without masks, so that a recipe without any reads as it always did.
    if (!settings.localMasks.empty())
        map.insert(QStringLiteral("localMasks"), encodeMasks(settings.localMasks));
    return map;
}

bool decodeSettings(const QCborMap &map, Settings &settings)
{
    settings.localMasks.clear();
    const QCborValue localMasks = map.value(QStringLiteral("localMasks"));
    if (localMasks.isArray() && !decodeMasks(localMasks.toArray(), settings.localMasks))
        return false;
    load(map, settings, SettingFloats);
    load(map, settings, SettingInts);
    load(map, settings, SettingBools);
    load(map, settings, SettingBands);
    load(map, settings, SettingWheels);
    const QCborMap geometry = map.value(QStringLiteral("geometry")).toMap();
    load(geometry, settings.geometry, GeometryInts);
    load(geometry, settings.geometry, GeometryFloats);
    load(geometry, settings.geometry, GeometryDoubles);
    load(geometry, settings.geometry, GeometryBools);
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
