#include "CameraProfileEngine.h"
#include "WorkingColorSpace.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QCryptographicHash>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace CameraProfileEngine {
namespace {

QString normalized(QString value);

bool sameNormalized(const QString &first, const QString &second)
{
    return !first.isEmpty() && normalized(first) == normalized(second);
}

bool manifestFieldMatches(const QJsonObject &entry, const char *name,
                          const QString &actual)
{
    const QJsonValue expectedValue = entry.value(QLatin1String(name));
    if (expectedValue.isUndefined())
        return true;
    return expectedValue.isString() && sameNormalized(expectedValue.toString(), actual);
}

QString manifestProfilePath(const QString &root, const QJsonObject &entry,
                            const char *name)
{
    const QJsonValue value = entry.value(QLatin1String(name));
    if (!value.isString() || value.toString().isEmpty())
        return {};
    const QFileInfo rootInfo(root);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    const QFileInfo candidate(QDir(root).filePath(value.toString()));
    const QString canonicalCandidate = candidate.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalCandidate.isEmpty())
        return {};
    const QString rootPrefix = QDir::fromNativeSeparators(canonicalRoot) +
        QLatin1Char('/');
    const QString candidatePath = QDir::fromNativeSeparators(canonicalCandidate);
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    if (!candidatePath.startsWith(rootPrefix, caseSensitivity))
        return {};
    return canonicalCandidate;
}

Selection selectionFromManifest(const QString &root, const QString &cameraMake,
                                const QString &cameraModel,
                                const QString &lensMake,
                                const QString &lensModel)
{
    Selection selection;
    QFile manifest(QDir(root).filePath(QStringLiteral("index.json")));
    if (!manifest.open(QIODevice::ReadOnly))
        return selection;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(),
                                                             &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return selection;
    const QJsonObject rootObject = document.object();
    if (rootObject.value(QStringLiteral("version")).toInt() != 1)
        return selection;
    const QJsonArray entries = rootObject.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        const bool cameraMatches =
            manifestFieldMatches(entry, "cameraMake", cameraMake) &&
            manifestFieldMatches(entry, "cameraModel", cameraModel);
        const bool lensMatches =
            manifestFieldMatches(entry, "lensMake", lensMake) &&
            manifestFieldMatches(entry, "lensModel", lensModel);
        if (selection.dcpPath.isEmpty() && cameraMatches &&
            entry.value(QStringLiteral("cameraModel")).isString()) {
            selection.dcpPath = manifestProfilePath(root, entry, "dcp");
        }
        if (selection.lcpPath.isEmpty() && cameraMatches && lensMatches &&
            entry.value(QStringLiteral("lensModel")).isString()) {
            selection.lcpPath = manifestProfilePath(root, entry, "lcp");
        }
        if (!selection.dcpPath.isEmpty() && !selection.lcpPath.isEmpty())
            break;
    }
    return selection;
}

Selection selectionFromManifests(const QStringList &roots,
                                 const QString &cameraMake,
                                 const QString &cameraModel,
                                 const QString &lensMake,
                                 const QString &lensModel)
{
    Selection selection;
    for (const QString &root : roots) {
        const Selection manifestSelection = selectionFromManifest(
            root, cameraMake, cameraModel, lensMake, lensModel);
        if (selection.dcpPath.isEmpty())
            selection.dcpPath = manifestSelection.dcpPath;
        if (selection.lcpPath.isEmpty())
            selection.lcpPath = manifestSelection.lcpPath;
        if (!selection.dcpPath.isEmpty() && !selection.lcpPath.isEmpty())
            break;
    }
    return selection;
}

QString persistentSelectionCacheFile()
{
    QString cacheRoot = QStandardPaths::writableLocation(
        QStandardPaths::GenericCacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
    }
    if (cacheRoot.isEmpty())
        return {};
    QDir cacheDirectory(cacheRoot);
    if (!cacheDirectory.exists() && !QDir().mkpath(cacheRoot))
        return {};
    return cacheDirectory.filePath(QStringLiteral("IPR-profile-selection-v1.ini"));
}

