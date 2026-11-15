#ifndef IPR_TOOLS_SIDECARJSON_H
#define IPR_TOOLS_SIDECARJSON_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

// Readable form of a .ipr for debugging (ADR-0021): the sections of the CBOR
// document as JSON, with the compressed history opened. Reads the file as it is,
// without bounding or converting anything, so that it shows what is really stored.
namespace SidecarJson {
// Nothing, and an error, for bytes that are not a sidecar. A former binary sidecar
// gives its format and version only: its fields have no names.
std::optional<QJsonObject> fromBytes(const QByteArray &bytes, QString *error = nullptr);
}

#endif // IPR_TOOLS_SIDECARJSON_H
