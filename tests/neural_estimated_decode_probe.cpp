// Manual check only: decodes RAW files with the neural CFA stage as the editor does, and prints
// whether the network was applied and whether the photo's noise law was qualified or estimated.
// Usage: neuralEstimatedDecodeProbe <model.onnx> <raw>...
#include "ImageImporter.h"
#include "NeuralDevelopment.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>

#include <iostream>
#include <memory>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);
    if (arguments.size() < 2) {
        std::cerr << "usage: neuralEstimatedDecodeProbe <model.onnx> <raw>...\n";
        return 2;
    }
    QString modelId, reason;
    const std::shared_ptr<NeuralCfa::Service> service = NeuralDevelopment::create(arguments.front(), &modelId, &reason);
    if (!service) {
        std::cerr << "no network: " << reason.toStdString() << '\n';
        return 1;
    }
    bool all = true;
    for (const QString &path : arguments.mid(1)) {
        RawDecoder::Options options;
        options.automaticWhiteBalance = false;   // IPR develops the RAW (and runs the network) with the camera's
        options.preferCameraNeutral = true;
        options.listDcpProfiles = true;
        options.useNormalizationEnvironment = false;
        options.signedDevelopment = true;
        auto report = std::make_shared<NeuralDevelopment::Report>();
        options.cfaStageFactory = NeuralDevelopment::Factory{service, 0.5f, {}, {}, report};
        QElapsedTimer timer;
        timer.start();
        const ImageImport::Result loaded = ImageImport::load(path, options);
        const auto &m = loaded.metadata;
        const bool estimated = service->estimated(m.cameraMake, m.cameraModel, m.iso);
        std::cout << QFileInfo(path).fileName().toStdString() << " | " << m.cameraModel.toStdString() << " ISO " << m.iso
                  << " | law " << (estimated ? "estimated" : "qualified") << " | network "
                  << (report->applied ? "applied" : "not applied") << (report->reason.isEmpty() ? "" : ": ")
                  << report->reason.toStdString() << " | " << timer.elapsed() << " ms"
                  << (loaded.image.isNull() ? " | no image: " + loaded.error.toStdString() : "") << '\n';
        all = all && report->applied && !loaded.image.isNull();
    }
    return all ? 0 : 1;
}
