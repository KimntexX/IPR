#include "NeuralDevelopment.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>

static void initializeNeuralResources()
{
    Q_INIT_RESOURCE(ipr_ui_defaults);
}

namespace NeuralDevelopment {
RawDevelopment::CfaStage Factory::operator()(const RawInput::Input &input) const
{
    if (!service) {
        const QString why = error;
        return [why](ipr::Image &, const std::array<std::array<int, 2>, 2> &, QString *reason) {
            if (reason) *reason = why;
            return false;
        };
    }
    QString why;
    auto stage = service->stageFor(input, strength, cancelled, &why);
    if (!stage) qInfo() << "Neural CFA unavailable for this RAW:" << why;
    return stage;
}

std::shared_ptr<NeuralCfa::Service> create(const QString &overridePath, QString *modelId, QString *reason)
{
    static const bool resourcesReady = [] { initializeNeuralResources(); return true; }();
    Q_UNUSED(resourcesReady);
    QFile resource(QStringLiteral(":/packaging/neural-model.json"));
    if (!resource.open(QIODevice::ReadOnly)) {
        if (reason) *reason = QStringLiteral("Manifeste neuronal absent.");
        return {};
    }
    const auto manifest = NeuralCfa::parseManifest(resource.readAll(), reason);
    if (!manifest) return {};
    if (modelId) *modelId = manifest->modelId;
    const auto catalog = NoiseLaw::loadCatalog(QStringLiteral(":/config/noise-laws.json"), reason);
    if (!catalog) return {};
    QString path = overridePath;
    if (path.isEmpty()) path = qEnvironmentVariable("IPR_NEURAL_MODEL");
    if (path.isEmpty()) path = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("models/%1.onnx").arg(manifest->modelId));
    return NeuralCfa::Service::create(path, *manifest, *catalog, reason);
}

void bindCancellation(RawDecoder::Options &options)
{
    if (auto *factory = options.cfaStageFactory.target<Factory>())
        factory->cancelled = options.cancellationRequested;
}
}
