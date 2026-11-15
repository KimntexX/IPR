#ifndef IPR_RAW_NEURALCFA_H
#define IPR_RAW_NEURALCFA_H

#include "CfaNoiseStage.h"
#include "NoiseLaw.h"
#include "RawDevelopment.h"
#include "RawInput.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace ipr::neural {
class NeuralRawDenoiser;
}

// The neural CFA denoiser of the application: one ONNX session and the measured noise
// laws, shared by every RAW the decoder develops while the preference is on.
namespace NeuralCfa {

// packaging/neural-model.json: which network, its file digest, the units it was trained in.
struct ModelManifest
{
    QString modelId;
    QString sha256;                         // of the ONNX file, lower-case hex
    double inputDivisor = 0.0;              // counts per network unit (training normalization)
    CfaNoiseStage::Pattern measuredPattern{};  // 2x2 cell of the noise laws' camera
};

std::optional<ModelManifest> parseManifest(const QByteArray &json, QString *reason);

class Service
{
public:
    // The model file is checked against the manifest's SHA-256 before the session is made;
    // the session itself loads lazily, on the first non-zero strength.
    static std::unique_ptr<Service> create(const QString &modelPath, const ModelManifest &manifest,
                                           NoiseLaw::Catalog catalog, QString *reason);
    ~Service();
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    // The stage for one RAW at a strength in (0, 1]; empty with a reason when the camera
    // or its ISO has no measured law. The stage keeps the session alive while it exists.
    // `report` (optional) learns, once per run, whether the network was applied.
    RawDevelopment::CfaStage stageFor(const RawInput::Input &input, float strength,
                                      std::function<bool()> cancelled, QString *reason,
                                      std::function<void(bool applied, const QString &why)> report = {}) const;
    // Whether a photo can get the network, from its metadata alone (before any decoding). A
    // camera or ISO the catalogue lacks can, its law estimated from the photo when it is
    // decoded (decision of 13 November 2026); with the estimate off, false with a user-facing
    // reason. A RAW without optical black to read its read noise from is refused at decoding.
    bool available(const QString &make, const QString &model, double iso, QString *reason) const;
    // Whether this photo's noise law would be estimated from the photo, not a qualified one.
    bool estimated(const QString &make, const QString &model, double iso) const;
    void setEstimateUnqualified(bool estimate) { m_estimateUnqualified = estimate; }
    // DirectML, CPU or Unavailable, after the first denoising.
    QString engine() const;

private:
    Service(std::shared_ptr<ipr::neural::NeuralRawDenoiser> denoiser, ModelManifest manifest, NoiseLaw::Catalog catalog);

    std::shared_ptr<ipr::neural::NeuralRawDenoiser> m_denoiser;
    ModelManifest m_manifest;
    NoiseLaw::Catalog m_catalog;
    bool m_estimateUnqualified = true;
};

}

#endif // IPR_RAW_NEURALCFA_H
