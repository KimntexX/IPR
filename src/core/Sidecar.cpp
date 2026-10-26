#include "Sidecar.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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
    if (magic != 0x49505231u || version < 1 || version > 8 || file.size() > 1024 * 1024 ||
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
        if (loaded.denoiseLuminance < 0 || loaded.denoiseLuminance > 100 ||
            loaded.denoiseChrominance < 0 || loaded.denoiseChrominance > 100 ||
            loaded.sharpeningAmount < 0 || loaded.sharpeningAmount > 200 ||
            !std::isfinite(loaded.sharpeningRadius) ||
            loaded.sharpeningRadius < 0.3f || loaded.sharpeningRadius > 3.0f ||
            loaded.sharpeningMasking < 0 || loaded.sharpeningMasking > 100)
            return false;
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
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return false;
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
    if (stream.status() != QDataStream::Ok)
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
    stream << quint32(0x49505231u) << quint16(8) << qint64(sourceInfo.size())
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
    return stream.status() == QDataStream::Ok && file.commit();
}
}