QString persistentSelectionKey(const QString &cameraMake,
                               const QString &cameraModel,
                               const QString &lensMake,
                               const QString &lensModel,
                               const QStringList &roots)
{
    QStringList rootIdentities;
    rootIdentities.reserve(roots.size());
    for (const QString &root : roots) {
        const QFileInfo manifest(QDir(root).filePath(QStringLiteral("index.json")));
        rootIdentities.append(QDir::cleanPath(root) + QLatin1Char(':') +
                              QString::number(manifest.size()) + QLatin1Char(':') +
                              QString::number(manifest.lastModified().toMSecsSinceEpoch()));
    }
    const QString source = normalized(cameraMake) + QLatin1Char('|') +
        normalized(cameraModel) + QLatin1Char('|') + normalized(lensMake) +
        QLatin1Char('|') + normalized(lensModel) + QLatin1Char('|') +
        rootIdentities.join(QLatin1Char('|'));
    return QString::fromLatin1(QCryptographicHash::hash(
        source.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool restoreProfilePath(QSettings &settings, const QString &group,
                        const QString &name, QString *path)
{
    const QString prefix = group + QLatin1Char('/') + name + QLatin1Char('/');
    if (!settings.contains(prefix + QStringLiteral("path")))
        return false;
    const QString cachedPath = settings.value(prefix + QStringLiteral("path")).toString();
    if (cachedPath.isEmpty()) {
        *path = {};
        return true;
    }
    const QFileInfo fileInfo(cachedPath);
    bool sizeOk = false;
    bool modifiedOk = false;
    const qint64 size = settings.value(prefix + QStringLiteral("size")).toLongLong(&sizeOk);
    const qint64 modified = settings.value(prefix + QStringLiteral("modified")).toLongLong(&modifiedOk);
    if (!fileInfo.isFile() || !sizeOk || !modifiedOk || fileInfo.size() != size ||
        fileInfo.lastModified().toMSecsSinceEpoch() != modified)
        return false;
    *path = cachedPath;
    return true;
}

void storeProfilePath(QSettings &settings, const QString &group,
                      const QString &name, const QString &path)
{
    const QString prefix = group + QLatin1Char('/') + name + QLatin1Char('/');
    settings.setValue(prefix + QStringLiteral("path"), path);
    if (path.isEmpty()) {
        settings.remove(prefix + QStringLiteral("size"));
        settings.remove(prefix + QStringLiteral("modified"));
        return;
    }
    const QFileInfo fileInfo(path);
    settings.setValue(prefix + QStringLiteral("size"), fileInfo.size());
    settings.setValue(prefix + QStringLiteral("modified"),
                      fileInfo.lastModified().toMSecsSinceEpoch());
}

bool restorePersistentSelection(const QString &cacheKey, Selection *selection)
{
    const QString fileName = persistentSelectionCacheFile();
    if (fileName.isEmpty())
        return false;
    QSettings settings(fileName, QSettings::IniFormat);
    QString dcpPath;
    QString lcpPath;
    if (!restoreProfilePath(settings, cacheKey, QStringLiteral("dcp"), &dcpPath) ||
        !restoreProfilePath(settings, cacheKey, QStringLiteral("lcp"), &lcpPath) ||
        (dcpPath.isEmpty() && lcpPath.isEmpty()))
        return false;
    selection->dcpPath = dcpPath;
    selection->lcpPath = lcpPath;
    return true;
}

void storePersistentSelection(const QString &cacheKey, const Selection &selection)
{
    if (selection.dcpPath.isEmpty() && selection.lcpPath.isEmpty())
        return;
    const QString fileName = persistentSelectionCacheFile();
    if (fileName.isEmpty())
        return;
    QSettings settings(fileName, QSettings::IniFormat);
    storeProfilePath(settings, cacheKey, QStringLiteral("dcp"), selection.dcpPath);
    storeProfilePath(settings, cacheKey, QStringLiteral("lcp"), selection.lcpPath);
    settings.sync();
    if (qEnvironmentVariableIsSet("IPR_LOG_RAW_TIMING")) {
        qInfo().noquote()
            << QStringLiteral("IPR profile selection cache | file=%1 | status=%2")
                   .arg(fileName)
                   .arg(settings.status());
    }
}

constexpr qint64 MaxProfileSize = 64 * 1024 * 1024;

QString normalized(QString value)
{
    static const QRegularExpression nonAlphaNumeric(
        QStringLiteral("[^\\p{L}\\p{N}]"));
    value = value.toCaseFolded();
    value.remove(nonAlphaNumeric);
    return value;
}

bool readU16(const QByteArray &bytes, quint64 offset, bool littleEndian,
             quint16 *value)
{
    if (!value || offset > quint64(bytes.size()) ||
        quint64(bytes.size()) - offset < 2)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    *value = littleEndian ? quint16(p[0] | (quint16(p[1]) << 8))
                          : quint16((quint16(p[0]) << 8) | p[1]);
    return true;
}

bool readU32(const QByteArray &bytes, quint64 offset, bool littleEndian,
             quint32 *value)
{
    if (!value || offset > quint64(bytes.size()) ||
        quint64(bytes.size()) - offset < 4)
        return false;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    if (littleEndian) {
        *value = quint32(p[0]) | (quint32(p[1]) << 8) |
                 (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    } else {
        *value = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
                 (quint32(p[2]) << 8) | quint32(p[3]);
    }
    return true;
}

bool readU64(const QByteArray &bytes, quint64 offset, bool littleEndian,
             quint64 *value)
{
    quint32 low = 0;
    quint32 high = 0;
    if (!value || !readU32(bytes, offset + (littleEndian ? 0 : 4),
                           littleEndian, &low) ||
        !readU32(bytes, offset + (littleEndian ? 4 : 0),
                 littleEndian, &high))
        return false;
    *value = (quint64(high) << 32) | low;
    return true;
}

double readReal(const QByteArray &bytes, quint64 offset, quint16 type,
                bool littleEndian, bool *ok)
{
    quint32 numerator = 0;
    quint32 denominator = 0;
    quint64 bits64 = 0;
    quint32 bits32 = 0;
    *ok = false;
    switch (type) {
    case 3: {
        quint16 value = 0;
        if (!readU16(bytes, offset, littleEndian, &value))
            return 0.0;
        *ok = true;
        return value;
    }
    case 4:
        if (!readU32(bytes, offset, littleEndian, &numerator))
            return 0.0;
        *ok = true;
        return numerator;
    case 9: {
        if (!readU32(bytes, offset, littleEndian, &numerator))
            return 0.0;
        *ok = true;
        return qint32(numerator);
    }
    case 5:
        if (!readU32(bytes, offset, littleEndian, &numerator) ||
            !readU32(bytes, offset + 4, littleEndian, &denominator) ||
            denominator == 0)
            return 0.0;
        *ok = true;
        return double(numerator) / double(denominator);
    case 10: {
        quint32 denominatorSigned = 0;
        if (!readU32(bytes, offset, littleEndian, &numerator) ||
            !readU32(bytes, offset + 4, littleEndian, &denominatorSigned) ||
            qint32(denominatorSigned) == 0)
            return 0.0;
        *ok = true;
        return double(qint32(numerator)) / double(qint32(denominatorSigned));
    }
    case 11:
        if (!readU32(bytes, offset, littleEndian, &bits32))
            return 0.0;
        {
            float value = 0.0f;
            std::memcpy(&value, &bits32, sizeof(value));
            *ok = std::isfinite(value);
            return value;
        }
    case 12:
        if (!readU64(bytes, offset, littleEndian, &bits64))
            return 0.0;
        {
            double value = 0.0;
            std::memcpy(&value, &bits64, sizeof(value));
            *ok = std::isfinite(value);
            return value;
        }
    default:
        return 0.0;
    }
}

struct TiffEntry
{
    quint16 type = 0;
    quint32 count = 0;
    quint32 valueOffset = 0;
};

struct HueSatTable
{
    QVector<double> values;
    int hueDivisions = 0;
    int satDivisions = 0;
    int valDivisions = 0;

    bool isEmpty() const { return values.isEmpty(); }
};

class DcpProfile
{
public:
    bool load(const QByteArray &bytes, QString *error)
    {
        if (bytes.size() < 8) {
            if (error)
                *error = QStringLiteral("En-tête TIFF/DCP incomplet.");
            return false;
        }

        const QByteArray order = bytes.left(2);
        if (order != QByteArrayLiteral("II") && order != QByteArrayLiteral("MM")) {
            if (error)
                *error = QStringLiteral("Ordre des octets TIFF invalide.");
            return false;
        }
        littleEndian = order == QByteArrayLiteral("II");
        quint16 magic = 0;
        quint32 ifdOffset = 0;
        if (!readU16(bytes, 2, littleEndian, &magic) || magic != 0x4352 ||
            !readU32(bytes, 4, littleEndian, &ifdOffset)) {
            if (error)
                *error = QStringLiteral("Le fichier n'est pas un profil DCP TIFF.");
            return false;
        }

        quint16 entryCount = 0;
        if (!readU16(bytes, ifdOffset, littleEndian, &entryCount) ||
            entryCount > 4096) {
            if (error)
                *error = QStringLiteral("Répertoire TIFF DCP invalide.");
            return false;
        }

        for (quint16 i = 0; i < entryCount; ++i) {
            const quint64 entryOffset = quint64(ifdOffset) + 2 + quint64(i) * 12;
            quint16 tag = 0;
            TiffEntry entry;
            if (!readU16(bytes, entryOffset, littleEndian, &tag) ||
                !readU16(bytes, entryOffset + 2, littleEndian, &entry.type) ||
                !readU32(bytes, entryOffset + 4, littleEndian, &entry.count) ||
                !readU32(bytes, entryOffset + 8, littleEndian, &entry.valueOffset)) {
                if (error)
                    *error = QStringLiteral("Entrée TIFF DCP tronquée.");
                return false;
            }
            tags.insert(tag, entry);
            entryOffsets.insert(tag, entryOffset);
        }

        cameraRestriction = xmpCameraRestriction(bytes);
        colorMatrix1 = matrixFromTag(bytes, 50721);
        colorMatrix2 = matrixFromTag(bytes, 50722);
        forwardMatrix1 = matrixFromTag(bytes, 50964);
        forwardMatrix2 = matrixFromTag(bytes, 50965);
        illuminant1 = numericValues(bytes, 50778).value(0, 0.0);
        illuminant2 = numericValues(bytes, 50779).value(0, 0.0);
        QVector<double> mapDimensions = numericValues(bytes, 50937);
        if (mapDimensions.size() == 3 &&
            !loadHueSatMap(bytes, mapDimensions, 50938, &hueSatMap1, error))
            return false;
        if (mapDimensions.size() == 3 &&
            !loadHueSatMap(bytes, mapDimensions, 50939, &hueSatMap2, error))
            return false;
        mapDimensions = numericValues(bytes, 50981);
        if (mapDimensions.size() == 3 &&
            !loadHueSatMap(bytes, mapDimensions, 50982, &lookTable, error))
            return false;
        hueSatEncoding = int(numericValues(bytes, 51107).value(0, 0.0));
        lookTableEncoding = int(numericValues(bytes, 51108).value(0, 0.0));
        if ((hueSatEncoding != 0 && hueSatEncoding != 1) ||
            (lookTableEncoding != 0 && lookTableEncoding != 1)) {
            if (error)
                *error = QStringLiteral("Encodage de table DCP non pris en charge.");
            return false;
        }
        toneCurve = numericValues(bytes, 50940);
        if (!validateToneCurve(error))
            return false;

        const bool hasForward1 = forwardMatrix1.size() == 9;
        const bool hasForward2 = forwardMatrix2.size() == 9;
        const bool hasColor1 = colorMatrix1.size() == 9;
        const bool hasColor2 = colorMatrix2.size() == 9;
        if (!hasForward1 && !hasForward2 && !hasColor1 && !hasColor2) {
            if (error)
                *error = QStringLiteral(
                    "Le DCP ne contient aucune matrice 3×3 exploitable.");
            return false;
        }
        baselineExposure = numericValues(bytes, 51109).value(0, 0.0);
        if (!std::isfinite(baselineExposure) ||
            std::abs(baselineExposure) > 16.0) {
            if (error)
                *error = QStringLiteral("Compensation d'exposition DCP hors limites.");
            return false;
        }
        profileName = stringValue(bytes, 50936);
        return true;
    }

    quint64 accountedBytes() const
    {
        quint64 bytes = sizeof(DcpProfile) + 8192 +
            quint64(cameraRestriction.capacity() + profileName.capacity()) * sizeof(QChar);
        for (const auto *v : {&colorMatrix1, &colorMatrix2, &forwardMatrix1, &forwardMatrix2,
             &toneCurve, &hueSatMap1.values, &hueSatMap2.values, &lookTable.values})
            bytes += quint64(v->capacity()) * sizeof(double);
        return bytes + quint64(tags.size() + entryOffsets.size()) * 128;
    }

    bool matches(const QString &make, const QString &model,
                 const QString &fileName) const
    {
        const QString restriction = normalized(cameraRestriction);
        const QString camera = normalized(make + QLatin1Char(' ') + model);
        const QString modelKey = normalized(model);
        if (!restriction.isEmpty())
            return restriction == camera || restriction == modelKey ||
                   camera.contains(restriction);
        const QString fileKey =
            normalized(QFileInfo(fileName).completeBaseName());
        const QString makeModelKey = normalized(make + QLatin1Char(' ') + model);
        return !modelKey.isEmpty() &&
               ((!makeModelKey.isEmpty() && fileKey.startsWith(makeModelKey)) ||
                fileKey.startsWith(modelKey));
    }

    bool apply(cv::Mat &rgb, double whiteBalanceKelvin, QString *error,
               const DcpRenderOptions &options) const
    {
        if (!std::isfinite(options.cameraBaselineExposureEv) ||
            std::abs(options.cameraBaselineExposureEv) > 4.0 ||
            !std::isfinite(options.profileExposureCompensationEv) ||
            std::abs(options.profileExposureCompensationEv) > 2.0) {
            if (error)
                *error = QStringLiteral("Exposition de base caméra DCP invalide.");
            return false;
        }
        if (rgb.empty() || rgb.type() != CV_32FC3) {
            if (error)
                *error = QStringLiteral("L'image caméra DCP doit être RGB flottante.");
            return false;
        }

        // XYZ D50 -> linear Rec.2020 D65, adaptation exactly once.
        const cv::Matx33d &xyzToWorking = WorkingColor::fromXyzD50();
        // DCP HSV tables and profile tone curves operate in linear ProPhoto
        // (D50), NOT in our final Rec.2020 working space. These are color-space
        // definitions, not empirical camera calibration coefficients.
        const cv::Matx33d proPhotoToXyz(
            0.7976749, 0.1351917, 0.0313534,
            0.2880402, 0.7118741, 0.0000857,
            0.0,       0.0,       0.8252100);
        const cv::Matx33d xyzToProPhoto = proPhotoToXyz.inv();
        const cv::Matx33d proPhotoToWorking = xyzToWorking * proPhotoToXyz;
        const double matrixWeight = illuminantWeight(whiteBalanceKelvin);
        std::array<double, 9> cameraToXyz{};
        if (!cameraToXyzMatrix(matrixWeight, cameraToXyz)) {
            if (error)
                *error = QStringLiteral("Impossible de construire la matrice DCP.");
            return false;
        }
        const HueSatTable hueMap =
            interpolateTables(hueSatMap1, hueSatMap2, matrixWeight);
        const double exposureScale = std::exp2(
            baselineExposure + options.cameraBaselineExposureEv + options.profileExposureCompensationEv);
        // Extrema interpolation preserves HSV hue, not Lab hue or saturation.
        // Bounded curves may contain small descending segments (Camera Neutral
        // does). Guard inverted mapped extrema per pixel, not by rejecting the
        // entire curve. Unbounded/non-covering curves retain legacy behavior.
        bool hueStable = options.preserveToneCurveHue && toneCurve.size() >= 4 &&
                         toneCurve.first() == 0.0 &&
                         toneCurve[toneCurve.size() - 2] == 1.0;
        for (qsizetype i = 1; hueStable && i < toneCurve.size(); i += 2)
            hueStable = toneCurve[i] >= 0.0 && toneCurve[i] <= 1.0;
        cv::parallel_for_(cv::Range(0, rgb.rows), [&](const cv::Range &rows) {
            for (int y = rows.start; y < rows.end; ++y) {
                cv::Vec3f *row = rgb.ptr<cv::Vec3f>(y);
                for (int x = 0; x < rgb.cols; ++x) {
                    const cv::Vec3f in = row[x];
                    const double x2 =
                        cameraToXyz[0] * in[0] + cameraToXyz[1] * in[1] +
                        cameraToXyz[2] * in[2];
                    const double y2 =
                        cameraToXyz[3] * in[0] + cameraToXyz[4] * in[1] +
                        cameraToXyz[5] * in[2];
                    const double z2 =
                        cameraToXyz[6] * in[0] + cameraToXyz[7] * in[1] +
                        cameraToXyz[8] * in[2];
                    const cv::Vec3d xyz(x2, y2, z2);
                    // Preserve the matrix-only path, including extended range.
                    if (hueMap.isEmpty() && lookTable.isEmpty() && toneCurve.isEmpty()) {
                        row[x] = cv::Vec3f(xyzToWorking * (xyz * exposureScale));
                        continue;
                    }
                    cv::Vec3f out(xyzToProPhoto * (xyz * exposureScale));
                    if (!hueMap.isEmpty())
                        out = applyHueSatMap(out, hueMap, hueSatEncoding);
                    if (!lookTable.isEmpty())
                        out = applyHueSatMap(out, lookTable, lookTableEncoding);
                    if (toneCurve.size() >= 4 && toneCurve.size() % 2 == 0) {
                        if (hueStable) {
                            const cv::Vec3f bounded(std::clamp(out[0], 0.f, 1.f),
                                                    std::clamp(out[1], 0.f, 1.f),
                                                    std::clamp(out[2], 0.f, 1.f));
                            const float low = std::min({bounded[0], bounded[1], bounded[2]});
                            const float high = std::max({bounded[0], bounded[1], bounded[2]});
                            const float a = applyCurve(low), b = applyCurve(high);
                            for (int c = 0; c < 3; ++c)
                                out[c] = b < a ? applyCurve(bounded[c]) :
                                    (high > low ? a + (bounded[c] - low) /
                                                     (high - low) * (b - a) : a);
                        } else {
                            for (int c = 0; c < 3; ++c)
                                out[c] = applyCurve(out[c]);
                        }
                    }
                    // Do not clip to the smaller sRGB gamut here.
                    row[x] = cv::Vec3f(proPhotoToWorking * cv::Vec3d(out));
                }
            }
        });
        return true;
    }

    QString profileName;

    bool whiteBalanceGains(double sourceKelvin, const cv::Vec3d &neutral,
                            int temperature, int tint, cv::Vec3d &gains,
                            QString *error) const
    {
        const auto fail = [error] {
            if (error) *error = QStringLiteral("Balance DCP : matrices ou blanc capteur invalides.");
            return false;
        };
        if (!std::isfinite(sourceKelvin) || sourceKelvin < 2000 || sourceKelvin > 50000 ||
            temperature < 2000 || temperature > 50000 || tint < -150 || tint > 150)
            return fail();
        gains = cv::Vec3d(1, 1, 1);
        if (temperature == int(std::lround(sourceKelvin)) && tint == 0)
            return true;
        const QVector<double> &a = colorMatrix1.size() == 9 ? colorMatrix1 : colorMatrix2;
        const QVector<double> &b = colorMatrix2.size() == 9 ? colorMatrix2 : a;
        if (a.size() != 9 || !cv::checkRange(neutral) ||
            neutral[0] <= 0 || neutral[1] <= 0 || neutral[2] <= 0)
            return fail();
        const auto matrix = [&](double kelvin) {
            cv::Matx33d result;
            const double w = illuminantWeight(kelvin);
            for (int i = 0; i < 9; ++i) result.val[i] = a[i] * (1-w) + b[i] * w;
            return result;
        };
        const auto planckUv = [](double kelvin) {
            // Krystek rational approximation in CIE 1960 UCS (not CIE 1976).
            const double t2 = kelvin * kelvin;
            return cv::Vec2d(
                (.860117757 + 1.54118254e-4*kelvin + 1.28641212e-7*t2) /
                    (1 + 8.42420235e-4*kelvin + 7.08145163e-7*t2),
                (.317398726 + 4.22806245e-5*kelvin + 4.20481691e-8*t2) /
                    (1 - 2.89741816e-5*kelvin + 1.61456053e-7*t2));
        };
        const cv::Matx33d sourceMatrix = matrix(sourceKelvin);
        if (std::abs(cv::determinant(sourceMatrix)) < 1e-10) return fail();
        const cv::Vec3d white = sourceMatrix.inv() * neutral;
        const double d = white[0] + 15*white[1] + 3*white[2];
        if (!std::isfinite(d) || d <= 0 || white[1] <= 0) return fail();
        // Carry the measured off-locus As Shot white, not a universal +5 tint.
        const cv::Vec2d offset = cv::Vec2d(4*white[0]/d, 6*white[1]/d) - planckUv(sourceKelvin);
        cv::Vec2d target = planckUv(temperature) + offset;
        const cv::Vec2d tangent = planckUv(temperature + 1.0) - planckUv(temperature - 1.0);
        const double length = cv::norm(tangent);
        if (length <= 0) return fail();
        // Normal to the locus, Adobe DNG's tint distance convention (-3000).
        // This is a continuous-locus approximation, not Lightroom's private model.
        target += cv::Vec2d(-tangent[1], tangent[0]) * (-double(tint)/3000.0/length);
        if (target[1] <= 0) return fail();
        const cv::Vec3d xyz(1.5*target[0]/target[1], 1,
                            (4-target[0]-10*target[1])/(2*target[1]));
        const cv::Vec3d targetNeutral = matrix(temperature) * xyz;
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(targetNeutral[c]) || targetNeutral[c] <= 1e-10) return fail();
            gains[c] = neutral[c] / targetNeutral[c];
        }
        gains /= gains[1]; // Green anchor: no arbitrary exposure/profile-name weight.
        return cv::checkRange(gains);
    }

private:
    static HueSatTable interpolateTables(const HueSatTable &first,
                                         const HueSatTable &second,
                                         double weight)
    {
        if (first.isEmpty())
            return second;
        if (second.isEmpty() || weight <= 0.0)
            return first;
        if (weight >= 1.0)
            return second;
        if (first.hueDivisions != second.hueDivisions ||
            first.satDivisions != second.satDivisions ||
            first.valDivisions != second.valDivisions)
            return weight < 0.5 ? first : second;
        HueSatTable result = first;
        for (qsizetype i = 0; i < result.values.size(); ++i) {
            const double firstValue = first.values[i];
            double difference = second.values[i] - firstValue;
            if (i % 3 == 0) {
                difference = std::fmod(difference + 540.0, 360.0) - 180.0;
            }
            result.values[i] = firstValue + difference * weight;
        }
        return result;
    }

    QVector<double> matrixFromTag(const QByteArray &bytes, quint16 tag) const
    {
        return numericValues(bytes, tag);
    }

    bool loadHueSatMap(const QByteArray &bytes, const QVector<double> &dimensions,
                       quint16 dataTag, HueSatTable *table, QString *error)
    {
        if (dimensions[0] < 1 || dimensions[1] < 2 || dimensions[2] < 1 ||
            dimensions[0] > 360 || dimensions[1] > 64 || dimensions[2] > 64) {
            if (error)
                *error = QStringLiteral("Dimensions de table DCP invalides.");
            return false;
        }
        QVector<double> values = numericValues(bytes, dataTag);
        const qsizetype expected = qsizetype(dimensions[0]) *
                                   qsizetype(dimensions[1]) *
                                   qsizetype(dimensions[2]) * 3;
        if (values.size() != expected) {
            if (values.isEmpty())
                return true;
            if (error)
                *error = QStringLiteral("Données de table DCP tronquées.");
            return false;
        }
        for (double value : values) {
            if (!std::isfinite(value) || std::abs(value) > 3600.0) {
                if (error)
                    *error = QStringLiteral("Valeur de table DCP invalide.");
                return false;
            }
        }
        table->hueDivisions = int(dimensions[0]);
        table->satDivisions = int(dimensions[1]);
        table->valDivisions = int(dimensions[2]);
        table->values = std::move(values);
        return true;
    }

    bool validateToneCurve(QString *error) const
    {
        if (toneCurve.isEmpty())
            return true;
        if (toneCurve.size() % 2 != 0) {
            if (error)
                *error = QStringLiteral("Courbe tonale DCP invalide.");
            return false;
        }
        for (qsizetype i = 0; i < toneCurve.size(); ++i) {
            if (!std::isfinite(toneCurve[i]) || toneCurve[i] < -0.1 ||
                toneCurve[i] > 4.0 ||
                (i >= 2 && (i % 2 == 0) && toneCurve[i] <= toneCurve[i - 2])) {
                if (error)
                    *error = QStringLiteral("Courbe tonale DCP invalide.");
                return false;
            }
        }
        return true;
    }

    static double illuminantKelvin(double illuminant)
    {
        switch (int(illuminant)) {
        case 17: return 2856.0;
        case 18: return 4874.0;
        case 19: return 6774.0;
        case 20: return 5500.0;
        case 21: return 6500.0;
        case 22: return 7500.0;
        case 23: return 5000.0;
        case 24: return 3200.0;
        case 25: return 5000.0;
        case 26: return 6500.0;
        case 1: return 5500.0;
        default: return 0.0;
        }
    }

    double illuminantWeight(double kelvin) const
    {
        if (forwardMatrix2.size() != 9 && colorMatrix2.size() != 9)
            return 0.0;
        if (forwardMatrix1.size() != 9 && colorMatrix1.size() != 9)
            return 1.0;
        const double first = illuminantKelvin(illuminant1);
        const double second = illuminantKelvin(illuminant2);
        if (first <= 0.0 || second <= 0.0 || std::abs(first - second) < 1.0)
            return kelvin <= first ? 0.0 : 1.0;
        const double reciprocal = 1.0 / qBound(1500.0, kelvin, 50000.0);
        const double w = (reciprocal - 1.0 / first) /
                         (1.0 / second - 1.0 / first);
        return qBound(0.0, w, 1.0);
    }

    bool cameraToXyzMatrix(double weight, std::array<double, 9> &result) const
    {
        const QVector<double> &forwardA =
            forwardMatrix1.size() == 9 ? forwardMatrix1 : forwardMatrix2;
        const QVector<double> &forwardB =
            forwardMatrix2.size() == 9 ? forwardMatrix2 : forwardA;
        if (forwardA.size() == 9) {
            for (int i = 0; i < 9; ++i)
                result[i] = forwardA[i] * (1.0 - weight) + forwardB[i] * weight;
            return true;
        }

        const QVector<double> &colorA =
            colorMatrix1.size() == 9 ? colorMatrix1 : colorMatrix2;
        const QVector<double> &colorB =
            colorMatrix2.size() == 9 ? colorMatrix2 : colorA;
        if (colorA.size() != 9)
            return false;
        double matrix[9];
        for (int i = 0; i < 9; ++i)
            matrix[i] = colorA[i] * (1.0 - weight) + colorB[i] * weight;
        const double det =
            matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
            matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
            matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
        if (!std::isfinite(det) || std::abs(det) < 1e-10)
            return false;
        const double inv[9] = {
            (matrix[4] * matrix[8] - matrix[5] * matrix[7]) / det,
            (matrix[2] * matrix[7] - matrix[1] * matrix[8]) / det,
            (matrix[1] * matrix[5] - matrix[2] * matrix[4]) / det,
            (matrix[5] * matrix[6] - matrix[3] * matrix[8]) / det,
            (matrix[0] * matrix[8] - matrix[2] * matrix[6]) / det,
            (matrix[2] * matrix[3] - matrix[0] * matrix[5]) / det,
            (matrix[3] * matrix[7] - matrix[4] * matrix[6]) / det,
            (matrix[1] * matrix[6] - matrix[0] * matrix[7]) / det,
            (matrix[0] * matrix[4] - matrix[1] * matrix[3]) / det
        };
        const double sourceWhite[3] = {
            inv[0] + inv[1] + inv[2],
            inv[3] + inv[4] + inv[5],
            inv[6] + inv[7] + inv[8]
        };
        const double bradford[9] = {
            0.8951, 0.2664, -0.1614,
            -0.7502, 1.7135, 0.0367,
            0.0389, -0.0685, 1.0296
        };
        const double inverseBradford[9] = {
            0.9869929, -0.1470543, 0.1599627,
            0.4323053, 0.5183603, 0.0492912,
            -0.0085287, 0.0400428, 0.9684867
        };
        const double targetWhite[3] = {0.96422, 1.0, 0.82521};
        double sourceCone[3] = {};
        double targetCone[3] = {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                sourceCone[r] += bradford[r * 3 + c] * sourceWhite[c];
                targetCone[r] += bradford[r * 3 + c] * targetWhite[c];
            }
            if (std::abs(sourceCone[r]) < 1e-10)
                return false;
        }
        double adaptation[9] = {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                for (int k = 0; k < 3; ++k) {
                    adaptation[r * 3 + c] +=
                        inverseBradford[r * 3 + k] *
                        (targetCone[k] / sourceCone[k]) *
                        bradford[k * 3 + c];
                }
            }
        }
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                for (int k = 0; k < 3; ++k)
                    result[r * 3 + c] += adaptation[r * 3 + k] * inv[k * 3 + c];
            }
        }
        return true;
    }

    QString stringValue(const QByteArray &bytes, quint16 tag) const
    {
        const auto it = tags.constFind(tag);
        if (it == tags.cend() || it->type != 2 || it->count == 0 ||
            it->count > 4096)
            return {};
        const quint64 offset = it->count <= 4
                                   ? quint64(it->valueOffset) >> 16
                                   : it->valueOffset;
        // For inline ASCII the entry's value bytes are stored in file order.
        quint64 actualOffset = offset;
        if (it->count <= 4) {
            const quint64 entryOffset =
                findEntryOffset(tag);
            actualOffset = entryOffset + 8;
        }
        if (actualOffset > quint64(bytes.size()) ||
            quint64(bytes.size()) - actualOffset < it->count)
            return {};
        return QString::fromLatin1(bytes.constData() + actualOffset,
                                   int(it->count)).trimmed();
    }

    QString xmpCameraRestriction(const QByteArray &bytes) const
    {
        const auto it = tags.constFind(700);
        if (it == tags.cend() || it->count == 0 ||
            it->count > quint32(MaxProfileSize))
            return {};
        const quint64 offset = it->count <= 4
                                   ? findEntryOffset(700) + 8
                                   : it->valueOffset;
        if (offset > quint64(bytes.size()) ||
            quint64(bytes.size()) - offset < it->count)
            return {};
        const QByteArray xmp(bytes.constData() + offset, int(it->count));
        QXmlStreamReader xml(xmp);
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            for (const QXmlStreamAttribute &attribute : xml.attributes()) {
                if (attribute.name().compare(
                        QStringLiteral("CameraModelRestriction"),
                        Qt::CaseInsensitive) == 0)
                    return attribute.value().toString().trimmed();
            }
        }
        return {};
    }

    quint64 findEntryOffset(quint16 tag) const
    {
        return entryOffsets.value(tag, std::numeric_limits<quint64>::max());
    }

    QVector<double> numericValues(const QByteArray &bytes, quint16 tag) const
    {
        const auto it = tags.constFind(tag);
        if (it == tags.cend() || it->count == 0 ||
            it->count > quint32(MaxProfileSize / sizeof(double)))
            return {};
        quint32 unitSize = 0;
        switch (it->type) {
        case 3: unitSize = 2; break;
        case 4:
        case 9: unitSize = 4; break;
        case 5:
        case 10: unitSize = 8; break;
        case 11: unitSize = 4; break;
        case 12: unitSize = 8; break;
        default: return {};
        }
        const quint64 bytesNeeded = quint64(unitSize) * it->count;
        const quint64 offset = bytesNeeded <= 4
                                   ? findEntryOffset(tag) + 8
                                   : it->valueOffset;
        if (offset > quint64(bytes.size()) ||
            quint64(bytes.size()) - offset < bytesNeeded)
            return {};
        QVector<double> result;
        result.reserve(int(it->count));
        for (quint32 i = 0; i < it->count; ++i) {
            bool ok = false;
            const double value = readReal(bytes, offset + quint64(i) * unitSize,
                                          it->type, littleEndian, &ok);
            if (!ok)
                return {};
            result.append(value);
        }
        return result;
    }

    float applyCurve(float value) const
    {
        const double x = qBound(0.0, double(value), 1.0);
        for (qsizetype i = 0; i + 3 < toneCurve.size(); i += 2) {
            const double x0 = toneCurve[i];
            const double y0 = toneCurve[i + 1];
            const double x1 = toneCurve[i + 2];
            const double y1 = toneCurve[i + 3];
            if (x <= x1 && x1 > x0) {
                const double t = (x - x0) / (x1 - x0);
                return float(y0 + (y1 - y0) * t);
            }
        }
        return float(toneCurve.last());
    }

    cv::Vec3f applyHueSatMap(const cv::Vec3f &input,
                             const HueSatTable &table, int encoding) const
    {
        if (table.isEmpty())
            return input;
        cv::Vec3f tableInput;
        for (int c = 0; c < 3; ++c)
            tableInput[c] = qBound(0.0f, input[c], 1.0f);
        const float r = tableInput[0];
        const float g = tableInput[1];
        const float b = tableInput[2];
        const float maximum = std::max({r, g, b});
        const float minimum = std::min({r, g, b});
        const float delta = maximum - minimum;
        float hue = 0.0f;
        if (delta > 0.0f) {
            if (maximum == r)
                hue = std::fmod((g - b) / delta, 6.0f);
            else if (maximum == g)
                hue = (b - r) / delta + 2.0f;
            else
                hue = (r - g) / delta + 4.0f;
            hue = std::fmod(hue / 6.0f + 1.0f, 1.0f);
        }
        const float saturation = maximum <= 0.0f ? 0.0f : delta / maximum;
        // Encoding affects only the HSV Value axis, not RGB or H/S.
        // The tag is inapplicable to a 2.5D table (one Value division).
        const bool encodeValue = encoding == 1 && table.valDivisions > 1;
        const float value = encodeValue
            ? (maximum <= 0.0031308f ? maximum * 12.92f
               : 1.055f * std::pow(maximum, 1.0f / 2.4f) - 0.055f)
            : maximum;

        const float hCoord = hue * table.hueDivisions;
        const float sCoord = saturation * (table.satDivisions - 1);
        const float vCoord = value * (table.valDivisions - 1);
        const int h0 = int(std::floor(hCoord)) % table.hueDivisions;
        const int h1 = (h0 + 1) % table.hueDivisions;
        const int s0 = qBound(0, int(std::floor(sCoord)), table.satDivisions - 1);
        const int s1 = qMin(s0 + 1, table.satDivisions - 1);
        const int v0 = qBound(0, int(std::floor(vCoord)), table.valDivisions - 1);
        const int v1 = qMin(v0 + 1, table.valDivisions - 1);
        const float hf = hCoord - std::floor(hCoord);
        const float sf = sCoord - s0;
        const float vf = vCoord - v0;

        cv::Vec3f modify(0.0f, 1.0f, 1.0f);
        for (int dv = 0; dv < 2; ++dv) {
            const int vi = dv ? v1 : v0;
            const float vw = dv ? vf : 1.0f - vf;
            for (int dh = 0; dh < 2; ++dh) {
                const int hi = dh ? h1 : h0;
                const float hw = dh ? hf : 1.0f - hf;
                for (int ds = 0; ds < 2; ++ds) {
                    const int si = ds ? s1 : s0;
                    const float sw = ds ? sf : 1.0f - sf;
                    const qsizetype index =
                        ((qsizetype(vi) * table.hueDivisions + hi) * table.satDivisions +
                         si) * 3;
                    const float weight = vw * hw * sw;
                    for (int c = 0; c < 3; ++c)
                        modify[c] += float(table.values[index + c]) * weight -
                                     (c == 0 ? 0.0f : weight);
                }
            }
        }

        float outHue = std::fmod(hue + modify[0] / 360.0f, 1.0f);
        if (outHue < 0.0f)
            outHue += 1.0f;
        const float outSaturation = qBound(0.0f, saturation * modify[1], 1.0f);
        float outValue = qBound(0.0f, value * modify[2], 1.0f);
        if (encodeValue)
            outValue = outValue <= 0.04045f ? outValue / 12.92f
                : std::pow((outValue + 0.055f) / 1.055f, 2.4f);
        const float sector = outHue * 6.0f;
        const int i = int(std::floor(sector)) % 6;
        const float f = sector - std::floor(sector);
        const float p = outValue * (1.0f - outSaturation);
        const float q = outValue * (1.0f - f * outSaturation);
        const float t = outValue * (1.0f - (1.0f - f) * outSaturation);
        cv::Vec3f result;
        switch (i) {
        case 0: result = {outValue, t, p}; break;
        case 1: result = {q, outValue, p}; break;
        case 2: result = {p, outValue, t}; break;
        case 3: result = {p, q, outValue}; break;
        case 4: result = {t, p, outValue}; break;
        default: result = {outValue, p, q}; break;
        }
        return result;
    }

    bool littleEndian = true;
    QString cameraRestriction;
    QVector<double> colorMatrix1;
    QVector<double> colorMatrix2;
    QVector<double> forwardMatrix1;
    QVector<double> forwardMatrix2;
    double illuminant1 = 0.0;
    double illuminant2 = 0.0;
    QVector<double> toneCurve;
    HueSatTable hueSatMap1;
    HueSatTable hueSatMap2;
    HueSatTable lookTable;
    int hueSatEncoding = 0;
    int lookTableEncoding = 0;
    double baselineExposure = 0.0;
    QHash<quint16, TiffEntry> tags;
    QHash<quint16, quint64> entryOffsets;
};

