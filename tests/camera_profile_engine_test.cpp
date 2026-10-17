#include "CameraProfileEngine.h"
#include "CameraSignal.h"
#include "imageeditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <future>

// Exercise the exact renderer/proxy used by preview and export through the
// existing test-generator friend; no extra production API is introduced.
class IprTestImageGenerator {
public:
    static ColorPipeline::WorkingImage render(const ColorPipeline::WorkingImage &source,
                                               int temperature, int tint, float ev = 0) {
        ImageEditor::Settings settings;
        settings.temperature = temperature;
        settings.tint = tint;
        settings.brightness = ev;
        return ImageEditor::renderWorkingImage(source, settings);
    }
    static ColorPipeline::WorkingImage proxy(const ColorPipeline::WorkingImage &source) {
        return ImageEditor::buildEditProxy(source);
    }
};

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
    {
        CameraProfileEngine::configurePreparedProfileCache(64ULL*1024*1024,16);
        const cv::Mat input(2,2,CV_32FC3,cv::Scalar(.15f,.2f,.05f));
        QString error;
        auto before=input.clone(),warm=input.clone();
        if(!CameraProfileEngine::dcpMatchesCamera(dcpPath,"Canon","EOS R6 Mark III") ||
            !CameraProfileEngine::applyDcp(dcpPath,before,5200,&error) ||
            !CameraProfileEngine::applyDcp(dcpPath,warm,5200,&error))return 1;
        auto stats=CameraProfileEngine::preparedProfileCacheStats();
        if(!check(stats.preparations==1&&stats.hits>=2&&cv::norm(before,warm,cv::NORM_INF)==0,
            "matching and rendering reuse one immutable DCP preparation"))return 1;
        if(!writeFile(dcpPath,makeDcp(false,false,false,false,0,0,45.f)))return 1;
        auto changed=input.clone();
        if(!CameraProfileEngine::applyDcp(dcpPath,changed,5200,&error))return 1;
        if(!check(cv::norm(before,changed,cv::NORM_INF)>1e-6&&
            CameraProfileEngine::preparedProfileCacheStats().preparations==2,"modified DCP bytes change output immediately"))return 1;
        auto r7Bytes=makeDcp(false,false,false,false,0,0,20.f);
        const QByteArray restriction("Canon EOS R6 Mark III");
        r7Bytes.replace(restriction,QByteArray("Canon EOS R7").leftJustified(restriction.size(),' '));
        const auto r7Path=temporary.filePath("synthetic-r7.dcp");
        if(!writeFile(r7Path,r7Bytes)||!writeFile(dcpPath,makeDcp()))return 1;
        auto r7Expected=input.clone();
        if(!CameraProfileEngine::dcpMatchesCamera(r7Path,"Canon","EOS R7")||
            CameraProfileEngine::dcpMatchesCamera(r7Path,"Canon","EOS R6 Mark III")||
            !CameraProfileEngine::applyDcp(r7Path,r7Expected,5200,&error))return 1;
        std::vector<std::future<bool>> readers;
        for(int i=0;i<8;++i)readers.push_back(std::async(std::launch::async,[&,i]{
            for(int n=0;n<10;++n){auto rgb=input.clone();
                if(!CameraProfileEngine::applyDcp(i%2?dcpPath:r7Path,rgb,5200,nullptr)||
                    cv::norm(rgb,i%2?before:r7Expected,cv::NORM_INF)!=0)return false;
            }return true;
        }));
        for(auto &reader:readers)if(!check(reader.get(),"R6/R7 concurrent rendering never mutates shared prepared data"))return 1;
        CameraProfileEngine::configurePreparedProfileCache(0,16);
        auto uncached=input.clone();
        if(!CameraProfileEngine::applyDcp(dcpPath,uncached,5200,&error)||
            !check(cv::norm(uncached,before,cv::NORM_INF)==0,"cache does not change DCP arithmetic"))return 1;
        CameraProfileEngine::configurePreparedProfileCache(64ULL*1024*1024,16);
    }
    {
        CameraSignal::Source signal;
        signal.rgb = cv::Mat(2, 3, CV_32FC3, cv::Scalar(.2f,.3f,.4f)).clone();
        signal.cameraMake = "Canon";
        signal.cameraModel = "EOS R6 Mark III";
        signal.whiteBalanceKelvin = 5200.;
        signal.cameraNeutral = {.5, 1, .8};
        signal.whiteBalanceApplied = true;
        QString error;
        const auto original = signal.rgb.clone();
        const auto ownedPixels = signal.rgb.data;
        auto expected = original.clone();
        CameraProfileEngine::DcpRenderOptions options;
        options.cameraBaselineExposureEv = .26;
        options.profileExposureCompensationEv = -.02;
        if (!CameraProfileEngine::applyDcp(dcpPath, expected, 5200, &error, options)) return 1;
        auto result = CameraSignal::render(std::move(signal), dcpPath, options, &error);
        if (!check(!result.isNull() && result.dcpContext && signal.rgb.empty() &&
                   result.dcpContext->cameraRgb.data == ownedPixels &&
                   cv::norm(result.dcpContext->cameraRgb, original, cv::NORM_INF) == 0,
                   "camera signal transfers immutable storage without an extra source clone")) return 1;
        std::vector<cv::Mat> channels;
        cv::split(expected, channels);
        channels.push_back(cv::Mat(expected.size(), CV_32FC1, cv::Scalar(1.f)));
        cv::Mat expectedRgba;cv::merge(channels, expectedRgba);
        if (!check(cv::norm(result.linearRgba, expectedRgba, cv::NORM_INF) == 0 &&
                   result.colorSpace == ColorPipeline::workingColorSpace() &&
                   result.referenceWhiteBalanceKelvin == 5200. &&
                   result.dcpContext->options.profileExposureCompensationEv == -.02,
                   "shared rendering matches the DCP exactly, including composed gain once")) return 1;
        signal.rgb = original.clone();
        signal.coordinates = CameraSignal::Coordinates::LinearSrgb;
        if (!check(!CameraSignal::validate(signal, &error), "sRGB cannot masquerade as camera RGB")) return 1;
        signal.coordinates = CameraSignal::Coordinates::CameraRgb;
        signal.whiteBalanceApplied = false;
        if (!check(!CameraSignal::validate(signal), "unapplied WB is outside the contract")) return 1;
        signal.whiteBalanceApplied = true;
        signal.cameraModel = "Other camera";
        if (!check(CameraSignal::render(std::move(signal), dcpPath, {}, &error).isNull(),
                   "a signal cannot use another camera's DCP")) return 1;
        signal.cameraModel = "EOS R6 Mark III";
        signal.cameraNeutral = {1, 0, 1};
        if (!check(!CameraSignal::validate(signal), "partially missing camera neutral is rejected")) return 1;
        signal.cameraNeutral = {0, 0, 0};
        signal.whiteBalanceKelvin.reset();
        if (!check(CameraSignal::validate(signal), "unavailable neutral/temperature is explicit")) return 1;
        signal.rgb.at<cv::Vec3f>(0,0)[0] = -1.f;
        signal.rgb.at<cv::Vec3f>(0,0)[1] = 2.f;
        if (!check(CameraSignal::validate(signal), "signed and extended signal is not clipped at the contract")) return 1;
        signal.rgb.at<cv::Vec3f>(0,0)[0] = std::numeric_limits<float>::quiet_NaN();
        if (!check(!CameraSignal::validate(signal), "nonfinite signal is rejected before DCP")) return 1;
    }

    const QString wbPath = QDir(dcpDirectory).filePath("WB.dcp");
    if (!check(writeFile(wbPath, makeDcp(true, false, false, false, 0, 0,
                                        0, 0, 1, .8f, true)), "WB fixture")) return 1;
    CameraProfileEngine::DcpWorkingContext wb;
    wb.profilePath = wbPath;
    wb.sourceKelvin = 5200.25;
    wb.cameraNeutral = cv::Vec3d(.95, 1, 1.08);
    wb.cameraRgb = cv::Mat(2, 3, CV_32FC3, cv::Scalar(.18, .15, .22));
    wb.options.cameraBaselineExposureEv = -.25;
    wb.options.profileExposureCompensationEv = -.02;
    const cv::Mat untouched = wb.cameraRgb.clone();
    QString wbError;
    cv::Vec3d gains;
    if (!check(CameraProfileEngine::whiteBalanceGains(wb, 5200, 0, gains, &wbError) &&
                   gains == cv::Vec3d(1,1,1), "rounded As Shot is exact identity")) return 1;
    cv::Mat neutralExpected = untouched.clone(), rendered;
    if (!check(CameraProfileEngine::applyDcp(wbPath, neutralExpected, wb.sourceKelvin,
                                             &wbError, wb.options) &&
                   CameraProfileEngine::renderWhiteBalance(wb, 5200, 0, rendered, &wbError) &&
                   cv::norm(rendered, neutralExpected, cv::NORM_INF) == 0,
               "neutral DCP preserves fractional CCT and exposure options exactly")) return 1;
    for (int temperature : {2000, 3500, 5200, 9000, 50000}) {
        for (int tint : (temperature == 5200 ? std::vector<int>{-150,-25,0,25,150} : std::vector<int>{0})) {
            if (!check(CameraProfileEngine::whiteBalanceGains(wb, temperature, tint, gains, &wbError) &&
                           gains[1] == 1 && gains[0] > 0 && gains[2] > 0,
                       "individual temperature/tint sweeps have finite positive camera gains")) return 1;
            cv::Mat expected;
            cv::transform(untouched, expected, cv::Matx33f(
                float(gains[0]),0,0, 0,1,0, 0,0,float(gains[2])));
            const double kelvin = temperature == 5200 && tint == 0 ? wb.sourceKelvin : temperature;
            if (!check(CameraProfileEngine::applyDcp(wbPath, expected, kelvin, &wbError, wb.options) &&
                           CameraProfileEngine::renderWhiteBalance(wb, temperature, tint, rendered, &wbError) &&
                           cv::norm(expected, rendered, cv::NORM_INF) == 0 && cv::checkRange(rendered),
                       "WB is before nonlinear DCP, not a post-curve multiplication")) return 1;
        }
    }
    if (!check(cv::norm(wb.cameraRgb, untouched, cv::NORM_INF) == 0,
               "repeated WB edits never mutate the retained camera source")) return 1;
    ColorPipeline::WorkingImage editorSource;
    std::vector<cv::Mat> editorChannels;
    cv::split(neutralExpected, editorChannels);
    editorChannels.push_back(cv::Mat(neutralExpected.size(), CV_32FC1, cv::Scalar(1)));
    cv::merge(editorChannels, editorSource.linearRgba);
    editorSource.referenceWhiteBalanceKelvin = wb.sourceKelvin;
    editorSource.dcpContext = std::make_shared<CameraProfileEngine::DcpWorkingContext>(wb);
    const auto editorNeutral = IprTestImageGenerator::render(editorSource, 5200, 0);
    if (!check(editorNeutral.linearRgba.data == editorSource.linearRgba.data,
               "editor neutral is the untouched source, not a WB round trip")) return 1;
    if (!CameraProfileEngine::renderWhiteBalance(wb, 2000, 0, rendered, &wbError)) return 1;
    const auto editorCold = IprTestImageGenerator::render(editorSource, 2000, 0, 1);
    cv::Mat expectedRgba;
    editorChannels.clear();
    cv::split(rendered * 2.f, editorChannels);
    editorChannels.push_back(cv::Mat(rendered.size(), CV_32FC1, cv::Scalar(1)));
    cv::merge(editorChannels, expectedRgba);
    if (!check(!editorCold.isNull() && cv::norm(expectedRgba, editorCold.linearRgba, cv::NORM_INF) < 1e-6,
               "editor applies WB once before exposure, preserves alpha")) return 1;
    auto large = editorSource;
    cv::repeat(editorSource.linearRgba, 1, 800, large.linearRgba);
    auto largeContext = std::make_shared<CameraProfileEngine::DcpWorkingContext>(wb);
    cv::repeat(wb.cameraRgb, 1, 800, largeContext->cameraRgb);
    large.dcpContext = largeContext;
    const auto proxy = IprTestImageGenerator::proxy(large);
    if (!check(proxy.dcpContext && proxy.dcpContext->cameraRgb.size() == proxy.linearRgba.size() &&
                   proxy.linearRgba.cols == 2048 && large.dcpContext->cameraRgb.cols == 2400 &&
                   !IprTestImageGenerator::render(proxy, 2000, 0).isNull(),
               "proxy WB uses its own camera geometry without mutating the full-resolution source")) return 1;
    CameraProfileEngine::whiteBalanceGains(wb, 2000, 0, gains, &wbError);
    if (!check(gains[0] < gains[2], "cold temperature favours blue")) return 1;
    CameraProfileEngine::whiteBalanceGains(wb, 9000, 0, gains, &wbError);
    if (!check(gains[0] > gains[2], "warm temperature favours red")) return 1;
    if (!check(!CameraProfileEngine::whiteBalanceGains(wb, 1999, 0, gains, &wbError) &&
                   !CameraProfileEngine::whiteBalanceGains(wb, 5200, 151, gains, &wbError),
               "invalid UI ranges fail explicitly")) return 1;
    wb.profilePath = dcpPath; // ForwardMatrix alone cannot characterize sensor white.
    if (!check(!CameraProfileEngine::whiteBalanceGains(wb, 2000, 0, gains, &wbError),
               "missing ColorMatrix cannot silently use a post-DCP RAW approximation")) return 1;

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
    if (!check(manifestSelection.dcpPath == QFileInfo(manifestDcpPath).canonicalFilePath() &&
                   manifestSelection.lcpPath == QFileInfo(manifestLcpPath).canonicalFilePath(),
               "manifest should bypass the recursive profile search"))
        return 1;
    if (!check(CameraProfileEngine::dcpMatchesCamera(manifestDcpPath,"Canon","EOS R6 Mark III") &&
               !CameraProfileEngine::dcpMatchesCamera(manifestDcpPath,"Nikon","Z 8"),
               "manual DCP selection must be checked against camera restriction")) return 1;
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
    if (!check(environmentProfiles.dcpPath == QFileInfo(manifestDcpPath).canonicalFilePath() &&
                   environmentProfiles.lcpPath == QFileInfo(manifestLcpPath).canonicalFilePath(),
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
                   cv::norm(linearTableInput - srgbTableInput) < 1e-6,
               "DCP encoding must not affect single-Value tables"))
        return 1;

    // A 120-degree HSV rotation of (.4,.2,.1) in ProPhoto gives (.1,.4,.2).
    // Identity ForwardMatrix means the synthetic camera input is XYZ D50.
    const cv::Matx33f proPhotoToXyz(
        .7976749f, .1351917f, .0313534f,
        .2880402f, .7118741f, .0000857f,
        0.f, 0.f, .82521f);
    const cv::Matx33f xyzToWorking(WorkingColor::fromXyzD50());
    const QString curveDcp = QDir(dcpDirectory).filePath("monotone-curve.dcp");
    if (!check(writeFile(curveDcp, makeDcp(false, false, false, false,
                         0, 0, 0.f, 0.f, 1, 1.f, true)), "tone profile written"))
        return 1;
    const CameraProfileEngine::DcpRenderOptions stable{0.0, true};
    const cv::Vec3f triplets[] = {{.1f,.4f,.7f}, {.1f,.7f,.4f}, {.4f,.1f,.7f},
                                 {.4f,.7f,.1f}, {.7f,.1f,.4f}, {.7f,.4f,.1f}};
    for (const auto &triplet : triplets) {
        cv::Mat current(1,1,CV_32FC3);
        current.at<cv::Vec3f>(0,0) = proPhotoToXyz * triplet;
        cv::Mat channelCurve = current.clone();
        cv::Vec3f expected;
        for (int c=0;c<3;++c)
            expected[c] = .16f + (triplet[c]-.1f)/.6f*.72f;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,current,5500.,&error,stable) &&
                   cv::norm(current.at<cv::Vec3f>(0,0) - xyzToWorking*(proPhotoToXyz*expected)) < 1e-5,
                   "curve preserves relative middle channel for all permutations")) return 1;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,channelCurve,5500.,&error) &&
                   cv::norm(current-channelCurve) > .01, "hue-preserving curve differs from per-channel evaluation")) return 1;
    }
    for (float value : {0.f, .25f, 1.f}) {
        cv::Mat gray(1,1,CV_32FC3);
        gray.at<cv::Vec3f>(0,0) = proPhotoToXyz*cv::Vec3f(value,value,value);
        const float expected = value <= .5f ? value*1.6f : .8f+(value-.5f)*.4f;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,gray,5500.,&error,stable) &&
                   cv::norm(gray.at<cv::Vec3f>(0,0) - xyzToWorking*(proPhotoToXyz*cv::Vec3f(expected,expected,expected))) < 1e-5,
                   "equal channels, black and white are finite and correct")) return 1;
    }
    const QString descendingDcp = QDir(dcpDirectory).filePath("descending-curve.dcp");
    if (!check(writeFile(descendingDcp, makeDcp(false,false,false,false,
                        0,0,0.f,0.f,1,1.f,true,1.f,.1f)), "descending profile written")) return 1;
    cv::Mat descending(1,1,CV_32FC3);
    descending.at<cv::Vec3f>(0,0)=proPhotoToXyz*cv::Vec3f(.4f,.6f,.8f);
    cv::Mat descendingLegacy=descending.clone();
    if (!check(CameraProfileEngine::applyDcp(descendingDcp,descending,5500.,&error,stable) &&
               CameraProfileEngine::applyDcp(descendingDcp,descendingLegacy,5500.,&error) &&
               cv::norm(descending-descendingLegacy)<1e-6,
               "inverted extrema use per-pixel legacy fallback")) return 1;
    cv::Mat exposureA(1,1,CV_32FC3,cv::Scalar(.02f,.03f,.01f));
    cv::Mat exposureB = exposureA * 2.f;
    const CameraProfileEngine::DcpRenderOptions plusOne{1.0, false};
    if (!check(CameraProfileEngine::applyDcp(dcpPath,exposureA,5500.,&error,plusOne) &&
               CameraProfileEngine::applyDcp(dcpPath,exposureB,5500.,&error) &&
               cv::norm(exposureA-exposureB) < 1e-6,
               "camera exposure applied before nonlinear tables")) return 1;
    cv::Mat invalidExposure = exposureA.clone();
    if (!check(!CameraProfileEngine::applyDcp(dcpPath,invalidExposure,5500.,&error,{5.0,true}) &&
               cv::norm(invalidExposure-exposureA) == 0,
               "invalid exposure rejected before pixel mutation")) return 1;
    CameraProfileEngine::DcpRenderOptions compensated;
    compensated.cameraBaselineExposureEv = .35;
    compensated.profileExposureCompensationEv = -.02;
    auto equivalent = compensated;
    equivalent.cameraBaselineExposureEv = .33;
    equivalent.profileExposureCompensationEv = 0;
    cv::Mat profileGain(2,3,CV_32FC3,cv::Scalar(.03f,.1f,.2f)), referenceGain = profileGain.clone();
    if (!check(CameraProfileEngine::applyDcp(wbPath,profileGain,5200.,&error,compensated) &&
        CameraProfileEngine::applyDcp(wbPath,referenceGain,5200.,&error,equivalent) &&
        cv::norm(profileGain-referenceGain,cv::NORM_INF) < 1e-6,
        "profile compensation composes once with source baseline before nonlinear DCP")) return 1;
    compensated.profileExposureCompensationEv = 3;
    referenceGain = profileGain.clone();
    if (!check(!CameraProfileEngine::applyDcp(wbPath,profileGain,5200.,&error,compensated) &&
        cv::norm(profileGain-referenceGain,cv::NORM_INF) == 0,
        "invalid profile compensation rejected before pixel mutation")) return 1;
    const QString rotationDcp = QDir(dcpDirectory).filePath("prophoto-rotation.dcp");
    if (!check(writeFile(rotationDcp, makeDcp(false, false, false, false,
                          0, 0, 120.f)), "ProPhoto hue test profile should be written"))
        return 1;
    cv::Mat rotated(1, 1, CV_32FC3);
    rotated.at<cv::Vec3f>(0, 0) = proPhotoToXyz * cv::Vec3f(.4f, .2f, .1f);
    if (!check(CameraProfileEngine::applyDcp(rotationDcp, rotated, 5500., &error) &&
               cv::norm(rotated.at<cv::Vec3f>(0, 0) -
                   xyzToWorking * (proPhotoToXyz * cv::Vec3f(.08f, .32f, .16f))) < 1e-5,
               "DCP hue rotation must be evaluated in ProPhoto, not sRGB"))
        return 1;

    const QString encoded3dDcp = QDir(dcpDirectory).filePath("encoded-3d.dcp");
    if (!check(writeFile(encoded3dDcp, makeDcp(false, false, false, false,
                              1, 1, 0.f, 0.f, 2)), "3D encoded profile should be written"))
        return 1;
    cv::Mat encodedColor(1, 1, CV_32FC3);
    encodedColor.at<cv::Vec3f>(0, 0) = proPhotoToXyz * cv::Vec3f(.4f, .2f, .1f);
    const float encodedV = 1.055f * std::pow(.4f, 1.f / 2.4f) - .055f;
    const float expectedV = std::pow((encodedV * .8f + .055f) / 1.055f, 2.4f);
    if (!check(CameraProfileEngine::applyDcp(encoded3dDcp, encodedColor, 5500., &error) &&
               cv::norm(encodedColor.at<cv::Vec3f>(0, 0) - xyzToWorking *
                   (proPhotoToXyz * cv::Vec3f(expectedV, expectedV * .5f, expectedV * .25f))) < 1e-5,
               "sRGB encoding must modify only HSV Value, preserving hue/saturation"))
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
    CameraProfileEngine::clearPreparedProfileCache();
    const auto lcpInput=image.clone();
    if (!check(CameraProfileEngine::applyLcp(lcpPath, image, capture, &error),
               "LCP multi-setting geometry, CA and vignette should apply"))
        return 1;
    auto lcpWarm=lcpInput.clone();
    if(!CameraProfileEngine::applyLcp(lcpPath,lcpWarm,capture,&error)||
        !check(cv::norm(image,lcpWarm,cv::NORM_INF)==0 &&
            CameraProfileEngine::preparedProfileCacheStats().preparations==1 &&
            CameraProfileEngine::preparedProfileCacheStats().hits==1,
            "LCP parsing is shared without changing the resampling result"))return 1;
    auto otherCapture=capture;otherCapture.focalLengthMm=24.;otherCapture.aperture=2.8;
    auto otherExpected=lcpInput.clone();
    if(!CameraProfileEngine::applyLcp(lcpPath,otherExpected,otherCapture,&error))return 1;
    std::vector<std::future<bool>> lcpReaders;
    for(int i=0;i<8;++i)lcpReaders.push_back(std::async(std::launch::async,[&,i]{
        auto rgb=lcpInput.clone();
        return CameraProfileEngine::applyLcp(lcpPath,rgb,i%2?capture:otherCapture,nullptr) &&
            cv::norm(rgb,i%2?image:otherExpected,cv::NORM_INF)==0;
    }));
    for(auto &reader:lcpReaders)if(!check(reader.get(),"LCP capture settings stay local to concurrent calls"))return 1;
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
    const cv::Mat originalChannels = channelImage.clone();
    CameraProfileEngine::LcpApplied applied;
    cv::Mat disabled = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, disabled, {}, &error,
                    {false, false}, &applied) && cv::norm(disabled - originalChannels) == 0.0 &&
                    !applied.geometry && !applied.vignetting && !applied.chromaticAberration,
               "disabled optics must be pixel-identical and report no correction")) return 1;
    cv::Mat caOnly = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, caOnly, {}, &error,
                    {false, true}, &applied) && applied.chromaticAberration && !applied.geometry &&
                    !applied.vignetting && cv::norm(caOnly - originalChannels) > 0.001,
               "CA toggle works independently from lens correction")) return 1;
    cv::Mat lensOnly = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(lcpPath, lensOnly, capture, &error,
                    {true, false}, &applied) && !applied.chromaticAberration &&
                    applied.geometry && applied.vignetting,
               "lens toggle applies geometry/vignette independently from CA")) return 1;
    if (!check(CameraProfileEngine::lcpMatchesLens(lcpPath, "Canon", "EOS R6 Mark III", "Canon", "RF 24-70mm F2.8 L IS USM") &&
                    !CameraProfileEngine::lcpMatchesLens(lcpPath, "Other", "Other camera", "Other", "Unknown lens"),
               "saved LCP must remain compatible with camera and lens")) return 1;
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
