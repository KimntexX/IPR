#include "CameraProfileEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

void appendU16(QByteArray &bytes, quint16 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
}

void appendU32(QByteArray &bytes, quint32 value)
{
    bytes.append(char(value & 0xff));
    bytes.append(char((value >> 8) & 0xff));
    bytes.append(char((value >> 16) & 0xff));
    bytes.append(char((value >> 24) & 0xff));
}

void appendFloat(QByteArray &bytes, float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

QByteArray makeDcp(bool colorMatrixOnly = false, bool dualIlluminant = false,
                   bool halfSecondMatrix = false,
                   bool differentSecondHueSat = false,
                   int hueSatEncoding = 0, int lookTableEncoding = 0,
                   float firstHueShift = 0.0f,
                   float secondHueShift = 0.0f)
{
    const QByteArray xmp =
        QByteArrayLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" "
                          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
                          "<rdf:RDF><rdf:Description "
                          "crs:CameraModelRestriction=\"Canon EOS R6 Mark III\"/>"
                          "</rdf:RDF></x:xmpmeta>");

    const quint16 entryCount = (dualIlluminant ? 10 : 6) + 2;
    constexpr quint32 ifdOffset = 8;
    const quint32 xmpOffset = ifdOffset + 2 + entryCount * 12 + 4;
    const quint32 dimensionsOffset = xmpOffset + quint32(xmp.size());
    const quint32 mapOffset = dimensionsOffset + 12;
    const quint32 secondMapOffset = mapOffset + 6 * 4;
    const quint32 lookDimensionsOffset =
        secondMapOffset + (dualIlluminant ? 6 * 4 : 0);
    const quint32 lookDataOffset = lookDimensionsOffset + 12;
    const quint32 matrixOffset = lookDataOffset + 6 * 4;
    const quint32 secondMatrixOffset = matrixOffset + 9 * 8;

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
    appendU32(bytes, 6);
    appendU32(bytes, mapOffset);

    if (dualIlluminant) {
        appendU16(bytes, 50939);
        appendU16(bytes, 11);
        appendU32(bytes, 6);
        appendU32(bytes, secondMapOffset);
    }
    appendU16(bytes, 50981);
    appendU16(bytes, 4);
    appendU32(bytes, 3);
    appendU32(bytes, lookDimensionsOffset);

    appendU16(bytes, 50982);
    appendU16(bytes, 11);
    appendU32(bytes, 6);
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
    appendU32(bytes, 0);
    bytes.append(xmp);

    appendU32(bytes, 1);
    appendU32(bytes, 2);
    appendU32(bytes, 1);
    for (int i = 0; i < 2; ++i) {
        appendFloat(bytes, firstHueShift);
        appendFloat(bytes, 1.0f);
        appendFloat(bytes, 1.0f);
    }
    if (dualIlluminant) {
        for (int i = 0; i < 2; ++i) {
            appendFloat(bytes, secondHueShift);
            appendFloat(bytes, i == 0 && differentSecondHueSat ? 0.5f : 1.0f);
            appendFloat(bytes, 1.0f);
        }
    }
    appendU32(bytes, 1);
    appendU32(bytes, 2);
    appendU32(bytes, 1);
    for (int i = 0; i < 2; ++i) {
        appendFloat(bytes, 0.0f);
        appendFloat(bytes, 1.0f);
        appendFloat(bytes, 0.8f);
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
    return bytes;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(contents) == contents.size();
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "temporary profile directory should be available"))
        return 1;

    const QString profileRoot = QDir(temporary.path()).filePath("profiles");
    const QString dcpDirectory = QDir(profileRoot).filePath("dcp");
    const QString lcpDirectory = QDir(profileRoot).filePath("lcp");
    if (!QDir().mkpath(dcpDirectory) || !QDir().mkpath(lcpDirectory))
        return 1;

    const QString dcpPath = QDir(dcpDirectory).filePath("R6.dcp");
    if (!check(writeFile(dcpPath, makeDcp()), "synthetic DCP should be written"))
        return 1;

    const QString lcpPath = QDir(lcpDirectory).filePath("RF24-70.lcp");
    const QByteArray lcp =
        QByteArrayLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
                          "<rdf:Description crs:CameraModelRestriction=\"Canon EOS R6 Mark III\" "
                          "crs:Lens=\"Canon RF 24-70mm F2.8 L IS USM\">"
                          "<crs:PerspectiveModel crs:FocalLength=\"24\" "
                          "crs:FocusDistance=\"2\" crs:ApertureValue=\"4\">"
                          "<crs:GeometryModel crs:RadialDistortParam1=\"0.25\" "
                          "crs:RadialDistortParam2=\"0\" crs:RadialDistortParam3=\"0\"/>"
                          "<crs:VignetteModel crs:VignetteModelParam1=\"-2\" "
                          "crs:VignetteModelParam2=\"0\" crs:VignetteModelParam3=\"0\"/>"
                          "<crs:ChromaticRedGreenModel crs:RadialDistortParam1=\"0.5\"/>"
                          "<crs:ChromaticBlueGreenModel crs:RadialDistortParam1=\"-0.5\"/>"
                          "</crs:PerspectiveModel>"
                          "<crs:PerspectiveModel crs:FocalLength=\"70\" "
                          "crs:FocusDistance=\"2\" crs:ApertureValue=\"4\">"
                          "<crs:GeometryModel crs:RadialDistortParam1=\"0.35\"/>"
                          "<crs:VignetteModel crs:VignetteModelParam1=\"-2\"/>"
                          "</crs:PerspectiveModel>"
                          "</rdf:Description></rdf:RDF>");
    if (!check(writeFile(lcpPath, lcp), "synthetic LCP should be written"))
        return 1;

    const CameraProfileEngine::Selection selection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"), QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"),
            {profileRoot});
    if (!check(selection.dcpPath == dcpPath && selection.lcpPath == lcpPath,
               "profiles should be selected by camera and lens restrictions"))
        return 1;

    const QString manifestDcpPath = QDir(dcpDirectory).filePath("selected.dcp");
    const QString manifestLcpPath = QDir(lcpDirectory).filePath("selected.lcp");
    if (!check(writeFile(manifestDcpPath, makeDcp()) &&
                   writeFile(manifestLcpPath, lcp),
               "profiles selected by the manifest should be written"))
        return 1;
    const QByteArray manifest = QByteArrayLiteral(
        "{\"version\":1,\"entries\":[{"
        "\"cameraMake\":\"Canon\","
        "\"cameraModel\":\"Canon EOS R6 Mark III\","
        "\"lensMake\":\"Canon\","
        "\"lensModel\":\"Canon RF 24-70mm F2.8 L IS USM\","
        "\"dcp\":\"dcp/selected.dcp\","
        "\"lcp\":\"lcp/selected.lcp\"}]}");
    if (!check(writeFile(QDir(profileRoot).filePath("index.json"), manifest),
               "profile manifest should be written"))
        return 1;
    const CameraProfileEngine::Selection manifestSelection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"), QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"),
            {profileRoot});
    if (!check(manifestSelection.dcpPath == manifestDcpPath &&
                   manifestSelection.lcpPath == manifestLcpPath,
               "manifest should bypass the recursive profile search"))
        return 1;
    const CameraProfileEngine::Selection directDirectories =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"), QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"),
            {dcpDirectory, lcpDirectory});
    if (!check(directDirectories.dcpPath == dcpPath &&
                   directDirectories.lcpPath == lcpPath,
               "profiles should be discoverable from direct DCP and LCP directories"))
        return 1;
    qputenv("IPR_ADOBE_CAMERARAW_DIR", profileRoot.toLocal8Bit());
    const CameraProfileEngine::Selection environmentProfiles =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"), QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"));
    qunsetenv("IPR_ADOBE_CAMERARAW_DIR");
    if (!check(environmentProfiles.dcpPath == manifestDcpPath &&
                   environmentProfiles.lcpPath == manifestLcpPath,
               "default profile lookup should honor the manifest in the CameraRaw root override"))
        return 1;
    const QString lensOnlyDirectory =
        QDir(temporary.path()).filePath("AdobeLensProfiles");
    if (!QDir().mkpath(lensOnlyDirectory))
        return 1;
    const QString lensOnlyPath =
        QDir(lensOnlyDirectory).filePath("lens-only.lcp");
    const QByteArray lensOnlyLcp =
        QByteArrayLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:stCamera=\"http://ns.adobe.com/photoshop/1.0/camera-raw-settings/\">"
                          "<rdf:Description stCamera:LensPrettyName=\"Canon RF 24-70mm F2.8 L IS USM\">"
                          "<stCamera:PerspectiveModel stCamera:FocalLength=\"24\">"
                          "<stCamera:GeometryModel stCamera:RadialDistortParam1=\"0.1\"/>"
                          "</stCamera:PerspectiveModel>"
                          "</rdf:Description></rdf:RDF>");
    if (!check(writeFile(lensOnlyPath, lensOnlyLcp),
               "lens-only Adobe-style LCP should be written"))
        return 1;
    const CameraProfileEngine::Selection lensOnlySelection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"), QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"),
            {lensOnlyDirectory});
    if (!check(lensOnlySelection.lcpPath == lensOnlyPath,
               "an Adobe lens profile without a camera restriction should match by lens"))
        return 1;
    const QString lensAliasPath =
        QDir(lensOnlyDirectory).filePath("lens-alias.lcp");
    const QByteArray lensAliasLcp =
        QByteArrayLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:stCamera=\"http://ns.adobe.com/photoshop/1.0/camera-raw-settings/\">"
                          "<rdf:Description>"
                          "<stCamera:CameraProfile stCamera:Lens=\"70-200mm F2.8 DG OS HSM | Sports 018\" "
                          "stCamera:LensPrettyName=\"SIGMA 70-200mm F2.8 DG OS HSM S018\"/>"
                          "<stCamera:PerspectiveModel stCamera:FocalLength=\"135\">"
                          "<stCamera:GeometryModel stCamera:RadialDistortParam1=\"0.1\"/>"
                          "</stCamera:PerspectiveModel>"
                          "</rdf:Description></rdf:RDF>");
    if (!check(writeFile(lensAliasPath, lensAliasLcp),
               "Adobe-style lens aliases should be written"))
        return 1;
    const CameraProfileEngine::Selection lensAliasSelection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"), {},
            QStringLiteral("70-200mm F2.8 DG OS HSM | Sports 018"),
            {lensOnlyDirectory});
    if (!check(lensAliasSelection.lcpPath == lensAliasPath,
               "Adobe LCP lens metadata alias should match the RAW lens string"))
        return 1;
    const QString lensScoringDirectory =
        QDir(temporary.path()).filePath("LensScoring");
    if (!QDir().mkpath(lensScoringDirectory))
        return 1;
    const QString genericZoomPath =
        QDir(lensScoringDirectory).filePath("000-generic-zoom.lcp");
    QByteArray genericZoomLcp = lcp;
    genericZoomLcp.replace(
        QByteArrayLiteral("Canon RF 24-70mm F2.8 L IS USM"),
        QByteArrayLiteral("24-70mm"));
    if (!check(writeFile(genericZoomPath, genericZoomLcp),
               "generic focal-range LCP should be written"))
        return 1;
    const QString exactZoomPath =
        QDir(lensScoringDirectory).filePath("target-lens.lcp");
    QByteArray exactZoomLcp = lcp;
    exactZoomLcp.replace(QByteArrayLiteral("RadialDistortParam3=\"0\""),
                         QByteArrayLiteral("RadialDistortParam3=\"-163.951\""));
    if (!check(writeFile(exactZoomPath, exactZoomLcp),
               "specific high-order LCP should be written"))
        return 1;
    const CameraProfileEngine::Selection lensScoringSelection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("Canon EOS R6 Mark III"),
            QStringLiteral("Canon"),
            QStringLiteral("Canon RF 24-70mm F2.8 L IS USM"),
            {lensScoringDirectory});
    if (!check(lensScoringSelection.lcpPath == exactZoomPath,
               "a specific LCP should outrank a generic focal-range match"))
        return 1;
    const CameraProfileEngine::Selection noMatch =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Nikon"), QStringLiteral("Nikon Z 8"),
            QStringLiteral("Nikon"), QStringLiteral("NIKKOR Z 24-70mm"),
            {profileRoot});
    if (!check(noMatch.dcpPath.isEmpty() && noMatch.lcpPath.isEmpty(),
               "profiles should not be selected for a different camera or lens"))
        return 1;
    const QString adobeDcpDirectory =
        QDir(temporary.path()).filePath("AdobeCameraProfiles");
    if (!QDir().mkpath(adobeDcpDirectory))
        return 1;
    QByteArray adobeDcp = makeDcp();
    const QByteArray restriction =
        QByteArrayLiteral("crs:CameraModelRestriction=\"Canon EOS R6 Mark III\"");
    adobeDcp.replace(restriction, QByteArray(restriction.size(), ' '));
    const QString adobeDcpPath =
        QDir(adobeDcpDirectory).filePath("Canon EOS R6 Mark III Adobe Standard.dcp");
    if (!check(writeFile(adobeDcpPath, adobeDcp),
               "Adobe-style unrestricted DCP should be written"))
        return 1;
    const CameraProfileEngine::Selection adobeDcpSelection =
        CameraProfileEngine::findProfiles(
            QStringLiteral("Canon"), QStringLiteral("EOS R6 Mark III"),
            {}, {}, {adobeDcpDirectory});
    if (!check(adobeDcpSelection.dcpPath == adobeDcpPath,
               "Adobe DCP without a camera restriction should match its camera-prefixed filename"))
        return 1;

    cv::Mat cameraRgb(2, 2, CV_32FC3, cv::Scalar(0.96422f, 1.0f, 0.82521f));
    QString error;
    if (!check(CameraProfileEngine::applyDcp(dcpPath, cameraRgb, 5500.0, &error),
               "DCP matrix should apply to linear camera RGB"))
        return 1;
    if (!check(cameraRgb.type() == CV_32FC3 &&
                   cv::norm(cameraRgb.at<cv::Vec3f>(0, 0) -
                            cv::Vec3f(0.8f, 0.8f, 0.8f)) < 0.01,
               "DCP ForwardMatrix and look table should transform D50 white"))
        return 1;

    const QString srgbTableDcp =
        QDir(dcpDirectory).filePath("R6-srgb-tables.dcp");
    if (!check(writeFile(srgbTableDcp, makeDcp(false, false, false, false, 1, 1)),
               "sRGB-encoded DCP tables should be written"))
        return 1;
    cv::Mat linearTableInput(1, 1, CV_32FC3, cv::Scalar(0.45f, 0.25f, 0.1f));
    cv::Mat srgbTableInput = linearTableInput.clone();
    if (!check(CameraProfileEngine::applyDcp(dcpPath, linearTableInput, 5500.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(srgbTableDcp, srgbTableInput,
                                                 5500.0, &error) &&
                   cv::norm(linearTableInput - srgbTableInput) > 0.01,
               "DCP sRGB table encoding should use sRGB transfer functions"))
        return 1;

    const QString colorOnlyDcp =
        QDir(dcpDirectory).filePath("R6-color-matrix.dcp");
    if (!check(writeFile(colorOnlyDcp, makeDcp(true)),
               "ColorMatrix-only DCP should be written"))
        return 1;
    cv::Mat colorOnlyRgb(1, 1, CV_32FC3, cv::Scalar(1.0f, 1.0f, 1.0f));
    if (!check(CameraProfileEngine::applyDcp(colorOnlyDcp, colorOnlyRgb, 5500.0,
                                             &error) &&
                   cv::norm(colorOnlyRgb.at<cv::Vec3f>(0, 0) -
                            cv::Vec3f(0.8f, 0.8f, 0.8f)) < 0.01,
               "DCP without ForwardMatrix should adapt ColorMatrix through D50"))
        return 1;

    const QString dualDcp = QDir(dcpDirectory).filePath("R6-dual.dcp");
    if (!check(writeFile(dualDcp, makeDcp(false, true, true, false)),
               "dual-illuminant DCP should be written"))
        return 1;
    cv::Mat warmRgb(1, 1, CV_32FC3, cv::Scalar(0.8f, 0.4f, 0.2f));
    cv::Mat daylightRgb = warmRgb.clone();
    if (!check(CameraProfileEngine::applyDcp(dualDcp, warmRgb, 2856.0, &error) &&
                   CameraProfileEngine::applyDcp(dualDcp, daylightRgb, 6500.0,
                                                 &error) &&
                   cv::norm(warmRgb - daylightRgb) > 0.1,
               "DCP matrices should interpolate according to the capture illuminant"))
        return 1;

    const QString dualMapDcp = QDir(dcpDirectory).filePath("R6-dual-map.dcp");
    if (!check(writeFile(dualMapDcp, makeDcp(false, true, false, true)),
               "dual-illuminant HueSatMap DCP should be written"))
        return 1;
    cv::Mat warmColors(1, 1, CV_32FC3, cv::Scalar(0.5315f, 0.4769f, 0.1927f));
    cv::Mat daylightColors = warmColors.clone();
    if (!check(CameraProfileEngine::applyDcp(dualMapDcp, warmColors, 2856.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(dualMapDcp, daylightColors,
                                                 6500.0, &error) &&
                   cv::norm(warmColors - daylightColors) > 0.01,
               "secondary DCP HueSatMapData2 should vary with illuminant"))
    {
        return 1;
    }

    const QString wrapHueDcp =
        QDir(dcpDirectory).filePath("R6-wrap-hue.dcp");
    if (!check(writeFile(wrapHueDcp,
                         makeDcp(false, true, false, false, 0, 0, 179.0f,
                                 -179.0f)),
               "wrapped dual-illuminant HueSatMap DCP should be written"))
        return 1;
    cv::Mat endpointHue(1, 1, CV_32FC3, cv::Scalar(0.6f, 0.3f, 0.15f));
    cv::Mat midpointHue = endpointHue.clone();
    if (!check(CameraProfileEngine::applyDcp(wrapHueDcp, endpointHue, 2856.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(wrapHueDcp, midpointHue,
                                                 3980.0, &error) &&
                   cv::norm(endpointHue - midpointHue) < 0.1,
               "DCP hue interpolation should follow the shortest circular path"))
        return 1;

    cv::Mat image(32, 32, CV_32FC3);
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x)
            image.at<cv::Vec3f>(y, x) =
                cv::Vec3f(float(x) / 31.0f, float(y) / 31.0f, 0.5f);
    }
    const cv::Vec3f before = image.at<cv::Vec3f>(8, 8);
    CameraProfileEngine::CaptureSettings capture;
    capture.focalLengthMm = 50.0;
    capture.focusDistanceM = 2.0;
    capture.aperture = 4.0;
    if (!check(CameraProfileEngine::applyLcp(lcpPath, image, capture, &error),
               "LCP multi-setting geometry, CA and vignette should apply"))
        return 1;
    if (!check(image.type() == CV_32FC3 &&
                   cv::norm(image.at<cv::Vec3f>(8, 8) - before) > 0.001,
               "LCP remapping should resample image pixels"))
        return 1;
    if (!check(std::abs(image.at<cv::Vec3f>(8, 8)[0] - before[0]) > 0.001,
               "LCP chromatic-aberration model should remap color channels"))
        return 1;

    cv::Mat uniform(32, 32, CV_32FC3, cv::Scalar(0.25f, 0.25f, 0.25f));
    if (!check(CameraProfileEngine::applyLcp(lcpPath, uniform, capture, &error),
               "LCP vignetting model should apply"))
        return 1;
    if (!check(uniform.at<cv::Vec3f>(0, 0)[0] >
                   uniform.at<cv::Vec3f>(16, 16)[0],
               "LCP vignette compensation should brighten the frame edges"))
        return 1;

    const QString textLcpPath = QDir(lcpDirectory).filePath("text-parameters.lcp");
    const QByteArray textLcp =
        QByteArrayLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                          "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
                          "<rdf:Description>"
                          "<crs:PerspectiveModel>"
                          "<crs:ChromaticRedGreenModel>"
                          "<crs:RadialDistortParam1>0.75</crs:RadialDistortParam1>"
                          "</crs:ChromaticRedGreenModel>"
                          "<crs:ChromaticBlueGreenModel>"
                          "<crs:RadialDistortParam1>-0.75</crs:RadialDistortParam1>"
                          "</crs:ChromaticBlueGreenModel>"
                          "</crs:PerspectiveModel>"
                          "</rdf:Description></rdf:RDF>");
    if (!check(writeFile(textLcpPath, textLcp),
               "text-value LCP should be written"))
        return 1;

    cv::Mat channelImage(32, 32, CV_32FC3);
    for (int y = 0; y < channelImage.rows; ++y) {
        for (int x = 0; x < channelImage.cols; ++x) {
            channelImage.at<cv::Vec3f>(y, x) =
                cv::Vec3f(float(x) / 31.0f, 0.4f, float(y) / 31.0f);
        }
    }
    const cv::Vec3f channelBefore = channelImage.at<cv::Vec3f>(8, 8);
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, channelImage, {}, &error),
               "LCP text parameters should parse and apply"))
        return 1;
    const cv::Vec3f channelAfter = channelImage.at<cv::Vec3f>(8, 8);
    if (!check(std::abs(channelAfter[0] - channelBefore[0]) > 0.001f &&
                   std::abs(channelAfter[2] - channelBefore[2]) > 0.001f &&
                   std::abs(channelAfter[1] - channelBefore[1]) < 1e-6f,
               "LCP red-green and blue-green models should remap red and blue only"))
        return 1;

    return 0;
}
