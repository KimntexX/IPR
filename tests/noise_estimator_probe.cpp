// Measure the shared Haar component, not the complete affine/adaptive model.
#include "NoiseEstimation.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (std::string(argv[1]) == "--version") {
        std::cout << "OpenCV " << CV_VERSION << '\n';
#if defined(_MSC_VER)
        std::cout << "MSVC " << _MSC_VER << '\n';
#elif defined(__clang__)
        std::cout << "Clang " << __clang_version__ << '\n';
#elif defined(__GNUC__)
        std::cout << "GCC " << __VERSION__ << '\n';
#else
        std::cout << "Compiler unknown\n";
#endif
        return 0;
    }
    std::ifstream input(argv[1], std::ios::binary);
    char magic[8]{};
    input.read(magic, 8);
    if (!input || std::string(magic, 8) != "IPRNS001") return 3;
    // Study format follows host byte order; the Python producer checks it.
    std::uint32_t count = 0;
    input.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (!input || count == 0 || count > 10000) return 4;
    std::cout << std::setprecision(17);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t rows = 0, columns = 0;
        input.read(reinterpret_cast<char *>(&rows), sizeof(rows));
        input.read(reinterpret_cast<char *>(&columns), sizeof(columns));
        if (!input || rows < 2 || columns < 2 || rows > 4096 || columns > 4096) return 5;
        cv::Mat plane(static_cast<int>(rows), static_cast<int>(columns), CV_32F);
        const auto bytes = static_cast<std::streamsize>(plane.total() * sizeof(float));
        input.read(reinterpret_cast<char *>(plane.data), bytes);
        if (!input) return 6;
        if (!cv::checkRange(plane)) return 7;
        const float sigma = NoiseEstimation::haarSigma(plane);
        if (!std::isfinite(sigma) || sigma < 0) return 8;
        std::cout << index << ' ' << sigma << '\n';
    }
    if (input.peek() != std::char_traits<char>::eof()) return 9;
    return std::cout.good() ? 0 : 10;
}
