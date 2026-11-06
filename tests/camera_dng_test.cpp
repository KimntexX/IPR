#include "CameraDng.h"
#include "CameraSignal.h"
#include "ImageExporter.h"
#include "RawDecoder.h"
#include "synthetic_dcp.h"
#include "OpenCvBridge.h"
#include "image_testing.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <tiffio.h>

#include <cmath>
#include <iostream>
#include <random>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

CameraDng::Content sample()
{
    CameraDng::Content content;
    content.cameraRgb = ipr::Image::create({180, 120}, ipr::Depth::F32, 3);
    std::mt19937 random(3);
    std::uniform_real_distribution<float> value(-0.005f, 0.95f);
    for (int y = 0; y < 120; ++y)
        for (int v = 0; v < 180 * 3; ++v)
            content.cameraRgb.mutableRow<float>(y)[v] = value(random);
    content.cameraMake = QStringLiteral("Canon");
    content.cameraModel = QStringLiteral("EOS R6 Mark III");
    content.lensMake = QStringLiteral("Sigma");
    content.lensModel = QStringLiteral("70-200mm F2.8");
    content.iso = 1600;
    content.exposureSeconds = 0.005;
    content.focalLengthMm = 200.0;
    content.captureDate = QDateTime(QDate(2026, 10, 30), QTime(22, 44, 40), Qt::UTC);
    content.cameraNeutral = ipr::Vec3d(0.5, 1.0, 0.7);
    content.whiteBalanceKelvin = 4321.0;
    content.renderOptions.cameraBaselineExposureEv = 0.25;
    content.renderOptions.profileExposureCompensationEv = -0.1;
    content.renderOptions.preserveToneCurveHue = true;
    content.calibratedWhite = 13995;
    content.colorMatrix = {0.9, -0.3, -0.1, -0.4, 1.2, 0.2, -0.1, 0.2, 0.6};
    content.provenance = QStringLiteral("IPR stack of 9 frames, référence é");
    return content;
}


void roundTrip(const QDir &folder)
{
    const CameraDng::Content written = sample();
    const QString path = folder.filePath(QStringLiteral("signal.dng"));
    QString error;
    check(CameraDng::write(written, path, &error), "a signal is written");
    check(CameraDng::isCameraDng(path), "it is known as a camera signal");

    CameraDng::Content read;
    check(CameraDng::read(path, read, &error), "and read back");
    check(read.cameraRgb.size() == written.cameraRgb.size(), "the same size");
    // One code of the file: at most the scale of the weakest neutral over the 16-bit range.
    const double tolerance = 2.0 / (65535.0 - 1024.0);
    const double worst = ipr::largestDifference(read.cameraRgb, written.cameraRgb);
    if (worst > tolerance)
        std::cerr << "worst " << worst << '\n';
    check(worst <= tolerance, "the same values within a code, below the black too");
    check(imagetest::rangeOf(read.cameraRgb).low < -0.004, "the values below the black stay");
    check(read.cameraMake == written.cameraMake && read.cameraModel == written.cameraModel, "the same camera");
    check(read.cameraNeutral == written.cameraNeutral && read.whiteBalanceKelvin == written.whiteBalanceKelvin, "the same balance");
    check(read.renderOptions.cameraBaselineExposureEv == 0.25 && read.renderOptions.profileExposureCompensationEv == -0.1 &&
              read.renderOptions.preserveToneCurveHue && read.calibratedWhite == 13995, "the same calibration");
    check(read.provenance == written.provenance, "the same provenance");
    check(read.lensModel == written.lensModel && read.iso == 1600 && read.exposureSeconds == 0.005 &&
              read.focalLengthMm == 200.0 && read.captureDate == written.captureDate, "the same capture");

    // A signal above white keeps its range, and tells it through the baseline exposure.
    CameraDng::Content bright = sample();
    imagetest::at<ipr::Vec3f>(bright.cameraRgb, 5, 5) = ipr::Vec3f(3.f, 3.f, 3.f);
    const QString brightPath = folder.filePath(QStringLiteral("bright.dng"));
    check(CameraDng::write(bright, brightPath, &error) && CameraDng::read(brightPath, read, &error), "a bright signal goes through");
    const ipr::Vec3f high = imagetest::at<ipr::Vec3f>(read.cameraRgb, 5, 5);
    check(std::abs(high[1] - 3.f) < 0.001f, "above white stays above white");

    // What other software reads: the standard tags of a LinearRaw file.
    TIFF *tiff = TIFFOpen(path.toUtf8().constData(), "r");
    check(tiff != nullptr, "an ordinary TIFF container");
    if (tiff) {
        uint16_t photometric = 0, bits = 0, count = 0;
        const float *neutral = nullptr, *black = nullptr;
        const float *matrix = nullptr;
        float baseline = 0.f;
        const char *model = nullptr, *unique = nullptr;
        check(TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) == 1 && photometric == 34892, "LinearRaw");
        check(TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits) == 1 && bits == 16, "on 16 bits");
        check(TIFFGetField(tiff, TIFFTAG_ASSHOTNEUTRAL, &count, &neutral) == 1 && count == 3 &&
                  std::abs(neutral[0] - 0.5f) < 1e-6f && std::abs(neutral[1] - 1.f) < 1e-6f, "the as-shot neutral");
        check(TIFFGetField(tiff, TIFFTAG_BLACKLEVEL, &count, &black) == 1 && black[0] == 1024.f, "the black level");
        check(TIFFGetField(tiff, TIFFTAG_COLORMATRIX1, &count, &matrix) == 1 && count == 9 && std::abs(matrix[0] - 0.9f) < 1e-3f, "the colour matrix");
        check(TIFFGetField(tiff, TIFFTAG_BASELINEEXPOSURE, &baseline) == 1 && std::abs(baseline - 0.15f) < 1e-3f, "the baseline exposure");
        check(TIFFGetField(tiff, TIFFTAG_MODEL, &model) == 1 && QString::fromUtf8(model) == written.cameraModel, "the model");
        check(TIFFGetField(tiff, TIFFTAG_UNIQUECAMERAMODEL, &unique) == 1 &&
                  QString::fromUtf8(unique) == QStringLiteral("Canon EOS R6 Mark III"), "the unique model");
        TIFFClose(tiff);
    }
}

