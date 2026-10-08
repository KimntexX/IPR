#include <QCoreApplication>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <tiffio.h>

#include <stdexcept>

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

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write fixture");
}

struct Result { int code; QByteArray output; };
Result run(const QString &executable, const QStringList &args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, args);
    check(process.waitForStarted(10000), "Generator did not start: " + process.errorString());
    check(process.waitForFinished(60000), "Generator timed out");
    if (process.exitStatus() != QProcess::NormalExit)
        check(false, "Generator crashed: " + process.readAll());
    return {process.exitCode(), process.readAll()};
}

QJsonObject manifest(const QString &folder)
{
    return QJsonDocument::fromJson(read(folder + "/manifest.json")).object();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        check(app.arguments().size() == 2, "Expected generator executable argument");
        const QString executable = app.arguments().at(1);
        QTemporaryDir directory;
        check(directory.isValid(), "Cannot create temporary directory");
        const QString input = directory.filePath("photo with spaces.png");
        QImage image(48, 32, QImage::Format_RGB32);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, QColor(30 + x * 4, 40 + y * 5, 50 + (x + y) * 2));
        check(image.save(input), "Cannot save PNG fixture");
        const QByteArray original = read(input);
        const QString sidecar = directory.filePath("photo with spaces.ipr");
        const QString xmp = directory.filePath("photo with spaces.xmp");
        write(sidecar, "{\"settings\":{\"brightness\":4,\"contrast\":100}}");
        write(xmp, "do not change this metadata");
        const QByteArray originalSidecar = read(sidecar), originalXmp = read(xmp);

        const auto listed = run(executable, {"--list"});
        check(listed.code == 0 && listed.output.contains("mixer-violet-hue")
              && listed.output.contains("mixer-magenta-hue") && listed.output.contains("grading-midtones-hue"),
              "Feature catalogue incomplete: " + listed.output);
        const QString output = directory.filePath("campaign");
        const QStringList args{"--feature", "exposure", "--values=-1,0,1", "--output", output, input};
        const auto dry = run(executable, args + QStringList{"--dry-run"});
        check(dry.code == 0 && dry.output.contains("4 total") && !QFileInfo::exists(output),
              "Dry run must be write-free: " + dry.output);
        const auto generated = run(executable, args);
        check(generated.code == 0, "Export failed: " + generated.output);
        const QJsonObject document = manifest(output);
        const QJsonArray exports = document.value("exports").toArray();
        check(document.value("status") == "complete" && exports.size() == 4, "Missing neutral or exposure exports");
        check(document.value("bitDepth").toInt() == 16 && !document.value("hdr").toBool(), "Wrong output mode");
        check(document.value("profileSearchDirectories").toArray().size() >= 2,
              "Profile search roots missing");
        check(document.value("mixerBands").toArray().size() == 8, "Mixer band order missing");
        QList<QImage> rendered;
        for (const auto &entry : exports) {
            const QJsonObject object = entry.toObject();
            const QString path = output + '/' + object.value("file").toString();
            TIFF *tiff = TIFFOpen(QFile::encodeName(path).constData(), "r");
            check(tiff != nullptr, "Missing TIFF output");
            uint16_t bits = 0;
            uint32_t width = 0, height = 0, iccLength = 0;
            void *iccData = nullptr;
            TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
            TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
            TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
            TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &iccLength, &iccData);
            TIFFClose(tiff);
            check(bits == 16 && width == 48 && height == 32 && iccLength > 0,
                  "Export must be full-resolution 16-bit ICC-tagged TIFF");
            check(object.value("sha256").toString().size() == 64 && object.value("sourceSha256").toString().size() == 64,
                  "Hashes missing");
            const auto settings = object.value("settings").toObject();
            check(settings.value("contrast").toInt() == 0 && settings.value("tint").toInt() == 0,
                  "Sidecars or previous settings contaminated export");
            check(settings.value("exposure").toDouble() == object.value("value").toDouble(), "Exposure/value mismatch");
            QImage exported(path);
            check(!exported.isNull() && exported.colorSpace() == QColorSpace(QColorSpace::SRgb), "Cannot read sRGB export");
            rendered.append(exported);
        }
        check(rendered.at(0) == rendered.at(2), "Zero exposure must match baseline");
        check(rendered.at(1) != rendered.at(0) && rendered.at(3) != rendered.at(0), "Exposure variants have no effect");
        check(run(executable, args).code != 0, "Existing campaign must not be overwritten");
        check(manifest(output) == document, "Rejected run modified existing manifest");

        const QString allOutput = directory.filePath("parameters");
        // Exercise every post-render feature with its complete default sweep.
        // auto-wb needs actual RAW decoding, tested separately in rawDecoderTests.
        QStringList names;
        for (const QByteArray &line : listed.output.split('\n')) {
            const QString name = QString::fromUtf8(line).section(' ', 0, 0);
            if (!name.isEmpty() && name != "auto-wb" && name != "neutral") names.append(name);
        }
        const auto all = run(executable, {"--feature", names.join(','), "--format", "PNG", "--output", allOutput, input});
        check(all.code == 0, "Feature sweep failed: " + all.output);
        const auto allManifest = manifest(allOutput);
        const auto allExports = allManifest.value("exports").toArray();
        check(allExports.size() == allManifest.value("expectedExports").toInt(), "Sweep is incomplete");
        bool violetSeen = false, magentaSeen = false, curveSeen = false;
        for (const auto &entry : allExports) {
            const auto object = entry.toObject();
            const auto settings = object.value("settings").toObject();
            const QString feature = object.value("feature").toString();
            const int value = object.value("value").toInt();
            if (feature == "mixer-violet-hue" || feature == "mixer-magenta-hue") {
                const auto hues = settings.value("mixerHue").toArray();
                const int band = feature == "mixer-violet-hue" ? 6 : 7;
                check(hues.at(band).toInt() == value && hues.at(13 - band).toInt() == 0,
                      "Violet/magenta are not isolated");
                violetSeen |= band == 6;
                magentaSeen |= band == 7;
            }
            if (feature.startsWith("sharpening-") && feature != "sharpening-amount")
                check(settings.value("sharpeningAmount").toInt() == 50, "Radius/masking support amount missing");
            if (feature == "grading-shadows-hue")
                check(settings.value("grading").toArray().at(0).toObject().value("saturation").toInt() == 25,
                      "Grading hue would be invisible without saturation");
            if (feature == "curve-luminance") {
                const auto points = settings.value("toneCurves").toArray().at(0).toArray();
                check(points.size() == 3, "Curve preset missing");
                curveSeen = true;
            }
            check(QFileInfo::exists(allOutput + '/' + object.value("file").toString()), "Missing sweep file");
        }
        check(violetSeen && magentaSeen && curveSeen, "Feature coverage missing");
        check(read(input) == original && read(sidecar) == originalSidecar && read(xmp) == originalXmp,
              "Generation modified originals/sidecars");

        // Canonical duplicates are ignored; distinct paths with the same stem
        // must never collide, even when their source bytes are identical.
        check(QDir().mkpath(directory.filePath("other")), "Cannot create second fixture folder");
        const QString secondInput = directory.filePath("other/photo with spaces.png");
        write(secondInput, original);
        const QString multiOutput = directory.filePath("multiple");
        const auto multi = run(executable, {"--output", multiOutput, "--input", input, input, secondInput});
        check(multi.code == 0, "Multiple inputs failed: " + multi.output);
        const auto multiExports = manifest(multiOutput).value("exports").toArray();
        check(multiExports.size() == 2 && multiExports.at(0).toObject().value("file")
              != multiExports.at(1).toObject().value("file"), "Same-name sources collided or duplicate repeated");