struct LcpData
{
    QString camera;
    QString lens;
    QString lensPrettyName;
    struct Model
    {
        double focalLength = 0.0;
        double focusDistance = 0.0;
        double aperture = 0.0;
        std::array<double, 5> geometry{};
        std::array<double, 3> vignetting{};
        std::array<double, 5> redGreen{};
        std::array<double, 5> blueGreen{};
        double geometryScale = 1.0;
        double geometryCenterX = 0.5;
        double geometryCenterY = 0.5;
        double vignetteCenterX = 0.5;
        double vignetteCenterY = 0.5;
        double redCenterX = 0.5;
        double redCenterY = 0.5;
        double blueCenterX = 0.5;
        double blueCenterY = 0.5;
        bool hasGeometry = false;
        bool hasVignetting = false;
        bool hasRedGreen = false;
        bool hasBlueGreen = false;
    };
    QVector<Model> models;
};

bool parseLcp(const QByteArray &bytes, LcpData *profile, QString *error)
{
    if (!profile || bytes.size() > MaxProfileSize) {
        if (error)
            *error = QStringLiteral("Profil LCP illisible ou trop volumineux.");
        return false;
    }
    QXmlStreamReader xml(bytes);
    LcpData parsed;
    enum class ModelSection { None, Geometry, Vignette, RedGreen, BlueGreen };
    ModelSection section = ModelSection::None;
    bool insideModel = false;
    LcpData::Model current;
    auto assignParameters = [](LcpData::Model &model, ModelSection section,
                               const QXmlStreamAttributes &attributes) {
        bool hasRadial1 = false;
        for (const QXmlStreamAttribute &attribute : attributes) {
            const QString key = attribute.name().toString().toCaseFolded();
            bool ok = false;
            const double value = attribute.value().toDouble(&ok);
            if (!ok)
                continue;
            if (key == QStringLiteral("focallength"))
                model.focalLength = value;
            else if (key == QStringLiteral("focusdistance"))
                model.focusDistance = value;
            else if (key == QStringLiteral("aperturevalue"))
                model.aperture = value;
            else if (key == QStringLiteral("imagexcenter")) {
                if (section == ModelSection::Vignette)
                    model.vignetteCenterX = value;
                else if (section == ModelSection::RedGreen)
                    model.redCenterX = value;
                else if (section == ModelSection::BlueGreen)
                    model.blueCenterX = value;
                else
                    model.geometryCenterX = value;
            } else if (key == QStringLiteral("imageycenter")) {
                if (section == ModelSection::Vignette)
                    model.vignetteCenterY = value;
                else if (section == ModelSection::RedGreen)
                    model.redCenterY = value;
                else if (section == ModelSection::BlueGreen)
                    model.blueCenterY = value;
                else
                    model.geometryCenterY = value;
            } else if (key == QStringLiteral("scalefactor") &&
                       section == ModelSection::Geometry)
                model.geometryScale = value;
            else if (section == ModelSection::Geometry) {
                if (key == QStringLiteral("distortiona") ||
                    key == QStringLiteral("radialdistortparam1")) {
                    model.geometry[0] = value;
                    model.hasGeometry = true;
                    hasRadial1 = true;
                } else if (key == QStringLiteral("distortionb") ||
                           key == QStringLiteral("radialdistortparam2"))
                    model.geometry[1] = value;
                else if (key == QStringLiteral("distortionc") ||
                         key == QStringLiteral("radialdistortparam3"))
                    model.geometry[2] = value;
                else if (key == QStringLiteral("tangentialdistortparam1"))
                    model.geometry[3] = value;
                else if (key == QStringLiteral("tangentialdistortparam2"))
                    model.geometry[4] = value;
            } else if (section == ModelSection::Vignette) {
                if (key == QStringLiteral("vignettemodelparam1"))
                    model.vignetting[0] = value;
                else if (key == QStringLiteral("vignettemodelparam2"))
                    model.vignetting[1] = value;
                else if (key == QStringLiteral("vignettemodelparam3"))
                    model.vignetting[2] = value;
            } else if (section == ModelSection::RedGreen) {
                if (key == QStringLiteral("radialdistortparam1"))
                    model.redGreen[0] = value, model.hasRedGreen = true;
                else if (key == QStringLiteral("radialdistortparam2"))
                    model.redGreen[1] = value;
                else if (key == QStringLiteral("radialdistortparam3"))
                    model.redGreen[2] = value;
                else if (key == QStringLiteral("tangentialdistortparam1"))
                    model.redGreen[3] = value;
                else if (key == QStringLiteral("tangentialdistortparam2"))
                    model.redGreen[4] = value;
            } else if (section == ModelSection::BlueGreen) {
                if (key == QStringLiteral("radialdistortparam1"))
                    model.blueGreen[0] = value, model.hasBlueGreen = true;
                else if (key == QStringLiteral("radialdistortparam2"))
                    model.blueGreen[1] = value;
                else if (key == QStringLiteral("radialdistortparam3"))
                    model.blueGreen[2] = value;
                else if (key == QStringLiteral("tangentialdistortparam1"))
                    model.blueGreen[3] = value;
                else if (key == QStringLiteral("tangentialdistortparam2"))
                    model.blueGreen[4] = value;
            }
        }
        if (section == ModelSection::Geometry && hasRadial1)
            model.hasGeometry = true;
        if (section == ModelSection::Vignette)
            model.hasVignetting = true;
        if (section == ModelSection::RedGreen)
            model.hasRedGreen = true;
        if (section == ModelSection::BlueGreen)
            model.hasBlueGreen = true;
    };
    QString textParameter;
    ModelSection textSection = ModelSection::None;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString element = xml.name().toString().toCaseFolded();
            const auto attributes = xml.attributes();
            if (element == QStringLiteral("perspectivemodel") ||
                element == QStringLiteral("fisheyemodel")) {
                if (insideModel && (current.hasGeometry ||
                                    current.hasVignetting ||
                                    current.hasRedGreen ||
                                    current.hasBlueGreen))
                    parsed.models.append(current);
                current = {};
                insideModel = true;
                section = ModelSection::Geometry;
                assignParameters(current, section, attributes);
            } else if (insideModel &&
                       element == QStringLiteral("geometrymodel")) {
                section = ModelSection::Geometry;
                assignParameters(current, section, attributes);
            } else if (insideModel &&
                       element == QStringLiteral("vignettemodel")) {
                section = ModelSection::Vignette;
                assignParameters(current, section, attributes);
            } else if (insideModel &&
                       element == QStringLiteral("chromaticredgreenmodel")) {
                section = ModelSection::RedGreen;
                assignParameters(current, section, attributes);
            } else if (insideModel &&
                       element == QStringLiteral("chromaticbluegreenmodel")) {
                section = ModelSection::BlueGreen;
                assignParameters(current, section, attributes);
            } else if (insideModel &&
                       element == QStringLiteral("chromaticgreenmodel")) {
                section = ModelSection::None;
            } else if (insideModel &&
                       element == QStringLiteral("description")) {
                assignParameters(current, section, attributes);
            } else if (insideModel) {
                textParameter = element;
                textSection = section;
            } else if (!insideModel) {
                for (const QXmlStreamAttribute &attribute : attributes) {
                    const QString key =
                        attribute.name().toString().toCaseFolded();
                    if (key == QStringLiteral("cameramodelrestriction") ||
                        key == QStringLiteral("cameramodel")) {
                        parsed.camera = attribute.value().toString();
                    } else if (key == QStringLiteral("lens") ||
                               key == QStringLiteral("lensmodelrestriction")) {
                        parsed.lens = attribute.value().toString();
                    } else if (key == QStringLiteral("lensprettyname")) {
                        parsed.lensPrettyName = attribute.value().toString();
                    }
                }
            }
        } else if (xml.isCharacters() && !xml.isWhitespace() &&
                   insideModel && !textParameter.isEmpty()) {
            bool ok = false;
            const double value = xml.text().toDouble(&ok);
            const QString key = textParameter;
            if (ok) {
                if (key == QStringLiteral("focallength"))
                    current.focalLength = value;
                else if (key == QStringLiteral("focusdistance"))
                    current.focusDistance = value;
                else if (key == QStringLiteral("aperturevalue"))
                    current.aperture = value;
                else if (key == QStringLiteral("imagexcenter")) {
                    if (textSection == ModelSection::Vignette)
                        current.vignetteCenterX = value;
                    else if (textSection == ModelSection::RedGreen)
                        current.redCenterX = value;
                    else if (textSection == ModelSection::BlueGreen)
                        current.blueCenterX = value;
                    else
                        current.geometryCenterX = value;
                } else if (key == QStringLiteral("imageycenter")) {
                    if (textSection == ModelSection::Vignette)
                        current.vignetteCenterY = value;
                    else if (textSection == ModelSection::RedGreen)
                        current.redCenterY = value;
                    else if (textSection == ModelSection::BlueGreen)
                        current.blueCenterY = value;
                    else
                        current.geometryCenterY = value;
                } else if (key == QStringLiteral("scalefactor"))
                    current.geometryScale = value;
                else if (textSection == ModelSection::Geometry) {
                    if (key == QStringLiteral("distortiona") ||
                        key == QStringLiteral("radialdistortparam1")) {
                        current.geometry[0] = value;
                        current.hasGeometry = true;
                    } else if (key == QStringLiteral("distortionb") ||
                               key == QStringLiteral("radialdistortparam2"))
                        current.geometry[1] = value;
                    else if (key == QStringLiteral("distortionc") ||
                             key == QStringLiteral("radialdistortparam3"))
                        current.geometry[2] = value;
                    else if (key == QStringLiteral("tangentialdistortparam1"))
                        current.geometry[3] = value;
                    else if (key == QStringLiteral("tangentialdistortparam2"))
                        current.geometry[4] = value;
                } else if (textSection == ModelSection::Vignette) {
                    if (key == QStringLiteral("vignettemodelparam1"))
                        current.vignetting[0] = value;
                    else if (key == QStringLiteral("vignettemodelparam2"))
                        current.vignetting[1] = value;
                    else if (key == QStringLiteral("vignettemodelparam3"))
                        current.vignetting[2] = value;
                    current.hasVignetting = true;
                } else if (textSection == ModelSection::RedGreen) {
                    if (key == QStringLiteral("radialdistortparam1")) {
                        current.redGreen[0] = value;
                        current.hasRedGreen = true;
                    } else if (key == QStringLiteral("radialdistortparam2"))
                        current.redGreen[1] = value;
                    else if (key == QStringLiteral("radialdistortparam3"))
                        current.redGreen[2] = value;
                    else if (key == QStringLiteral("tangentialdistortparam1"))
                        current.redGreen[3] = value;
                    else if (key == QStringLiteral("tangentialdistortparam2"))
                        current.redGreen[4] = value;
                } else if (textSection == ModelSection::BlueGreen) {
                    if (key == QStringLiteral("radialdistortparam1")) {
                        current.blueGreen[0] = value;
                        current.hasBlueGreen = true;
                    } else if (key == QStringLiteral("radialdistortparam2"))
                        current.blueGreen[1] = value;
                    else if (key == QStringLiteral("radialdistortparam3"))
                        current.blueGreen[2] = value;
                    else if (key == QStringLiteral("tangentialdistortparam1"))
                        current.blueGreen[3] = value;
                    else if (key == QStringLiteral("tangentialdistortparam2"))
                        current.blueGreen[4] = value;
                }
            }
        } else if (xml.isEndElement()) {
            const QString element = xml.name().toString().toCaseFolded();
            if (element == textParameter) {
                textParameter.clear();
                textSection = ModelSection::None;
            }
            if (element == QStringLiteral("geometrymodel") ||
                element == QStringLiteral("vignettemodel") ||
                element == QStringLiteral("chromaticredgreenmodel") ||
                element == QStringLiteral("chromaticbluegreenmodel") ||
                element == QStringLiteral("chromaticgreenmodel")) {
                section = ModelSection::Geometry;
            } else if (element == QStringLiteral("perspectivemodel") ||
                       element == QStringLiteral("fisheyemodel")) {
                if (current.hasGeometry || current.hasVignetting ||
                    current.hasRedGreen || current.hasBlueGreen)
                    parsed.models.append(current);
                current = {};
                section = ModelSection::None;
                insideModel = false;
            }
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("XML LCP invalide : %1").arg(xml.errorString());
        return false;
    }
    if (parsed.models.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                "Le profil LCP ne contient pas de modèles optiques pris en charge.");
        return false;
    }
    for (const LcpData::Model &model : parsed.models) {
        const auto validParameters = [](const auto &parameters, double limit) {
            for (double value : parameters) {
                if (!std::isfinite(value) || std::abs(value) > limit)
                    return false;
            }
            return true;
        };
        if (!validParameters(model.geometry, 10000.0) ||
            !validParameters(model.vignetting, 1000000.0) ||
            !validParameters(model.redGreen, 100.0) ||
            !validParameters(model.blueGreen, 100.0) ||
            !std::isfinite(model.geometryScale) ||
            std::abs(model.geometryScale) > 100.0 ||
            model.geometryCenterX < 0.0 || model.geometryCenterX > 1.0 ||
            model.geometryCenterY < 0.0 || model.geometryCenterY > 1.0 ||
            model.vignetteCenterX < 0.0 || model.vignetteCenterX > 1.0 ||
            model.vignetteCenterY < 0.0 || model.vignetteCenterY > 1.0) {
            if (error)
                *error = QStringLiteral("Paramètres LCP invalides ou hors limites.");
            return false;
        }
    }
    *profile = parsed;
    return true;
}