void refusals(const QDir &folder)
{
    const QString path = folder.filePath(QStringLiteral("refused.dng"));
    QString error;
    CameraDng::Content empty = sample();
    empty.cameraRgb = ipr::Image();
    check(!CameraDng::write(empty, path, &error) && !error.isEmpty(), "no signal, no file");
    CameraDng::Content wrongType = sample();
    wrongType.cameraRgb = ipr::filled({4, 4}, ipr::Depth::U8, 3, 1);
    check(!CameraDng::write(wrongType, path, &error), "a wrong type is refused");
    CameraDng::Content noBalance = sample();
    noBalance.cameraNeutral = ipr::Vec3d(0, 0, 0);
    check(!CameraDng::write(noBalance, path, &error), "an unknown balance is refused");
    CameraDng::Content noMatrix = sample();
    noMatrix.colorMatrix = {};
    check(!CameraDng::write(noMatrix, path, &error), "an unknown matrix is refused");
    check(!QFile::exists(path), "and nothing is left");
    check(!CameraDng::write(sample(), folder.filePath(QStringLiteral("no/such/folder/a.dng")), &error), "an unwritable path is refused");

    // Not a camera signal: a missing file, a broken one, and a TIFF of another origin.
    CameraDng::Content read;
    check(!CameraDng::isCameraDng(folder.filePath(QStringLiteral("absent.dng"))), "a missing file is not one");
    check(!CameraDng::read(folder.filePath(QStringLiteral("absent.dng")), read, &error), "and cannot be read");
    const QString broken = folder.filePath(QStringLiteral("broken.dng"));
    QFile file(broken);
    check(file.open(QIODevice::WriteOnly) && file.write("not a tiff at all") > 0, "a broken file for the test");
    file.close();
    check(!CameraDng::isCameraDng(broken), "a broken file is not one");
    const QString plain = folder.filePath(QStringLiteral("plain.tif"));
    QImage(8, 8, QImage::Format_RGB888).save(plain, "TIFF");
    check(!CameraDng::isCameraDng(plain), "a plain TIFF is not one");
    check(!CameraDng::read(plain, read, &error) && read.cameraRgb.empty(), "and gives no signal");
}

