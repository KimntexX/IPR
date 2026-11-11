#include "NoiseLaw.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace NoiseLaw {
namespace {

constexpr qint64 MaximumCatalogBytes = 1024 * 1024;

bool fail(QString *reason, const QString &message)
{
    if (reason)
        *reason = message;
    return false;
}

bool positiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool parseIso(const QJsonObject &object, CatalogIso &entry, QString *reason)
{
    entry.iso = object.value(QStringLiteral("iso")).toInt(0);
    const QJsonArray positions = object.value(QStringLiteral("positions")).toArray();
    if (entry.iso <= 0 || positions.size() != 4)
        return fail(reason, QStringLiteral("each ISO needs a positive value and four CFA positions"));
    for (int i = 0; i < 4; ++i) {
        const QJsonObject position = positions.at(i).toObject();
        const QJsonValue slope = position.value(QStringLiteral("shot_slope_dn"));
        const double read = position.value(QStringLiteral("read_variance_dn2")).toDouble(-1.0);
        if (!positiveFinite(read))
            return fail(reason, QStringLiteral("ISO %1: read variance must be positive").arg(entry.iso));
        entry.readVariance[size_t(i)] = read;
        if (slope.isNull())
            continue;
        if (!slope.isDouble() || !positiveFinite(slope.toDouble()))
            return fail(reason, QStringLiteral("ISO %1: shot slope must be positive or null").arg(entry.iso));
        entry.shotSlope[size_t(i)] = slope.toDouble();
    }
    return true;
}

// Linear in log value against log ISO between the two nearest ISOs carrying the quantity.
template <typename Value>
std::optional<double> interpolate(const std::vector<CatalogIso> &isos, int iso, Value value)
{
    const CatalogIso *below = nullptr;
    const CatalogIso *above = nullptr;
    for (const CatalogIso &entry : isos) {
        if (!value(entry))
            continue;
        if (entry.iso == iso)
            return *value(entry);
        if (entry.iso < iso && (!below || entry.iso > below->iso))
            below = &entry;
        if (entry.iso > iso && (!above || entry.iso < above->iso))
            above = &entry;
    }
    if (!below || !above)
        return std::nullopt;
    const double t = std::log(double(iso) / below->iso) / std::log(double(above->iso) / below->iso);
    return std::exp((1.0 - t) * std::log(*value(*below)) + t * std::log(*value(*above)));
}

} // namespace

std::optional<Catalog> parseCatalog(const QByteArray &json, QString *reason)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(reason, QStringLiteral("noise law catalogue is not a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema_version")).toInt() != 1) {
        fail(reason, QStringLiteral("unsupported noise law catalogue schema"));
        return std::nullopt;
    }
    Catalog catalog;
    for (const QJsonValue &cameraValue : root.value(QStringLiteral("cameras")).toArray()) {
        const QJsonObject object = cameraValue.toObject();
        CatalogCamera camera;
        camera.make = object.value(QStringLiteral("make")).toString();
        camera.model = object.value(QStringLiteral("model")).toString();
        camera.proof = object.value(QStringLiteral("provenance")).toObject().value(QStringLiteral("proof")).toString();
        if (camera.make.isEmpty() || camera.model.isEmpty()) {
            fail(reason, QStringLiteral("each camera needs a make and a model"));
            return std::nullopt;
        }
        for (const QJsonValue &isoValue : object.value(QStringLiteral("isos")).toArray()) {
            CatalogIso entry;
            if (!parseIso(isoValue.toObject(), entry, reason))
                return std::nullopt;
            camera.isos.push_back(entry);
        }
        std::sort(camera.isos.begin(), camera.isos.end(),
                  [](const CatalogIso &a, const CatalogIso &b) { return a.iso < b.iso; });
        const bool duplicate = std::adjacent_find(camera.isos.begin(), camera.isos.end(),
            [](const CatalogIso &a, const CatalogIso &b) { return a.iso == b.iso; }) != camera.isos.end();
        if (camera.isos.empty() || duplicate) {
            fail(reason, QStringLiteral("%1 %2: ISO list empty or duplicated").arg(camera.make, camera.model));
            return std::nullopt;
        }
        catalog.cameras.push_back(std::move(camera));
    }
    if (catalog.cameras.empty()) {
        fail(reason, QStringLiteral("noise law catalogue lists no camera"));
        return std::nullopt;
    }
    return catalog;
}

std::optional<Catalog> loadCatalog(const QString &path, QString *reason)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(reason, QStringLiteral("cannot open %1").arg(path));
        return std::nullopt;
    }
    if (file.size() > MaximumCatalogBytes) {
        fail(reason, QStringLiteral("noise law catalogue larger than 1 MiB"));
        return std::nullopt;
    }
    return parseCatalog(file.readAll(), reason);
}

std::optional<Law> resolve(const Catalog &catalog, const QString &make, const QString &model,
                           int iso, QString *reason)
{
    const auto camera = std::find_if(catalog.cameras.begin(), catalog.cameras.end(), [&](const CatalogCamera &entry) {
        return entry.make.compare(make.trimmed(), Qt::CaseInsensitive) == 0
            && entry.model.compare(model.trimmed(), Qt::CaseInsensitive) == 0;
    });
    if (camera == catalog.cameras.end()) {
        fail(reason, QStringLiteral("no measured noise law for %1 %2").arg(make, model));
        return std::nullopt;
    }
    if (iso <= 0) {
        fail(reason, QStringLiteral("unknown ISO"));
        return std::nullopt;
    }
    Law law;
    bool measured = true;
    for (size_t i = 0; i < 4; ++i) {
        const auto slope = interpolate(camera->isos, iso, [i](const CatalogIso &entry) { return entry.shotSlope[i]; });
        const auto read = interpolate(camera->isos, iso, [i](const CatalogIso &entry) {
            return std::optional<double>(entry.readVariance[i]);
        });
        if (!slope || !read) {
            fail(reason, QStringLiteral("ISO %1 outside the measured noise laws of %2 %3").arg(iso).arg(make, model));
            return std::nullopt;
        }
        law.positions[i] = {*slope, *read};
        const auto exact = std::find_if(camera->isos.begin(), camera->isos.end(),
                                        [iso](const CatalogIso &entry) { return entry.iso == iso; });
        measured = measured && exact != camera->isos.end() && exact->shotSlope[i].has_value();
    }
    law.source = measured ? Source::Measured : Source::Interpolated;
    law.provenance = QStringLiteral("%1, ISO %2 %3").arg(camera->proof).arg(iso)
                         .arg(measured ? QStringLiteral("measured") : QStringLiteral("interpolated in log-ISO"));
    return law;
}

} // namespace NoiseLaw