#ifdef IPR_WITH_LIBRAW
        const QString rawPlan = directory.filePath("plan.CR3");
        write(rawPlan, "no decode should be attempted during a dry run");
        const QString dryAll = directory.filePath("dry-all");
        const auto allPlan = run(executable, {"--feature", "all", "--dry-run", "--output", dryAll, rawPlan});
        check(allPlan.code == 0 && !QFileInfo::exists(dryAll), "All-feature RAW plan failed: " + allPlan.output);
#endif

        const QString rejected = directory.filePath("rejected");
        for (const QStringList &options : QList<QStringList>{
                 {"--feature", "masques"}, {"--feature", "contrast", "--values=101"},
                 {"--feature", "contrast", "--values=0.5"}, {"--feature", "exposure", "--values=nan"},
                 {"--feature", "exposure", "--values=1,1"}, {"--feature", "auto-wb"},
                 {"--format", "HDR"}, {"--grading-saturation", "0"}}) {
            check(run(executable, options + QStringList{"--output", rejected, input}).code != 0,
                  "Invalid arguments were accepted");
            check(!QFileInfo::exists(rejected), "Invalid arguments created output");
        }
        const QString broken = directory.filePath("broken.png");
        write(broken, "not an image");
        check(run(executable, {"--output", rejected, broken}).code != 0, "Decode failure ignored");
        check(manifest(rejected).value("status") == "failed", "Partial campaign not marked failed");
        QTextStream(stdout) << "Generator: feature sweeps, TIFF/ICC, isolation, sidecars and error handling passed.\n";
        return 0;
    } catch (const std::exception &error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
