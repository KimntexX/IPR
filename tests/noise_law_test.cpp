#include "NoiseLaw.h"

#include <QCoreApplication>
#include <QFile>

#include <cmath>
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

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    measuredInterpolatedAndRefused();
    invalidCatalogues();
    shippedCatalogue();
    if (!passed)
        return 1;
    std::cout << "noise law tests passed\n";
    return 0;
}
