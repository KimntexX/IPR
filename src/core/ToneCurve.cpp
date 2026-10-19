#include "ToneCurve.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ToneCurve {
namespace {
float evaluate(const QVector<Point> &curve, float value)
{
    if (curve.isEmpty())
        return value;

    std::vector<double> weights(static_cast<size_t>(curve.size()), 1.0);
    for (qsizetype index = 0; index < curve.size(); ++index) {
        for (qsizetype other = 0; other < curve.size(); ++other) {
            if (index != other) {
                weights[static_cast<size_t>(index)] /=
                    static_cast<double>(curve.at(index).x - curve.at(other).x);
            }
        }
    }

    const auto evaluateInsideRange = [&curve, &weights](double sample) {
        double numerator = 0.0;
        double denominator = 0.0;
        for (qsizetype index = 0; index < curve.size(); ++index) {
            const double distance = sample - curve.at(index).x;
            if (std::abs(distance) < 1e-9)
                return static_cast<double>(curve.at(index).y);
            const double term = weights[static_cast<size_t>(index)] / distance;
            numerator += term * curve.at(index).y;
            denominator += term;
        }
        return numerator / denominator;
    };

    if (value <= 1.0f)
        return static_cast<float>(evaluateInsideRange(value));

    const qsizetype endpoint = curve.size() - 1;
    double derivative = 0.0;
    for (qsizetype index = 0; index < endpoint; ++index) {
        derivative +=
            weights[static_cast<size_t>(index)] / weights[static_cast<size_t>(endpoint)] *
            (curve.at(index).y - curve.at(endpoint).y) /
            (curve.at(endpoint).x - curve.at(index).x);
    }
    return curve.constLast().y + (value - curve.constLast().x) * static_cast<float>(derivative);
}
}

bool isIdentity(const Curves &curves)
{
    return std::all_of(curves.cbegin(), curves.cend(), [](const QVector<Point> &curve) { return curve.isEmpty(); });
}

void apply(cv::Mat &linearRgb, const Curves &curves)
{
    if (isIdentity(curves)) return;
    const QVector<Point> &luminanceCurve = curves[Luminance];
    const QVector<Point> &redCurve = curves[Red];
    const QVector<Point> &greenCurve = curves[Green];
    const QVector<Point> &blueCurve = curves[Blue];
    for (int y = 0; y < linearRgb.rows; ++y) {
        cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < linearRgb.cols; ++x) {
            const float luminance = WorkingColor::nonNegativeLuminance(row[x]);
            WorkingColor::scaleToLuminance(row[x], luminance, evaluate(luminanceCurve, luminance));
            row[x][0] = evaluate(redCurve, row[x][0]);
            row[x][1] = evaluate(greenCurve, row[x][1]);
            row[x][2] = evaluate(blueCurve, row[x][2]);
        }
    }
}
}
