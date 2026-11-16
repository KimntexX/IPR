#include "CameraDng.h"
#include "CameraSignal.h"
#include "ImageExporter.h"
#include "RawDecoder.h"
#include "synthetic_dcp.h"
#include "Pixels.h"
#include "image_testing.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

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


// Of a 16-bit float, plus the rounding of the scale by the neutral.
constexpr double halfPrecision = 1.0 / 2048.0 + 1e-6;

// The largest error relative to the value. Below the smallest normal half, 2^-14, the file's
// precision is absolute: the floor is that value over the weakest scale of the neutral, 0.5.
double largestRelativeError(const ipr::Image &read, const ipr::Image &written)
{
    double worst = 0.0;
    for (int y = 0; y < written.height(); ++y)
        for (int x = 0; x < written.width(); ++x)
            for (int c = 0; c < 3; ++c) {
                const double expected = written.row<ipr::Vec3f>(y)[x][c];
                const double error = std::abs(double(read.row<ipr::Vec3f>(y)[x][c]) - expected);
                worst = std::max(worst, error / std::max(std::abs(expected), std::ldexp(1.0, -13)));
            }
    return worst;
}

// A file of the first format, 16-bit codes over a black of 1024, as IPR exported them before
// its floating-point samples: the user's files must keep opening.
bool writeIntegerFile(const QString &path, const ipr::Vec3d &neutral, double headroom, const std::vector<uint16_t> &codes, int width)
{
    TIFF *tiff = TIFFOpen(path.toUtf8().constData(), "w");
    if (!tiff)
        return false;
    const QByteArray text = QStringLiteral("{\"ipr_camera_dng\":1,\"neutral\":[%1,%2,%3],\"headroom\":%4,"
                                           "\"kelvin\":4321,\"baseline_ev\":0.25,\"iso\":800}")
                                .arg(neutral[0]).arg(neutral[1]).arg(neutral[2]).arg(headroom).toUtf8();
    const int height = int(codes.size()) / (3 * width);
    bool written = TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(width)) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(height)) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, 34892) == 1 &&
                   TIFFSetField(tiff, TIFFTAG_MAKE, "Canon") == 1 &&
                   TIFFSetField(tiff, TIFFTAG_MODEL, "EOS 60D") == 1 &&
                   TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, text.constData()) == 1;
    std::vector<uint16_t> row(size_t(width) * 3);
    for (int y = 0; written && y < height; ++y) {
        std::copy_n(codes.begin() + std::ptrdiff_t(size_t(y) * row.size()), row.size(), row.begin());
        written = TIFFWriteScanline(tiff, row.data(), uint32_t(y), 0) >= 0;
    }
    TIFFClose(tiff);
    return written;
}

