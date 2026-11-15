#include "CameraProfileEngine.h"
#include "RenderChain.h"
#include "synthetic_dcp.h"
#include "LocalValues.h"
#include "CameraDenoise.h"
#include "CameraSignal.h"
#include "imageeditor.h"
#include "Pixels.h"
#include "image_testing.h"

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
        return RenderChain::workingImage(source, settings);
    }
    static ColorPipeline::WorkingImage denoised(const ColorPipeline::WorkingImage &source, int temperature,
                                                 int luminance, bool beforeProfile) {
        ImageEditor::Settings settings;
        settings.temperature = temperature;
        settings.denoiseLuminance = luminance;
        settings.denoiseBeforeProfile = beforeProfile;
        return RenderChain::workingImage(source, settings);
    }
    static ColorPipeline::WorkingImage proxy(const ColorPipeline::WorkingImage &source) {
        return RenderChain::editProxy(source);
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

using imagetest::at;

// A camera signal of one colour.
ipr::Image colour(int width, int height, float r, float g, float b)
{
    ipr::Image image = ipr::Image::create({width, height}, ipr::Depth::F32, 3);
    ipr::fill(image, std::vector<double>{r, g, b});
    return image;
}

// The image repeated side by side, as many times.
ipr::Image repeatedAcross(const ipr::Image &image, int times)
{
    ipr::Image out = ipr::Image::create({image.width() * times, image.height()}, image.depth(), image.channels());
    for (int i = 0; i < times; ++i) {
        ipr::Image place = out.mutableView({i * image.width(), 0, image.width(), image.height()});
        ipr::copyInto(image, place);
    }
    return out;
}

using SyntheticDcp::appendFloat;
using SyntheticDcp::appendU16;
using SyntheticDcp::appendU32;

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(contents) == contents.size();
}

