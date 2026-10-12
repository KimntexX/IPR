#define IPR_GENERATOR_UNIT_TEST
#include "../src/tools/generate_test_images.cpp"

#include <QColorSpace>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <tiffio.h>

namespace {
void check(bool ok, const QString &message)
{
    if (!ok) throw std::runtime_error(message.toUtf8().constData());
}

QByteArray read(const QString &path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path);
    return file.readAll();
}

void writeFixture(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write fixture");
}

struct Result { int code; QByteArray output; };
Result run(const QString &executable, const QString &cwd, const QStringList &args)
{
    QProcess process;
    process.setWorkingDirectory(cwd);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, args);
    check(process.waitForStarted(10000), "Generator did not start: " + process.errorString());
    check(process.waitForFinished(60000), "Generator timed out");
    check(process.exitStatus() == QProcess::NormalExit, "Generator crashed: " + process.readAll());
    return {process.exitCode(), process.readAll()};
}

void checkTiff(const QString &path)
{
    TIFF *tiff = TIFFOpen(QFile::encodeName(path).constData(), "r");
    check(tiff != nullptr, "Missing TIFF: " + path);
    uint16_t bits = 0;
    uint32_t width = 0, height = 0, iccLength = 0;
    void *icc = nullptr;
    TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &iccLength, &icc);
    TIFFClose(tiff);
    check(bits == 16 && width == 48 && height == 32 && iccLength > 0,
          "Export must be full-resolution, 16-bit and ICC-tagged");
}

