#include "SidecarJson.h"

#include <QCborMap>
#include <QCborValue>
#include <QDataStream>

namespace SidecarJson {

std::optional<QJsonObject> fromBytes(const QByteArray &bytes, QString *error)
{
    if (bytes.startsWith("IPR1")) {
        QDataStream stream(bytes);
        quint32 magic = 0;
        quint16 version = 0;
        stream >> magic >> version;
        return QJsonObject{{QStringLiteral("binaryFormat"), int(version)}};
    }
    QCborParserError parseError;
    QCborValue value = QCborValue::fromCbor(bytes, &parseError);
    if (parseError.error != QCborError::NoError) {
        if (error) *error = parseError.errorString();
        return std::nullopt;
    }
    if (value.isTag())
        value = value.taggedValue();
    if (!value.isMap()) {
        if (error) *error = QStringLiteral("Not a CBOR map.");
        return std::nullopt;
    }
    QCborMap map = value.toMap();
    const QCborValue history = map.value(QStringLiteral("history"));
    if (history.isByteArray()) {
        const QCborValue opened = QCborValue::fromCbor(qUncompress(history.toByteArray()));
        map.insert(QStringLiteral("history"), opened.isMap() ? opened : QCborValue(QStringLiteral("damaged")));
    }
    return map.toJsonObject();
}

}
