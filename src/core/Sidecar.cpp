#include "Sidecar.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace Sidecar {
namespace {
using EditRecipe::RawDevelopment;
using EditRecipe::Settings;
using EditRecipe::UserImageMetadata;

void readInt(QDataStream &stream, int &target)
{
    qint32 value = 0;
    stream >> value;
    target = value;
}

void writeInt(QDataStream &stream, int value) { stream << qint32(value); }

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
}

QString path(const QString &fileName)
{
    return fileName + QStringLiteral(".ipr");
}

bool read(const QString &fileName, Settings *settings, UserImageMetadata *metadata, RawDevelopment *raw)
{
    if (!settings || !metadata)
        return false;
    const QFileInfo sourceInfo(fileName);
    QFile file(path(fileName));
    if (!sourceInfo.exists() || !file.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_4);
    quint32 magic = 0;
    quint16 version = 0;
    qint64 sourceSize = -1;
    qint64 sourceModified = -1;
    stream >> magic >> version >> sourceSize >> sourceModified;
    if (magic != 0x49505231u || version < 1 || version > 9 || file.size() > 1024 * 1024 ||
        sourceSize != sourceInfo.size() ||
        sourceModified != sourceInfo.lastModified().toMSecsSinceEpoch())
        return false;

    Settings loaded;
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
        // Preserve technical band identity: legacy last band was magenta at
        // 300 degrees, not violet. Newly inserted bands start neutral.
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
            return false;
        for (qint32 index = 0; index < count; ++index) {
            ToneCurve::Point point;
            stream >> point.x >> point.y;
            curve.append(point);
        }
    }
    UserImageMetadata loadedMetadata;
    stream >> loadedMetadata.rating >> loadedMetadata.colorLabel >> loadedMetadata.flagged
           >> loadedMetadata.ratingSet >> loadedMetadata.colorLabelSet;
    RawDevelopment loadedRaw;
    if (version >= 5) {
        stream >> loadedRaw.profile >> loadedRaw.profileSha256 >> loadedRaw.manifest
               >> loadedRaw.manifestSha256 >> loadedRaw.normalizationEnabled >> loadedRaw.engineVersion;
    }
    if (version >= 6) {
        stream >> loadedRaw.lensProfile >> loadedRaw.lensProfileSha256 >> loadedRaw.lensProfileResolved
               >> loadedRaw.lensCorrectionEnabled >> loadedRaw.chromaticAberrationEnabled;
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
            return false;
        loaded.pointColor.count = size_t(count);
        for (qint32 index = 0; index < count; ++index) {
            PointColor::Point &point = loaded.pointColor.points[size_t(index)];
            stream >> point.source[0] >> point.source[1] >> point.source[2];
            readInt(stream, point.hueShift); readInt(stream, point.saturationShift);
            readInt(stream, point.luminanceShift); readInt(stream, point.variance); readInt(stream, point.range);
        }
    }
    if (stream.status() != QDataStream::Ok || !bound(loaded, loadedMetadata))
        return false;
    *settings = std::move(loaded);
    *metadata = std::move(loadedMetadata);
    if (raw) *raw = std::move(loadedRaw);
    return true;
}

bool write(const QString &fileName, const Settings &settings, const UserImageMetadata &metadata,
           const RawDevelopment &raw)
{
    const QFileInfo sourceInfo(fileName);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return false;
    QSaveFile file(path(fileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << quint32(0x49505231u) << quint16(9) << qint64(sourceInfo.size())
           << qint64(sourceInfo.lastModified().toMSecsSinceEpoch());
    stream << settings.brightness;
    writeInt(stream, settings.contrast); writeInt(stream, settings.blur); stream << settings.grayscale;
    writeInt(stream, settings.shadows); writeInt(stream, settings.blacks); writeInt(stream, settings.highlights);
    writeInt(stream, settings.whites); writeInt(stream, settings.temperature); writeInt(stream, settings.tint);
    writeInt(stream, settings.vibrance); writeInt(stream, settings.saturation); writeInt(stream, settings.texture);
    writeInt(stream, settings.clarity); writeInt(stream, settings.dehaze); writeInt(stream, settings.grain);
    writeInt(stream, settings.denoiseLuminance);
    writeInt(stream, settings.denoiseChrominance);
    writeInt(stream, settings.sharpeningAmount);
    stream << settings.sharpeningRadius;
    writeInt(stream, settings.sharpeningMasking);
    for (size_t index = 0; index < settings.mixerHue.size(); ++index) {
        writeInt(stream, settings.mixerHue[index]); writeInt(stream, settings.mixerSaturation[index]);
        writeInt(stream, settings.mixerLuminance[index]);
    }
    for (size_t index = 0; index < 3; ++index) {
        writeInt(stream, settings.gradingHue[index]); writeInt(stream, settings.gradingSaturation[index]);
    }
    stream << settings.automaticWhiteBalance;
    for (const QVector<ToneCurve::Point> &curve : settings.toneCurves) {
        stream << qint32(curve.size());
        for (const ToneCurve::Point &point : curve)
            stream << point.x << point.y;
    }
    stream << metadata.rating << metadata.colorLabel << metadata.flagged
           << metadata.ratingSet << metadata.colorLabelSet;
    stream << raw.profile << raw.profileSha256 << raw.manifest << raw.manifestSha256
           << raw.normalizationEnabled << raw.engineVersion;
    stream << raw.lensProfile << raw.lensProfileSha256 << raw.lensProfileResolved
           << raw.lensCorrectionEnabled << raw.chromaticAberrationEnabled;
    writeInt(stream, settings.gradingBlending);
    writeInt(stream, settings.gradingBalance);
    writeInt(stream, settings.gradingHue[3]);
    writeInt(stream, settings.gradingSaturation[3]);
    for (int luminance : settings.gradingLuminance) writeInt(stream, luminance);
    const size_t points = std::min(settings.pointColor.count, PointColor::MaxPoints);
    writeInt(stream, int(points));
    for (size_t index = 0; index < points; ++index) {
        const PointColor::Point &point = settings.pointColor.points[index];
        stream << point.source[0] << point.source[1] << point.source[2];
        writeInt(stream, point.hueShift); writeInt(stream, point.saturationShift);
        writeInt(stream, point.luminanceShift); writeInt(stream, point.variance); writeInt(stream, point.range);
    }
    return stream.status() == QDataStream::Ok && file.commit();
}
}
