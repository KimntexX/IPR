#include "NeuralRawDenoiser.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef IPR_HAS_ONNX_RUNTIME
#include <onnxruntime_cxx_api.h>
#ifdef IPR_HAS_DIRECTML
#include <dml_provider_factory.h>
#endif
#endif

namespace ipr::neural {
namespace {
bool valid(const BayerPlanes& image)
{
    constexpr auto limit = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (!image.width || !image.height || image.width > limit || image.height > limit
        || image.width > std::numeric_limits<std::size_t>::max() / image.height / 4)
        return false;
    return image.values.size() == image.width * image.height * 4
        && std::all_of(image.values.begin(), image.values.end(), [](float x) { return std::isfinite(x); });
}

bool valid(const NoiseLaw& law)
{
    for (std::size_t c = 0; c < 4; ++c)
        if (!std::isfinite(law.a[c]) || !std::isfinite(law.b[c]) || law.a[c] < 0 || law.b[c] < 0)
            return false;
    return true;
}

bool cancelled(const DenoiseOptions& options)
{
    return options.cancelled && options.cancelled();
}

#ifdef IPR_HAS_ONNX_RUNTIME
std::size_t reflect(std::int64_t coordinate, std::size_t size)
{
    if (size == 1) return 0;
    const auto period = 2 * (static_cast<std::int64_t>(size) - 1);
    auto p = coordinate % period;
    if (p < 0) p += period;
    return static_cast<std::size_t>(p < static_cast<std::int64_t>(size) ? p : period - p);
}
#endif
} // namespace

std::vector<float> sigmaMap(const BayerPlanes& image, const NoiseLaw& law)
{
    if (!valid(image) || !valid(law)) throw std::invalid_argument("Invalid Bayer planes or noise law");
    const auto count = image.width * image.height;
    std::vector<float> sigma(image.values.size());
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t y = 0; y < image.height; ++y) {
            for (std::size_t x = 0; x < image.width; ++x) {
                const auto left = x > 2 ? x - 2 : 0;
                const auto top = y > 2 ? y - 2 : 0;
                const auto right = std::min(image.width, x + 3);
                const auto bottom = std::min(image.height, y + 3);
                double sum = 0;
                for (auto yy = top; yy < bottom; ++yy)
                    for (auto xx = left; xx < right; ++xx)
                        sum += image.values[c * count + yy * image.width + xx];
                const auto mean = std::max(0.0, sum / static_cast<double>((right-left) * (bottom-top)));
                const double variance = law.a[c] * mean + law.b[c];
                if (variance > std::numeric_limits<float>::max())
                    throw std::invalid_argument("Noise variance exceeds float range");
                sigma[c * count + y * image.width + x] = static_cast<float>(std::sqrt(variance));
            }
        }
    }
    return sigma;
}

struct NeuralRawDenoiser::Impl {
    explicit Impl(SessionOptions value) : options(std::move(value)) {}
    SessionOptions options;
    mutable std::mutex mutex;
    std::string provider = "Unavailable";
#ifdef IPR_HAS_ONNX_RUNTIME
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "IPR neural RAW"};
    std::unique_ptr<Ort::Session> session;

    void initialize()
    {
        if (session) return;
        if (options.modelPath.empty() || options.cpuThreads < 1)
            throw std::invalid_argument("A model path and positive thread count are required");
        const auto path = std::filesystem::u8path(options.modelPath);
        auto create = [&](bool directML) {
            Ort::SessionOptions settings;
            settings.SetIntraOpNumThreads(options.cpuThreads);
            settings.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            settings.DisableMemPattern();
#ifdef IPR_HAS_DIRECTML
            if (directML) Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(settings, 0));
#else
            (void)directML;
#endif
            return std::make_unique<Ort::Session>(environment, path.c_str(), settings);
        };
#ifdef IPR_HAS_DIRECTML
        if (options.preferDirectML) {
            try { session = create(true); provider = "DirectML"; }
            catch (const Ort::Exception&) { session.reset(); }
        }
#endif
        if (!session) { session = create(false); provider = "CPU"; }
        if (session->GetInputCount() != 2 || session->GetOutputCount() != 1) {
            session.reset(); provider = "Unavailable";
            throw std::invalid_argument("Expected planes and sigma inputs, corrected output");
        }
    }
#endif
};

NeuralRawDenoiser::NeuralRawDenoiser(SessionOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
NeuralRawDenoiser::~NeuralRawDenoiser() = default;

std::string NeuralRawDenoiser::engine() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->provider;
}

