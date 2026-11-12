#ifndef IPR_NEURAL_NEURALRAWDENOISER_H
#define IPR_NEURAL_NEURALRAWDENOISER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ipr::neural {

// Contiguous planar R, G1, G2, B; signed, black-subtracted sensor units.
// Normalization divisor is supplied by RawDevelopment, never hardcoded here.
struct BayerPlanes {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<float> values;
};

struct NoiseLaw {
    std::array<float, 4> a{};
    std::array<float, 4> b{};
};

struct DenoiseOptions {
    float strength = 0.0f;
    std::size_t coreSize = 1024;
    std::size_t halo = 192;
    std::function<bool()> cancelled;
};

enum class DenoiseStatus { Success, Cancelled, InvalidInput, Unavailable, Failed };

struct DenoiseResult {
    DenoiseStatus status = DenoiseStatus::Failed;
    BayerPlanes image;
    std::string error;
    explicit operator bool() const noexcept { return status == DenoiseStatus::Success; }
};

struct SessionOptions {
    std::string modelPath;
    bool preferDirectML = true;
    int cpuThreads = 8;
};

// Owns the runtime session. Concurrent calls are serialized (DirectML contract).
// Cancellation is checked before loading and between bounded tile inferences.
class NeuralRawDenoiser {
public:
    explicit NeuralRawDenoiser(SessionOptions options);
    ~NeuralRawDenoiser();
    NeuralRawDenoiser(const NeuralRawDenoiser&) = delete;
    NeuralRawDenoiser& operator=(const NeuralRawDenoiser&) = delete;

    DenoiseResult denoise(const BayerPlanes& input, const NoiseLaw& law,
                          const DenoiseOptions& options);
    std::string engine() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Same noisy-only 5x5 mean as training, clipped window at image boundaries.
std::vector<float> sigmaMap(const BayerPlanes& input, const NoiseLaw& law);

} // namespace ipr::neural

#endif // IPR_NEURAL_NEURALRAWDENOISER_H
