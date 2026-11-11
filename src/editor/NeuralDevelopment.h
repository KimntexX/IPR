#ifndef IPR_EDITOR_NEURALDEVELOPMENT_H
#define IPR_EDITOR_NEURALDEVELOPMENT_H

#include "NeuralCfa.h"
#include "RawDecoder.h"
#include <memory>

// Adapter only: service/session lifetime, recipe identity and decode cancellation.
namespace NeuralDevelopment {
struct Factory {
    std::shared_ptr<NeuralCfa::Service> service;
    float strength = 0;
    QString error;
    std::function<bool()> cancelled;
    RawDevelopment::CfaStage operator()(const RawInput::Input &input) const;
};
std::shared_ptr<NeuralCfa::Service> create(const QString &overridePath,
                                        QString *modelId, QString *reason);
void bindCancellation(RawDecoder::Options &options);
}
#endif // IPR_EDITOR_NEURALDEVELOPMENT_H
