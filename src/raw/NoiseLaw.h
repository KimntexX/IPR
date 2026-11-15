#ifndef IPR_RAW_NOISELAW_H
#define IPR_RAW_NOISELAW_H

#include "Image.h"

#include <QByteArray>
#include <QString>
#include <array>
#include <optional>
#include <vector>

// Measured sensor noise laws: variance = shotSlope * signal + readVariance, in
// native DN, per ISO and per position of the 2x2 CFA cell in raster order.
// The catalogue (config/noise-laws.json) is generated from the measurement
// proofs by scripts/build_noise_laws.py. Free functions, no state.
namespace NoiseLaw {

struct PositionLaw
{
    double shotSlope = 0.0;     // DN of variance per DN of signal
    double readVariance = 0.0;  // DN^2
};

struct CatalogIso
{
    int iso = 0;
    // A shot slope the measurement could not fit is absent and interpolated.
    std::array<std::optional<double>, 4> shotSlope{};
    std::array<double, 4> readVariance{};
};

struct CatalogCamera
{
    QString make;
    QString model;
    std::vector<CatalogIso> isos;  // sorted by ISO
    QString proof;
};

struct Catalog
{
    std::vector<CatalogCamera> cameras;
};

enum class Source { Measured, Interpolated, Estimated };

struct Law
{
    std::array<PositionLaw, 4> positions{};
    Source source = Source::Measured;
    QString provenance;
};

// Parses and validates a catalogue; nullopt with a reason on any defect.
std::optional<Catalog> parseCatalog(const QByteArray &json, QString *reason);
std::optional<Catalog> loadCatalog(const QString &path, QString *reason);

// Law of an ISO for one camera: measured values when the ISO is in the
// catalogue, otherwise each quantity is interpolated linearly in log value
// against log ISO between the nearest ISOs that carry it. No extrapolation:
// a camera absent from the catalogue or an ISO outside its measured range
// gives nullopt and a reason.
std::optional<Law> resolve(const Catalog &catalog, const QString &make, const QString &model,
                           int iso, QString *reason);

// The law of one photo from its own pixels, for a camera the catalogue does not hold. Per site of
// the 2x2 cell in raster order: the read variance as measured on the optical black
// (readVariance, counts^2), the shot slope from the flattest 16-pixel blocks of the site's plane
// (residual to its four neighbours, robust variance, its tenth percentile per signal class
// corrected for pure noise, read variance removed, over the signal; the lower half of the
// classes, texture only adding). mosaic: U16, the visible area in counts; black per site; white
// in counts. A slope the photo cannot show (no unsaturated signal, near-black frame) is zero:
// the read noise alone. nullopt with a reason on an unusable input or an unknown read variance.
// The single-RAW study: docs/validation/analyses/analyse-restauration-neuronale.md on the
// research branch patch_noise_neural (12 and 13 November 2026).
std::optional<Law> estimate(const ipr::Image &mosaic, const std::array<double, 4> &black,
                            const std::array<double, 4> &readVariance, double white, QString *reason);

}

#endif // IPR_RAW_NOISELAW_H