void integerFormat(const QDir &folder)
{
    const ipr::Vec3d neutral(0.5, 1.0, 0.8);
    const double headroom = 2.0;
    const std::vector<uint16_t> codes{1024, 1024, 1024, 0, 0, 0, 65535, 65535, 65535, 33279, 33279, 33279};
    const QString path = folder.filePath(QStringLiteral("integer.dng"));
    check(writeIntegerFile(path, neutral, headroom, codes, 2), "a file of the first format");
    check(CameraDng::isCameraDng(path), "is still known as a camera signal");
    CameraDng::Content read;
    QString error;
    check(CameraDng::read(path, read, &error), "and still read");
    if (read.cameraRgb.size() != ipr::Size(2, 2)) {
        check(false, "of its size");
        return;
    }
    // A code is (code - 1024) * headroom / 64511 in the file's channels, before the balance.
    const auto expected = [&](uint16_t code, int c) { return (double(code) - 1024.0) * headroom / 64511.0 / neutral[c]; };
    double worst = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, std::abs(double(read.cameraRgb.row<ipr::Vec3f>(i / 2)[i % 2][c]) - expected(codes[size_t(i * 3 + c)], c)));
    check(worst < 1e-6, "with the values it was written with, below the black too");
    check(read.cameraNeutral == neutral && read.renderOptions.cameraBaselineExposureEv == 0.25 && read.iso == 800,
          "and its description");

    // A broken headroom is refused, not guessed.
    const QString broken = folder.filePath(QStringLiteral("integer-broken.dng"));
    check(writeIntegerFile(broken, neutral, 20.0, codes, 2), "a first-format file with a broken headroom");
    check(!CameraDng::read(broken, read, &error) && !error.isEmpty(), "is refused with its reason");
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
    // A 16-bit float: 11 significant bits, whatever the level.
    const double worst = largestRelativeError(read.cameraRgb, written.cameraRgb);
    if (worst > halfPrecision)
        std::cerr << "worst " << worst << '\n';
    check(worst <= halfPrecision, "the same values within a half's precision, below the black too");
    check(imagetest::rangeOf(read.cameraRgb).low < -0.004, "the values below the black stay");
    check(read.cameraMake == written.cameraMake && read.cameraModel == written.cameraModel, "the same camera");
    check(read.cameraNeutral == written.cameraNeutral && read.whiteBalanceKelvin == written.whiteBalanceKelvin, "the same balance");
    check(read.renderOptions.cameraBaselineExposureEv == 0.25 && read.renderOptions.profileExposureCompensationEv == -0.1 &&
              read.renderOptions.preserveToneCurveHue && read.calibratedWhite == 13995, "the same calibration");
    check(read.provenance == written.provenance, "the same provenance");
    check(read.lensModel == written.lensModel && read.iso == 1600 && read.exposureSeconds == 0.005 &&
              read.focalLengthMm == 200.0 && read.captureDate == written.captureDate, "the same capture");

    // A merge of exposures: from 1e-4 to a thousand times white, the same relative precision.
    CameraDng::Content wide = sample();
    const double pixels = double(wide.cameraRgb.width()) * double(wide.cameraRgb.height());
    for (int y = 0; y < wide.cameraRgb.height(); ++y)
        for (int x = 0; x < wide.cameraRgb.width(); ++x)
            for (int c = 0; c < 3; ++c)
                wide.cameraRgb.mutableRow<ipr::Vec3f>(y)[x][c] =
                    float(1e-4 * std::pow(1e7, double(y * wide.cameraRgb.width() + x) / pixels) * (1.0 + 0.1 * c));
    const QString widePath = folder.filePath(QStringLiteral("wide.dng"));
    check(CameraDng::write(wide, widePath, &error) && CameraDng::read(widePath, read, &error), "a wide range goes through");
    check(imagetest::rangeOf(read.cameraRgb).high > 900.f, "far above white stays far above white");
    check(largestRelativeError(read.cameraRgb, wide.cameraRgb) <= halfPrecision, "with the same relative precision");

    // What other software reads: the standard tags of a LinearRaw file.
    TIFF *tiff = TIFFOpen(path.toUtf8().constData(), "r");
    check(tiff != nullptr, "an ordinary TIFF container");
    if (tiff) {
        uint16_t photometric = 0, bits = 0, count = 0, format = 0, compression = 0, predictor = 0;
        const float *neutral = nullptr, *black = nullptr;
        const uint8_t *version = nullptr;
        const float *matrix = nullptr;
        float baseline = 0.f;
        const char *model = nullptr, *unique = nullptr;
        check(TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric) == 1 && photometric == 34892, "LinearRaw");
        check(TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits) == 1 && bits == 16 &&
                  TIFFGetField(tiff, TIFFTAG_SAMPLEFORMAT, &format) == 1 && format == SAMPLEFORMAT_IEEEFP, "16-bit floats");
        check(TIFFGetField(tiff, TIFFTAG_COMPRESSION, &compression) == 1 && compression == COMPRESSION_ADOBE_DEFLATE &&
                  TIFFGetField(tiff, TIFFTAG_PREDICTOR, &predictor) == 1 && predictor == PREDICTOR_FLOATINGPOINT,
              "deflated after the floating-point predictor");
        check(TIFFGetField(tiff, TIFFTAG_DNGVERSION, &version) == 1 && version && version[0] == 1 && version[1] == 4,
              "DNG 1.4, the first with floating-point samples");
        check(TIFFGetField(tiff, TIFFTAG_ASSHOTNEUTRAL, &count, &neutral) == 1 && count == 3 &&
                  std::abs(neutral[0] - 0.5f) < 1e-6f && std::abs(neutral[1] - 1.f) < 1e-6f, "the as-shot neutral");
        check(TIFFGetField(tiff, TIFFTAG_BLACKLEVEL, &count, &black) != 1 && TIFFGetField(tiff, TIFFTAG_WHITELEVEL, &count, &black) != 1,
              "no black and no white level: 0 and 1, the defaults of floating-point samples");
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
            const double worst = ipr::largestDifference(direct.linearRgba, opened.linearRgba);
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
    integerFormat(folder);
    refusals(folder);
    decoding(folder);
    publishing(folder);
    if (passed)
        std::cout << "camera dng: ok\n";
    return passed ? 0 : 1;
}
