#include "Histogram.h"

#include "WorkingColorSpace.h"

#include <opencv2/core.hpp>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <cmath>

namespace Histogram {
namespace {
int bin(float linear)
{
    const float encoded = WorkingColor::srgbEncode(std::clamp(linear, 0.0f, 1.0f));
    return std::clamp(static_cast<int>(std::lround(encoded * 255.0f)), 0, 255);
}

std::array<float, 256> decodingTable()
{
    std::array<float, 256> table{};
    for (int code = 0; code < 256; ++code)
        table[size_t(code)] = WorkingColor::srgbDecode(code / 255.f);
    return table;
}
}

QVariantList compute(const QImage &preview)
{
    using Bins = std::array<std::array<quint64, 256>, 4>;
    Bins bins{};
    QMutex merging;
    static const std::array<float, 256> decode = decodingTable();
    // Counts add up in any order: the same result on any number of threads.
    cv::parallel_for_(cv::Range(0, preview.height()), [&](const cv::Range &rows) {
        Bins local{};
        for (int y = rows.start; y < rows.end; ++y) {
            const uchar *row = preview.constScanLine(y);
            for (int x = 0; x < preview.width(); ++x) {
                const uchar *p = row + 4 * x;
                if (p[3] == 0)   // The empty zones of a transformed image have no tone.
                    continue;
                const float luminance = .2126f * decode[p[0]] + .7152f * decode[p[1]] + .0722f * decode[p[2]];
                ++local[0][static_cast<size_t>(bin(luminance))];
                ++local[1][p[0]];
                ++local[2][p[1]];
                ++local[3][p[2]];
            }
        }
        const QMutexLocker lock(&merging);
        for (size_t channel = 0; channel < bins.size(); ++channel)
            for (size_t value = 0; value < 256; ++value)
                bins[channel][value] += local[channel][value];
    });
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
