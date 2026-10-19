#include "Histogram.h"

#include "WorkingColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Histogram {
QVariantList compute(const QImage &preview)
{
    std::array<std::array<quint64, 256>, 4> bins{};
    const auto bin = [](float linear) {
        const float encoded = WorkingColor::srgbEncode(std::clamp(linear, 0.0f, 1.0f));
        return std::clamp(static_cast<int>(std::lround(encoded * 255.0f)), 0, 255);
    };
    const auto decode = [](uchar code) { return WorkingColor::srgbDecode(code / 255.f); };
    for (int y = 0; y < preview.height(); ++y) {
        const uchar *row = preview.constScanLine(y);
        for (int x = 0; x < preview.width(); ++x) {
            const uchar *p = row + 4 * x;
            const float luminance = .2126f * decode(p[0]) + .7152f * decode(p[1]) + .0722f * decode(p[2]);
            ++bins[0][static_cast<size_t>(bin(luminance))];
            ++bins[1][p[0]];
            ++bins[2][p[1]];
            ++bins[3][p[2]];
        }
    }
    QVariantList histogram;
    histogram.reserve(4);
    for (const auto &channel : bins) {
        QVariantList values;
        values.reserve(256);
        for (const quint64 value : channel)
            values.append(static_cast<double>(value));
        histogram.append(QVariant::fromValue(values));
    }
    return histogram;
}
}