void decoding(const QDir &folder)
{
    if (!RawDecoder::supportedExtensions().contains(QStringLiteral("dng")))
        return;
    const QString profiles = folder.filePath(QStringLiteral("profiles"));
    QDir().mkpath(QDir(profiles).filePath(QStringLiteral("dcp")));
    const QString dcp = QDir(profiles).filePath(QStringLiteral("dcp/R6.dcp"));
    QFile file(dcp);
    check(file.open(QIODevice::WriteOnly) && file.write(SyntheticDcp::make(true)) > 0, "a synthetic profile");
    file.close();
    const CameraProfileEngine::DcpCameraData camera = CameraProfileEngine::dcpCameraData(dcp);
    check(camera.colorMatrix[0] != 0.0, "the profile gives its matrix");
    check(CameraProfileEngine::dcpCameraData(folder.filePath(QStringLiteral("absent.dcp"))).colorMatrix[0] == 0.0, "no profile, no matrix");

    CameraDng::Content written = sample();
    written.colorMatrix = camera.colorMatrix;
    const QString path = folder.filePath(QStringLiteral("opened.dng"));
    QString error;
    check(CameraDng::write(written, path, &error), "a signal for the decoder");

    // The decoder gives what the signal renders to directly, not LibRaw's view of it.
    RawDecoder::Options options;
    options.profileDirectories = {profiles};
    options.automaticWhiteBalance = false;
    options.useNormalizationEnvironment = false;
    ImageMetadata metadata;
    const ColorPipeline::WorkingImage opened = RawDecoder::decode(path, options, &metadata, &error);
    if (opened.isNull())
        std::cerr << error.toStdString() << '\n';
    check(!opened.isNull(), "IPR opens its own DNG");
    check(metadata.decoderId == QStringLiteral("ipr-camera-dng"), "by its own reader");
    check(metadata.rawNormalizationApplied && metadata.normalizationBaselineExposureEv == 0.25 &&
              metadata.normalizationWhiteLevel == 13995, "with its calibration");
    check(metadata.lensModel == written.lensModel && metadata.iso == 1600 && metadata.captureDate == written.captureDate,
          "and its capture in the panel");
    if (!opened.isNull()) {
        CameraSignal::Source signal;
        signal.rgb = written.cameraRgb.clone();
        signal.cameraMake = written.cameraMake;
        signal.cameraModel = written.cameraModel;
        signal.whiteBalanceKelvin = written.whiteBalanceKelvin;
        signal.whiteBalanceApplied = true;
        signal.cameraNeutral = written.cameraNeutral;
        const ColorPipeline::WorkingImage direct = CameraSignal::render(std::move(signal), dcp, written.renderOptions, &error);
        check(!direct.isNull() && direct.linearRgba.size() == opened.linearRgba.size(), "the same render");
        if (!direct.isNull()) {
            const double worst = ipr::largestDifference(ipr::fromMat(direct.linearRgba), ipr::fromMat(opened.linearRgba));
            if (worst > 2e-3)
                std::cerr << "render worst " << worst << '\n';
            check(worst <= 2e-3, "within the file's precision");
        }
    }

    // Other software's route to the same file: LibRaw opens it as an ordinary linear DNG.
    check(RawDecoder::sourceDimensions(path) == QSize(180, 120), "LibRaw reads its size");

    // A camera without a profile cannot be rendered: said, not guessed.
    CameraDng::Content unknown = sample();
    unknown.colorMatrix = written.colorMatrix;
    unknown.cameraMake = QStringLiteral("Nobody");
    unknown.cameraModel = QStringLiteral("Camera 1");
    const QString unknownPath = folder.filePath(QStringLiteral("unknown.dng"));
    check(CameraDng::write(unknown, unknownPath, &error), "a signal of an unknown camera");
    error.clear();
    check(RawDecoder::decode(unknownPath, options, &metadata, &error).isNull() && !error.isEmpty(), "it fails with its reason");
}

void publishing(const QDir &folder)
{
    const QString target = folder.filePath(QStringLiteral("published.dng"));
    const auto writing = [](const QString &staged, QString *error) { return CameraDng::write(sample(), staged, error); };
    const ImageExporter::Result first = ImageExporter::publishEncoded(target, QStringLiteral("DNG"), writing);
    check(first.success && first.path == target && QFile::exists(target), "a file is published");
    const qint64 size = QFileInfo(target).size();
    const ImageExporter::Result second = ImageExporter::publishEncoded(target, QStringLiteral("DNG"), writing);
    check(second.success && second.path != target && QFile::exists(second.path), "an existing name is not taken");
    check(QFileInfo(target).size() == size, "nor its file touched");
    const QStringList before = folder.entryList(QDir::Files | QDir::Hidden);
    const ImageExporter::Result failed = ImageExporter::publishEncoded(
        folder.filePath(QStringLiteral("failed.dng")), QStringLiteral("DNG"), [](const QString &, QString *error) {
            *error = QStringLiteral("no");
            return false;
        });
    check(!failed.success && failed.message.contains(QStringLiteral("no")), "a failure tells its reason");
    check(folder.entryList(QDir::Files | QDir::Hidden) == before, "and leaves nothing");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "a temporary folder");
    const QDir folder(directory.path());
    roundTrip(folder);
    refusals(folder);
    decoding(folder);
    publishing(folder);
    if (passed)
        std::cout << "camera dng: ok\n";
    return passed ? 0 : 1;
}
