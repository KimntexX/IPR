#ifndef IPR_RAW_CAMERACHARACTERIZATION_H
#define IPR_RAW_CAMERACHARACTERIZATION_H

#include <QJsonObject>
#include <QString>
#include <array>
#include <optional>

namespace CameraCharacterization {

// Native sensor metadata, not RGB values after a DCP or display transform.
struct SensorContext
{
    QString extension;
    QString make;
    QString model;
    std::array<unsigned, 4> black{};
    unsigned globalBlack = 0;
    unsigned nativeWhite = 0;
    // Canon specular white of the file (maker notes), 0 when unknown. A rule
    // with specular_white_min/max matches only within those bounds: on the
    // EOS 60D it tells apart ISO modes whose black and native white are equal.
    unsigned specularWhite = 0;
    QString decoderId;
    QString decoderVersion;
};

// Reads at most 1 MiB; parsed catalogs are cached by content, bounded to 8 entries.
// No RAW files, network, SQL database or DCP are read by this module.
std::optional<QJsonObject> loadCatalog(const QString &path, QString *reason);
std::optional<QJsonObject> selectSensorMode(const QJsonObject &catalog,
                                           const SensorContext &context, QString *reason);
}

#endif // IPR_RAW_CAMERACHARACTERIZATION_H
