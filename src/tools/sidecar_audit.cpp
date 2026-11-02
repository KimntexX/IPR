#include "RecipeCodec.h"
#include "Sidecar.h"

#include <QCborMap>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <map>

// Conversion of real .ipr files (B0.4): iprSidecarAudit DIR OUTPUT.json. Reads every
// sidecar under DIR, then converts a copy beside an empty stand-in for its photo,
// of the same size and date: nothing under DIR is ever written.
namespace {
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray digest(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

// The version a sidecar was written at: its binary header, or 0 for another format.
int binaryVersion(const QByteArray &bytes)
{
    QDataStream stream(bytes);
    quint32 magic = 0;
    quint16 version = 0;
    stream >> magic >> version;
    return magic == 0x49505231u ? int(version) : 0;
}

// What a conversion must keep, the mode aside, which older files do not record.
QCborMap content(const Sidecar::Document &document)
{
    QCborMap map;
    map.insert(QStringLiteral("settings"), RecipeCodec::encodeSettings(document.settings));
    map.insert(QStringLiteral("metadata"), RecipeCodec::encodeMetadata(document.metadata));
    map.insert(QStringLiteral("raw"), RecipeCodec::encodeRaw(document.raw));
    return map;
}

QJsonObject audit(const QString &sidecar, const QDir &work)
{
    const QFileInfo sidecarInfo(sidecar);
    const QString photo = sidecar.chopped(4);
    const QByteArray before = readBytes(sidecar);
    QJsonObject record{{"sidecar", sidecar}, {"sha256", QString::fromLatin1(digest(before))},
                       {"bytes", int(before.size())}, {"binary_version", binaryVersion(before)}};
    const QFileInfo photoInfo(photo);
    if (!photoInfo.exists()) {
        record["outcome"] = QStringLiteral("no_photo");
        return record;
    }
    const auto read = Sidecar::read(photo);
    if (!read) {
        // Its photo has changed since, or it is damaged: an ordinary open ignores it too.
        record["outcome"] = QStringLiteral("not_read");
        return record;
    }
    // A stand-in with the photo's identity, in the working folder.
    const QString standIn = work.filePath(photoInfo.fileName());
    QFile stub(standIn);
    if (!stub.open(QIODevice::WriteOnly) || !stub.resize(photoInfo.size())) {
        record["outcome"] = QStringLiteral("work_error");
        return record;
    }
    stub.close();
    {
        QFile dated(standIn);
        if (!dated.open(QIODevice::ReadWrite) ||
            !dated.setFileTime(photoInfo.lastModified(), QFileDevice::FileModificationTime)) {
            record["outcome"] = QStringLiteral("work_error");
            return record;
        }
    }
    QFile copy(Sidecar::path(standIn));
    if (!copy.open(QIODevice::WriteOnly) || copy.write(before) != before.size()) {
        record["outcome"] = QStringLiteral("work_error");
        return record;
    }
    copy.close();
    const auto first = Sidecar::read(standIn);
    const bool written = first && Sidecar::write(standIn, *first);
    const auto second = written ? Sidecar::read(standIn) : std::nullopt;
    const bool kept = first && second && content(*first) == content(*second) && content(*first) == content(*read);
    record["outcome"] = !first || !written || !second ? QStringLiteral("conversion_failed")
                        : second->format != Sidecar::Format::Current ? QStringLiteral("not_current")
                        : !kept ? QStringLiteral("content_changed") : QStringLiteral("converted");
    QFile::remove(Sidecar::path(standIn));
    QFile::remove(standIn);
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 3) {
        std::cerr << "Usage: iprSidecarAudit DIR OUTPUT.json\n";
        return 2;
    }
    if (QFileInfo::exists(arguments.at(2))) {
        std::cerr << "Refusing to overwrite evidence\n";
        return 2;
    }
    QTemporaryDir work;
    if (!work.isValid()) {
        std::cerr << "No working folder\n";
        return 1;
    }
    QJsonArray records;
    std::map<QString, int> outcomes;
    std::map<int, int> versions;
    bool untouched = true;
    QDirIterator files(arguments.at(1), {QStringLiteral("*.ipr")}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString sidecar = files.next();
        const QByteArray before = readBytes(sidecar);
        const QJsonObject record = audit(sidecar, QDir(work.path()));
        untouched = untouched && readBytes(sidecar) == before;
        ++outcomes[record.value("outcome").toString()];
        ++versions[record.value("binary_version").toInt()];
        records.append(record);
    }
    QJsonObject outcomeCounts, versionCounts;
    for (const auto &[outcome, count] : outcomes) outcomeCounts[outcome] = count;
    for (const auto &[version, count] : versions) versionCounts[QString::number(version)] = count;
    const bool passed = untouched && !outcomes.count(QStringLiteral("conversion_failed")) &&
                        !outcomes.count(QStringLiteral("content_changed")) &&
                        !outcomes.count(QStringLiteral("not_current")) && !outcomes.count(QStringLiteral("work_error"));
    const QJsonObject proof{
        {"schema_version", 1}, {"data_version", 1}, {"group", "b0-foundation"},
        {"campaign_id", QFileInfo(arguments.at(2)).completeBaseName()},
        {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"status", passed ? "passed" : "failed"}, {"format_version", Sidecar::FormatVersion},
        {"method", "Each sidecar is read beside its real photo, then a copy is converted beside a stand-in of the "
                   "same size and date, and read again; nothing under the audited folder is written"},
        {"limitations", QJsonArray{"Checks the recipe's fields, not the rendering of the converted recipe",
                                   "Only the versions present in the audited folder are exercised"}},
        {"sources_untouched", untouched}, {"outcomes", outcomeCounts}, {"binary_versions", versionCounts},
        {"records", records}};
    QFile output(arguments.at(2));
    if (!output.open(QIODevice::NewOnly) || output.write(QJsonDocument(proof).toJson()) < 0) {
        std::cerr << "Cannot write the evidence\n";
        return 1;
    }
    std::cout << QJsonDocument(outcomeCounts).toJson(QJsonDocument::Compact).toStdString() << ' '
              << QJsonDocument(versionCounts).toJson(QJsonDocument::Compact).toStdString()
              << " untouched=" << untouched << " status=" << (passed ? "passed" : "failed") << '\n';
    return passed ? 0 : 1;
}
