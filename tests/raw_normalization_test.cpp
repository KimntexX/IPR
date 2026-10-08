#include "RawNormalization.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto hash = [](const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    };
    RawNormalization::Context c;
    c.fileName = dir.filePath("test.CR3");
    c.profilePath = dir.filePath("neutral.dcp");
    c.cameraMake = "Canon";
    c.cameraModel = "EOS R6 Mark III";
    c.librawVersion = "0.22.2-Release";
    c.iso = 100;
    c.black.fill(512);
    c.cameraWhiteBalance = c.nativeBaselineUnknown = true;
    if (!write(c.fileName, "raw") || !write(c.profilePath, "dcp")) return 1;
    const QJsonObject entry{{"camera_make", c.cameraMake}, {"camera_model", c.cameraModel},
        {"iso", 100}, {"raw_sha256", hash("raw")}, {"profile_sha256", hash("dcp")},
        {"white_level", 13995}, {"baseline_exposure_ev", .26},
        {"black_rgbg", QJsonArray{512,512,512,512}},
        {"dng_sha256", QString(64, 'a')}, {"provenance", "audited DNG"}};
    const QString path = dir.filePath("normalization.json");
    auto save = [&](const QJsonArray &entries, QString version = "0.22.2-Release") {
        return write(path, QJsonDocument(QJsonObject{{"schema", 1},
            {"libraw_version", version}, {"entries", entries}}).toJson());
    };
    int assertions = 0;
    const auto check = [&](bool ok, const char *label) {
        ++assertions;
        if (!ok) std::cerr << "FAILED: " << label << '\n';
        return ok;
    };
    QString reason;
    if (!save({entry})) return 1;
    const QString cacheBefore = RawNormalization::cacheToken(path);
    auto result = RawNormalization::resolve(path, c, &reason);
    if (!check(result && result->whiteLevel == 13995 && result->baselineExposureEv == .26 && reason.isEmpty(), "valid calibration")) return 1;
    if (!check(!RawNormalization::resolve({}, c), "opt-in only")) return 1;
    auto changed = c;
    changed.black[0] = 513;
    if (!check(!RawNormalization::resolve(path, changed, &reason) && !reason.isEmpty(), "black mismatch")) return 1;
    changed = c; changed.cameraWhiteBalance = false;
    if (!check(!RawNormalization::resolve(path, changed), "no auto WB")) return 1;
    changed = c; changed.nativeBaselineUnknown = false;
    if (!check(!RawNormalization::resolve(path, changed), "no duplicate native exposure")) return 1;
    changed = c; changed.cameraModel = "Other";
    if (!check(!RawNormalization::resolve(path, changed), "camera mismatch")) return 1;
    changed = c; changed.iso = 200;
    if (!check(!RawNormalization::resolve(path, changed), "ISO mismatch")) return 1;
    if (!save({entry,entry})) return 1;
    if (!check(cacheBefore != RawNormalization::cacheToken(path) &&
               cacheBefore != RawNormalization::cacheToken({}), "cache key follows calibration contents")) return 1;
    if (!check(!RawNormalization::resolve(path, c, &reason) && reason.contains("Ambiguous"), "duplicate calibration")) return 1;
    if (!save({entry}, "other")) return 1;
    if (!check(!RawNormalization::resolve(path, c), "LibRaw mismatch")) return 1;
    for (const auto &key : {QString("white_level"), QString("baseline_exposure_ev"), QString("provenance"), QString("dng_sha256"), QString("raw_sha256")}) {
        auto invalid = entry; invalid.remove(key);
        if (!save({invalid})) return 1;
        if (!check(!RawNormalization::resolve(path, c), "missing required value")) return 1;
    }
    auto invalid = entry; invalid["white_level"] = 512;
    if (!save({invalid}) || !check(!RawNormalization::resolve(path, c), "white below black")) return 1;
    invalid = entry; invalid["white_level"] = 13995.5;
    if (!save({invalid}) || !check(!RawNormalization::resolve(path, c), "fractional white")) return 1;
    invalid = entry; invalid["baseline_exposure_ev"] = 3;
    if (!save({invalid}) || !check(!RawNormalization::resolve(path, c), "unbounded exposure")) return 1;
    if (!save({entry}) || !write(c.fileName, "changed")) return 1;
    if (!check(!RawNormalization::resolve(path, c), "RAW hash mismatch")) return 1;
    if (!write(c.fileName,"raw") || !write(c.profilePath,"changed")) return 1;
    if (!check(!RawNormalization::resolve(path, c), "profile hash mismatch")) return 1;
    if (!write(path,"{broken") || !check(!RawNormalization::resolve(path,c), "malformed JSON")) return 1;
    if (!write(path,QByteArray(1024*1024+1,' ')) || !check(!RawNormalization::resolve(path,c), "manifest size bound")) return 1;
    std::cout << assertions << " normalization assertions passed\n";
    return 0;
}
