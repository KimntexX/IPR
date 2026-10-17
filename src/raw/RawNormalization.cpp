#include "RawNormalization.h"
#include "CameraCharacterization.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
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
    if (!c.cameraWhiteBalance || !c.nativeBaselineUnknown || c.profilePath.isEmpty())
        return reject("Unsupported RAW/WB/baseline/profile context; normalization skipped.");
    const auto catalog = CameraCharacterization::loadCatalog(manifestPath, reason);
    if (!catalog) return std::nullopt;
    const auto root = *catalog;
    const auto selected = CameraCharacterization::selectSensorMode(root,
        {QFileInfo(c.fileName).suffix(), c.cameraMake, c.cameraModel, c.black,
         c.globalBlack, c.nativeWhiteLevel, c.decoderId, c.decoderVersion}, reason);
    if (!selected) return std::nullopt;
    const QString profileHash = hashFile(c.profilePath);
    if (profileHash.isEmpty())
        return reject("Cannot verify profile hash.");
    std::optional<Calibration> result;
    {
        const auto entry = *selected;
        if (!digest(entry.value("profile_sha256").toString()))
            return reject("Invalid normalization hash.");
        if (entry.value("profile_sha256").toString() != profileHash)
            return reject("Profile outside characterized rendering conditions.");
        const double white = entry.value("white_level").toDouble(-1);
        const double ev = entry.value("baseline_exposure_ev").toDouble(100);
        if (!std::isfinite(white) || std::floor(white) != white || white <= 0 ||
            white > 65535 || !std::isfinite(ev) || std::abs(ev) > 2 ||
            entry.value("provenance").toString().trimmed().isEmpty())
            return reject("Invalid normalization values/provenance.");
        for (int channel = 0; channel < 4; ++channel) {
            if (white <= double(c.globalBlack) + c.black[channel] + 256)
                return reject("Measured black does not match normalization contract.");
        }
        result = Calibration{unsigned(white), ev, entry.value("provenance").toString()};
    }
    if (!result || !root.contains("profile_rendering"))
        return result;
    const auto rendering = root.value("profile_rendering");
    if (!rendering.isArray() || rendering.toArray().size() > 256)
        return reject("Invalid profile rendering parameters.");
    bool found = false;
    for (const auto &value : rendering.toArray()) {
        if (!value.isObject()) return reject("Invalid profile rendering entry.");
        const auto entry = value.toObject();
        if (entry.value("camera_make").toString() != c.cameraMake ||
            entry.value("camera_model").toString() != c.cameraModel ||
            entry.value("profile_sha256").toString() != profileHash)
            continue;
        const double compensation = entry.value("exposure_compensation_ev").toDouble(100);
        if (found || !std::isfinite(compensation) || std::abs(compensation) > 2 ||
            entry.value("baseline_exposure_ev").toDouble(100) != result->baselineExposureEv ||
            entry.value("provenance").toString().trimmed().isEmpty())
            return reject("Invalid or ambiguous profile rendering compensation.");
        found = true;
        result->profileExposureCompensationEv = compensation;
        result->profileRenderingProvenance = entry.value("provenance").toString();
    }
    return result;
}
}
