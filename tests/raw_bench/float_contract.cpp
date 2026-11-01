#include "ColorPipeline.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) { std::cerr << "Need output JSON path\n"; return 2; }
    QJsonArray rows;
    bool passed = true;
    const auto record = [&](const char *name, bool ok, const QJsonObject &details) {
        QJsonObject item = details; item.insert("case", name); item.insert("pass", ok);
        rows.append(item); passed = passed && ok;
    };
    {
        cv::Mat input(2, 2, CV_32FC3, cv::Scalar(-0.2f, 0.1f, 1.4f));
        const cv::Mat original = input.clone();
        QString error;
        const auto image = ColorPipeline::fromLinearSrgb(input, &error);
        const bool valid = !image.isNull() && image.linearRgba.type() == CV_32FC4 &&
                           cv::checkRange(image.linearRgba, true);
        bool extended = false;
        if (valid) {
            const auto p = image.linearRgba.at<cv::Vec4f>(0, 0);
            extended = (p[0] < 0 || p[1] < 0 || p[2] < 0) &&
                       (p[0] > 1 || p[1] > 1 || p[2] > 1) && p[3] == 1.f;
        }
        const bool unchanged = cv::norm(original, input, cv::NORM_INF) == 0;
        record("finite_negative_and_above_one", valid && extended && unchanged && error.isEmpty(),
               QJsonObject{{"finite_output", valid}, {"extended_range", extended}, {"input_unchanged", unchanged}, {"error", error}});
    }
    for (int kind = 0; kind < 3; ++kind) {
        cv::Mat input(2, 2, CV_32FC3, cv::Scalar(0.1f, 0.2f, 0.3f));
        input.at<cv::Vec3f>(0, 0)[0] = kind == 0 ? std::numeric_limits<float>::quiet_NaN()
            : (kind == 1 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
        QString error;
        const auto image = ColorPipeline::fromLinearSrgb(input, &error);
        const bool empty = image.isNull();
        const bool nonfinite = !empty && !cv::checkRange(image.linearRgba, true);
        record(kind == 0 ? "reject_NaN" : (kind == 1 ? "reject_positive_infinity" : "reject_negative_infinity"),
               empty && !error.isEmpty(), QJsonObject{{"empty_output", empty}, {"returned_nonfinite_image", nonfinite}, {"error", error}});
    }
    QJsonObject result{{"schema_version", 1}, {"engine_revision", IPR_ENGINE_REVISION},
        {"measured_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"scope", "Public float input boundary only; valid extended signal and malformed NaN/Inf, not visual DCP behavior"},
        {"cases", rows}, {"verdict", passed ? "pass_float_contract" : "fail_float_contract"}};
    QFile output(QString::fromLocal8Bit(argv[1]));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { std::cerr << "Output exists or inaccessible\n"; return 2; }
    const auto bytes = QJsonDocument(result).toJson();
    if (output.write(bytes) != bytes.size()) return 2;
    std::cout << (passed ? "pass_float_contract" : "fail_float_contract") << '\n';
    return passed ? 0 : 1;
}
