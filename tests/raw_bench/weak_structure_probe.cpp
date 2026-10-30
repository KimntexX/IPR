#include "CameraDenoise.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/imgproc.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}

constexpr int Extent = 256;
constexpr double Pi = 3.14159265358979323846;

cv::Mat structure(int pattern, int period, double phase, double amplitude)
{
    cv::Mat result(Extent, Extent, CV_32FC3);
    for (int y = 0; y < Extent; ++y) for (int x = 0; x < Extent; ++x) {
        const double a = 2 * Pi * x / period + phase;
        const double b = 2 * Pi * y / period + phase;
        const std::array<double, 4> values{std::sin(a), std::sin(b),
            std::sin((a + b) / std::sqrt(2.)), std::sin(a) * std::cos(b * 2. / 3.)};
        result.at<cv::Vec3f>(y, x) = cv::Vec3f::all(float(.03 + amplitude * values[std::size_t(pattern)]));
    }
    return result;
}

cv::Mat noise()
{
    cv::Mat field(Extent, Extent, CV_32F);
    cv::RNG rng(19012026);
    rng.fill(field, cv::RNG::NORMAL, 0, 1);
    const cv::Mat kernel = (cv::Mat_<float>(3, 1) << .25f, .5f, .25f);
    cv::sepFilter2D(field, field, -1, kernel, kernel, {-1, -1}, 0, cv::BORDER_REFLECT_101);
    cv::Scalar mean, sigma;
    cv::meanStdDev(field, mean, sigma);
    require(sigma[0] > 0, "Degenerate noise");
    field = (field - mean[0]) * (.002 / sigma[0]);
    cv::Mat result;
    cv::merge(std::vector<cv::Mat>{field, field, field}, result);
    return result;
}

QJsonObject metric(const cv::Mat &output, const cv::Mat &truth, int roiSize)
{
    const cv::Rect roi((truth.cols - roiSize) / 2, (truth.rows - roiSize) / 2, roiSize, roiSize);
    const auto outMean = cv::mean(output(roi)), cleanMean = cv::mean(truth(roi));
    double error = 0, numerator = 0, denominator = 0;
    for (int y = roi.y; y < roi.y + roi.height; ++y) for (int x = roi.x; x < roi.x + roi.width; ++x) {
        const auto a = output.at<cv::Vec3f>(y, x), b = truth.at<cv::Vec3f>(y, x);
        for (int c = 0; c < 3; ++c) {
            const double d = double(a[c]) - b[c], t = double(b[c]) - cleanMean[c];
            error += d * d;
            numerator += (double(a[c]) - outMean[c]) * t;
            denominator += t * t;
        }
    }
    return {{"rgb_mse", error / (3 * roi.area())},
        {"pattern_gain", denominator > 1e-15 ? QJsonValue(numerator / denominator) : QJsonValue(QJsonValue::Null)},
        {"mean_shift", outMean[0] - cleanMean[0]}};
}

QJsonObject filtered(const cv::Mat &input, const cv::Mat &truth, int amount, int roiSize = 128)
{
    CameraDenoise::Settings settings;
    require(CameraDenoise::settingsFromAmount(amount, settings), "Bad amount");
    CameraDenoise::Context context;
    QJsonArray models;
    context.noiseModelObserver = [&](const CameraDenoise::NoiseModelSummary &s) {
        QJsonArray nodes;
        for (const auto &n : s.nodes) nodes.append(QJsonObject{{"signal", n.first},
            {"sigma_I", n.second[0]}, {"sigma_Co", n.second[1]}, {"sigma_Cg", n.second[2]}});
        models.append(QJsonObject{{"level", s.level}, {"candidates", s.candidates},
            {"flat_candidates", s.flatCandidates}, {"supported", s.supported},
            {"pooled_available", s.globalFallbackAvailable}, {"dark_floor", s.darkFloorApplied},
            {"pooled_sigma_I", s.globalSigma[0]}, {"nodes", nodes}});
    };
    auto output = input;
    const auto start = std::chrono::steady_clock::now();
    require(CameraDenoise::apply(output, settings, context), "Filter failed");
    require(cv::checkRange(output), "Nonfinite output");
    if (amount == 0) require(cv::norm(output, input, cv::NORM_INF) == 0, "Zero changed source");
    auto result = metric(output, truth, roiSize);
    result.insert("master", amount);
    result.insert("models", models);
    result.insert("patch_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    cv::setNumThreads(1);
    try {
        if (app.arguments().contains("--layout-control")) {
            QJsonArray layouts;
            for (int pattern = 0; pattern < 4; ++pattern) {
                const auto original = structure(pattern, 16, 0, .0015);
                cv::Mat padded;
                cv::copyMakeBorder(original, padded, 256, 256, 256, 256, cv::BORDER_CONSTANT, cv::Scalar::all(.03));
                require(cv::norm(original(cv::Rect(15,15,226,226)),
                    padded(cv::Rect(271,271,226,226)), cv::NORM_INF) == 0, "Local signal changed");
                layouts.append(QJsonObject{{"pattern", std::array<const char *, 4>{"horizontal", "vertical", "diagonal", "product"}[std::size_t(pattern)]},
                    {"original_clean_100", filtered(original, original, 100, 96)},
                    {"flat_surround_clean_100", filtered(padded, padded, 100, 96)}});
            }
            const QJsonObject result{{"layouts", layouts}, {"roi_size", 96}, {"padding", 256},
                {"unchanged_local_margin", 80}, {"maximum_filter_margin", 65},
                {"scope", "Clean input only. Same central96 signal and surrounding80px; global estimation witnesses and tile layout change; not a new denoising algorithm or proof of one internal function alone"}};
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
            return 0;
        }
        const auto perturbation = noise();
        QJsonArray rows;
        for (int pattern = 0; pattern < 4; ++pattern) for (int period : {16, 32})
            for (double phase : {0., .73}) for (double amplitude : {.0015, .003}) {
                const auto truth = structure(pattern, period, phase, amplitude);
                const cv::Mat observed = truth + perturbation;
                rows.append(QJsonObject{{"pattern", std::array<const char *, 4>{"horizontal", "vertical", "diagonal", "product"}[std::size_t(pattern)]},
                    {"period", period}, {"phase", phase}, {"amplitude", amplitude},
                    {"clean_zero", filtered(truth, truth, 0)},
                    {"clean_25", filtered(truth, truth, 25)}, {"clean_100", filtered(truth, truth, 100)},
                    {"noisy_zero", filtered(observed, truth, 0)}, {"noisy_100", filtered(observed, truth, 100)}});
        }
        const cv::Mat flat(Extent, Extent, CV_32FC3, cv::Scalar::all(.03));
        const cv::Mat noisyFlat = flat + perturbation;
        const QJsonObject result{{"cases", rows}, {"flat_clean_100", filtered(flat, flat, 100)},
            {"flat_noisy_zero", filtered(noisyFlat, flat, 0)}, {"flat_noisy_100", filtered(noisyFlat, flat, 100)},
            {"seed", 19012026}, {"extent", Extent}, {"noise_sample_sigma", .002},
            {"noise", "Achromatic Gaussian, fixed separable binomial3 correlation, same realization per case; NOT sensor/RCD noise"},
            {"scope", "Current filter diagnostics only; full synthetic RGB, central128px ROI, no CFA, no DCP, no new protection algorithm"}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
