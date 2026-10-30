#include "CameraDenoise.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef IPR_BENCH_ORACLE
#include "noise_bench_control.h"
#endif

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
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(output.data), int(output.total()*output.elemSize()));
    return {{"models", models}, {"rgb_mse", mse/(3*roi.area())},
        {"pixels_sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
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
        QJsonObject result{{"noisy_100", run(observed, truth)}, {"clean_100", run(truth, truth)},
            {"settings", QJsonArray{100,100}}, {"roi_yx_size", QJsonArray{144,144,96}},
            {"workers", CameraDenoise::workerThreads()}};
#ifdef IPR_BENCH_ORACLE
        const auto reference = decode(parsed.object().value("reference"));
        const cv::Mat flat(256,256,CV_32FC3,cv::Scalar::all(.03));
        const cv::Mat noiseOnly = observed-truth+flat;
        using IprNoiseBench::Operation;
        IprNoiseBench::operation(Operation::PooledBlind);
        result.insert("blind_pooled_noisy", run(observed,truth));
        result.insert("blind_pooled_clean", run(truth,truth));
        IprNoiseBench::operation(Operation::CapturePooled);
        result.insert("same_noise_model_capture", run(noiseOnly,flat));
        IprNoiseBench::operation(Operation::FrozenPooled);
        result.insert("same_noise_pooled_noisy", run(observed,truth));
        result.insert("same_noise_pooled_clean_stress", run(truth,truth));
        IprNoiseBench::operation(Operation::CapturePooled);
        result.insert("independent_noise_model_capture", run(reference,flat));
        IprNoiseBench::operation(Operation::FrozenPooled);
        result.insert("independent_noise_pooled_noisy", run(observed,truth));
        result.insert("independent_noise_pooled_clean_stress", run(truth,truth));
        IprNoiseBench::operation(Operation::Zero);
        result.insert("zero_model_clean", run(truth,truth));
        IprNoiseBench::operation(Operation::Bypass);
        result.insert("observer_note", "models are observed BEFORE override; frozen model actually applied is the pooled state from corresponding capture");
#endif
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
