#include "NeuralRawDenoiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ipr::neural;
namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void identical(const BayerPlanes& a, const BayerPlanes& b)
{
    require(a.width == b.width && a.height == b.height && a.values.size() == b.values.size(), "Shape mismatch");
    require(std::memcmp(a.values.data(), b.values.data(), a.values.size()*sizeof(float)) == 0, "Pixel mismatch");
}
}
int main(int argc, char** argv)
{
    try {
        BayerPlanes image{17, 19, std::vector<float>(4*17*19)};
        for (std::size_t i=0; i<image.values.size(); ++i)
            image.values[i] = static_cast<float>(static_cast<int>(i%71)-35) / 17.0f;
        image.values[0] = -0.0f;
        NoiseLaw law{{.01f,.02f,.03f,.04f},{.001f,.002f,.003f,.004f}};
        NeuralRawDenoiser disabled({});
        const auto bypass = disabled.denoise(image, {}, {});
        require(static_cast<bool>(bypass), "Bypass failed without runtime/model");
        identical(image,bypass.image);
        auto bad = image; bad.values.pop_back();
        require(disabled.denoise(bad,law,{}).status == DenoiseStatus::InvalidInput, "Malformed input accepted");
        bad = image; bad.values[0] = std::numeric_limits<float>::quiet_NaN();
        require(disabled.denoise(bad,law,{}).status == DenoiseStatus::InvalidInput, "NaN accepted");
        DenoiseOptions cancelled; cancelled.strength = 1; cancelled.cancelled = [] { return true; };
        require(disabled.denoise(image,law,cancelled).status == DenoiseStatus::Cancelled, "Cancellation failed");
        DenoiseOptions invalid; invalid.strength = 2;
        require(disabled.denoise(image,law,invalid).status == DenoiseStatus::InvalidInput, "Invalid strength accepted");
        invalid.strength=1; invalid.coreSize=7;
        require(disabled.denoise(image,law,invalid).status == DenoiseStatus::InvalidInput, "Unaligned core accepted");
        invalid.coreSize=8; invalid.halo=8;
        require(disabled.denoise(image,law,invalid).status == DenoiseStatus::InvalidInput, "Insufficient halo accepted");
        const BayerPlanes constant{1,1,{-1,1,2,3}};
        const auto sigma = sigmaMap(constant,law);
        require(std::abs(sigma[0]-std::sqrt(law.b[0])) < 1e-7f, "Negative mean was not clamped");
        require(std::abs(sigma[1]-std::sqrt(law.a[1]+law.b[1])) < 1e-7f, "Boundary mean counts padding");
        // Explicit corner average: only four valid pixels, not a padded 5x5.
        BayerPlanes corner{2,2,std::vector<float>(16,1)};
        corner.values[0]=5;
        require(std::abs(sigmaMap(corner,law)[0]-std::sqrt(2*law.a[0]+law.b[0])) < 1e-7f, "Corner sigma mismatch");
        if (argc > 1) {
            NeuralRawDenoiser missing({"nonexistent-neural-fixture.onnx",false,2});
            DenoiseOptions active; active.strength=1;
            const auto failed=missing.denoise(image,law,active);
            require(failed.status == DenoiseStatus::Failed && failed.image.values.empty(), "Missing model not reported");
            const auto afterFailure=missing.denoise(image,law,{});
            require(static_cast<bool>(afterFailure),"Failed session broke zero bypass");
            identical(image,afterFailure.image);
            for (const bool preferDml : {false,true}) {
                NeuralRawDenoiser runtime({argv[1],preferDml,2});
                DenoiseOptions options; options.strength = 1; options.coreSize=8;
                auto result=runtime.denoise(image,law,options);
                require(static_cast<bool>(result), result.error.c_str());
                identical(image,result.image);
                options.coreSize=32;
                const auto whole=runtime.denoise(image,law,options);
                require(static_cast<bool>(whole), whole.error.c_str());
                identical(result.image,whole.image);
                options.strength=.5f;
                const auto half=runtime.denoise(image,law,options);
                require(static_cast<bool>(half),half.error.c_str());
                identical(image,half.image);
                options.strength=1;
                int checks=0; options.coreSize=8;
                options.cancelled=[&] { return ++checks >= 5; };
                const auto stopped=runtime.denoise(image,law,options);
                require(stopped.status == DenoiseStatus::Cancelled && stopped.image.values.empty(), "Partial output escaped cancellation");
                std::cout << "Runtime verified: " << runtime.engine() << '\n';
            }
        }
        if (argc > 2 && argv[2][0]) {
            for (const bool preferDml : {false,true}) {
                NeuralRawDenoiser runtime({argv[2],preferDml,2});
                DenoiseOptions small; small.strength=1; small.coreSize=8;
                auto tiled=runtime.denoise(image,law,small);
                small.coreSize=32;
                auto whole=runtime.denoise(image,law,small);
                require(static_cast<bool>(tiled),tiled.error.c_str());
                require(static_cast<bool>(whole),whole.error.c_str());
                float difference=0;
                for (std::size_t i=0; i<image.values.size(); ++i)
                    difference=std::max(difference,std::abs(tiled.image.values[i]-whole.image.values[i]));
                require(difference < 2e-6f, "Spatial model tile seam detected");
                std::cout << "Spatial model seams verified: " << runtime.engine() << ", max difference " << difference << '\n';
            }
        }
        std::cout << "Neural RAW contract tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
