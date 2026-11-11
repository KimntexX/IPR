// The application's neural CFA service: manifest, model check, stage per RAW. With a model
// path argument the real network runs on a synthetic R6 III mosaic.
#include "NeuralCfa.h"
#include "image_testing.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <random>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

const QByteArray ValidManifest = R"({"model_id": "test", "sha256": "%1", "input_divisor": 12303,
  "noise_law_pattern": [["R", "G1"], ["G2", "B"]]})";

void manifests()
{
    QString reason;
    const QByteArray valid = QString::fromLatin1(ValidManifest).arg(QString(64, QLatin1Char('a'))).toLatin1();
    const auto manifest = NeuralCfa::parseManifest(valid, &reason);
    check(manifest && manifest->inputDivisor == 12303.0 && manifest->measuredPattern[1][0] == RawInput::Green2,
          "a complete manifest parses");
    check(!NeuralCfa::parseManifest(R"({"model_id": "x", "sha256": null, "input_divisor": 12303})", &reason),
          "a manifest without SHA-256 is refused");
    check(!NeuralCfa::parseManifest(QString::fromLatin1(ValidManifest).arg(QString(64, QLatin1Char('a')))
                                        .replace(QStringLiteral("12303"), QStringLiteral("0")).toLatin1(), &reason),
          "a manifest without a positive divisor is refused");
    check(!NeuralCfa::parseManifest(QString::fromLatin1(ValidManifest).arg(QString(64, QLatin1Char('a')))
                                        .replace(QStringLiteral("\"G2\""), QStringLiteral("\"R\"")).toLatin1(), &reason),
          "a manifest pattern that is not a Bayer cell is refused");
}

void modelCheck()
{
    QTemporaryDir folder;
    const QString path = folder.filePath(QStringLiteral("model.onnx"));
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write("not the model") > 0, "temporary model written");
    file.close();
    QString reason;
    const auto manifest = NeuralCfa::parseManifest(
        QString::fromLatin1(ValidManifest).arg(QString(64, QLatin1Char('b'))).toLatin1(), &reason);
    const auto catalog = NoiseLaw::loadCatalog(QStringLiteral(":/config/noise-laws.json"), &reason);
    check(manifest && catalog, "manifest and catalogue load");
    if (!manifest || !catalog)
        return;
    check(!NeuralCfa::Service::create(path, *manifest, *catalog, &reason) && reason.contains(QStringLiteral("does not match")),
          "a model whose digest differs from the manifest is refused");
    check(!NeuralCfa::Service::create(folder.filePath(QStringLiteral("missing.onnx")), *manifest, *catalog, &reason),
          "a missing model is refused");
}

RawInput::Input r6Input(double iso)
{
    RawInput::Input input;
    input.make = QStringLiteral("Canon");
    input.model = QStringLiteral("EOS R6 Mark III");
    input.iso = iso;
    input.pattern = {{{RawInput::Red, RawInput::Green}, {RawInput::Green2, RawInput::Blue}}};
    input.black = {2048, 2048, 2048, 2048};
    input.white = 16383;
    input.asShotMultipliers = {2000, 1024, 1024, 1700};
    return input;
}

void realModel(const QString &modelPath, const QString &manifestPath)
{
    QString reason;
    QFile manifestFile(manifestPath);
    check(manifestFile.open(QIODevice::ReadOnly), "model manifest readable");
    const auto manifest = NeuralCfa::parseManifest(manifestFile.readAll(), &reason);
    const auto catalog = NoiseLaw::loadCatalog(QStringLiteral(":/config/noise-laws.json"), &reason);
    check(manifest && catalog, "the shipped manifest and catalogue load");
    if (!manifest || !catalog)
        return;
    const auto service = NeuralCfa::Service::create(modelPath, *manifest, *catalog, &reason);
    check(service != nullptr, "the real model matches its manifest");
    if (!service)
        return;
    check(!service->stageFor(r6Input(80000), 1.f, {}, &reason), "an ISO above the measured laws gives no stage");
    check(service->available(QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"), 6400, &reason),
          "an R6 III at ISO 6400 can get the network");
    check(!service->available(QStringLiteral("Canon"), QStringLiteral("Canon EOS 60D"), 1600, &reason)
              && reason.contains(QStringLiteral("boîtier")), "a 60D is refused with a camera reason");
    check(!service->available(QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 102400, &reason)
              && reason.contains(QStringLiteral("ISO 102400")), "an ISO outside the laws is refused with an ISO reason");
    check(!service->stageFor(r6Input(6400), 0.f, {}, &reason), "a zero strength gives no stage");
    const RawDevelopment::CfaStage stage = service->stageFor(r6Input(6400), 1.f, {}, &reason);
    check(bool(stage), "an R6 III at ISO 6400 gets a stage");
    if (!stage)
        return;
    // A flat grey at 2000 counts above black with ISO 6400 Poisson-Gaussian noise.
    ipr::Image mosaic = ipr::Image::create({256, 192}, ipr::Depth::F32, 1);
    std::mt19937 random(5);
    std::normal_distribution<float> noise(0.f, std::sqrt(12.8f * 2000.f + 540.f));
    double before = 0.0, after = 0.0;
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            imagetest::at<float>(mosaic, y, x) = 2000.f + noise(random);
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x)
            before += std::pow(imagetest::at<float>(mosaic, y, x) - 2000.0, 2);
    check(stage(mosaic, r6Input(6400).pattern, &reason), "the stage runs");
    double mean = 0.0;
    for (int y = 0; y < 192; ++y)
        for (int x = 0; x < 256; ++x) {
            after += std::pow(imagetest::at<float>(mosaic, y, x) - 2000.0, 2);
            mean += imagetest::at<float>(mosaic, y, x);
        }
    mean /= 256.0 * 192.0;
    std::cout << "engine " << service->engine().toStdString() << ", noise power ratio " << after / before
              << ", mean " << mean << '\n';
    check(after < 0.1 * before, "the network removes most of the noise of a flat grey");
    check(std::abs(mean - 2000.0) < 10.0, "the flat grey keeps its level");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    manifests();
    modelCheck();
    if (argc == 3)
        realModel(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
    if (!passed)
        return 1;
    std::cout << "neural CFA service tests passed\n";
    return 0;
}
