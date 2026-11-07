#include "Sidecar.h"

#include "RecipeCodec.h"

#include <QCborMap>
#include <QCborValue>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

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
    if (!RecipeCodec::decodeSettings(map.value(QStringLiteral("settings")).toMap(), document.settings))
        return std::nullopt;
    RecipeCodec::decodeMetadata(map.value(QStringLiteral("metadata")).toMap(), document.metadata);
    RecipeCodec::decodeRaw(map.value(QStringLiteral("raw")).toMap(), document.raw);
    // The history is secondary: a damaged one is dropped, the recipe still opens.
    const QCborValue history = map.value(QStringLiteral("history"));
    if (history.isByteArray()) {
        const QCborValue steps = QCborValue::fromCbor(qUncompress(history.toByteArray()));
        if (auto restored = EditHistory::fromCbor(steps.toMap()))
            document.history = std::move(*restored);
    }
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
    if (stream.status() != QDataStream::Ok || !RecipeCodec::bound(loaded))
        return std::nullopt;
    metadata.rating = std::clamp(metadata.rating, 0, 5);
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
    return parse(*bytes, identityOf(sourceInfo));
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
    QCborMap map{
        {QStringLiteral("ipr"), QCborMap{
            {QStringLiteral("format"), FormatVersion},
            {QStringLiteral("writer"), QCoreApplication::applicationVersion()},
            {QStringLiteral("photo"), QCborMap{{QStringLiteral("size"), photo.size},
                                               {QStringLiteral("modified"), photo.modified}}}}},
        {QStringLiteral("settings"), RecipeCodec::encodeSettings(document.settings)},
        {QStringLiteral("metadata"), RecipeCodec::encodeMetadata(document.metadata)},
        {QStringLiteral("raw"), RecipeCodec::encodeRaw(document.raw)}};
    // Successive steps differ by a few fields: compressed together they stay small.
    if (!document.history.isEmpty())
        map.insert(QStringLiteral("history"),
                   qCompress(QCborValue(document.history.toCbor()).toCbor()));
    QSaveFile file(path(fileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QCborValue(QCborKnownTags::Signature, map).toCbor();
    return file.write(bytes) == bytes.size() && file.commit();
}
}
