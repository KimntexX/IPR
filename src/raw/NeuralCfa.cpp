#include "NeuralCfa.h"

#include "NeuralRawDenoiser.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace NeuralCfa {
namespace {

bool fail(QString *reason, const QString &message)
{
    if (reason)
        *reason = message;
    return false;
}

std::optional<int> colourOf(const QString &name)
{
    if (name == QLatin1String("R")) return RawInput::Red;
    if (name == QLatin1String("G1")) return RawInput::Green;
    if (name == QLatin1String("G2")) return RawInput::Green2;
    if (name == QLatin1String("B")) return RawInput::Blue;
    return std::nullopt;
}

QString fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

std::optional<ModelManifest> parseManifest(const QByteArray &json, QString *reason)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(reason, QStringLiteral("neural model manifest is not a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    ModelManifest manifest;
    manifest.modelId = root.value(QStringLiteral("model_id")).toString();
    manifest.sha256 = root.value(QStringLiteral("sha256")).toString();
    manifest.inputDivisor = root.value(QStringLiteral("input_divisor")).toDouble(0.0);
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (manifest.modelId.isEmpty() || !hex.match(manifest.sha256).hasMatch()
        || !std::isfinite(manifest.inputDivisor) || manifest.inputDivisor <= 0.0) {
        fail(reason, QStringLiteral("neural model manifest lacks a model id, SHA-256 or input divisor"));
        return std::nullopt;
    }
    const QJsonArray rows = root.value(QStringLiteral("noise_law_pattern")).toArray();
    if (rows.size() != 2) {
        fail(reason, QStringLiteral("neural model manifest lacks the 2x2 pattern of its noise laws"));
        return std::nullopt;
    }
    for (int y = 0; y < 2; ++y) {
        const QJsonArray row = rows.at(y).toArray();
        for (int x = 0; x < 2; ++x) {
            const auto colour = row.size() == 2 ? colourOf(row.at(x).toString()) : std::nullopt;
            if (!colour) {
                fail(reason, QStringLiteral("neural model manifest has an unknown CFA colour"));
                return std::nullopt;
            }
            manifest.measuredPattern[size_t(y)][size_t(x)] = *colour;
        }
    }
    if (!CfaNoiseStage::planeOfSite(manifest.measuredPattern)) {
        fail(reason, QStringLiteral("neural model manifest pattern is not a Bayer cell"));
        return std::nullopt;
    }
    return manifest;
}

std::unique_ptr<Service> Service::create(const QString &modelPath, const ModelManifest &manifest,
                                         NoiseLaw::Catalog catalog, QString *reason)
{
    const QString digest = fileSha256(modelPath);
    if (digest.isEmpty()) {
        fail(reason, QStringLiteral("neural model unreadable: %1").arg(modelPath));
        return nullptr;
    }
    if (digest != manifest.sha256) {
        fail(reason, QStringLiteral("neural model %1 does not match its manifest").arg(modelPath));
        return nullptr;
    }
    ipr::neural::SessionOptions options;
    options.modelPath = QFile::encodeName(modelPath).toStdString();
    auto denoiser = std::make_shared<ipr::neural::NeuralRawDenoiser>(options);
    return std::unique_ptr<Service>(new Service(std::move(denoiser), manifest, std::move(catalog)));
}

Service::Service(std::shared_ptr<ipr::neural::NeuralRawDenoiser> denoiser, ModelManifest manifest, NoiseLaw::Catalog catalog)
    : m_denoiser(std::move(denoiser)), m_manifest(std::move(manifest)), m_catalog(std::move(catalog))
{
}

Service::~Service() = default;

bool Service::available(const QString &make, const QString &model, double iso, QString *reason) const
{
    QString why;
    if (NoiseLaw::resolve(m_catalog, make, model, int(std::lround(iso)), &why))
        return true;
    // A camera of the catalogue answers for some ISO: the photo's ISO is outside its laws.
    const bool knownCamera = NoiseLaw::resolve(m_catalog, make, model, 6400, nullptr).has_value()
        || NoiseLaw::resolve(m_catalog, make, model, 3200, nullptr).has_value();
    fail(reason, knownCamera ? QStringLiteral("Débruitage IA indisponible à ISO %1 pour ce boîtier.").arg(int(std::lround(iso)))
                             : QStringLiteral("Débruitage IA indisponible pour ce boîtier."));
    return false;
}

RawDevelopment::CfaStage Service::stageFor(const RawInput::Input &input, float strength,
                                           std::function<bool()> cancelled, QString *reason,
                                           std::function<void(bool applied, const QString &why)> report) const
{
    if (!std::isfinite(strength) || strength <= 0.f || strength > 1.f) {
        fail(reason, QStringLiteral("neural strength outside (0, 1]"));
        return {};
    }
    CfaNoiseStage::Settings settings;
    settings.catalog = &m_catalog;
    settings.measuredPattern = m_manifest.measuredPattern;
    settings.networkDivisor = m_manifest.inputDivisor;
    settings.report = std::move(report);
    const std::shared_ptr<ipr::neural::NeuralRawDenoiser> denoiser = m_denoiser;
    settings.denoiser = [denoiser, strength, cancelled](const CfaNoiseStage::Planes &planes,
                                                        const CfaNoiseStage::NormalizedLaw &law, QString *error) {
        ipr::neural::BayerPlanes input;
        input.width = planes.width;
        input.height = planes.height;
        input.values = planes.values;
        ipr::neural::NoiseLaw networkLaw;
        networkLaw.a = law.a;
        networkLaw.b = law.b;
        ipr::neural::DenoiseOptions options;
        options.strength = strength;
        options.cancelled = cancelled;
        ipr::neural::DenoiseResult result = denoiser->denoise(input, networkLaw, options);
        if (!result) {
            if (error)
                *error = QString::fromStdString(result.error.empty() ? std::string("neural denoising unavailable") : result.error);
            return std::optional<CfaNoiseStage::Planes>();
        }
        CfaNoiseStage::Planes output;
        output.width = result.image.width;
        output.height = result.image.height;
        output.values = std::move(result.image.values);
        return std::optional<CfaNoiseStage::Planes>(std::move(output));
    };
    return CfaNoiseStage::makeStage(input, settings, reason);
}

QString Service::engine() const
{
    return QString::fromStdString(m_denoiser->engine());
}

} // namespace NeuralCfa
