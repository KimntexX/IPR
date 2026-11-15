#include "NoiseLaw.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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
    // EXIF often repeats the make in the model ("Canon EOS R6 Mark III"); LibRaw does not.
    QString bareModel = model.trimmed();
    if (bareModel.startsWith(make.trimmed() + QLatin1Char(' '), Qt::CaseInsensitive))
        bareModel = bareModel.mid(make.trimmed().size() + 1).trimmed();
    const auto camera = std::find_if(catalog.cameras.begin(), catalog.cameras.end(), [&](const CatalogCamera &entry) {
        return entry.make.compare(make.trimmed(), Qt::CaseInsensitive) == 0
            && entry.model.compare(bareModel, Qt::CaseInsensitive) == 0;
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

namespace {

constexpr int EstimateBlock = 16;
constexpr double EstimateQuantile = 0.1;
// Tenth percentile of the block variance below for unit Gaussian noise (4000 simulated blocks,
// tests/neural/estimate_single_raw_law.py on the research branch patch_noise_neural,
// quantile_bias(0.1)).
constexpr double EstimateQuantileBias = 0.8057036930236089;

double medianOf(std::vector<double> &values)
{
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2)
        return upper;
    return (*std::max_element(values.begin(), values.begin() + std::ptrdiff_t(middle)) + upper) / 2.0;
}

// numpy's default (linear) quantile of values.
double quantileOf(std::vector<double> values, double q)
{
    std::sort(values.begin(), values.end());
    const double position = q * double(values.size() - 1);
    const size_t below = size_t(std::floor(position));
    const size_t above = std::min(below + 1, values.size() - 1);
    return values[below] + (position - double(below)) * (values[above] - values[below]);
}

// The slope of one site's plane (signal in counts above black).
double slopeOf(const std::vector<double> &plane, int width, int height, double readVariance, double headroom)
{
    const int rw = width - 2, rh = height - 2;
    const int bw = rw / EstimateBlock, bh = rh / EstimateBlock;
    if (bw <= 0 || bh <= 0)
        return 0.0;
    std::vector<double> means, peaks, variances;
    means.reserve(size_t(bw) * size_t(bh));
    peaks.reserve(means.capacity());
    variances.reserve(means.capacity());
    std::vector<double> residual(size_t(EstimateBlock) * EstimateBlock);
    const auto at = [&](int x, int y) { return plane[size_t(y) * size_t(width) + size_t(x)]; };
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            double sum = 0.0, peak = -1e300;
            size_t k = 0;
            for (int j = 0; j < EstimateBlock; ++j)
                for (int i = 0; i < EstimateBlock; ++i) {
                    const int x = 1 + bx * EstimateBlock + i, y = 1 + by * EstimateBlock + j;
                    const double value = at(x, y);
                    sum += value;
                    peak = std::max(peak, value);
                    residual[k++] = value - 0.25 * (at(x, y - 1) + at(x, y + 1) + at(x - 1, y) + at(x + 1, y));
                }
            std::vector<double> spread = residual;
            const double centre = medianOf(spread);
            for (double &value : spread)
                value = std::abs(value - centre);
            const double deviation = medianOf(spread) / 0.6745;
            means.push_back(sum / double(EstimateBlock * EstimateBlock));
            peaks.push_back(peak);
            variances.push_back(deviation * deviation / 1.25);
        }
    std::vector<double> kept;
    for (size_t b = 0; b < means.size(); ++b)
        if (peaks[b] < 0.95 * headroom && means[b] > 0.0)
            kept.push_back(means[b]);
    if (kept.size() < 40)
        return 0.0;
    std::vector<double> edges;
    for (int e = 0; e <= 24; ++e)
        edges.push_back(quantileOf(kept, double(e) / 24.0));
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::vector<double> estimates;
    for (size_t e = 0; e + 1 < edges.size(); ++e) {
        std::vector<double> classVariance, classMean;
        for (size_t b = 0; b < means.size(); ++b)
            if (peaks[b] < 0.95 * headroom && means[b] > 0.0 && means[b] >= edges[e] && means[b] < edges[e + 1]) {
                classVariance.push_back(variances[b]);
                classMean.push_back(means[b]);
            }
        if (classVariance.size() < 40)
            continue;
        const double signal = medianOf(classMean);
        estimates.push_back((quantileOf(classVariance, EstimateQuantile) / EstimateQuantileBias - readVariance) / signal);
    }
    if (estimates.empty())
        return 0.0;
    std::sort(estimates.begin(), estimates.end());
    const size_t lower = std::max<size_t>(1, estimates.size() / 2);
    double sum = 0.0;
    for (size_t i = 0; i < lower; ++i)
        sum += estimates[i];
    return std::max(0.0, sum / double(lower));
}

}

std::optional<Law> estimate(const ipr::Image &mosaic, const std::array<double, 4> &black,
                            const std::array<double, 4> &readVariance, double white, QString *reason)
{
    const auto refuse = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return std::optional<Law>();
    };
    if (mosaic.empty() || mosaic.depth() != ipr::Depth::U16 || mosaic.channels() != 1 ||
        mosaic.width() < 2 * (EstimateBlock + 2) || mosaic.height() < 2 * (EstimateBlock + 2))
        return refuse(QStringLiteral("no mosaic to estimate the noise law from"));
    for (int p = 0; p < 4; ++p)
        if (!(readVariance[size_t(p)] > 0.0) || !std::isfinite(readVariance[size_t(p)]) ||
            !std::isfinite(black[size_t(p)]) || !(white > black[size_t(p)]))
            return refuse(QStringLiteral("no optical black to read the read noise from"));
    Law law;
    law.source = Source::Estimated;
    law.provenance = QStringLiteral("estimated from this photo (no qualified law for this camera)");
    const int width = mosaic.width() / 2, height = mosaic.height() / 2;
    std::vector<double> plane(size_t(width) * size_t(height));
    for (int p = 0; p < 4; ++p) {
        const int dy = p / 2, dx = p % 2;
        for (int y = 0; y < height; ++y) {
            const auto *row = mosaic.row<std::uint16_t>(2 * y + dy);
            for (int x = 0; x < width; ++x)
                plane[size_t(y) * size_t(width) + size_t(x)] = double(row[2 * x + dx]) - black[size_t(p)];
        }
        law.positions[size_t(p)] = {slopeOf(plane, width, height, readVariance[size_t(p)], white - black[size_t(p)]),
                                    readVariance[size_t(p)]};
    }
    return law;
}

} // namespace NoiseLaw