QString makeBase(QTemporaryDir &directory, const QImage &image)
{
    const QString base = directory.filePath("tests/image/base");
    check(QDir().mkpath(base), "Cannot create base fixture");
    check(image.save(base + "/photo with spaces.png"), "Cannot save source fixture");
    return base;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(app.arguments().size() == 2, "Expected generator executable argument");
        const QString executable = app.arguments().at(1);
        QImage image(48, 32, QImage::Format_RGB32);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, QColor(30 + x * 4, 40 + y * 5, 50 + (x + y) * 2));

        // Unit-test the current presets and their isolation, not a legacy mode.
        IprTestImageGenerator::Settings baseline;
        for (const Feature &feature : features()) {
            check(groups.contains(featureGroup(feature.name)), "Unmapped group");
            check(!featureFolder(feature.name).contains("generation-"), "Dated directory returned");
            for (double value : feature.values) {
                const auto settings = IprTestImageGenerator::adjusted(baseline, feature.name, value);
                if (feature.name.startsWith("mixer-")) {
                    const auto parts = feature.name.split('-');
                    const int selectedBand = bands.indexOf(parts.at(1));
                    for (int band = 0; band < ColorMixer::BandCount; ++band) {
                        check(settings.mixerHue.at(band) ==
                              (parts.at(2) == "hue" && band == selectedBand ? int(value) : 0), "Hue isolation broken");
                        check(settings.mixerSaturation.at(band) ==
                              (parts.at(2) == "saturation" && band == selectedBand ? int(value) : 0), "Saturation isolation broken");
                        check(settings.mixerLuminance.at(band) ==
                              (parts.at(2) == "luminance" && band == selectedBand ? int(value) : 0), "Luminance isolation broken");
                    }
                }
                if (feature.name == "tint") check(settings.tint == int(value), "Tint value mismatch");
                if (feature.name == "temperature") check(settings.temperature == int(value), "Temperature mismatch");
                if (feature.name == "sharpening-radius" || feature.name == "sharpening-masking")
                    check(settings.sharpeningAmount == 50, "Missing sharpening support");
                if (feature.name == "grading-shadows-hue")
                    check(settings.gradingSaturation.at(0) == 25, "Missing grading support");
            }
        }
        check(featureFolder("mixer-violet-hue") == "color-mixer/violet"
              && featureFolder("mixer-magenta-hue") == "color-mixer/magenta", "Violet/magenta folders conflated");
        check(featureFolder("vibrance") == "couleur/vibrance"
              && featureFolder("saturation") == "couleur/saturation", "Existing colour folders not reused");

        QTemporaryDir directory;
        check(directory.isValid(), "Cannot create temporary directory");
        const QString base = makeBase(directory, image);
        const QString input = base + "/photo with spaces.png";
        const QByteArray original = read(input);
        writeFixture(base + "/photo with spaces.png.ipr", "{\"settings\":{\"brightness\":4}}");
        writeFixture(base + "/photo with spaces.xmp", "do not change this metadata");
        const QByteArray sidecar = read(base + "/photo with spaces.png.ipr");
        check(QDir().mkpath(base + "/lumiere/exposure"), "Cannot create shared LR folder");
        const QString lr = base + "/lumiere/exposure/photo with spaces-2.tif";
        writeFixture(lr, "LR sentinel: must not be decoded or overwritten");
        // Subdirectory images must never be selected as inputs.
        check(QDir().mkpath(base + "/nested"), "Cannot create nested folder");
        writeFixture(base + "/nested/broken.png", "not an image");

        for (const QStringList &args : QList<QStringList>{{}, {"unknown"}, {"all", "couleur"}, {"--feature", "exposure"},
                                                         {"--group", "couleur"}, {"--legacy"}, {"help", "all"}})
            check(run(executable, directory.path(), args).code != 0, "Removed/invalid arguments accepted");
        check(!QFileInfo::exists(base + "/neutre"), "Rejected arguments wrote output");
        check(run(executable, directory.path(), {"optique"}).code != 0,
              "Optics with no RAW source reported a completed generation");
        check(!QFileInfo::exists(base + "/neutre"), "Ineligible group wrote output");

        const auto lighting = run(executable, directory.path(), {"lumiere"});
        check(lighting.code == 0, "Lighting generation failed: " + lighting.output);
        const QString neutral = base + "/neutre/photo with spaces-IPR-neutre-lumiere.tiff";
        checkTiff(neutral);
        const QString zero = base + "/lumiere/exposure/photo with spaces-IPR-exposure-0.tiff";
        check(QImage(zero) == QImage(neutral), "EV0 differs from current baseline");
        check(QImage(base + "/lumiere/exposure/photo with spaces-IPR-exposure--1.tiff") != QImage(neutral),
              "Exposure sweep has no effect");
        check(!QFileInfo::exists(base + "/couleur"), "One group generated another group");
        const QByteArray before = read(zero);
        check(run(executable, directory.path(), {"lumiere"}).code != 0, "Existing IPR exports accepted");
        check(read(zero) == before && read(lr) == "LR sentinel: must not be decoded or overwritten",
              "Existing IPR/LR exports modified");

        // A separate group can use the same source without reusing an old neutral.
        const auto colour = run(executable, directory.path(), {"couleur"});
        check(colour.code == 0, "Colour generation failed: " + colour.output);
        checkTiff(base + "/couleur/temperature/photo with spaces-IPR-temperature-5200.tiff");
        check(QImage(base + "/couleur/temperature/photo with spaces-IPR-temperature-5200.tiff") ==
              QImage(base + "/couleur/nuance/photo with spaces-IPR-tint-0.tiff"), "Manual WB anchors differ");
        check(read(input) == original && read(base + "/photo with spaces.png.ipr") == sidecar
              && read(base + "/photo with spaces.xmp") == "do not change this metadata", "Source/sidecars modified");

        QTemporaryDir allDirectory;
        const QString allBase = makeBase(allDirectory, image);
        const auto all = run(executable, allDirectory.path(), {"all"});
        check(all.code == 0, "All groups failed: " + all.output);
        qsizetype expected = 1; // one fresh neutral for all
        for (const Feature &feature : features()) {
            if (rawOnly(feature.name)) continue;
            for (double value : feature.values) {
                checkTiff(allBase + '/' + featureFolder(feature.name) + '/'
                          + fileName("photo with spaces", feature.name, value));
                ++expected;
            }
        }
        qsizetype written = 0;
        QDirIterator outputs(allBase, QDir::Files, QDirIterator::Subdirectories);
        while (outputs.hasNext()) {
            const QString path = outputs.next();
            if (path == allBase + "/photo with spaces.png") continue;
            check(path.endsWith(".tiff") && !path.contains("generation-"), "Non-image/datetime auxiliary file written: " + path);
            ++written;
        }
        check(written == expected, "All group export count incorrect");

        // Multiple root sources, case-insensitive extensions, empty/corrupt inputs.
        QTemporaryDir multiDirectory;
        const QString multiBase = makeBase(multiDirectory, image);
        check(image.save(multiBase + "/second.PNG", "PNG"), "Cannot create second source");
        check(run(executable, multiDirectory.path(), {"bruit"}).code == 0, "Multiple root images failed");
        checkTiff(multiBase + "/bruit/couleur/second-IPR-denoise-chrominance-100.tiff");
        QTemporaryDir badDirectory;
        check(QDir().mkpath(badDirectory.filePath("tests/image/base")), "Cannot create empty base");
        check(run(executable, badDirectory.path(), {"couleur"}).code != 0, "Empty base accepted");
        writeFixture(badDirectory.filePath("tests/image/base/broken.png"), "not an image");
        check(run(executable, badDirectory.path(), {"couleur"}).code != 0, "Corrupt source accepted");
        check(!QFileInfo::exists(badDirectory.filePath("tests/image/base/manifest.json")), "Failure wrote auxiliary manifest");

        // Current RAW decode options, independent of an installed camera profile.
        RawDecoder::Options received;
        QString error;
        check(RawDecoder::registerFormatHandler({"generatormockraw"},
            [&](const QString &, const RawDecoder::Options &options, RawDecoder::SourceMetadata *, QString *) {
                received = options;
                ColorPipeline::WorkingImage result;
                result.linearRgba = cv::Mat(8, 8, CV_32FC4, cv::Scalar(.1f, .2f, .3f, 1.f)).clone();
                result.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
                return result;
            }, &error), "Cannot register mock decoder");
        IprTestImageGenerator::load("sample.generatormockraw", true, false, true);
        check(received.automaticWhiteBalance && !received.lensCorrectionEnabled
              && received.chromaticAberrationEnabled && received.preferCameraNeutral
              && !received.useNormalizationEnvironment, "RAW decode options incorrect");

        QTextStream(stdout) << "Generator: root discovery, groups/all, current presets, TIFF/ICC, isolation, shared folders and safety passed.\n";
        return 0;
    } catch (const std::exception &error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
