#ifndef IPR_TESTS_NOISE_IMPULSE_FILTER_H
#define IPR_TESTS_NOISE_IMPULSE_FILTER_H

#include <opencv2/core.hpp>
#include <QJsonArray>

namespace NoiseImpulseStudy {
struct Result { cv::Mat plane, changed; };
Result correct(const cv::Mat &plane, const cv::Mat &sigma, double threshold);
QJsonArray syntheticControls();
}

#endif // IPR_TESTS_NOISE_IMPULSE_FILTER_H
