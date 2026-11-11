#include "CfaNoiseStage.h"

#include "RawInput.h"

#include <cmath>

namespace CfaNoiseStage {
namespace {

int planeOfColour(int colour)
{
    switch (colour) {
    case RawInput::Red: return 0;
    case RawInput::Green: return 1;
    case RawInput::Green2: return 2;
    case RawInput::Blue: return 3;
    default: return -1;
    }
}

bool usableMosaic(const ipr::Image &mosaic)
{
    return !mosaic.empty() && mosaic.depth() == ipr::Depth::F32 && mosaic.channels() == 1
        && mosaic.width() >= 2 && mosaic.height() >= 2;
}

bool usableDivisor(double divisor)
{
    return std::isfinite(divisor) && divisor > 0.0;
}

} // namespace

std::optional<std::array<std::array<int, 2>, 2>> planeOfSite(const Pattern &pattern)
{
    std::array<std::array<int, 2>, 2> planes{};
    std::array<int, 4> seen{};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const int plane = planeOfColour(pattern[size_t(y)][size_t(x)]);
            if (plane < 0 || seen[size_t(plane)]++)
                return std::nullopt;
            planes[size_t(y)][size_t(x)] = plane;
        }
    return planes;
}

std::optional<Planes> pack(const ipr::Image &mosaic, const Pattern &pattern, double divisor)
{
    const auto planeAt = planeOfSite(pattern);
    if (!planeAt || !usableMosaic(mosaic) || !usableDivisor(divisor))
        return std::nullopt;
    Planes planes;
    planes.width = std::size_t(mosaic.width() / 2);
    planes.height = std::size_t(mosaic.height() / 2);
    const std::size_t area = planes.width * planes.height;
    planes.values.resize(4 * area);
    const float inverse = float(1.0 / divisor);
    for (std::size_t cy = 0; cy < planes.height; ++cy)
        for (int dy = 0; dy < 2; ++dy) {
            const float *row = mosaic.row<float>(int(2 * cy) + dy);
            for (std::size_t cx = 0; cx < planes.width; ++cx)
                for (int dx = 0; dx < 2; ++dx) {
                    const int plane = (*planeAt)[size_t(dy)][size_t(dx)];
                    planes.values[std::size_t(plane) * area + cy * planes.width + cx] = row[2 * cx + std::size_t(dx)] * inverse;
                }
        }
    return planes;
}

bool unpack(const Planes &planes, ipr::Image &mosaic, const Pattern &pattern, double divisor)
{
    const auto planeAt = planeOfSite(pattern);
    if (!planeAt || !usableMosaic(mosaic) || !usableDivisor(divisor)
        || planes.width != std::size_t(mosaic.width() / 2) || planes.height != std::size_t(mosaic.height() / 2)
        || planes.values.size() != 4 * planes.width * planes.height)
        return false;
    const std::size_t area = planes.width * planes.height;
    const float scale = float(divisor);
    for (std::size_t cy = 0; cy < planes.height; ++cy)
        for (int dy = 0; dy < 2; ++dy) {
            float *row = mosaic.mutableRow<float>(int(2 * cy) + dy);
            for (std::size_t cx = 0; cx < planes.width; ++cx)
                for (int dx = 0; dx < 2; ++dx) {
                    const int plane = (*planeAt)[size_t(dy)][size_t(dx)];
                    row[2 * cx + std::size_t(dx)] = planes.values[std::size_t(plane) * area + cy * planes.width + cx] * scale;
                }
        }
    return true;
}

std::optional<NormalizedLaw> normalize(const NoiseLaw::Law &law, const Pattern &measuredPattern, double divisor)
{
    const auto planeAt = planeOfSite(measuredPattern);
    if (!planeAt || !usableDivisor(divisor))
        return std::nullopt;
    NormalizedLaw normalized;
    for (int site = 0; site < 4; ++site) {
        const NoiseLaw::PositionLaw &position = law.positions[size_t(site)];
        if (!(position.shotSlope > 0.0) || !(position.readVariance > 0.0))
            return std::nullopt;
        const int plane = (*planeAt)[size_t(site / 2)][size_t(site % 2)];
        normalized.a[size_t(plane)] = float(position.shotSlope / divisor);
        normalized.b[size_t(plane)] = float(position.readVariance / (divisor * divisor));
    }
    return normalized;
}

} // namespace CfaNoiseStage
