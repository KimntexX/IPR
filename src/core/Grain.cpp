#include "Grain.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Grain {
namespace {

constexpr float StandardDeviation = 0.062f;   // encoded values, at 100
constexpr double Sigma = 0.75;                // grain size, pixels

std::uint32_t hash(std::uint32_t x, std::uint32_t y, std::uint32_t salt)
{
    std::uint32_t h = x * 0x9e3779b9u + y * 0x85ebca6bu + salt * 0xc2b2ae35u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// Unit Gaussian from two hashes (Box-Muller).
float gaussian(int x, int y)
{
    const float u = (float(hash(std::uint32_t(x), std::uint32_t(y), 1u) >> 8) + 0.5f) / 16777216.0f;
    const float v = float(hash(std::uint32_t(x), std::uint32_t(y), 2u) >> 8) / 16777216.0f;
    return std::sqrt(-2.0f * std::log(u)) * std::cos(6.2831853f * v);
}

// Share of the grain's deviation left by averaging it over blocks of n x n pixels:
// per axis, (1/n^2) integral over |u| < n of (n - |u|) rho(u), with the grain's
// correlation rho(u) = exp(-u^2 / (4 Sigma^2)); the 2D variance is its square.
double reducedDeviation(double n)
{
    const double s2 = 4.0 * Sigma * Sigma;
    const double integral = 2.0 * (n * std::sqrt(3.14159265358979 * s2) / 2.0 * std::erf(n / std::sqrt(s2))
                                   - s2 / 2.0 * (1.0 - std::exp(-n * n / s2)));
    return std::min(1.0, integral / (n * n));
}

float encode(float v)
{
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float decode(float e)
{
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void apply(cv::Mat &linearRgb, int amount, double pixelScale)
{
    if (amount <= 0 || linearRgb.empty() || linearRgb.type() != CV_32FC3) return;
    cv::Mat noise(linearRgb.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, noise.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            float *row = noise.ptr<float>(y);
            for (int x = 0; x < noise.cols; ++x) row[x] = gaussian(x, y);
        }
    });
    float deviation = StandardDeviation * float(std::min(amount, 100)) / 100.0f;
    if (pixelScale >= 1.0) {
        cv::GaussianBlur(noise, noise, cv::Size(0, 0), Sigma, Sigma, cv::BORDER_REFLECT);
        // The blur lowers the deviation of unit white noise to the norm of its kernel.
        const cv::Mat kernel = cv::getGaussianKernel(2 * int(std::ceil(4 * Sigma)) + 1, Sigma, CV_32F);
        deviation /= float(kernel.dot(kernel));
    } else {
        deviation *= float(reducedDeviation(1.0 / pixelScale));
    }
    cv::parallel_for_(cv::Range(0, linearRgb.rows), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            auto *row = linearRgb.ptr<cv::Vec3f>(y);
            const float *n = noise.ptr<float>(y);
            for (int x = 0; x < linearRgb.cols; ++x)
                for (int c = 0; c < 3; ++c)
                    row[x][c] = decode(std::max(0.0f, encode(std::max(0.0f, row[x][c])) + deviation * n[x]));
        }
    });
}

} // namespace Grain
