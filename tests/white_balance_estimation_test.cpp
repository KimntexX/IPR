#include "RawDecoder.h"

#include <QCoreApplication>

#include <iostream>

// Regression test for the "as-shot" white balance temperature estimated from
// LibRaw's cam_xyz/cam_mul data (see RawDecoder::estimateWhiteBalanceKelvin).
//
// A prior bug used cam_xyz as a camera-RGB-to-XYZ matrix (it is actually the
// inverse: XYZ-to-camera) and relied on pre_mul (a fixed per-camera-model
// daylight constant) instead of cam_mul (the actual as-shot multipliers).
// This made every photo from a given camera model report roughly the same,
// physically implausible temperature (~12000-13000 K on the Canon EOS R6
// Mark III corpus used here) regardless of the real shooting conditions,
// instead of matching the "As Shot" base temperature that Lightroom reports
// (~5200 K for this corpus, manual white balance).

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "Usage: whiteBalanceEstimationTest <image.CR3>\n";
        return 2;
    }

    const QString rawPath = QString::fromLocal8Bit(argv[1]);
    RawDecoder::SourceMetadata metadata;
    QString error;
    RawDecoder::Options options;
    options.automaticWhiteBalance = false; // use the camera's as-shot values
    const ColorPipeline::WorkingImage image =
        RawDecoder::decode(rawPath, options, &metadata, &error);
    if (!check(!image.isNull(), "the corpus RAW file should decode successfully"))
        return 1;
    if (!check(image.referenceWhiteBalanceKelvin.has_value(),
               "a source white balance temperature should be estimated"))
        return 1;

    const double kelvin = *image.referenceWhiteBalanceKelvin;
    std::cerr << "estimated source white balance: " << kelvin << " K\n";

    // Lightroom reports ~5200 K "As Shot" for this manual-white-balance
    // corpus; allow a generous margin for the McCamy cubic approximation
    // while still catching a regression back to the old ~12000+ K bug.
    if (!check(kelvin > 3500.0 && kelvin < 7000.0,
               "estimated temperature should be close to the camera's "
               "as-shot daylight white balance, not the old ~12000+ K bug"))
        return 1;

    return 0;
}