int lensMatchScore(const QString &candidate, const QString &lensMake,
                   const QString &lensModel)
{
    const QString lens = normalized(lensMake + QLatin1Char(' ') + lensModel);
    const QString lensKey = normalized(lensModel);
    const QString profileLens = normalized(candidate);
    if (profileLens.isEmpty() || lensKey.isEmpty())
        return 0;
    if (profileLens == lens || profileLens == lensKey)
        return 10000 + profileLens.size();

    static const QRegularExpression tokenPattern(
        QStringLiteral("\\p{L}+|\\d+(?:[.,]\\d+)?"));
    const auto tokens = [](const QString &value, bool numeric) {
        QSet<QString> result;
        QRegularExpressionMatchIterator matches =
            tokenPattern.globalMatch(value.toCaseFolded());
        while (matches.hasNext()) {
            const QString token = matches.next().captured(0);
            const bool isNumeric = token.at(0).isDigit();
            if (isNumeric == numeric &&
                (numeric || (token != QStringLiteral("mm") &&
                             token != QStringLiteral("f") &&
                             token.size() >= 2))) {
                result.insert(token);
            }
        }
        return result;
    };

    const QSet<QString> rawWords = tokens(lensMake + QLatin1Char(' ') +
                                              lensModel,
                                          false);
    const QSet<QString> profileWords = tokens(candidate, false);
    const QSet<QString> rawNumbers = tokens(lensMake + QLatin1Char(' ') +
                                                lensModel,
                                            true);
    const QSet<QString> profileNumbers = tokens(candidate, true);
    int sharedWords = 0;
    for (const QString &word : rawWords)
        sharedWords += profileWords.contains(word);
    int sharedNumbers = 0;
    for (const QString &number : rawNumbers)
        sharedNumbers += profileNumbers.contains(number);
    if (sharedWords == 0 || sharedNumbers == 0)
        return 0;

    const bool contains = lens.contains(profileLens) ||
                          profileLens.contains(lens);
    return sharedWords * 100 + sharedNumbers * 10 + (contains ? 10 : 0);
}

