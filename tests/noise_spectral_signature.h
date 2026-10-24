#ifndef IPR_TESTS_NOISE_SPECTRAL_SIGNATURE_H
#define IPR_TESTS_NOISE_SPECTRAL_SIGNATURE_H

#include <QJsonObject>
#include <QJsonArray>
#include <opencv2/core.hpp>

namespace NoiseSpectralStudy {
// Experimental dimensionless descriptors of one finite 32x32 float plane.
// A descriptor is not a noise probability or a pixel-level classification.
QJsonObject signature(const cv::Mat &tile);
QJsonArray controls();
QJsonArray injectedControls(const cv::Mat &developedDifference);
}

#endif // IPR_TESTS_NOISE_SPECTRAL_SIGNATURE_H
