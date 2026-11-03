#ifndef IPR_TESTS_SYNTHETIC_DCP_H
#define IPR_TESTS_SYNTHETIC_DCP_H

#include <QByteArray>
#include <QtGlobal>

#include <cstring>

// A small valid DCP for the Canon EOS R6 Mark III, to test without Adobe's profiles.
namespace SyntheticDcp {

inline void appendU16(QByteArray &bytes, quint16 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}

inline void appendU32(QByteArray &bytes, quint32 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
    bytes.append(char((value >> 16) & 0xff));
    bytes.append(char((value >> 24) & 0xff));
}

inline void appendFloat(QByteArray &bytes, float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

inline QByteArray make(bool colorMatrixOnly = false, bool dualIlluminant = false,
                   bool halfSecondMatrix = false,
                   bool differentSecondHueSat = false,
                   int hueSatEncoding = 0, int lookTableEncoding = 0,
                   float firstHueShift = 0.0f,
                   float secondHueShift = 0.0f,
                   int valueDivisions = 1, float lookValueScale = 0.8f,
                   bool withToneCurve = false, float curveMiddle = .8f,
                   float curveEnd = 1.f)
{
    const QByteArray xmp =
        QByteArrayLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
                          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
                          "<rdf:RDF><rdf:Description "
                          "crs:CameraModelRestriction=\"Canon EOS R6 Mark III\"/>"
                          "</rdf:RDF></x:xmpmeta>");

    const quint16 entryCount = (dualIlluminant ? 10 : 6) + 2 + (withToneCurve ? 1 : 0);
    constexpr quint32 ifdOffset = 8;
    const quint32 xmpOffset = ifdOffset + 2 + entryCount * 12 + 4;
    const quint32 dimensionsOffset = xmpOffset + quint32(xmp.size());
    const quint32 mapOffset = dimensionsOffset + 12;
    const quint32 secondMapOffset = mapOffset + 6 * valueDivisions * 4;
    const quint32 lookDimensionsOffset =
        secondMapOffset + (dualIlluminant ? 6 * valueDivisions * 4 : 0);
    const quint32 lookDataOffset = lookDimensionsOffset + 12;
    const quint32 matrixOffset = lookDataOffset + 6 * valueDivisions * 4;
    const quint32 secondMatrixOffset = matrixOffset + 9 * 8;
    const quint32 curveOffset = secondMatrixOffset + (dualIlluminant ? 9 * 8 : 0);

    QByteArray bytes = QByteArrayLiteral("II");
    appendU16(bytes, 0x4352);
    appendU32(bytes, ifdOffset);
    appendU16(bytes, entryCount);

    appendU16(bytes, 700);
    appendU16(bytes, 7);
    appendU32(bytes, quint32(xmp.size()));
    appendU32(bytes, xmpOffset);

    appendU16(bytes, 50937);
    appendU16(bytes, 4);
    appendU32(bytes, 3);
    appendU32(bytes, dimensionsOffset);

    appendU16(bytes, 50938);
    appendU16(bytes, 11);
    appendU32(bytes, 6 * valueDivisions);
    appendU32(bytes, mapOffset);

    if (dualIlluminant) {
        appendU16(bytes, 50939);
        appendU16(bytes, 11);
        appendU32(bytes, 6 * valueDivisions);
        appendU32(bytes, secondMapOffset);
    }
    appendU16(bytes, 50981);
    appendU16(bytes, 4);
    appendU32(bytes, 3);
    appendU32(bytes, lookDimensionsOffset);

    appendU16(bytes, 50982);
    appendU16(bytes, 11);
    appendU32(bytes, 6 * valueDivisions);
    appendU32(bytes, lookDataOffset);

    appendU16(bytes, 51107);
    appendU16(bytes, 3);
    appendU32(bytes, 1);
    appendU16(bytes, quint16(hueSatEncoding));
    appendU16(bytes, 0);
    appendU16(bytes, 51108);
    appendU16(bytes, 3);
    appendU32(bytes, 1);
    appendU16(bytes, quint16(lookTableEncoding));
    appendU16(bytes, 0);

    appendU16(bytes, colorMatrixOnly ? 50721 : 50964);
    appendU16(bytes, 10);
    appendU32(bytes, 9);
    appendU32(bytes, matrixOffset);
    if (dualIlluminant) {
        appendU16(bytes, 50778);
        appendU16(bytes, 3);
        appendU32(bytes, 1);
        appendU32(bytes, 17);
        appendU16(bytes, 50779);
        appendU16(bytes, 3);
        appendU32(bytes, 1);
        appendU32(bytes, 21);
        appendU16(bytes, 50965);
        appendU16(bytes, 10);
        appendU32(bytes, 9);
        appendU32(bytes, secondMatrixOffset);
    }
    if (withToneCurve) {
        appendU16(bytes, 50940);
        appendU16(bytes, 11);
        appendU32(bytes, 6);
        appendU32(bytes, curveOffset);
    }
    appendU32(bytes, 0);
    bytes.append(xmp);

    appendU32(bytes, 1);
    appendU32(bytes, 2);
    appendU32(bytes, valueDivisions);
    for (int i = 0; i < 2 * valueDivisions; ++i) {
        appendFloat(bytes, firstHueShift);
        appendFloat(bytes, 1.0f);
        appendFloat(bytes, 1.0f);
    }
    if (dualIlluminant) {
        for (int i = 0; i < 2 * valueDivisions; ++i) {
            appendFloat(bytes, secondHueShift);
            appendFloat(bytes, i == 0 && differentSecondHueSat ? 0.5f : 1.0f);
            appendFloat(bytes, 1.0f);
        }
    }
    appendU32(bytes, 1);
    appendU32(bytes, 2);
    appendU32(bytes, valueDivisions);
    for (int i = 0; i < 2 * valueDivisions; ++i) {
        appendFloat(bytes, 0.0f);
        appendFloat(bytes, 1.0f);
        appendFloat(bytes, lookValueScale);
    }

    for (int i = 0; i < 9; ++i) {
        const quint32 numerator = colorMatrixOnly
                                      ? quint32(i % 4 == 0 ? 1 : 0)
                                      : quint32(i % 4 == 0 ? 1 : 0);
        appendU32(bytes, numerator);
        appendU32(bytes, 1);
    }
    if (dualIlluminant) {
        for (int i = 0; i < 9; ++i) {
            appendU32(bytes, i % 4 == 0 ? (halfSecondMatrix ? 1 : 2) : 0);
            appendU32(bytes, 2);
        }
    }
    if (withToneCurve) {
        for (float value : {0.f, 0.f, .5f, curveMiddle, 1.f, curveEnd})
            appendFloat(bytes, value);
    }
    return bytes;
}

} // namespace SyntheticDcp

#endif // IPR_TESTS_SYNTHETIC_DCP_H