// Synthetic little-endian DCP entries; never edits an installed camera profile.
bool replaceEntry(QByteArray &bytes, quint16 tag, quint16 type, quint32 count, quint32 value)
{
    const auto u16 = [&](int offset) {
        return quint16(uchar(bytes[offset])) | quint16(uchar(bytes[offset + 1]) << 8);
    };
    const int entries = u16(8);
    for (int i = 0; i < entries; ++i) {
        const int offset = 10 + i * 12;
        if (u16(offset) != tag) continue;
        QByteArray replacement;
        appendU16(replacement, tag);
        appendU16(replacement, type);
        appendU32(replacement, count);
        appendU32(replacement, value);
        bytes.replace(offset, 12, replacement);
        return true;
    }
    return false;
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
    if (!check(writeFile(dcpPath, SyntheticDcp::make()), "synthetic DCP should be written"))
        return 1;
    {
        const QString invalidPath = QDir(dcpDirectory).filePath("malformed.dcp");
        const ipr::Image input = colour(2, 2, .15f, .2f, .05f);
        const auto rejected = [&](const QByteArray &bytes) {
            QString error;
            auto pixels = input.clone();
            return writeFile(invalidPath, bytes) &&
                !CameraProfileEngine::applyDcp(invalidPath, pixels, 5200, &error) &&
                !error.isEmpty() && ipr::largestDifference(input, pixels) == 0;
        };
        for (quint16 tag : {quint16(50938), quint16(50982)}) {
            auto bytes = SyntheticDcp::make();
            if (!replaceEntry(bytes, tag, 11, 6, 0xffffff00u) ||
                !check(rejected(bytes), "present truncated table is not silently omitted")) return 1;
            bytes = SyntheticDcp::make();
            if (!replaceEntry(bytes, tag, 11, 0, 0) ||
                !check(rejected(bytes), "present empty table is rejected")) return 1;
        }
        auto bytes = SyntheticDcp::make();
        if (!replaceEntry(bytes, 50937, 4, 3, 0xffffff00u) ||
            !check(rejected(bytes), "invalid dimensions do not silently disable tables")) return 1;
        for (float value : {.5f, 1.e30f, -1.f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
            quint32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            for (quint16 tag : {quint16(51107), quint16(51108), quint16(50778)}) {
                bytes = SyntheticDcp::make(false, true);
                if (!replaceEntry(bytes, tag, 11, 1, bits) ||
                    !check(rejected(bytes), "invalid enumeration rejected before integer conversion")) return 1;
            }
        }
    }
    {
        CameraProfileEngine::configurePreparedProfileCache(64ULL*1024*1024,16);
        const ipr::Image input = colour(2, 2, .15f, .2f, .05f);
        QString error;
        auto before=input.clone(),warm=input.clone();
        if(!CameraProfileEngine::dcpMatchesCamera(dcpPath,"Canon","EOS R6 Mark III") ||
            !CameraProfileEngine::applyDcp(dcpPath,before,5200,&error) ||
            !CameraProfileEngine::applyDcp(dcpPath,warm,5200,&error))return 1;
        auto stats=CameraProfileEngine::preparedProfileCacheStats();
        if(!check(stats.preparations==1&&stats.hits>=2&&ipr::largestDifference(before,warm)==0,
            "matching and rendering reuse one immutable DCP preparation"))return 1;
        if(!writeFile(dcpPath,SyntheticDcp::make(false,false,false,false,0,0,45.f)))return 1;
        auto changed=input.clone();
        if(!CameraProfileEngine::applyDcp(dcpPath,changed,5200,&error))return 1;
        if(!check(ipr::largestDifference(before,changed)>1e-6&&
            CameraProfileEngine::preparedProfileCacheStats().preparations==2,"modified DCP bytes change output immediately"))return 1;
        auto r7Bytes=SyntheticDcp::make(false,false,false,false,0,0,20.f);
        const QByteArray restriction("Canon EOS R6 Mark III");
        r7Bytes.replace(restriction,QByteArray("Canon EOS R7").leftJustified(restriction.size(),' '));
        const auto r7Path=temporary.filePath("synthetic-r7.dcp");
        if(!writeFile(r7Path,r7Bytes)||!writeFile(dcpPath,SyntheticDcp::make()))return 1;
        auto r7Expected=input.clone();
        if(!CameraProfileEngine::dcpMatchesCamera(r7Path,"Canon","EOS R7")||
            CameraProfileEngine::dcpMatchesCamera(r7Path,"Canon","EOS R6 Mark III")||
            !CameraProfileEngine::applyDcp(r7Path,r7Expected,5200,&error))return 1;
        std::vector<std::future<bool>> readers;
        for(int i=0;i<8;++i)readers.push_back(std::async(std::launch::async,[&,i]{
            for(int n=0;n<10;++n){auto rgb=input.clone();
                if(!CameraProfileEngine::applyDcp(i%2?dcpPath:r7Path,rgb,5200,nullptr)||
                    ipr::largestDifference(rgb,i%2?before:r7Expected)!=0)return false;
            }return true;
        }));
        for(auto &reader:readers)if(!check(reader.get(),"R6/R7 concurrent rendering never mutates shared prepared data"))return 1;
        CameraProfileEngine::configurePreparedProfileCache(0,16);
        auto uncached=input.clone();
        if(!CameraProfileEngine::applyDcp(dcpPath,uncached,5200,&error)||
            !check(ipr::largestDifference(uncached,before)==0,"cache does not change DCP arithmetic"))return 1;
        CameraProfileEngine::configurePreparedProfileCache(64ULL*1024*1024,16);
    }
    {
        CameraSignal::Source signal;
        signal.rgb = colour(3, 2, .2f, .3f, .4f);
        signal.cameraMake = "Canon";
        signal.cameraModel = "EOS R6 Mark III";
        signal.whiteBalanceKelvin = 5200.;
        signal.cameraNeutral = {.5, 1, .8};
        signal.whiteBalanceApplied = true;
        QString error;
        const auto original = signal.rgb.clone();
        const auto ownedPixels = signal.rgb.block();
        auto expected = original.clone();
        CameraProfileEngine::DcpRenderOptions options;
        options.cameraBaselineExposureEv = .26;
        options.profileExposureCompensationEv = -.02;
        if (!CameraProfileEngine::applyDcp(dcpPath, expected, 5200, &error, options)) return 1;
        auto result = CameraSignal::render(std::move(signal), dcpPath, options, &error);
        if (!check(!result.isNull() && result.dcpContext && signal.rgb.empty() &&
                   result.dcpContext->cameraRgb.block() == ownedPixels &&
                   ipr::largestDifference(result.dcpContext->cameraRgb, original) == 0,
                   "camera signal transfers immutable storage without an extra source clone")) return 1;
        if (!check(ipr::largestDifference(result.linearRgba, ipr::opaque(expected)) == 0 &&
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
        at<ipr::Vec3f>(signal.rgb, 0,0)[0] = -1.f;
        at<ipr::Vec3f>(signal.rgb, 0,0)[1] = 2.f;
        if (!check(CameraSignal::validate(signal), "signed and extended signal is not clipped at the contract")) return 1;
        at<ipr::Vec3f>(signal.rgb, 0,0)[0] = std::numeric_limits<float>::quiet_NaN();
        if (!check(!CameraSignal::validate(signal), "nonfinite signal is rejected before DCP")) return 1;
    }

    const QString wbPath = QDir(dcpDirectory).filePath("WB.dcp");
    if (!check(writeFile(wbPath, SyntheticDcp::make(true, false, false, false, 0, 0,
                                        0, 0, 1, .8f, true)), "WB fixture")) return 1;
    CameraProfileEngine::DcpWorkingContext wb;
    wb.profilePath = wbPath;
    wb.sourceKelvin = 5200.25;
    wb.cameraNeutral = ipr::Vec3d(.95, 1, 1.08);
    wb.cameraRgb = colour(3, 2, .18f, .15f, .22f);
    wb.options.cameraBaselineExposureEv = -.25;
    wb.options.profileExposureCompensationEv = -.02;
    const ipr::Image untouched = wb.cameraRgb.clone();
    QString wbError;
    ipr::Vec3d gains;
    if (!check(CameraProfileEngine::whiteBalanceGains(wb, 5200, 0, gains, &wbError) &&
                   gains == ipr::Vec3d(1,1,1), "rounded As Shot is exact identity")) return 1;
    ipr::Image neutralExpected = untouched.clone(), rendered;
    if (!check(CameraProfileEngine::applyDcp(wbPath, neutralExpected, wb.sourceKelvin,
                                             &wbError, wb.options) &&
                   CameraProfileEngine::renderFromCameraSignal(wb, 5200, 0, 0.0, rendered, &wbError) &&
                   ipr::largestDifference(rendered, neutralExpected) == 0,
               "neutral DCP preserves fractional CCT and exposure options exactly")) return 1;
    for (int temperature : {2000, 3500, 5200, 9000, 50000}) {
        for (int tint : (temperature == 5200 ? std::vector<int>{-150,-25,0,25,150} : std::vector<int>{0})) {
            if (!check(CameraProfileEngine::whiteBalanceGains(wb, temperature, tint, gains, &wbError) &&
                           gains[1] == 1 && gains[0] > 0 && gains[2] > 0,
                       "individual temperature/tint sweeps have finite positive camera gains")) return 1;
            ipr::Image expected = ipr::transformed(untouched, ipr::Matrix33f(
                float(gains[0]),0,0, 0,1,0, 0,0,float(gains[2])));
            const double kelvin = temperature == 5200 && tint == 0 ? wb.sourceKelvin : temperature;
            if (!check(CameraProfileEngine::applyDcp(wbPath, expected, kelvin, &wbError, wb.options) &&
                           CameraProfileEngine::renderFromCameraSignal(wb, temperature, tint, 0.0, rendered, &wbError) &&
                           ipr::largestDifference(expected, rendered) == 0 && ipr::allFinite(rendered),
                       "WB is before nonlinear DCP, not a post-curve multiplication")) return 1;
        }
    }
    if (!check(ipr::largestDifference(wb.cameraRgb, untouched) == 0,
               "repeated WB edits never mutate the retained camera source")) return 1;
    ColorPipeline::WorkingImage editorSource;
    editorSource.linearRgba = ipr::opaque(neutralExpected);
    editorSource.referenceWhiteBalanceKelvin = wb.sourceKelvin;
    editorSource.dcpContext = std::make_shared<CameraProfileEngine::DcpWorkingContext>(CameraProfileEngine::sharedCopy(wb));
    const auto editorNeutral = IprTestImageGenerator::render(editorSource, 5200, 0);
    if (!check(editorNeutral.linearRgba.block() == editorSource.linearRgba.block(),
               "editor neutral is the untouched source, not a WB round trip")) return 1;
    // Experimental camera-signal denoise: off, or at strength 0, the render is
    // unchanged; on, the camera signal goes through the profile again, from the
    // retained source, which it never mutates.
    if (!check(IprTestImageGenerator::denoised(editorSource, 5200, 0, true).linearRgba.block() ==
                   editorSource.linearRgba.block(),
               "camera denoise at strength 0 is the untouched source")) return 1;
    {
        const auto before = IprTestImageGenerator::denoised(editorSource, 5200, 40, true);
        if (!check(!before.isNull() && ipr::allFinite(before.linearRgba) &&
                       before.linearRgba.size() == neutralExpected.size() &&
                       ipr::largestDifference(wb.cameraRgb, untouched) == 0 &&
                       ipr::largestDifference(editorSource.dcpContext->cameraRgb, untouched) == 0,
                   "camera denoise renders through the profile without touching the retained signal")) return 1;
    }
    // Exposure scales the scene-linear camera signal before the DCP tone curve,
    // like WB: never a multiplication of the already rendered master.
    if (!CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 1.0, rendered, &wbError)) return 1;
    const auto editorCold = IprTestImageGenerator::render(editorSource, 2000, 0, 1);
    if (!check(!editorCold.isNull() && ipr::largestDifference(ipr::opaque(rendered), editorCold.linearRgba) < 1e-6,
               "editor applies WB and exposure once, before the DCP, preserves alpha")) return 1;
    ipr::Image coldNeutral;
    if (!CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, coldNeutral, &wbError)) return 1;
    if (!check(ipr::largestDifference(ipr::mapped(coldNeutral, [](float v) { return v * 2.f; }), rendered) > 1e-3,
               "with a nonlinear profile curve, +1 EV is not a post-curve doubling")) return 1;
    ipr::Image brighterOnce;
    if (!CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 1.0, brighterOnce, &wbError) ||
        !check(ipr::largestDifference(brighterOnce, rendered) == 0 &&
               ipr::largestDifference(wb.cameraRgb, untouched) == 0,
               "exposure edits never accumulate nor mutate the camera source")) return 1;
    {
        // A local exposure (LocalValues) acts on the camera signal as the global one does.
        ipr::Image local = ipr::filled(wb.cameraRgb.size(), ipr::Depth::F32, 1, 0);
        ipr::Image zone = ipr::filled(wb.cameraRgb.size(), ipr::Depth::U8, 1, 0);
        for (int y = 0; y < local.height(); ++y)
            for (int x = 0; x < local.width(); ++x)
                if ((x + y) % 2 == 0) { at<float>(local, y, x) = 1.0f; at<std::uint8_t>(zone, y, x) = 255; }
        const ipr::Image outside = ipr::bitwiseNot(zone);
        ipr::Image locallyBrighter, unchanged;
        LocalValues::Maps exposureMaps;
        exposureMaps[LocalValues::Exposure] = local.share();
        LocalValues::Maps zeroMaps;
        zeroMaps[LocalValues::Exposure] = ipr::filled(local.size(), ipr::Depth::F32, 1, 0);
        LocalValues::Maps misfitMaps;
        misfitMaps[LocalValues::Exposure] = ipr::Image::create({local.width(), local.height() + 1}, ipr::Depth::F32, 1);
        if (!check(CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, locallyBrighter, &wbError, exposureMaps) &&
                       imagetest::largestDifferenceWhere(locallyBrighter, rendered, zone) < 1e-5 &&
                       imagetest::largestDifferenceWhere(locallyBrighter, coldNeutral, outside) == 0,
                   "a local +1 EV renders as a global +1 EV inside its zone, nothing outside")) return 1;
        if (!check(CameraProfileEngine::renderFromCameraSignal(
                       wb, 2000, 0, 0.0, unchanged, &wbError, zeroMaps) &&
                       ipr::largestDifference(unchanged, coldNeutral) == 0 &&
                       !CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, unchanged, &wbError, misfitMaps),
                   "a map of zeros changes nothing; a map of another size is refused")) return 1;
        // Local white balance: the gains and the profile's illuminant follow the pixel.
        ipr::Image warmGlobal, tintedGlobal, warmLocal, tintedLocal, zeroWhiteBalance;
        LocalValues::Maps temperatureMaps, tintMaps, zeroWhiteMaps;
        temperatureMaps[LocalValues::Temperature] = ipr::mapped(local, [](float v) { return v * 2500.0f; });
        tintMaps[LocalValues::Tint] = ipr::mapped(local, [](float v) { return v * 30.0f; });
        zeroWhiteMaps[LocalValues::Temperature] = ipr::filled(local.size(), ipr::Depth::F32, 1, 0);
        zeroWhiteMaps[LocalValues::Tint] = ipr::filled(local.size(), ipr::Depth::F32, 1, 0);
        if (!check(CameraProfileEngine::renderFromCameraSignal(wb, 4500, 0, 0.0, warmGlobal, &wbError) &&
                       CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, warmLocal, &wbError, temperatureMaps) &&
                       imagetest::largestDifferenceWhere(warmLocal, warmGlobal, zone) < 1e-5 &&
                       imagetest::largestDifferenceWhere(warmLocal, coldNeutral, outside) == 0,
                   "a local temperature renders as a global one inside its zone, nothing outside")) return 1;
        if (!check(CameraProfileEngine::renderFromCameraSignal(wb, 2000, 30, 0.0, tintedGlobal, &wbError) &&
                       CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, tintedLocal, &wbError, tintMaps) &&
                       imagetest::largestDifferenceWhere(tintedLocal, tintedGlobal, zone) < 1e-5 &&
                       imagetest::largestDifferenceWhere(tintedLocal, coldNeutral, outside) == 0,
                   "a local tint renders as a global one inside its zone, nothing outside")) return 1;
        if (!check(CameraProfileEngine::renderFromCameraSignal(wb, 2000, 0, 0.0, zeroWhiteBalance, &wbError, zeroWhiteMaps) &&
                       ipr::largestDifference(zeroWhiteBalance, coldNeutral) == 0,
                   "maps of zeros keep the white balance exactly")) return 1;
    }
    auto large = ColorPipeline::sharedCopy(editorSource);
    large.linearRgba = repeatedAcross(editorSource.linearRgba, 800);
    auto largeContext = std::make_shared<CameraProfileEngine::DcpWorkingContext>(CameraProfileEngine::sharedCopy(wb));
    largeContext->cameraRgb = repeatedAcross(wb.cameraRgb, 800);
    large.dcpContext = largeContext;
    const auto proxy = IprTestImageGenerator::proxy(large);
    if (!check(proxy.dcpContext && proxy.dcpContext->cameraRgb.size() == proxy.linearRgba.size() &&
                   proxy.linearRgba.width() == 2048 && large.dcpContext->cameraRgb.width() == 2400 &&
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
    {
        // Experimental camera denoise with the signal kept before the lens corrections:
        // it denoises that signal, then applies the corrections again before the profile.
        CameraProfileEngine::DcpWorkingContext lens = CameraProfileEngine::sharedCopy(wb);
        lens.preLensCameraRgb = ipr::Image::create({30, 20}, ipr::Depth::F32, 3);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 30; ++x)
                at<ipr::Vec3f>(lens.preLensCameraRgb, y, x) = ipr::Vec3f(.05f + .01f * x, .1f + .005f * y, .2f);
        lens.lcpPath = lcpPath;
        lens.capture.focalLengthMm = 24;
        lens.capture.focusDistanceM = 2;
        lens.capture.aperture = 4;
        lens.cameraRgb = lens.preLensCameraRgb.clone();
        QString lensError;
        if (!CameraProfileEngine::applyLcp(lcpPath, lens.cameraRgb, lens.capture, &lensError, lens.lensOptions)) return 1;
        const ipr::Image preLensUntouched = lens.preLensCameraRgb.clone();
        ipr::Image corrected, uncorrected;
        auto uncorrectedContext = CameraProfileEngine::sharedCopy(lens);
        uncorrectedContext.cameraRgb = lens.preLensCameraRgb.share();
        if (!CameraProfileEngine::renderFromCameraSignal(lens, 5200, 0, 0.0, corrected, &lensError) ||
            !CameraProfileEngine::renderFromCameraSignal(uncorrectedContext, 5200, 0, 0.0, uncorrected, &lensError))
            return 1;
        ColorPipeline::WorkingImage lensSource;
        lensSource.linearRgba = ipr::opaque(corrected);
        lensSource.referenceWhiteBalanceKelvin = lens.sourceKelvin;
        lensSource.dcpContext = std::make_shared<CameraProfileEngine::DcpWorkingContext>(CameraProfileEngine::sharedCopy(lens));
        ipr::Image expectedCamera = lens.preLensCameraRgb.share();
        auto expectedContext = CameraProfileEngine::sharedCopy(lens);
        if (!CameraDenoise::apply(expectedCamera, 40, {}) ||
            !CameraProfileEngine::applyLcp(lcpPath, expectedCamera, lens.capture, &lensError, lens.lensOptions))
            return 1;
        expectedContext.cameraRgb = std::move(expectedCamera);
        ipr::Image expected;
        if (!CameraProfileEngine::renderFromCameraSignal(expectedContext, 5200, 0, 0.0, expected, &lensError)) return 1;
        const auto denoisedLens = IprTestImageGenerator::denoised(lensSource, 5200, 40, true);
        const ipr::Image renderedRgb = denoisedLens.isNull() ? ipr::Image() : ipr::withoutAlpha(denoisedLens.linearRgba);
        if (!check(!renderedRgb.empty() && ipr::largestDifference(renderedRgb, expected) < 1e-6 &&
                       ipr::largestDifference(corrected, uncorrected) > 1e-3 &&
                       ipr::largestDifference(lens.preLensCameraRgb, preLensUntouched) == 0,
                   "camera denoise before the lens corrections applies them again, source untouched")) return 1;
    }

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
    if (!check(writeFile(manifestDcpPath, SyntheticDcp::make()) &&
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
    QByteArray adobeDcp = SyntheticDcp::make();
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

    ipr::Image cameraRgb = colour(2, 2, 0.96422f, 1.0f, 0.82521f);
    QString error;
    if (!check(CameraProfileEngine::applyDcp(dcpPath, cameraRgb, 5500.0, &error),
               "DCP matrix should apply to linear camera RGB"))
        return 1;
    if (!check(cameraRgb.depth() == ipr::Depth::F32 && cameraRgb.channels() == 3 &&
                   ipr::length(at<ipr::Vec3f>(cameraRgb, 0, 0) -
                            ipr::Vec3f(0.8f, 0.8f, 0.8f)) < 0.01,
               "DCP ForwardMatrix and look table should transform D50 white"))
        return 1;

    const QString srgbTableDcp =
        QDir(dcpDirectory).filePath("R6-srgb-tables.dcp");
    if (!check(writeFile(srgbTableDcp, SyntheticDcp::make(false, false, false, false, 1, 1)),
               "sRGB-encoded DCP tables should be written"))
        return 1;
    ipr::Image linearTableInput = colour(1, 1, 0.45f, 0.25f, 0.1f);
    ipr::Image srgbTableInput = linearTableInput.clone();
    if (!check(CameraProfileEngine::applyDcp(dcpPath, linearTableInput, 5500.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(srgbTableDcp, srgbTableInput,
                                                 5500.0, &error) &&
                   imagetest::distance(linearTableInput, srgbTableInput) < 1e-6,
               "DCP encoding must not affect single-Value tables"))
        return 1;

    // A 120-degree HSV rotation of (.4,.2,.1) in ProPhoto gives (.1,.4,.2).
    // Identity ForwardMatrix means the synthetic camera input is XYZ D50.
    const ipr::Matrix33f proPhotoToXyz(
        .7976749f, .1351917f, .0313534f,
        .2880402f, .7118741f, .0000857f,
        0.f, 0.f, .82521f);
    const ipr::Matrix33f xyzToWorking(WorkingColor::fromXyzD50());
    const QString curveDcp = QDir(dcpDirectory).filePath("monotone-curve.dcp");
    if (!check(writeFile(curveDcp, SyntheticDcp::make(false, false, false, false,
                         0, 0, 0.f, 0.f, 1, 1.f, true)), "tone profile written"))
        return 1;
    const CameraProfileEngine::DcpRenderOptions stable{0.0, true};
    const ipr::Vec3f triplets[] = {{.1f,.4f,.7f}, {.1f,.7f,.4f}, {.4f,.1f,.7f},
                                 {.4f,.7f,.1f}, {.7f,.1f,.4f}, {.7f,.4f,.1f}};
    for (const auto &triplet : triplets) {
        ipr::Image current = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
        at<ipr::Vec3f>(current, 0,0) = proPhotoToXyz * triplet;
        ipr::Image channelCurve = current.clone();
        ipr::Vec3f expected;
        for (int c=0;c<3;++c)
            expected[c] = .16f + (triplet[c]-.1f)/.6f*.72f;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,current,5500.,&error,stable) &&
                   ipr::length(at<ipr::Vec3f>(current, 0,0) - xyzToWorking*(proPhotoToXyz*expected)) < 1e-5,
                   "curve preserves relative middle channel for all permutations")) return 1;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,channelCurve,5500.,&error) &&
                   imagetest::distance(current, channelCurve) > .01, "hue-preserving curve differs from per-channel evaluation")) return 1;
    }
    for (float value : {0.f, .25f, 1.f}) {
        ipr::Image gray = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
        at<ipr::Vec3f>(gray, 0,0) = proPhotoToXyz*ipr::Vec3f(value,value,value);
        const float expected = value <= .5f ? value*1.6f : .8f+(value-.5f)*.4f;
        if (!check(CameraProfileEngine::applyDcp(curveDcp,gray,5500.,&error,stable) &&
                   ipr::length(at<ipr::Vec3f>(gray, 0,0) - xyzToWorking*(proPhotoToXyz*ipr::Vec3f(expected,expected,expected))) < 1e-5,
                   "equal channels, black and white are finite and correct")) return 1;
    }
    const QString descendingDcp = QDir(dcpDirectory).filePath("descending-curve.dcp");
    if (!check(writeFile(descendingDcp, SyntheticDcp::make(false,false,false,false,
                        0,0,0.f,0.f,1,1.f,true,1.f,.1f)), "descending profile written")) return 1;
    ipr::Image descending = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
    at<ipr::Vec3f>(descending, 0,0)=proPhotoToXyz*ipr::Vec3f(.4f,.6f,.8f);
    ipr::Image descendingLegacy = descending.clone();
    if (!check(CameraProfileEngine::applyDcp(descendingDcp,descending,5500.,&error,stable) &&
               CameraProfileEngine::applyDcp(descendingDcp,descendingLegacy,5500.,&error) &&
               imagetest::distance(descending, descendingLegacy)<1e-6,
               "inverted extrema use per-pixel legacy fallback")) return 1;
    ipr::Image exposureA = colour(1, 1, .02f,.03f,.01f);
    ipr::Image exposureB = ipr::mapped(exposureA, [](float v) { return v * 2.f; });
    const CameraProfileEngine::DcpRenderOptions plusOne{1.0, false};
    if (!check(CameraProfileEngine::applyDcp(dcpPath,exposureA,5500.,&error,plusOne) &&
               CameraProfileEngine::applyDcp(dcpPath,exposureB,5500.,&error) &&
               imagetest::distance(exposureA, exposureB) < 1e-6,
               "camera exposure applied before nonlinear tables")) return 1;
    ipr::Image invalidExposure = exposureA.clone();
    if (!check(!CameraProfileEngine::applyDcp(dcpPath,invalidExposure,5500.,&error,{5.0,true}) &&
               imagetest::distance(invalidExposure, exposureA) == 0,
               "invalid exposure rejected before pixel mutation")) return 1;
    CameraProfileEngine::DcpRenderOptions compensated;
    compensated.cameraBaselineExposureEv = .35;
    compensated.profileExposureCompensationEv = -.02;
    auto equivalent = compensated;
    equivalent.cameraBaselineExposureEv = .33;
    equivalent.profileExposureCompensationEv = 0;
    ipr::Image profileGain = colour(3, 2, .03f,.1f,.2f);
    ipr::Image referenceGain = profileGain.clone();
    if (!check(CameraProfileEngine::applyDcp(wbPath,profileGain,5200.,&error,compensated) &&
        CameraProfileEngine::applyDcp(wbPath,referenceGain,5200.,&error,equivalent) &&
        ipr::largestDifference(profileGain, referenceGain) < 1e-6,
        "profile compensation composes once with source baseline before nonlinear DCP")) return 1;
    compensated.profileExposureCompensationEv = 3;
    referenceGain = profileGain.clone();
    if (!check(!CameraProfileEngine::applyDcp(wbPath,profileGain,5200.,&error,compensated) &&
        ipr::largestDifference(profileGain, referenceGain) == 0,
        "invalid profile compensation rejected before pixel mutation")) return 1;
    const QString rotationDcp = QDir(dcpDirectory).filePath("prophoto-rotation.dcp");
    if (!check(writeFile(rotationDcp, SyntheticDcp::make(false, false, false, false,
                          0, 0, 120.f)), "ProPhoto hue test profile should be written"))
        return 1;
    ipr::Image rotated = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
    at<ipr::Vec3f>(rotated, 0, 0) = proPhotoToXyz * ipr::Vec3f(.4f, .2f, .1f);
    if (!check(CameraProfileEngine::applyDcp(rotationDcp, rotated, 5500., &error) &&
               ipr::length(at<ipr::Vec3f>(rotated, 0, 0) -
                   xyzToWorking * (proPhotoToXyz * ipr::Vec3f(.08f, .32f, .16f))) < 1e-5,
               "DCP hue rotation must be evaluated in ProPhoto, not sRGB"))
        return 1;

    const QString encoded3dDcp = QDir(dcpDirectory).filePath("encoded-3d.dcp");
    if (!check(writeFile(encoded3dDcp, SyntheticDcp::make(false, false, false, false,
                              1, 1, 0.f, 0.f, 2)), "3D encoded profile should be written"))
        return 1;
    ipr::Image encodedColor = ipr::Image::create({1, 1}, ipr::Depth::F32, 3);
    at<ipr::Vec3f>(encodedColor, 0, 0) = proPhotoToXyz * ipr::Vec3f(.4f, .2f, .1f);
    const float encodedV = 1.055f * std::pow(.4f, 1.f / 2.4f) - .055f;
    const float expectedV = std::pow((encodedV * .8f + .055f) / 1.055f, 2.4f);
    if (!check(CameraProfileEngine::applyDcp(encoded3dDcp, encodedColor, 5500., &error) &&
               ipr::length(at<ipr::Vec3f>(encodedColor, 0, 0) - xyzToWorking *
                   (proPhotoToXyz * ipr::Vec3f(expectedV, expectedV * .5f, expectedV * .25f))) < 1e-5,
               "sRGB encoding must modify only HSV Value, preserving hue/saturation"))
        return 1;

    const QString colorOnlyDcp =
        QDir(dcpDirectory).filePath("R6-color-matrix.dcp");
    if (!check(writeFile(colorOnlyDcp, SyntheticDcp::make(true)),
               "ColorMatrix-only DCP should be written"))
        return 1;
    ipr::Image colorOnlyRgb = colour(1, 1, 1.0f, 1.0f, 1.0f);
    if (!check(CameraProfileEngine::applyDcp(colorOnlyDcp, colorOnlyRgb, 5500.0,
                                             &error) &&
                   ipr::length(at<ipr::Vec3f>(colorOnlyRgb, 0, 0) -
                            ipr::Vec3f(0.8f, 0.8f, 0.8f)) < 0.01,
               "DCP without ForwardMatrix should adapt ColorMatrix through D50"))
        return 1;

    const QString dualDcp = QDir(dcpDirectory).filePath("R6-dual.dcp");
    if (!check(writeFile(dualDcp, SyntheticDcp::make(false, true, true, false)),
               "dual-illuminant DCP should be written"))
        return 1;
    ipr::Image warmRgb = colour(1, 1, 0.8f, 0.4f, 0.2f);
    ipr::Image daylightRgb = warmRgb.clone();
    if (!check(CameraProfileEngine::applyDcp(dualDcp, warmRgb, 2856.0, &error) &&
                   CameraProfileEngine::applyDcp(dualDcp, daylightRgb, 6500.0,
                                                 &error) &&
                   imagetest::distance(warmRgb, daylightRgb) > 0.1,
               "DCP matrices should interpolate according to the capture illuminant"))
        return 1;

    const QString dualMapDcp = QDir(dcpDirectory).filePath("R6-dual-map.dcp");
    if (!check(writeFile(dualMapDcp, SyntheticDcp::make(false, true, false, true)),
               "dual-illuminant HueSatMap DCP should be written"))
        return 1;
    ipr::Image warmColors = colour(1, 1, 0.5315f, 0.4769f, 0.1927f);
    ipr::Image daylightColors = warmColors.clone();
    if (!check(CameraProfileEngine::applyDcp(dualMapDcp, warmColors, 2856.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(dualMapDcp, daylightColors,
                                                 6500.0, &error) &&
                   imagetest::distance(warmColors, daylightColors) > 0.01,
               "secondary DCP HueSatMapData2 should vary with illuminant"))
    {
        return 1;
    }

    const QString wrapHueDcp =
        QDir(dcpDirectory).filePath("R6-wrap-hue.dcp");
    if (!check(writeFile(wrapHueDcp,
                         SyntheticDcp::make(false, true, false, false, 0, 0, 179.0f,
                                 -179.0f)),
               "wrapped dual-illuminant HueSatMap DCP should be written"))
        return 1;
    ipr::Image endpointHue = colour(1, 1, 0.6f, 0.3f, 0.15f);
    ipr::Image midpointHue = endpointHue.clone();
    if (!check(CameraProfileEngine::applyDcp(wrapHueDcp, endpointHue, 2856.0,
                                             &error) &&
                   CameraProfileEngine::applyDcp(wrapHueDcp, midpointHue,
                                                 3980.0, &error) &&
                   imagetest::distance(endpointHue, midpointHue) < 0.1,
               "DCP hue interpolation should follow the shortest circular path"))
        return 1;

    ipr::Image image = ipr::Image::create({32, 32}, ipr::Depth::F32, 3);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            at<ipr::Vec3f>(image, y, x) =
                ipr::Vec3f(float(x) / 31.0f, float(y) / 31.0f, 0.5f);
    }
    const ipr::Vec3f before = at<ipr::Vec3f>(image, 8, 8);
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
        !check(ipr::largestDifference(image, lcpWarm)==0 &&
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
            ipr::largestDifference(rgb,i%2?image:otherExpected)==0;
    }));
    for(auto &reader:lcpReaders)if(!check(reader.get(),"LCP capture settings stay local to concurrent calls"))return 1;
    if (!check(image.depth() == ipr::Depth::F32 && image.channels() == 3 &&
                   ipr::length(at<ipr::Vec3f>(image, 8, 8) - before) > 0.001,
               "LCP remapping should resample image pixels"))
        return 1;
    if (!check(std::abs(at<ipr::Vec3f>(image, 8, 8)[0] - before[0]) > 0.001,
               "LCP chromatic-aberration model should remap color channels"))
        return 1;

    ipr::Image uniform = colour(32, 32, 0.25f, 0.25f, 0.25f);
    if (!check(CameraProfileEngine::applyLcp(lcpPath, uniform, capture, &error),
               "LCP vignetting model should apply"))
        return 1;
    if (!check(at<ipr::Vec3f>(uniform, 0, 0)[0] >
                   at<ipr::Vec3f>(uniform, 16, 16)[0],
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

    ipr::Image channelImage = ipr::Image::create({32, 32}, ipr::Depth::F32, 3);
    for (int y = 0; y < channelImage.height(); ++y) {
        for (int x = 0; x < channelImage.width(); ++x) {
            at<ipr::Vec3f>(channelImage, y, x) =
                ipr::Vec3f(float(x) / 31.0f, 0.4f, float(y) / 31.0f);
        }
    }
    const ipr::Vec3f channelBefore = at<ipr::Vec3f>(channelImage, 8, 8);
    ipr::Image originalChannels = channelImage.clone();
    CameraProfileEngine::LcpApplied applied;
    ipr::Image disabled = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, disabled, {}, &error,
                    {false, false}, &applied) && imagetest::distance(disabled, originalChannels) == 0.0 &&
                    !applied.geometry && !applied.vignetting && !applied.chromaticAberration,
               "disabled optics must be pixel-identical and report no correction")) return 1;
    ipr::Image caOnly = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, caOnly, {}, &error,
                    {false, true}, &applied) && applied.chromaticAberration && !applied.geometry &&
                    !applied.vignetting && imagetest::distance(caOnly, originalChannels) > 0.001,
               "CA toggle works independently from lens correction")) return 1;
    ipr::Image lensOnly = originalChannels.clone();
    if (!check(CameraProfileEngine::applyLcp(lcpPath, lensOnly, capture, &error,
                    {true, false}, &applied) && !applied.chromaticAberration &&
                    applied.geometry && applied.vignetting,
               "lens toggle applies geometry/vignette independently from CA")) return 1;
    {
        // A profile's model works in units of the focal length: the same lens moves a pixel
        // by what its focal length says, on any sensor. On a ramp, a pixel reads where it comes from.
        const auto profileWith = [&](const QByteArray &focal) {
            return QByteArrayLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" "
                                     "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
                                     "<rdf:Description crs:Lens=\"Ramp\"><crs:PerspectiveModel crs:FocalLength=\"70\">"
                                     "<crs:GeometryModel crs:RadialDistortParam1=\"0.35\"") + focal +
                   QByteArrayLiteral("/></crs:PerspectiveModel></rdf:Description></rdf:RDF>");
        };
        ipr::Image ramp = ipr::Image::create({1000, 600}, ipr::Depth::F32, 3);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x)
                at<ipr::Vec3f>(ramp, y, x) = ipr::Vec3f(float(x) / 1000.0f, 0.5f, 0.5f);
        const auto movedTo = [&](const QString &path, double focal, double sensor) {
            CameraProfileEngine::CaptureSettings settings;
            settings.focalLengthMm = focal;
            settings.sensorWidthMm = sensor;
            ipr::Image image = ramp.clone();
            QString failure;
            if (!CameraProfileEngine::applyLcp(path, image, settings, &failure, {true, false}))
                return -1.0;
            return double(at<ipr::Vec3f>(image, 300, 900)[0]) * 1000.0;
        };
        const auto expected = [](double unit) {
            const double dx = 400.0 / unit, r2 = dx * dx;
            return 500.0 + (1.0 + 0.35 * r2) * dx * unit;
        };
        const QString rampPath = QDir(lcpDirectory).filePath("ramp.lcp");
        const QString givenPath = QDir(lcpDirectory).filePath("ramp-given.lcp");
        if (!check(writeFile(rampPath, profileWith({})) && writeFile(givenPath, profileWith(" crs:FocalLengthX=\"1.5\"")),
                   "the ramp's profiles should be written"))
            return 1;
        if (!check(std::abs(movedTo(rampPath, 70.0, 36.0) - expected(70.0 / 35.0 * 1000.0)) < 0.02,
                   "a full frame works in units of its focal length")) return 1;
        if (!check(std::abs(movedTo(rampPath, 70.0, 22.3) - expected(70.0 * 36.0 / 22.3 / 35.0 * 1000.0)) < 0.02,
                   "a smaller sensor works in units of the equivalent focal length")) return 1;
        if (!check(std::abs(movedTo(rampPath, 70.0, 0.0) - expected(70.0 / 35.0 * 1000.0)) < 0.02,
                   "an unknown sensor is taken as a full frame")) return 1;
        if (!check(std::abs(movedTo(givenPath, 70.0, 22.3) - expected(1.5 * 1000.0)) < 0.02,
                   "a focal length given by the profile comes first")) return 1;
    }
    if (!check(CameraProfileEngine::lcpMatchesLens(lcpPath, "Canon", "EOS R6 Mark III", "Canon", "RF 24-70mm F2.8 L IS USM") &&
                    !CameraProfileEngine::lcpMatchesLens(lcpPath, "Other", "Other camera", "Other", "Unknown lens"),
               "saved LCP must remain compatible with camera and lens")) return 1;
    if (!check(CameraProfileEngine::applyLcp(textLcpPath, channelImage, {}, &error),
               "LCP text parameters should parse and apply"))
        return 1;
    const ipr::Vec3f channelAfter = at<ipr::Vec3f>(channelImage, 8, 8);
    if (!check(std::abs(channelAfter[0] - channelBefore[0]) > 0.001f &&
                   std::abs(channelAfter[2] - channelBefore[2]) > 0.001f &&
                   std::abs(channelAfter[1] - channelBefore[1]) < 1e-6f,
               "LCP red-green and blue-green models should remap red and blue only"))
        return 1;

    return 0;
}