int lcpMatchScore(const LcpData &profile, const QString &make,
                  const QString &model, const QString &lensMake,
                  const QString &lensModel)
{
    const QString cameraNeedle = normalized(profile.camera);
    const QString camera = normalized(make + QLatin1Char(' ') + model);
    const QString modelKey = normalized(model);
    const bool cameraMatches =
        cameraNeedle.isEmpty() || camera == cameraNeedle ||
        camera.contains(cameraNeedle) || cameraNeedle.contains(camera) ||
        modelKey == cameraNeedle || modelKey.contains(cameraNeedle) ||
        cameraNeedle.contains(modelKey);
    if (!cameraMatches)
        return 0;
    return qMax(lensMatchScore(profile.lens, lensMake, lensModel),
                lensMatchScore(profile.lensPrettyName, lensMake, lensModel));
}

LcpData::Model selectLcpModel(const LcpData &profile,
                              const CaptureSettings &capture)
{
    if (profile.models.size() == 1)
        return profile.models.first();
    const auto distance = [&capture](const LcpData::Model &model) {
        double result = 0.0;
        int dimensions = 0;
        const auto add = [&result, &dimensions](double actual, double requested,
                                                double scale) {
            if (actual > 0.0 && requested > 0.0) {
                const double d = (actual - requested) / scale;
                result += d * d;
                ++dimensions;
            }
        };
        add(model.focalLength, capture.focalLengthMm,
            qMax(1.0, capture.focalLengthMm));
        add(model.focusDistance, capture.focusDistanceM,
            qMax(1.0, capture.focusDistanceM));
        add(model.aperture, capture.aperture, qMax(1.0, capture.aperture));
        return dimensions == 0 ? 0.0 : std::sqrt(result / dimensions);
    };

    int first = -1;
    int second = -1;
    double firstDistance = std::numeric_limits<double>::max();
    double secondDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < profile.models.size(); ++i) {
        const double d = distance(profile.models[i]);
        if (d < firstDistance) {
            second = first;
            secondDistance = firstDistance;
            first = i;
            firstDistance = d;
        } else if (d < secondDistance) {
            second = i;
            secondDistance = d;
        }
    }
    LcpData::Model selected = profile.models[first];
    if (firstDistance == 0.0 || second < 0)
        return selected;
    const LcpData::Model &other = profile.models[second];
    const double denom = firstDistance + secondDistance;
    const double weight = denom > 0.0 ? secondDistance / denom : 0.0;
    const auto blend = [weight](auto &destination, const auto &source) {
        for (size_t i = 0; i < destination.size(); ++i)
            destination[i] = destination[i] * (1.0 - weight) +
                             source[i] * weight;
    };
    if (selected.hasGeometry && other.hasGeometry) {
        blend(selected.geometry, other.geometry);
        selected.geometryScale =
            selected.geometryScale * (1.0 - weight) + other.geometryScale * weight;
        selected.geometryCenterX =
            selected.geometryCenterX * (1.0 - weight) + other.geometryCenterX * weight;
        selected.geometryCenterY =
            selected.geometryCenterY * (1.0 - weight) + other.geometryCenterY * weight;
    } else if (other.hasGeometry) {
        selected.geometry = other.geometry;
        selected.hasGeometry = true;
    }
    if (selected.hasVignetting && other.hasVignetting) {
        blend(selected.vignetting, other.vignetting);
        selected.vignetteCenterX =
            selected.vignetteCenterX * (1.0 - weight) +
            other.vignetteCenterX * weight;
        selected.vignetteCenterY =
            selected.vignetteCenterY * (1.0 - weight) +
            other.vignetteCenterY * weight;
    } else if (other.hasVignetting) {
        selected.vignetting = other.vignetting;
        selected.vignetteCenterX = other.vignetteCenterX;
        selected.vignetteCenterY = other.vignetteCenterY;
        selected.hasVignetting = true;
    }
    if (selected.hasRedGreen && other.hasRedGreen) {
        blend(selected.redGreen, other.redGreen);
        selected.redCenterX =
            selected.redCenterX * (1.0 - weight) + other.redCenterX * weight;
        selected.redCenterY =
            selected.redCenterY * (1.0 - weight) + other.redCenterY * weight;
    } else if (other.hasRedGreen) {
        selected.redGreen = other.redGreen;
        selected.redCenterX = other.redCenterX;
        selected.redCenterY = other.redCenterY;
        selected.hasRedGreen = true;
    }
    if (selected.hasBlueGreen && other.hasBlueGreen) {
        blend(selected.blueGreen, other.blueGreen);
        selected.blueCenterX =
            selected.blueCenterX * (1.0 - weight) + other.blueCenterX * weight;
        selected.blueCenterY =
            selected.blueCenterY * (1.0 - weight) + other.blueCenterY * weight;
    } else if (other.hasBlueGreen) {
        selected.blueGreen = other.blueGreen;
        selected.blueCenterX = other.blueCenterX;
        selected.blueCenterY = other.blueCenterY;
        selected.hasBlueGreen = true;
    }
    return selected;
}

}

