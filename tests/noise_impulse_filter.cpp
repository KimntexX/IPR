#include "noise_impulse_filter.h"

#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace NoiseImpulseStudy {
Result correct(const cv::Mat &plane, const cv::Mat &sigma, double threshold)
{
    if (plane.empty() || plane.dims != 2 || sigma.dims != 2 || plane.type() != CV_32F || sigma.type() != CV_32F ||
        plane.size() != sigma.size() || !std::isfinite(threshold) || threshold <= 0)
        throw std::invalid_argument("Expected matching nonempty float planes and positive threshold");
    Result out{plane.clone(), cv::Mat::zeros(plane.size(), CV_8U)};
    cv::parallel_for_(cv::Range(1, std::max(1, plane.rows - 1)), [&](const cv::Range &rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            for (int x = 1; x + 1 < plane.cols; ++x) {
                const float value = plane.at<float>(y, x), s = sigma.at<float>(y, x);
                if (!std::isfinite(value) || !std::isfinite(s) || s <= 0) continue;
                const std::array<float, 8> neighbours{
                    plane.at<float>(y-1,x-1), plane.at<float>(y-1,x), plane.at<float>(y-1,x+1),
                    plane.at<float>(y,x-1), plane.at<float>(y,x+1),
                    plane.at<float>(y+1,x-1), plane.at<float>(y+1,x), plane.at<float>(y+1,x+1)};
                if (!std::all_of(neighbours.begin(), neighbours.end(), [](float v) { return std::isfinite(v); })) continue;
                auto sorted = neighbours;
                std::sort(sorted.begin(), sorted.end());
                const float median = sorted[3] * 0.5f + sorted[4] * 0.5f;
                if (std::abs(double(value) - median) <= threshold * s) continue;
                int close = 0;
                for (float v : neighbours) close += std::abs(double(v) - median) <= 3.0 * s;
                bool supported = false;
                for (int i = 0; i < 4; ++i)
                    supported |= std::abs(double(value) - neighbours[size_t(i)]) <= 2.0 * s &&
                                 std::abs(double(value) - neighbours[size_t(7-i)]) <= 2.0 * s;
                if (close < 6 || supported) continue;
                out.plane.at<float>(y,x) = median;
                out.changed.at<uchar>(y,x) = 255;
            }
        }
    });
    return out;
}

QJsonArray syntheticControls()
{
    QJsonArray records;
    for (int seed : {733, 947}) {
        for (const QString &scene : {QStringLiteral("flat"), QStringLiteral("slanted_edge"),
             QStringLiteral("thin_line"), QStringLiteral("single_pixel_point"),
             QStringLiteral("gaussian_stars"), QStringLiteral("fine_texture"), QStringLiteral("hdr_ramp")}) {
            cv::Mat clean(96,96,CV_32F);
            for (int y=0;y<96;++y) for (int x=0;x<96;++x) {
                double v=.25;
                if (scene=="slanted_edge") v=x+.3*y>60 ? .7 : .2;
                if (scene=="thin_line" && y==48) v=.8;
                if (scene=="single_pixel_point" && y==48 && x==48) v=.8;
                if (scene=="gaussian_stars") v+=.6*std::exp(-((x-48.)*(x-48.)+(y-48.)*(y-48.))/.98);
                if (scene=="fine_texture") v+=.04*std::sin(x*1.7)*std::cos(y*1.3);
                if (scene=="hdr_ramp") v=-.1+1.8*x/95.;
                clean.at<float>(y,x)=float(v);
            }
            for (const QString &noise : {QStringLiteral("clean"), QStringLiteral("gaussian"), QStringLiteral("mixed_impulse")}) {
                constexpr float sigmaValue=.01f;
                std::mt19937 random{unsigned(seed)};
                std::normal_distribution<float> gaussian(0,sigmaValue);
                std::uniform_real_distribution<float> uniform(0,1);
                cv::Mat noisy=clean.clone(), truth=cv::Mat::zeros(clean.size(),CV_8U);
                for (int y=0;y<96;++y) for (int x=0;x<96;++x) {
                    if (noise!="clean") noisy.at<float>(y,x)+=gaussian(random);
                    if (noise=="mixed_impulse" && uniform(random)<.01f) {
                        noisy.at<float>(y,x)+=uniform(random)<.5f ? -.12f : .12f;
                        truth.at<uchar>(y,x)=255;
                    }
                }
                const cv::Mat sigma(clean.size(),CV_32F,cv::Scalar(sigmaValue));
                const cv::Rect inner(4,4,88,88);
                const int actual=cv::countNonZero(truth(inner));
                const double baseline=cv::norm(noisy(inner),clean(inner),cv::NORM_L2SQR)/inner.area();
                for (double threshold : {4.,6.,8.}) {
                    const Result out=correct(noisy,sigma,threshold);
                    cv::Mat hits;
                    cv::bitwise_and(out.changed(inner),truth(inner),hits);
                    const int detected=cv::countNonZero(hits), changed=cv::countNonZero(out.changed(inner));
                    records.append(QJsonObject{{"seed",seed},{"scene",scene},{"noise",noise},{"threshold",threshold},
                        {"sigma",double(sigmaValue)},{"pixels",inner.area()},{"injected",actual},
                        {"detected_injected",detected},{"changed_outside_injection",changed-detected},
                        {"baseline_mse",baseline},
                        {"corrected_mse",cv::norm(out.plane(inner),clean(inner),cv::NORM_L2SQR)/inner.area()},
                        {"centre_before",double(noisy.at<float>(48,48))},{"centre_after",double(out.plane.at<float>(48,48))}});
                }
            }
        }
    }
    return records;
}
}
