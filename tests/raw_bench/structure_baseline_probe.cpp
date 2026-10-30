#include "CameraDenoise.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}

cv::Mat decode(const QJsonValue &value)
{
    const unsigned short one = 1;
    require(*reinterpret_cast<const unsigned char *>(&one) == 1, "Little-endian input required");
    const auto data = QByteArray::fromBase64(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    require(data.size() == 256*256*3*4, "Invalid float32 RGB input size");
    cv::Mat result(256, 256, CV_32FC3);
    std::memcpy(result.data, data.constData(), std::size_t(data.size()));
    require(cv::checkRange(result), "Nonfinite input");
    return result;
}

QJsonObject run(const cv::Mat &source, const cv::Mat &truth)
{
    CameraDenoise::Context context;
    QJsonArray models;
    context.noiseModelObserver = [&](const CameraDenoise::NoiseModelSummary &s) {
        QJsonArray nodes;
        for (const auto &n : s.nodes) nodes.append(QJsonObject{{"signal", n.first}, {"sigma_I", n.second[0]}});
        models.append(QJsonObject{{"level", s.level}, {"pooled_sigma_I", s.globalSigma[0]},
            {"pooled_available", s.globalFallbackAvailable}, {"supported", s.supported},
            {"candidates", s.candidates}, {"flat_candidates", s.flatCandidates}, {"nodes", nodes}});
    };
    auto output = source;
    require(CameraDenoise::apply(output, {100,100}, context), "Current filter failed");
    require(cv::checkRange(output), "Nonfinite output");
    const cv::Rect roi(144,144,96,96);
    const auto om = cv::mean(output(roi)), tm = cv::mean(truth(roi));
    double mse = 0, projection = 0, power = 0;
    for (int y = 144; y < 240; ++y) for (int x = 144; x < 240; ++x) {
        const auto a = output.at<cv::Vec3f>(y,x), b = truth.at<cv::Vec3f>(y,x);
        for (int c = 0; c < 3; ++c) {
            const double d = double(a[c])-b[c], t = double(b[c])-tm[c];
            mse += d*d; power += t*t; projection += (double(a[c])-om[c])*t;
        }
    }
    return {{"models", models}, {"rgb_mse", mse/(3*roi.area())},
        {"pattern_gain", power > 1e-20 ? QJsonValue(projection/power) : QJsonValue(QJsonValue::Null)}};
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    cv::setNumThreads(1);
    try {
        std::string line;
        require(bool(std::getline(std::cin, line)), "Missing input JSON");
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(QByteArray::fromStdString(line), &error);
        require(error.error == QJsonParseError::NoError && parsed.isObject(), "Invalid JSON");
        const auto observed = decode(parsed.object().value("observed"));
        const auto truth = decode(parsed.object().value("truth"));
        const QJsonObject result{{"noisy_100", run(observed, truth)}, {"clean_100", run(truth, truth)},
            {"settings", QJsonArray{100,100}}, {"roi_yx_size", QJsonArray{144,144,96}},
            {"workers", CameraDenoise::workerThreads()}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