namespace {
std::shared_ptr<const DcpProfile> preparedDcp(const QString &path, QString *error)
{
    return std::static_pointer_cast<const DcpProfile>(PreparedProfileCache::acquire(path, "dcp-parser-v1",
        [](const QByteArray &bytes, QString *failure) -> PreparedProfileCache::Loaded {
            auto profile = std::make_shared<DcpProfile>();
            if (!profile->load(bytes, failure)) return {};
            return {profile, profile->accountedBytes()};
        }, error));
}
std::shared_ptr<const LcpData> preparedLcp(const QString &path, QString *error)
{
    return std::static_pointer_cast<const LcpData>(PreparedProfileCache::acquire(path, "lcp-parser-v1",
        [](const QByteArray &bytes, QString *failure) -> PreparedProfileCache::Loaded {
            auto profile = std::make_shared<LcpData>();
            if (!parseLcp(bytes, profile.get(), failure)) return {};
            const quint64 cost = sizeof(LcpData) + 8192 +
                quint64(profile->camera.capacity() + profile->lens.capacity() + profile->lensPrettyName.capacity()) * sizeof(QChar) +
                quint64(profile->models.capacity()) * sizeof(LcpData::Model);
            return {profile, cost};
        }, error));
}
}

PreparedProfileCache::Statistics preparedProfileCacheStats() { return PreparedProfileCache::statistics(); }
void configurePreparedProfileCache(quint64 bytes, int entries) { PreparedProfileCache::configure(bytes, entries); }
void clearPreparedProfileCache() { PreparedProfileCache::clear(); }

