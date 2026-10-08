#include "RawNormalization.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <cmath>

namespace RawNormalization {
namespace {
QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool digest(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(text).hasMatch();
}
}

QString cacheToken(const QString &manifestPath)
{
    if (manifestPath.isEmpty())
        return QStringLiteral("normalization-disabled-v1");
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return manifestPath + QStringLiteral(":invalid");
    return manifestPath + QLatin1Char(':') +
        QString::fromLatin1(QCryptographicHash::hash(file.read(1024 * 1024 + 1),
                                                    QCryptographicHash::Sha256).toHex());
}

std::optional<Calibration> resolve(const QString &manifestPath,
                                   const Context &c, QString *reason)
{
    if (reason)
        reason->clear();
    const auto reject = [&](const char *message) -> std::optional<Calibration> {
        if (reason)
            *reason = QString::fromLatin1(message);
        return std::nullopt;
    };
    if (manifestPath.isEmpty())
        return std::nullopt;
    if (QFileInfo(c.fileName).suffix().compare(QStringLiteral("cr3"), Qt::CaseInsensitive) ||
        c.cameraMake.compare(QStringLiteral("Canon"), Qt::CaseInsensitive) ||
        !c.cameraWhiteBalance || !c.nativeBaselineUnknown || c.profilePath.isEmpty())
        return reject("Unsupported RAW/WB/baseline/profile context; normalization skipped.");
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024)
        return reject("Normalization manifest unreadable or exceeds 1 MiB.");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.read(1024 * 1024 + 1), &error);
    const auto root = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        root.value("schema").toDouble(-1) != 1 ||
        root.value("libraw_version").toString() != c.librawVersion ||
        !root.value("entries").isArray() || root.value("entries").toArray().size() > 256)
        return reject("Invalid normalization schema/version/entries.");
    QJsonArray candidates;
    for (const auto &value : root.value("entries").toArray()) {
        if (!value.isObject())
            return reject("Invalid normalization entry.");
        const auto entry = value.toObject();
        if (entry.value("camera_make").toString() == c.cameraMake &&
            entry.value("camera_model").toString() == c.cameraModel &&
            entry.value("iso").toDouble(-1) == c.iso)
            candidates.append(entry);
    }
    if (candidates.isEmpty())
        return std::nullopt;
    const QString rawHash = hashFile(c.fileName), profileHash = hashFile(c.profilePath);
    if (rawHash.isEmpty() || profileHash.isEmpty())
        return reject("Cannot verify RAW/profile hash.");
    std::optional<Calibration> result;
    for (const auto &value : candidates) {
        const auto entry = value.toObject();
        if (!digest(entry.value("raw_sha256").toString()) ||
            !digest(entry.value("profile_sha256").toString()))
            return reject("Invalid normalization hash.");
        if (entry.value("raw_sha256").toString() != rawHash ||
            entry.value("profile_sha256").toString() != profileHash)
            continue;
        if (result)
            return reject("Ambiguous normalization entries.");
        const double white = entry.value("white_level").toDouble(-1);
        const double ev = entry.value("baseline_exposure_ev").toDouble(100);
        const auto black = entry.value("black_rgbg").toArray();
        if (!std::isfinite(white) || std::floor(white) != white || white <= 0 ||
            white > 65535 || !std::isfinite(ev) || std::abs(ev) > 2 ||
            black.size() != 4 || !digest(entry.value("dng_sha256").toString()) ||
            entry.value("provenance").toString().trimmed().isEmpty())
            return reject("Invalid normalization values/provenance.");
        for (int channel = 0; channel < 4; ++channel) {
            if (black[channel].toDouble(-1) != c.black[channel] ||
                c.black[channel] != c.black[0] || white <= c.black[channel] + 256)
                return reject("Measured black does not match normalization contract.");
        }
        result = Calibration{unsigned(white), ev, entry.value("provenance").toString()};
    }
    return result;
}
}
