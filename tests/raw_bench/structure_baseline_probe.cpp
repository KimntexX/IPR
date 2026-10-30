#include "CameraDenoise.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <algorithm>
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
    cv::Vec3d opponentError(0,0,0), opponentBias(0,0,0);
    for (int y = 144; y < 240; ++y) for (int x = 144; x < 240; ++x) {
        const auto a = output.at<cv::Vec3f>(y,x), b = truth.at<cv::Vec3f>(y,x);
        const cv::Vec3d delta(double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]);
        const cv::Vec3d opposed(.25*delta[0]+.5*delta[1]+.25*delta[2],
            .5*delta[0]-.5*delta[2],delta[1]-.5*delta[0]-.5*delta[2]);
        for (int c=0;c<3;++c) {
            opponentError[c]+=opposed[c]*opposed[c];
            opponentBias[c]+=opposed[c];
        }
        for (int c = 0; c < 3; ++c) {
            const double d = double(a[c])-b[c], t = double(b[c])-tm[c];
            mse += d*d; power += t*t; projection += (double(a[c])-om[c])*t;
        }
    }
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(output.data), int(output.total()*output.elemSize()));
    return {{"models", models}, {"rgb_mse", mse/(3*roi.area())},
        {"opponent_mse",QJsonArray{opponentError[0]/roi.area(),opponentError[1]/roi.area(),opponentError[2]/roi.area()}},
        {"opponent_bias",QJsonArray{opponentBias[0]/roi.area(),opponentBias[1]/roi.area(),opponentBias[2]/roi.area()}},
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
        if (parsed.object().contains("read_reference")) {
            const auto readReference=decode(parsed.object().value("read_reference"));
            IprNoiseBench::operation(Operation::CapturePooled);
            result.insert("read_model_capture",run(readReference,flat));
            IprNoiseBench::operation(Operation::FrozenPooled);
            result.insert("read_pooled_noisy",run(observed,truth));
            result.insert("read_pooled_clean_stress",run(truth,truth));
        }
        if (parsed.object().contains("covariance_profile")) {
            const auto values=parsed.object().value("covariance_profile").toArray();
            require(values.size()==5,"Expected five covariance levels");
            IprNoiseBench::Covariances external{};
            for(int level=0;level<5;++level) {
                const auto rows=values[level].toArray();
                require(rows.size()==3,"Expected covariance rows");
                cv::Matx33f covariance;
                for(int y=0;y<3;++y) {
                    const auto row=rows[y].toArray();
                    require(row.size()==3,"Expected covariance columns");
                    for(int x=0;x<3;++x) {
                        require(row[x].isDouble()&&std::isfinite(row[x].toDouble()),"Nonfinite covariance");
                        covariance(y,x)=float(row[x].toDouble());
                        external[std::size_t(level)][std::size_t(3*y+x)]=covariance(y,x);
                    }
                }
                require(cv::checkRange(cv::Mat(covariance)),"Invalid float covariance");
                require(cv::norm(cv::Mat(covariance-covariance.t()),cv::NORM_INF)<1e-12,"Asymmetric covariance");
                cv::Mat eigenvalues;
                require(cv::eigen(cv::Mat(covariance),eigenvalues),"Covariance eigen failure");
                const double scale=std::max(1e-30,double(cv::norm(cv::Mat(covariance))));
                require(eigenvalues.at<float>(2)>=-1e-5*scale,"Non-PSD covariance");
            }
            IprNoiseBench::covariances(external);
            IprNoiseBench::operation(Operation::ExternalCovariance);
            result.insert("external_covariance_noisy",run(observed,truth));
            result.insert("external_covariance_clean_stress",run(truth,truth));
        }
        IprNoiseBench::operation(Operation::Zero);
        result.insert("zero_model_clean", run(truth,truth));
        IprNoiseBench::operation(Operation::Bypass);
        result.insert("observer_note", "models are observed BEFORE override; frozen model actually applied is the pooled state from corresponding capture");
#endif
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
