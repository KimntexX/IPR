// The production module with the real exported network on a real crop: its sigma map and its
// output must match the PyTorch reference written by tests/neural/make_reference_case.py
// (patch_neural_restoration). Arguments: model path, reference case folder.
#include "NeuralRawDenoiser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<float> readFloats(const std::string &path, std::size_t count)
{
    std::vector<float> values(count);
    std::ifstream stream(path, std::ios::binary);
    if (!stream.read(reinterpret_cast<char *>(values.data()), std::streamsize(count * sizeof(float))))
        return {};
    return values;
}

float largestDifference(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.size() != b.size())
        return INFINITY;
    float worst = 0.f;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: neuralModelCaseTests <model.onnx> <reference-case folder>\n";
        return 2;
    }
    const std::string model = argv[1], folder = argv[2];
    const std::size_t width = 768, height = 512, area = width * height;
    ipr::neural::BayerPlanes input;
    input.width = width;
    input.height = height;
    input.values = readFloats(folder + "/planes.bin", 4 * area);
    const std::vector<float> a = readFloats(folder + "/law_a.bin", 4), b = readFloats(folder + "/law_b.bin", 4);
    const std::vector<float> expected = readFloats(folder + "/expected.bin", 4 * area);
    const std::vector<float> sigma = readFloats(folder + "/sigma.bin", 4 * area);
    if (input.values.empty() || a.empty() || b.empty() || expected.empty() || sigma.empty()) {
        std::cerr << "FAILED: reference case unreadable\n";
        return 1;
    }
    ipr::neural::NoiseLaw law;
    std::copy(a.begin(), a.end(), law.a.begin());
    std::copy(b.begin(), b.end(), law.b.begin());

    bool passed = true;
    const float sigmaError = largestDifference(ipr::neural::sigmaMap(input, law), sigma);
    std::cout << "sigma map max difference " << sigmaError << '\n';
    passed = passed && sigmaError <= 1e-6f;

    for (const bool directML : {false, true}) {
        ipr::neural::SessionOptions session;
        session.modelPath = model;
        session.preferDirectML = directML;
        ipr::neural::NeuralRawDenoiser denoiser(session);
        ipr::neural::DenoiseOptions options;
        options.strength = 1.0f;
        const ipr::neural::DenoiseResult result = denoiser.denoise(input, law, options);
        if (!result) {
            std::cerr << "FAILED: " << (directML ? "DirectML" : "CPU") << " run: " << result.error << '\n';
            passed = false;
            continue;
        }
        const float error = largestDifference(result.image.values, expected);
        const float tolerance = denoiser.engine() == "DirectML" ? 1e-3f : 1e-4f;
        std::cout << denoiser.engine() << " output max difference " << error << " (tolerance " << tolerance << ")\n";
        passed = passed && error <= tolerance;
    }
    if (!passed) {
        std::cerr << "FAILED: neural model reference case\n";
        return 1;
    }
    std::cout << "neural model reference case passed\n";
    return 0;
}
