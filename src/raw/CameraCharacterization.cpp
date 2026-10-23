#include "CameraCharacterization.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <cmath>

namespace CameraCharacterization {
namespace {
std::optional<QJsonObject> reject(QString *reason, const char *message)
{
    if (reason) *reason = QString::fromLatin1(message);
    return std::nullopt;
}
}

std::optional<QJsonObject> loadCatalog(const QString &path, QString *reason)
{
    if (reason) reason->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024)
        return reject(reason, "Characterization catalog unreadable or exceeds 1 MiB.");
    const auto bytes = file.read(1024 * 1024 + 1);
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || file.error() != QFileDevice::NoError)
        return reject(reason, "Cannot read characterization catalog.");
    const auto key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    struct Cached { QByteArray key; QJsonObject catalog; };
    static QMutex mutex;
    static QList<Cached> cache;
    {
        QMutexLocker lock(&mutex);
        for (qsizetype i = 0; i < cache.size(); ++i) {
            if (cache[i].key == key) {
                const auto hit = cache.takeAt(i);
                cache.prepend(hit);
                return hit.catalog;
            }
        }
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    const auto root = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        root.value("schema").toDouble(-1) != 4 ||
        !root.value("entries").isArray() || root.value("entries").toArray().size() > 256)
        return reject(reason, "Invalid characterization schema/backend/entries.");
    {
        QMutexLocker lock(&mutex);
        // Concurrent readers may have parsed the same bytes; do not duplicate entries.
        for (qsizetype i = cache.size(); i-- > 0;)
            if (cache[i].key == key) cache.removeAt(i);
        cache.prepend({key, root});
        while (cache.size() > 8) cache.removeLast();
    }
    return root;
}

std::optional<QJsonObject> selectSensorMode(const QJsonObject &catalog,
                                           const SensorContext &c, QString *reason)
{
    if (reason) reason->clear();
    std::optional<QJsonObject> selected;
    static const QRegularExpression extensionPattern(QStringLiteral("^[a-z0-9]+$"));
    for (const auto &value : catalog.value("entries").toArray()) {
        if (!value.isObject()) return reject(reason, "Invalid characterization entry.");
        const auto entry = value.toObject();
        const auto extension = entry.value("format_extension").toString();
        if (!extensionPattern.match(extension).hasMatch() ||
            entry.value("camera_make").toString().trimmed().isEmpty() ||
            entry.value("camera_model").toString().trimmed().isEmpty() ||
            entry.value("decoder_id").toString().trimmed().isEmpty() ||
            entry.value("decoder_version").toString().trimmed().isEmpty())
            return reject(reason, "Invalid characterization format/camera identity.");
        if (entry.value("decoder_id").toString() != c.decoderId ||
            entry.value("decoder_version").toString() != c.decoderVersion ||
            extension != c.extension.toLower() || entry.value("camera_make").toString() != c.make ||
            entry.value("camera_model").toString() != c.model) continue;
        const auto bound = [&](const char *key) { return entry.value(key).toDouble(-1); };
        const double blackMin = bound("black_min"), blackMax = bound("black_max"),
                     whiteMin = bound("native_white_min"), whiteMax = bound("native_white_max");
        if (blackMin < 0 || blackMax < blackMin || blackMax > 65535 || whiteMin <= blackMax + 256 ||
            whiteMax < whiteMin || whiteMax > 65535 || std::floor(blackMin) != blackMin ||
            std::floor(blackMax) != blackMax || std::floor(whiteMin) != whiteMin || std::floor(whiteMax) != whiteMax)
            return reject(reason, "Invalid sensor mode bounds.");
        bool matches = c.nativeWhite >= whiteMin && c.nativeWhite <= whiteMax;
        const bool specularBounded = entry.contains("specular_white_min") || entry.contains("specular_white_max");
        if (specularBounded) {
            const double specularMin = bound("specular_white_min"), specularMax = bound("specular_white_max");
            if (specularMin <= 0 || specularMax < specularMin || specularMax > 65535 ||
                std::floor(specularMin) != specularMin || std::floor(specularMax) != specularMax)
                return reject(reason, "Invalid specular white bounds.");
            matches &= c.specularWhite >= specularMin && c.specularWhite <= specularMax;
        }
        for (unsigned black : c.black) {
            const double total = double(c.globalBlack) + black;
            matches &= total >= blackMin && total <= blackMax;
        }
        if (!matches) continue;
        if (selected) return reject(reason, "Ambiguous sensor modes.");
        selected = entry;
    }
    return selected;
}
}
