#include "NoiseLaw.h"

#include <QCoreApplication>
#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <iostream>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

const char *SampleCatalog = R"({"schema_version": 1, "cameras": [{"make": "Canon", "model": "EOS R6 Mark III",
  "provenance": {"proof": "docs/validation/raw/bank.json"}, "isos": [
  {"iso": 6400, "positions": [{"shot_slope_dn": 12, "read_variance_dn2": 500}, {"shot_slope_dn": 12, "read_variance_dn2": 500},
                              {"shot_slope_dn": 12, "read_variance_dn2": 500}, {"shot_slope_dn": 12, "read_variance_dn2": 500}]},
  {"iso": 12800, "positions": [{"shot_slope_dn": null, "read_variance_dn2": 2000}, {"shot_slope_dn": null, "read_variance_dn2": 2000},
                               {"shot_slope_dn": null, "read_variance_dn2": 2000}, {"shot_slope_dn": null, "read_variance_dn2": 2000}]},
  {"iso": 51200, "positions": [{"shot_slope_dn": 96, "read_variance_dn2": 32000}, {"shot_slope_dn": 96, "read_variance_dn2": 32000},
                               {"shot_slope_dn": 96, "read_variance_dn2": 32000}, {"shot_slope_dn": 96, "read_variance_dn2": 32000}]}]}]})";

void measuredInterpolatedAndRefused()
{
    QString reason;
    const auto catalog = NoiseLaw::parseCatalog(SampleCatalog, &reason);
    check(catalog.has_value(), "sample catalogue parses");
    if (!catalog)
        return;
    const auto measured = NoiseLaw::resolve(*catalog, QStringLiteral("canon"), QStringLiteral(" EOS R6 Mark III "), 6400, &reason);
    check(measured && measured->source == NoiseLaw::Source::Measured, "a measured ISO is reported as measured");
    check(measured && near(measured->positions[2].shotSlope, 12.0) && near(measured->positions[2].readVariance, 500.0),
          "a measured ISO returns its values");

    // 12800 has its read variance but no slope: the slope comes from 6400 and 51200 (log-log, t = 1/3).
    const auto missingSlope = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 12800, &reason);
    const double expectedSlope = std::exp((2.0 / 3.0) * std::log(12.0) + (1.0 / 3.0) * std::log(96.0));
    check(missingSlope && missingSlope->source == NoiseLaw::Source::Interpolated, "a missing slope is interpolated");
    check(missingSlope && near(missingSlope->positions[0].shotSlope, expectedSlope), "interpolation is linear in log-log");
    check(missingSlope && near(missingSlope->positions[0].readVariance, 2000.0), "the measured read variance is kept");

    const auto between = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 25600, &reason);
    check(between && near(between->positions[1].readVariance, std::exp(0.5 * std::log(2000.0) + 0.5 * std::log(32000.0))),
          "an intermediate ISO interpolates the read variance");

    check(!NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS 60D"), 6400, &reason),
          "an unknown camera is refused");
    check(NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"), 6400, &reason).has_value(),
          "an EXIF model repeating the make is recognised");
    check(!NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 3200, &reason),
          "an ISO below the measured range is refused");
    check(!NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 102400, &reason),
          "an ISO above the measured range is refused");
    check(!NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 0, &reason),
          "an unknown ISO is refused");
}

void invalidCatalogues()
{
    QString reason;
    check(!NoiseLaw::parseCatalog("not json", &reason), "malformed JSON is refused");
    check(!NoiseLaw::parseCatalog(R"({"schema_version": 2, "cameras": []})", &reason), "an unknown schema is refused");
    check(!NoiseLaw::parseCatalog(R"({"schema_version": 1, "cameras": []})", &reason), "an empty catalogue is refused");
    check(!NoiseLaw::parseCatalog(R"({"schema_version": 1, "cameras": [{"make": "Canon", "model": "X", "isos": [
        {"iso": 100, "positions": [{"shot_slope_dn": -1, "read_variance_dn2": 1}, {"shot_slope_dn": 1, "read_variance_dn2": 1},
        {"shot_slope_dn": 1, "read_variance_dn2": 1}, {"shot_slope_dn": 1, "read_variance_dn2": 1}]}]}]})", &reason),
          "a negative slope is refused");
    check(!NoiseLaw::parseCatalog(R"({"schema_version": 1, "cameras": [{"make": "Canon", "model": "X", "isos": [
        {"iso": 100, "positions": [{"shot_slope_dn": 1, "read_variance_dn2": 1}]}]}]})", &reason),
          "a missing CFA position is refused");
}

