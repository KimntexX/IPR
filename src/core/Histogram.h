#ifndef IPR_CORE_HISTOGRAM_H
#define IPR_CORE_HISTOGRAM_H

#include <QImage>
#include <QVariantList>

// Histogram of the displayed sRGB preview, after gamut mapping: four lists of
// 256 counts (luminance, red, green, blue). Channels count the displayed sRGB
// codes; luminance bins the linear Y of those codes in sRGB encoding. Do not
// encode Rec.2020 master channels with the sRGB transfer function instead.
namespace Histogram {
// preview: 4 bytes per pixel, RGB first (Format_RGBA8888 or RGBX8888).
QVariantList compute(const QImage &preview);
}

#endif // IPR_CORE_HISTOGRAM_H