DenoiseResult NeuralRawDenoiser::denoise(const BayerPlanes& input, const NoiseLaw& law,
                                       const DenoiseOptions& options)
{
    try {
        if (!valid(input) || !std::isfinite(options.strength) || options.strength < 0 || options.strength > 1)
            return {DenoiseStatus::InvalidInput, {}, "Invalid image or strength"};
        if (cancelled(options)) return {DenoiseStatus::Cancelled, {}, {}};
        // Do not require a model, runtime or noise law for the exact bypass.
        if (options.strength == 0) return {DenoiseStatus::Success, input, {}};
        if (!valid(law) || options.coreSize == 0 || options.coreSize > 4096
            || options.coreSize % 8 || options.halo < 192 || options.halo > 512 || options.halo % 8)
            return {DenoiseStatus::InvalidInput, {}, "Invalid noise law or tile geometry"};
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (cancelled(options)) return {DenoiseStatus::Cancelled, {}, {}};
#ifndef IPR_HAS_ONNX_RUNTIME
        return {DenoiseStatus::Unavailable, {}, "ONNX Runtime support is disabled in this build"};
#else
        impl_->initialize();
        const auto sigma = sigmaMap(input, law);
        if (cancelled(options)) return {DenoiseStatus::Cancelled, {}, {}};
        BayerPlanes output = input;
        const auto count = input.width * input.height;
        const auto side = options.coreSize + 2 * options.halo;
        const auto tileCount = side * side;
        std::vector<float> tile(4 * tileCount), tileSigma(4 * tileCount);
        const std::array<int64_t,4> shape{1,4,static_cast<int64_t>(side),static_cast<int64_t>(side)};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        for (std::size_t y = 0; y < input.height; y += options.coreSize) {
            for (std::size_t x = 0; x < input.width; x += options.coreSize) {
                if (cancelled(options)) return {DenoiseStatus::Cancelled, {}, {}};
                for (std::size_t yy = 0; yy < side; ++yy) {
                    const auto iy = reflect(static_cast<int64_t>(y + yy) - static_cast<int64_t>(options.halo), input.height);
                    for (std::size_t xx = 0; xx < side; ++xx) {
                        const auto ix = reflect(static_cast<int64_t>(x + xx) - static_cast<int64_t>(options.halo), input.width);
                        for (std::size_t c = 0; c < 4; ++c) {
                            const auto from = c*count + iy*input.width + ix;
                            const auto to = c*tileCount + yy*side + xx;
                            tile[to] = input.values[from]; tileSigma[to] = sigma[from];
                        }
                    }
                }
                std::array<Ort::Value,2> inputs{
                    Ort::Value::CreateTensor<float>(memory, tile.data(), tile.size(), shape.data(), shape.size()),
                    Ort::Value::CreateTensor<float>(memory, tileSigma.data(), tileSigma.size(), shape.data(), shape.size())};
                const char* inputNames[]{"planes", "sigma"};
                const char* outputNames[]{"corrected"};
                auto result = impl_->session->Run(Ort::RunOptions{}, inputNames, inputs.data(), inputs.size(), outputNames, 1);
                if (cancelled(options)) return {DenoiseStatus::Cancelled, {}, {}};
                if (result.size() != 1 || !result[0].IsTensor())
                    throw std::runtime_error("The model did not return a tensor");
                const auto info = result[0].GetTensorTypeAndShapeInfo();
                if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
                    || info.GetShape() != std::vector<int64_t>(shape.begin(),shape.end()))
                    throw std::runtime_error("Unexpected model output shape or element type");
                const float* corrected = result[0].GetTensorData<float>();
                if (!corrected) throw std::runtime_error("Missing model output");
                for (std::size_t c = 0; c < 4; ++c)
                    for (std::size_t yy = 0; yy < std::min(options.coreSize,input.height-y); ++yy)
                        for (std::size_t xx = 0; xx < std::min(options.coreSize,input.width-x); ++xx) {
                            const auto from = c*tileCount + (yy+options.halo)*side + xx+options.halo;
                            const auto to = c*count + (y+yy)*input.width + x+xx;
                            const auto value = corrected[from];
                            if (!std::isfinite(value)) throw std::runtime_error("Non-finite model output");
                            output.values[to] = options.strength == 1 ? value
                                : (value == input.values[to] ? input.values[to]
                                    : input.values[to] + options.strength*(value-input.values[to]));
                            if (!std::isfinite(output.values[to])) throw std::runtime_error("Non-finite strength blend");
                        }
            }
        }
        return {DenoiseStatus::Success, std::move(output), {}};
#endif
    } catch (const std::invalid_argument& error) {
        return {DenoiseStatus::InvalidInput, {}, error.what()};
    } catch (const std::exception& error) {
        return {DenoiseStatus::Failed, {}, error.what()};
    }
}
} // namespace ipr::neural
