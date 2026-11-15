#include "RecipeCodec.h"
#include "Sidecar.h"
#include "SidecarJson.h"

#include <QCborValue>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    const QString source = QDir(directory.path()).filePath(QStringLiteral("photo.png"));
    QFile file(source);
    if (!check(directory.isValid() && file.open(QIODevice::WriteOnly) && file.write("x") == 1, "source written"))
        return 1;
    file.close();

    Sidecar::Document document;
    document.settings.contrast = 37;
    document.metadata.rating = 4;
    document.raw.profile = QStringLiteral("neutral.dcp");
    document.history.reset(RecipeCodec::encodeSettings({}));
    document.history.record(RecipeCodec::encodeSettings(document.settings));
    if (!check(Sidecar::write(source, document), "sidecar written")) return 1;

    bool ok = true;
    QString error;
    const auto json = SidecarJson::fromBytes(readBytes(Sidecar::path(source)), &error);
    ok &= check(json && error.isEmpty(), "a sidecar converts");
    if (!json) return 1;
    ok &= check(json->value("ipr").toObject().value("format").toInt() == Sidecar::FormatVersion,
                "the header is shown");
    ok &= check(json->value("settings").toObject().value("contrast").toInt() == 37 &&
                json->value("metadata").toObject().value("rating").toInt() == 4 &&
                json->value("raw").toObject().value("profile").toString() == QStringLiteral("neutral.dcp"),
                "the sections are shown under their names");
    const QJsonObject history = json->value("history").toObject();
    ok &= check(history.value("position").toInt() == 1 && history.value("steps").toArray().size() == 2,
                "the compressed history is opened");

    QByteArray binary;
    {
        QDataStream stream(&binary, QIODevice::WriteOnly);
        stream << quint32(0x49505231u) << quint16(7);
    }
    const auto former = SidecarJson::fromBytes(binary);
    ok &= check(former && former->value("binaryFormat").toInt() == 7, "a former binary sidecar gives its version");

    ok &= check(!SidecarJson::fromBytes(QByteArray("not cbor at all"), &error) && !error.isEmpty(),
                "other bytes give an error");
    ok &= check(!SidecarJson::fromBytes(QCborValue(12).toCbor(), &error), "a document that is no map gives an error");

    std::cout << (ok ? "sidecar json: ok\n" : "sidecar json: FAILED\n");
    return ok ? 0 : 1;
}