void shippedCatalogue()
{
    QString reason;
    const auto catalog = NoiseLaw::loadCatalog(QStringLiteral(":/config/noise-laws.json"), &reason);
    check(catalog.has_value(), "the bundled catalogue loads");
    if (!catalog)
        return;
    for (const int iso : {1600, 3200, 6400, 12800, 16000, 25600, 51200, 8000, 40000}) {
        const auto law = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), iso, &reason);
        check(law.has_value(), "every R6 III ISO from 100 to 51200 resolves");
        if (law)
            for (const auto &position : law->positions)
                check(position.shotSlope > 0 && position.readVariance > 0, "resolved laws are positive");
    }
    const auto low = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 3200, &reason);
    const auto high = NoiseLaw::resolve(*catalog, QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), 51200, &reason);
    check(low && high && high->positions[1].shotSlope > 10 * low->positions[1].shotSlope, "the shot slope grows with ISO");
}

} // namespace

// A mosaic of known Poisson-Gauss noise on smooth gradients: per site, variance = a * signal + b.
ipr::Image noisyMosaic(int width, int height, const std::array<double, 4> &slope, double readVariance, double black,
                       double white, double brightest, unsigned seed)
{
    ipr::Image mosaic = ipr::Image::create({width, height}, ipr::Depth::U16, 1);
    std::mt19937 random(seed);
    std::normal_distribution<double> unit(0.0, 1.0);
    for (int y = 0; y < height; ++y) {
        auto *row = mosaic.mutableRow<std::uint16_t>(y);
        for (int x = 0; x < width; ++x) {
            const int site = (y % 2) * 2 + (x % 2);
            const double signal = brightest * (0.05 + 0.95 * (0.5 * double(x) / width + 0.5 * double(y) / height));
            const double value = black + signal + std::sqrt(slope[size_t(site)] * signal + readVariance) * unit(random);
            row[x] = std::uint16_t(std::clamp(std::lround(value), 0L, std::lround(white)));
        }
    }
    return mosaic;
}

void estimatedFromOnePhoto()
{
    const std::array<double, 4> slope{6.8, 6.8, 6.7, 7.0}, black{2048, 2048, 2048, 2048};
    const std::array<double, 4> read{341, 341, 341, 341};
    const ipr::Image mosaic = noisyMosaic(1600, 1100, slope, 341, 2048, 12279, 6000, 5);
    QString reason;
    const auto law = NoiseLaw::estimate(mosaic, black, read, 12279, &reason);
    check(law.has_value(), "a law from one photo");
    if (law) {
        check(law->source == NoiseLaw::Source::Estimated, "marked as estimated");
        for (int p = 0; p < 4; ++p) {
            const double ratio = law->positions[size_t(p)].shotSlope / slope[size_t(p)];
            check(ratio > 0.9 && ratio < 1.12, "the slope of a flat noisy photo, within its estimate's spread");
            check(law->positions[size_t(p)].readVariance == read[size_t(p)], "the read variance of the optical black");
        }
    }
    // A near-black frame shows no slope: zero, the read noise alone.
    const ipr::Image dark = noisyMosaic(800, 600, {0, 0, 0, 0}, 341, 2048, 12279, 0.0, 9);
    const auto darkLaw = NoiseLaw::estimate(dark, black, read, 12279, &reason);
    check(darkLaw && darkLaw->positions[0].shotSlope == 0.0 && darkLaw->positions[3].shotSlope >= 0.0,
          "no signal: no slope, never a negative one");
    // A saturated frame has no class to measure: zero as well.
    const ipr::Image white = noisyMosaic(800, 600, slope, 341, 2048, 12279, 20000, 11);
    const auto whiteLaw = NoiseLaw::estimate(white, black, read, 12279, &reason);
    check(whiteLaw && whiteLaw->positions[1].shotSlope >= 0.0, "a saturated frame never gives a negative slope");
    // Refusals: no read noise, no mosaic, a wrong type.
    check(!NoiseLaw::estimate(mosaic, black, {0, 341, 341, 341}, 12279, &reason), "no read noise: refused");
    check(!NoiseLaw::estimate(ipr::Image(), black, read, 12279, &reason), "no mosaic: refused");
    check(!NoiseLaw::estimate(ipr::Image::create({100, 100}, ipr::Depth::F32, 1), black, read, 12279, &reason),
          "not a U16 mosaic: refused");
    check(!NoiseLaw::estimate(mosaic, black, read, 1000, &reason), "a white below the black: refused");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    measuredInterpolatedAndRefused();
    invalidCatalogues();
    shippedCatalogue();
    estimatedFromOnePhoto();
    if (!passed)
        return 1;
    std::cout << "noise law tests passed\n";
    return 0;
}