QStringList defaultProfileDirectories()
{
    QStringList roots;
    roots.append(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("profiles")));
    roots.append(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/profiles")));
    roots.append(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/IPR/profiles")));
    roots.append(QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
        QStringLiteral("IPR/profiles"), QStandardPaths::LocateDirectory));
    roots.append(QDir::current().filePath(QStringLiteral("profiles")));
    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (!appData.isEmpty())
        roots.append(QDir(appData).filePath(QStringLiteral("profiles")));
    QString adobeRoot = qEnvironmentVariable("IPR_ADOBE_CAMERARAW_DIR");
#ifdef Q_OS_WIN
    if (adobeRoot.isEmpty()) {
        QString programData = qEnvironmentVariable("ProgramData");
        if (programData.isEmpty())
            programData = QStringLiteral("C:/ProgramData");
        adobeRoot = QDir(programData).filePath(
            QStringLiteral("Adobe/CameraRaw"));
    }
#elif defined(Q_OS_LINUX)
    if (adobeRoot.isEmpty()) {
        const QString wslAdobeRoot =
            QStringLiteral("/mnt/c/ProgramData/Adobe/CameraRaw");
        if (QDir(wslAdobeRoot).exists())
            adobeRoot = wslAdobeRoot;
    }
#endif
    if (!adobeRoot.isEmpty())
        roots.append(adobeRoot);
    roots.removeDuplicates();
    return roots;
}

bool dcpMatchesCamera(const QString &path, const QString &make, const QString &model)
{
    const auto profile = preparedDcp(path, nullptr);
    return profile && profile->matches(make, model, path);
}

QStringList availableDcpProfiles(const QString &make, const QString &model,
                                 const QStringList &directories)
{
    if (model.trimmed().isEmpty())
        return {};
    const auto roots = directories.isEmpty() ? defaultProfileDirectories() : directories;
    const QString key = make + QLatin1Char('|') + model + QLatin1Char('|') + roots.join('|');
    static QMutex mutex;
    static QHash<QString, QStringList> cache;
    QMutexLocker lock(&mutex);
    if (cache.contains(key))
        return cache.value(key);
    QStringList paths;
    const auto configured = selectionFromManifests(roots, make, model, {}, {});
    if (!configured.dcpPath.isEmpty() && dcpMatchesCamera(configured.dcpPath, make, model))
        paths.append(configured.dcpPath);
    const QString modelKey = normalized(model);
    for (const auto &root : roots) {
        QDirIterator iterator(root, {QStringLiteral("*.dcp"), QStringLiteral("*.DCP")},
                              QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            // Avoid loading thousands of unrelated Adobe profiles.
            if (normalized(QFileInfo(path).completeBaseName()).contains(modelKey) &&
                dcpMatchesCamera(path, make, model))
                paths.append(QFileInfo(path).canonicalFilePath());
        }
    }
    paths.removeDuplicates();
    std::sort(paths.begin(), paths.end());
    cache.insert(key, paths);
    return paths;
}

Selection findProfiles(const QString &cameraMake, const QString &cameraModel,
                       const QString &lensMake, const QString &lensModel,
                       const QStringList &profileDirectories)
{
    if (profileDirectories.isEmpty()) {
        static QMutex cacheMutex;
        static QHash<QString, Selection> selectionCache;
        const QStringList roots = defaultProfileDirectories();
        const QString cacheKey = persistentSelectionKey(
            cameraMake, cameraModel, lensMake, lensModel, roots);
        QMutexLocker locker(&cacheMutex);
        const auto cached = selectionCache.constFind(cacheKey);
        if (cached != selectionCache.cend())
            return cached.value();
        Selection selection;
        if (restorePersistentSelection(cacheKey, &selection)) {
            selectionCache.insert(cacheKey, selection);
            return selection;
        }
        selection = findProfiles(cameraMake, cameraModel, lensMake, lensModel,
                                 roots);
        if (qEnvironmentVariableIsSet("IPR_LOG_RAW_TIMING")) {
            qInfo().noquote()
                << QStringLiteral("IPR profile selection cache | storing | dcp=%1 | lcp=%2")
                       .arg(selection.dcpPath, selection.lcpPath);
        }
        storePersistentSelection(cacheKey, selection);
        selectionCache.insert(cacheKey, selection);
        return selection;
    }

    Selection selection;
    const QStringList roots = profileDirectories;
    selection = selectionFromManifests(roots, cameraMake, cameraModel,
                                       lensMake, lensModel);
    if (!selection.dcpPath.isEmpty() || !selection.lcpPath.isEmpty())
        return selection;
    QStringList dcpFiles;
    QStringList lcpFiles;
    for (const QString &root : roots) {
        if (!QDir(root).exists())
            continue;
        QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString suffix = QFileInfo(path).suffix().toCaseFolded();
            if (suffix == QStringLiteral("dcp"))
                dcpFiles.append(path);
            else if (suffix == QStringLiteral("lcp"))
                lcpFiles.append(path);
        }
    }
    dcpFiles.removeDuplicates();
    lcpFiles.removeDuplicates();
    std::sort(dcpFiles.begin(), dcpFiles.end());
    std::sort(lcpFiles.begin(), lcpFiles.end());

    for (const QString &path : dcpFiles) {
        const auto profile = preparedDcp(path, nullptr);
        if (profile && profile->matches(cameraMake, cameraModel, path)) {
            selection.dcpPath = path;
            break;
        }
    }
    int bestLcpScore = 0;
    for (const QString &path : lcpFiles) {
        const auto profile = preparedLcp(path, nullptr);
        if (!profile)
            continue;
        const int score = lcpMatchScore(*profile, cameraMake, cameraModel,
                                        lensMake, lensModel);
        if (score > bestLcpScore) {
            selection.lcpPath = path;
            bestLcpScore = score;
        }
    }
    return selection;
}

bool applyDcp(const QString &fileName, cv::Mat &cameraRgb,
              double whiteBalanceKelvin, QString *error,
              const DcpRenderOptions &options)
{
    const auto profile = preparedDcp(fileName, error);
    if (!profile)
        return false;
    return profile->apply(cameraRgb, whiteBalanceKelvin, error, options);
}

bool whiteBalanceGains(const DcpWorkingContext &source, int temperature, int tint,
                       cv::Vec3d &gains, QString *error)
{
    const auto profile = preparedDcp(source.profilePath, error);
    return profile &&
        profile->whiteBalanceGains(source.sourceKelvin, source.cameraNeutral,
                                  temperature, tint, gains, error);
}

bool renderWhiteBalance(const DcpWorkingContext &source, int temperature, int tint,
                         cv::Mat &linearRgb, QString *error)
{
    if (source.cameraRgb.empty() || source.cameraRgb.type() != CV_32FC3) {
        if (error) *error = QStringLiteral("Source capteur DCP absente.");
        return false;
    }
    const auto profile = preparedDcp(source.profilePath, error);
    cv::Vec3d gains;
    if (!profile ||
        !profile->whiteBalanceGains(source.sourceKelvin, source.cameraNeutral,
                                   temperature, tint, gains, error)) return false;
    cv::Mat rgb;
    cv::transform(source.cameraRgb, rgb, cv::Matx33f(
        float(gains[0]),0,0, 0,float(gains[1]),0, 0,0,float(gains[2])));
    const double kelvin = temperature == int(std::lround(source.sourceKelvin)) && tint == 0
        ? source.sourceKelvin : double(temperature);
    if (!profile->apply(rgb, kelvin, error, source.options)) return false;
    linearRgb = std::move(rgb);
    return true;
}

bool lcpMatchesLens(const QString &path, const QString &cameraMake,
                    const QString &cameraModel, const QString &lensMake,
                    const QString &lensModel)
{
    const auto profile = preparedLcp(path, nullptr);
    return profile && lcpMatchScore(*profile, cameraMake, cameraModel, lensMake, lensModel) > 0;
}

bool applyLcp(const QString &fileName, cv::Mat &linearRgb,
              const CaptureSettings &capture, QString *error,
              const LcpRenderOptions &options, LcpApplied *applied)
{
    if (applied) *applied = {};
    const auto profile = preparedLcp(fileName, error);
    if (!profile)
        return false;
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3) {
        if (error)
            *error = QStringLiteral("L'image corrigée par LCP doit être RGB flottante.");
        return false;
    }

    const LcpData::Model model = selectLcpModel(*profile, capture);
    const double scale = double(std::max(linearRgb.cols, linearRgb.rows));
    const double cx = model.geometryCenterX * linearRgb.cols;
    const double cy = model.geometryCenterY * linearRgb.rows;
    cv::Mat mapX(linearRgb.size(), CV_32FC1);
    cv::Mat mapY(linearRgb.size(), CV_32FC1);
    cv::parallel_for_(cv::Range(0, linearRgb.rows),
                      [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            float *mx = mapX.ptr<float>(y);
            float *my = mapY.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x) {
                const double dx = (x - cx) / scale;
            const double dy = (y - cy) / scale;
            const double r2 = dx * dx + dy * dy;
            const double radial = 1.0 + r2 *
                (model.geometry[0] + r2 *
                 (model.geometry[1] + r2 * model.geometry[2]));
            const double tangentialX = model.geometry[3] * dy +
                                       model.geometry[4] * dx;
            const double tangentialY = model.geometry[3] * dx +
                                       model.geometry[4] * dy;
            mx[x] = float(cx + model.geometryScale *
                         (radial * dx + 2.0 * tangentialX * dx +
                          model.geometry[4] * r2) * scale);
                my[x] = float(cy + model.geometryScale *
                             (radial * dy + 2.0 * tangentialY * dy +
                              model.geometry[3] * r2) * scale);
            }
        }
    });

    cv::Mat geometryCorrected = linearRgb;
    if (options.lensCorrection && model.hasGeometry) {
        cv::remap(linearRgb, geometryCorrected, mapX, mapY, cv::INTER_LINEAR,
                  cv::BORDER_REPLICATE);
    }

    if (options.chromaticAberration && (model.hasRedGreen || model.hasBlueGreen)) {
        std::vector<cv::Mat> channels;
        cv::split(geometryCorrected, channels);
        const auto correctChromaticChannel = [&](int channel,
                                                 const std::array<double, 5> &ca,
                                                 double centerX,
                                                 double centerY) {
            cv::Mat caX(linearRgb.size(), CV_32FC1);
            cv::Mat caY(linearRgb.size(), CV_32FC1);
            const double caCx = centerX * linearRgb.cols;
            const double caCy = centerY * linearRgb.rows;
            cv::parallel_for_(cv::Range(0, linearRgb.rows),
                              [&](const cv::Range &rows) {
                for (int y = rows.start; y < rows.end; ++y) {
                    float *mx = caX.ptr<float>(y);
                    float *my = caY.ptr<float>(y);
                    for (int x = 0; x < linearRgb.cols; ++x) {
                        const double dx = (x - caCx) / scale;
                    const double dy = (y - caCy) / scale;
                    const double r2 = dx * dx + dy * dy;
                    const double radial = 1.0 + r2 *
                        (ca[0] + r2 * (ca[1] + r2 * ca[2]));
                    const double tx = ca[3] * dy + ca[4] * dx;
                    const double ty = ca[3] * dx + ca[4] * dy;
                    mx[x] = float(caCx + (radial * dx + 2.0 * tx * dx +
                                            ca[4] * r2) * scale);
                        my[x] = float(caCy + (radial * dy + 2.0 * ty * dy +
                                                ca[3] * r2) * scale);
                    }
                }
            });
            cv::Mat corrected;
            cv::remap(geometryCorrected, corrected, caX, caY, cv::INTER_LINEAR,
                      cv::BORDER_REPLICATE);
            cv::extractChannel(corrected, channels[channel], channel);
        };
        if (model.hasRedGreen)
            correctChromaticChannel(0, model.redGreen,
                                    model.redCenterX, model.redCenterY);
        if (model.hasBlueGreen)
            correctChromaticChannel(2, model.blueGreen,
                                    model.blueCenterX, model.blueCenterY);
        cv::merge(channels, geometryCorrected);
    }

    if (options.lensCorrection && model.hasVignetting) {
        const double vignetteCx = model.vignetteCenterX * linearRgb.cols;
        const double vignetteCy = model.vignetteCenterY * linearRgb.rows;
        const double focalLength = capture.focalLengthMm > 0.0
                                       ? capture.focalLengthMm
                                       : model.focalLength;
        const double focalScale =
            focalLength > 0.0 ? focalLength / 35.0 * scale : scale;
        cv::parallel_for_(cv::Range(0, geometryCorrected.rows),
                          [&](const cv::Range &rows) {
            for (int y = rows.start; y < rows.end; ++y) {
                cv::Vec3f *row = geometryCorrected.ptr<cv::Vec3f>(y);
                for (int x = 0; x < geometryCorrected.cols; ++x) {
                    const double dx = (x - vignetteCx) / focalScale;
                const double dy = (y - vignetteCy) / focalScale;
                const double r2 = dx * dx + dy * dy;
                const double opticalAttenuation =
                    1.0 + r2 *
                              (model.vignetting[0] + r2 *
                               (model.vignetting[1] +
                                r2 * model.vignetting[2]));
                const double correction =
                    opticalAttenuation > 1e-6 ? 1.0 / opticalAttenuation : 4.0;
                const double gain = qBound(
                    0.25, correction, 4.0);
                    row[x] *= float(gain);
                }
            }
        });
    }

    if (applied) {
        applied->geometry = options.lensCorrection && model.hasGeometry;
        applied->vignetting = options.lensCorrection && model.hasVignetting;
        applied->chromaticAberration = options.chromaticAberration && (model.hasRedGreen || model.hasBlueGreen);
    }
    linearRgb = std::move(geometryCorrected);
    return true;
}

}
