#include "imageeditor.h"
#include "DecodedImageCache.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "RawDecoder.h"
#include "CameraProfileEngine.h"
#include <tiffio.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

QString awaitExport(ImageEditor &editor, const QUrl &target,
                    const QString &format)
{
    QEventLoop loop;
    QString finalStatus;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::statusMessageChanged, &loop, [&] {
            const QString status = editor.statusMessage();
            if (status.startsWith(QStringLiteral("Image exportée")) ||
                status.startsWith(QStringLiteral("Échec de l'export")) ||
                status.startsWith(QStringLiteral("Échec de la conversion couleur")) ||
                status.startsWith(QStringLiteral("Format d'export non pris en charge"))) {
                finalStatus = status;
                loop.quit();
            }
        });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    editor.exportImage(target, format, 92);
    // A synchronous validation error can arrive before exec() (Qt 6.4).
    if (finalStatus.isEmpty())
        loop.exec();
    QObject::disconnect(connection);
    if (!finalStatus.startsWith(QStringLiteral("Image exportée")))
        std::cerr << "Export status: " << finalStatus.toStdString() << '\n';
    return finalStatus;
}

bool awaitHistogram(ImageEditor &editor)
{
    QEventLoop loop;
    bool available = editor.histogram().size() == 4;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::histogramChanged, &loop, [&] {
            available = editor.histogram().size() == 4;
            if (available)
                loop.quit();
        });
    if (!available) {
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QObject::disconnect(connection);
    return available;
}

bool awaitLoad(ImageEditor &editor, const QUrl &fileUrl)
{
    QEventLoop loop;
    bool loaded = !editor.busy() && editor.hasImage();
    const auto connection = QObject::connect(
        &editor, &ImageEditor::hasImageChanged, &loop, [&] {
            loaded = !editor.busy() && editor.hasImage();
            if (loaded)
                loop.quit();
        });
    const auto busyConnection = QObject::connect(
        &editor, &ImageEditor::busyChanged, &loop, [&] {
            loaded = !editor.busy() && editor.hasImage();
            if (loaded)
                loop.quit();
        });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    editor.loadImage(fileUrl);
    loaded = !editor.busy() && editor.hasImage();
    if (!loaded)
        loop.exec();
    QObject::disconnect(connection);
    QObject::disconnect(busyConnection);
    return loaded;
}

bool verifyDecodedImageCache()
{
    ColorPipeline::WorkingImage image;
    image.linearRgba = cv::Mat::zeros(10, 10, CV_32FC4);
    DecodedImageCache::Entry sharedProxy{image, image, {}};
    const quint64 imageBytes = DecodedImageCache::entrySizeBytes(sharedProxy);
    if (!check(imageBytes == 10U * 10U * 4U * sizeof(float),
               "a shallow edit proxy must not be counted twice"))
        return false;

    auto context = std::make_shared<CameraProfileEngine::DcpWorkingContext>();
    context->cameraRgb = cv::Mat::zeros(10, 10, CV_32FC3);
    auto withContext = sharedProxy;
    withContext.image.dcpContext = context;
    withContext.editProxy.dcpContext = context;
    if (!check(DecodedImageCache::entrySizeBytes(withContext) == imageBytes + 1200,
               "shared camera source counted exactly once in LRU budget")) return false;
    DecodedImageCache limited(imageBytes);
    limited.insert("oversized-context", withContext);
    if (!check(limited.count() == 0, "pre-DCP storage cannot bypass the LRU budget")) return false;

    DecodedImageCache cache(imageBytes * 2);
    cache.insert(QStringLiteral("first"), sharedProxy);
    cache.insert(QStringLiteral("second"), sharedProxy);
    if (!check(cache.count() == 2 && cache.sizeBytes() == imageBytes * 2,
               "cache should retain entries within its explicit budget"))
        return false;

    if (!check(cache.find(QStringLiteral("first")).has_value(),
               "a cache lookup should return the stored image"))
        return false;
    cache.insert(QStringLiteral("third"), sharedProxy);
    if (!check(cache.find(QStringLiteral("first")).has_value() &&
                   !cache.find(QStringLiteral("second")).has_value() &&
                   cache.find(QStringLiteral("third")).has_value(),
               "cache should evict the least-recently-used entry first"))
        return false;

    cache.setBudgetBytes(imageBytes);
    return check(cache.count() == 1 && cache.sizeBytes() == imageBytes &&
                     cache.find(QStringLiteral("third")).has_value(),
                 "reducing the budget should release old cached buffers");
}

bool verifyToneCurvesAndHistogram(ImageEditor &editor, const QString &directory)
{
    const QVariantList curve{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
        QVariantMap{{QStringLiteral("x"), 0.6}, {QStringLiteral("y"), 0.9}},
        QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
    };
    const QColor original(96, 144, 200, 255);
    for (int channel = ImageEditor::LuminanceCurve;
         channel <= ImageEditor::BlueCurve; ++channel) {
        editor.setToneCurve(channel, curve);
        if (!check(awaitHistogram(editor),
                   "tone curve should publish a complete histogram"))
            return false;

        const QVariantList histogram = editor.histogram();
        if (!check(histogram.size() == 4,
                   "histogram should provide luminance and RGB channels"))
            return false;
        for (const QVariant &histogramChannel : histogram) {
            const QVariantList values = histogramChannel.toList();
            double count = 0.0;
            for (const QVariant &value : values)
                count += value.toDouble();
            if (!check(values.size() == 256 && std::abs(count - 6.0) < 0.01,
                       "each histogram channel should contain all source pixels"))
                return false;
        }

        const QString path = directory + QStringLiteral("/tone-%1.png").arg(channel);
        const QString status = awaitExport(
            editor, QUrl::fromLocalFile(path), QStringLiteral("PNG"));
        const QImage output(path);
        if (!check(status.startsWith(QStringLiteral("Image exportée")) &&
                       !output.isNull(),
                   "tone curve export should succeed"))
            return false;
        const QColor pixel = output.pixelColor(0, 0);
        if (channel == ImageEditor::LuminanceCurve) {
            if (!check(pixel.red() > original.red() &&
                           pixel.green() > original.green() &&
                           pixel.blue() > original.blue(),
                       "luminance curve should brighten all RGB channels"))
                return false;
        } else {
            const int changed = channel - ImageEditor::RedCurve;
            QImage sample(1,1,QImage::Format_RGBA8888);
            sample.setPixelColor(0,0,original); sample.setColorSpace(QColorSpace(QColorSpace::SRgb));
            auto expectedMaster = ColorPipeline::fromSourceImage(sample);
            auto &v = expectedMaster.linearRgba.at<cv::Vec4f>(0,0)[changed];
            // Interpolating (0,0), (.6,.9), (1,1) gives this quadratic.
            v += 1.25f * v * (1.f - v);
            const auto expected = ColorPipeline::toSrgb(expectedMaster,
                ColorPipeline::OutputFormat::Preview8Bit).pixelColor(0,0);
            if (!check(std::abs(pixel.red()-expected.red()) <= 2 &&
                       std::abs(pixel.green()-expected.green()) <= 2 &&
                       std::abs(pixel.blue()-expected.blue()) <= 2,
                       "RGB curve must edit a Rec.2020 channel before the sRGB output matrix"))
                return false;
        }
        editor.resetToneCurve(channel);
        if (!check(awaitHistogram(editor),
                   "tone curve reset should publish a complete histogram"))
            return false;
    }

    if (!check(editor.toneCurve(ImageEditor::LuminanceCurve).size() == 2,
               "reset tone curve should restore the identity endpoints"))
        return false;

    const QVariantList quadraticCurve{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
        QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.75}},
        QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
    };
    editor.setToneCurve(ImageEditor::LuminanceCurve, quadraticCurve);
    const QString quadraticPath = directory + QStringLiteral("/quadratic-tone.png");
    const QString quadraticStatus = awaitExport(
        editor, QUrl::fromLocalFile(quadraticPath), QStringLiteral("PNG"));
    const QImage quadraticImage(quadraticPath);
    editor.resetToneCurve(ImageEditor::LuminanceCurve);
    if (!check(quadraticStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !quadraticImage.isNull() &&
                   quadraticImage.pixelColor(0, 0).red() > 118,
               "one internal point should produce a quadratic tone curve"))
        return false;
    return true;
}

bool verifyOutput(ImageEditor &editor, const QString &directory,
                  const QString &format, const QString &inputSuffix,
                  const QString &expectedSuffix)
{
    const QString stem = directory + QStringLiteral("/export-%1-%2")
        .arg(format, inputSuffix.isEmpty() ? QStringLiteral("no-suffix") : inputSuffix);
    QString requested = stem;
    if (!inputSuffix.isEmpty())
        requested += QLatin1Char('.') + inputSuffix;
    const QUrl requestedUrl = QUrl::fromLocalFile(requested);
    const QString status = awaitExport(
        editor, requestedUrl, format);
    if (!check(status.startsWith(QStringLiteral("Image exportée")),
               "export should finish successfully"))
        return false;

    const QString expectedPath =
        stem + QLatin1Char('.') + expectedSuffix;
    if (!check(QFileInfo::exists(expectedPath),
               "export should use the extension matching the selected format"))
        return false;

    QFile existing(expectedPath);
    if (!existing.open(QIODevice::ReadOnly)) return false;
    const QByteArray originalBytes = existing.readAll();
    existing.close();
    const QString repeatedStatus = awaitExport(editor, requestedUrl, format);
    const QString repeatedPath = stem + QStringLiteral("-2.") + expectedSuffix;
    if (!existing.open(QIODevice::ReadOnly)) return false;
    if (!check(repeatedStatus.startsWith(QStringLiteral("Image exportée")) &&
               repeatedStatus.endsWith(repeatedPath) && !QImage(repeatedPath).isNull() &&
               existing.readAll() == originalBytes,
               "repeated JPEG/PNG/TIFF exports must be numbered without replacing the first file")) return false;
    existing.close();

    QImageReader reader(expectedPath);
    const QImage output = reader.read();
    if (!check(!output.isNull() && output.size() == QSize(3, 2),
               "exported file should decode with the original dimensions"))
        return false;

    if (format == QStringLiteral("PNG") || format == QStringLiteral("TIFF")) {
        if (!check(output.colorSpace().isValid() &&
                       output.colorSpace() == QColorSpace(QColorSpace::SRgb),
                   "lossless export should retain the sRGB profile"))
            return false;
    }

    if (format == QStringLiteral("PNG") &&
        !check(output.format() == QImage::Format_RGBA64,
               "PNG export should preserve high bit depth and alpha"))
        return false;
    if (format == QStringLiteral("TIFF") &&
        !check(output.format() == QImage::Format_RGBA64,
               "TIFF export should preserve high bit depth and alpha"))
        return false;

    const QColor pixel = output.pixelColor(0, 0);
    const int redTolerance = format == QStringLiteral("JPEG") ? 12 : 2;
    if (!check(pixel.alpha() == 255 &&
                   std::abs(pixel.red() - 96) <= redTolerance,
               "exported pixel values should match the rendered source"))
        return false;
    return true;
}

bool imagesDiffer(const QImage &first, const QImage &second)
{
    if (first.size() != second.size())
        return true;
    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            if (first.pixelColor(x, y) != second.pixelColor(x, y))
                return true;
        }
    }
    return false;
}

bool verifyUniqueExportNames(ImageEditor &editor, const QString &directory)
{
    const QByteArray marker("existing file: must not be overwritten");
    const auto occupy = [&](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(marker) == marker.size();
    };
    const auto unchanged = [&](const QString &path, const QByteArray &expected) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) && file.readAll() == expected;
    };
    for (const QString &format : {QStringLiteral("JPEG"), QStringLiteral("PNG"),
                                 QStringLiteral("TIFF"), QStringLiteral("DNG")}) {
        const QUrl first = editor.defaultExportFile(format);
        const QFileInfo info(first.toLocalFile());
        const auto numbered = [&](int number) {
            return info.dir().filePath(info.completeBaseName() + QStringLiteral("-%1.").arg(number) + info.suffix());
        };
        if (!check(first.isLocalFile() && occupy(first.toLocalFile()) && occupy(numbered(3)) &&
                   editor.defaultExportFile(format).toLocalFile() == numbered(2) &&
                   occupy(numbered(2)) && editor.defaultExportFile(format).toLocalFile() == numbered(4) &&
                   unchanged(first.toLocalFile(), marker),
                   "each format should suggest the first free number without changing existing files")) return false;
    }
    struct Case { const char *request; const char *existing; const char *expected; };
    const Case cases[] = {
        {"duplicate.png", "duplicate.png", "duplicate-2.png"},
        {"without-extension", "without-extension.png", "without-extension-2.png"},
        {"wrong-extension.jpg", "wrong-extension.png", "wrong-extension-2.png"},
        {"numbered-5.png", "numbered-5.png", "numbered-7.png"},
        {"padded-009.png", "padded-009.png", "padded-011.png"},
        {"name.with.dots.png", "name.with.dots.png", "name.with.dots-2.png"},
        {"été #100%.png", "été #100%.png", "été #100%-2.png"},
    };
    if (!occupy(directory + QStringLiteral("/numbered-6.png")) ||
        !occupy(directory + QStringLiteral("/padded-010.png"))) return false;
    for (const Case &test : cases) {
        const QString request = directory + QLatin1Char('/') + QString::fromUtf8(test.request);
        const QString existing = directory + QLatin1Char('/') + QString::fromUtf8(test.existing);
        const QString expected = directory + QLatin1Char('/') + QString::fromUtf8(test.expected);
        if (!occupy(existing)) return false;
        const QString status = awaitExport(editor, QUrl::fromLocalFile(request), QStringLiteral("PNG"));
        if (!check(status.startsWith(QStringLiteral("Image exportée")) && status.endsWith(expected) &&
                   !QImage(expected).isNull() && unchanged(existing, marker),
                   "numbering must use the normalized format and preserve existing file bytes")) return false;
    }
    const QString source = directory + QStringLiteral("/input.png");
    QFile original(source);
    if (!original.open(QIODevice::ReadOnly)) return false;
    const QByteArray sourceBytes = original.readAll();
    original.close();
    const QString status = awaitExport(editor, QUrl::fromLocalFile(source), QStringLiteral("PNG"));
    if (!check(status.startsWith(QStringLiteral("Image exportée")) &&
               status.endsWith(QStringLiteral("/input-2.png")) && unchanged(source, sourceBytes),
               "exporting to the source name must create a numbered copy instead of overwriting the image")) return false;

    // The dialog must count in the chosen export folder, not beside the RAW.
    const QString exportDirectory = directory + QStringLiteral("/other-destination");
    if (!QDir().mkpath(exportDirectory)) return false;
    QUrl previousTarget = QUrl::fromLocalFile(exportDirectory + QStringLiteral("/input-IPR.png"));
    for (int number = 1; number <= 4; ++number) {
        const QString expected = exportDirectory + QStringLiteral("/input-IPR") +
            (number == 1 ? QString() : QStringLiteral("-%1").arg(number)) + QStringLiteral(".png");
        const QUrl suggestion = editor.defaultExportFile(QStringLiteral("PNG"), previousTarget);
        if (!check(suggestion.toLocalFile() == expected,
                   "successive dialog suggestions must progress in the accepted destination, not the source folder")) return false;
        const QString exportStatus = awaitExport(editor, suggestion, QStringLiteral("PNG"));
        if (!check(exportStatus.startsWith(QStringLiteral("Image exportée")) && exportStatus.endsWith(expected),
                   "the suggested next name must match the actual exported file")) return false;
        previousTarget = suggestion;
    }
    const QUrl otherFormat = editor.defaultExportFile(QStringLiteral("JPEG"), previousTarget);
    return check(otherFormat.toLocalFile() == exportDirectory + QStringLiteral("/input-IPR.jpg") &&
                 editor.defaultExportFile(QStringLiteral("PNG"), QUrl(QStringLiteral("https://example.invalid/export.png"))).isEmpty(),
                 "changing format must retain the destination and nonlocal destinations must be rejected");
}

bool verifyColorMixer(ImageEditor &editor, const QString &directory)
{
    // Rec.2020 HSV centres with low enough chroma to fit the sRGB fixture file.
    // Otherwise gamut mapping changes the centre before it can be reimported.
    ColorPipeline::WorkingImage patches;
    patches.linearRgba = cv::Mat(1, 8, CV_32FC4);
    const std::array<cv::Vec4f, 8> colors{{
        {0.35f, 0.25f, 0.25f, 1.0f}, {0.35f, 0.30f, 0.25f, 1.0f},
        {0.35f, 0.35f, 0.25f, 1.0f}, {0.25f, 0.35f, 0.25f, 1.0f},
        {0.25f, 0.35f, 0.35f, 1.0f}, {0.25f, 0.25f, 0.35f, 1.0f},
        {0.30f, 0.25f, 0.35f, 1.0f}, {0.35f, 0.25f, 0.35f, 1.0f}}};
    for (int band = 0; band < 8; ++band)
        patches.linearRgba.at<cv::Vec4f>(0, band) = colors[band];
    const QString sourcePath = directory + QStringLiteral("/mixer-source.png");
    if (!ColorPipeline::toSrgb(patches, ColorPipeline::OutputFormat::HighBitDepth)
             .save(sourcePath, "PNG") ||
        !awaitLoad(editor, QUrl::fromLocalFile(sourcePath)))
        return false;
    editor.resetAdjustments();
    const auto exportVariant = [&](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name + QStringLiteral(".png");
        const QString status = awaitExport(editor, QUrl::fromLocalFile(path), QStringLiteral("PNG"));
        return status.startsWith(QStringLiteral("Image exportée")) ? QImage(path) : QImage{};
    };
    const QImage base = exportVariant(QStringLiteral("mixer-base"));
    if (base.isNull())
        return false;
    for (int band = 0; band < 8; ++band) {
        for (int component = 0; component < 3; ++component) {
            editor.resetAdjustments();
            editor.setColorMixer(band, component == 0 ? 70 : 0,
                                 component == 1 ? -70 : 0,
                                 component == 2 ? -70 : 0);
            const QImage output = exportVariant(
                QStringLiteral("mixer-%1-%2").arg(band).arg(component));
            if (!check(!output.isNull() &&
                       output.pixelColor(band, 0) != base.pixelColor(band, 0),
                       "each of eight mixer bands should work with global saturation/vibrance at zero"))
                return false;
            // Lightroom bands overlap their neighbours: only non-adjacent bands stay intact.
            for (int other = 0; other < 8; ++other) {
                if ((other - band + 8) % 8 <= 1 || (band - other + 8) % 8 <= 1)
                    continue;
                const QColor actual = output.pixelColor(other, 0);
                const QColor expected = base.pixelColor(other, 0);
                if (!check(std::abs(actual.red() - expected.red()) <= 2 &&
                           std::abs(actual.green() - expected.green()) <= 2 &&
                           std::abs(actual.blue() - expected.blue()) <= 2,
                           "a mixer band should preserve the non-adjacent band centres"))
                    return false;
            }
        }
    }
    editor.resetAdjustments();
    editor.setColorGrading(1, 200, 60);
    const QImage grading = exportVariant(QStringLiteral("grading-alone"));
    if (!check(!grading.isNull() && imagesDiffer(base, grading),
               "grading should work with global saturation/vibrance at zero"))
        return false;

    editor.resetAdjustments();
    editor.setColorMixer(1, 27, -43, 18);
    editor.setColorMixer(6, -31, 17, 42);
    editor.setColorMixer(7, 51, -22, -63);
    // Loading another image flushes the pending save of the source sidecar.
    const QUrl other = QUrl::fromLocalFile(directory + QStringLiteral("/input.png"));
    if (!awaitLoad(editor, other) || !awaitLoad(editor, QUrl::fromLocalFile(sourcePath)))
        return false;
    if (!check(editor.colorMixerValue(1, 0) == 27 &&
               editor.colorMixerValue(1, 1) == -43 && editor.colorMixerValue(1, 2) == 18,
               "orange settings should survive sidecar v4 roundtrip"))
        return false;
    if (!check(editor.colorMixerValue(6, 0) == -31 && editor.colorMixerValue(6, 1) == 17 &&
               editor.colorMixerValue(6, 2) == 42 && editor.colorMixerValue(7, 0) == 51 &&
               editor.colorMixerValue(7, 1) == -22 && editor.colorMixerValue(7, 2) == -63,
               "violet and magenta must roundtrip independently")) return false;
    QFile currentSidecar(sourcePath + QStringLiteral(".ipr"));
    if (!currentSidecar.open(QIODevice::ReadOnly)) return false;
    QDataStream currentStream(&currentSidecar);
    currentStream.setVersion(QDataStream::Qt_6_4);
    quint32 magic; quint16 savedVersion;
    currentStream >> magic >> savedVersion;
    if (!check(magic == 0x49505231u && savedVersion == 7, "new sidecars must declare version 7")) return false;
    currentSidecar.close();
    editor.resetAdjustments();
    for (int band = 0; band < 8; ++band)
        for (int component = 0; component < 3; ++component)
            if (!check(editor.colorMixerValue(band, component) == 0,
                       "reset should neutralize all eight mixer bands"))
                return false;

    for (quint16 version : {quint16(1), quint16(2), quint16(3), quint16(4), quint16(5), quint16(6)}) {
        const QString legacyPath = directory + QStringLiteral("/legacy-%1.png").arg(version);
        if (!QImage(sourcePath).save(legacyPath, "PNG"))
            return false;
        QFile sidecar(legacyPath + QStringLiteral(".ipr"));
        if (!sidecar.open(QIODevice::WriteOnly))
            return false;
        QDataStream stream(&sidecar);
        stream.setVersion(QDataStream::Qt_6_4);
        const QFileInfo info(legacyPath);
        stream << quint32(0x49505231u) << version << qint64(info.size())
               << qint64(info.lastModified().toMSecsSinceEpoch());
        stream << float(0) << qint32(0) << qint32(0) << false;
        for (int index = 0; index < 12; ++index)
            stream << qint32(index == 4 ? 6500 : 0);
        if (version >= 2)
            stream << qint32(12) << qint32(34) << qint32(56) << float(1.5) << qint32(78);
        for (int band = 0; band < (version >= 4 ? 8 : (version == 3 ? 7 : 6)); ++band)
            stream << qint32(11 + band) << qint32(21 + band) << qint32(31 + band);
        for (int zone = 0; zone < 3; ++zone)
            stream << qint32(200 + zone) << qint32(4 + zone);
        stream << false;
        for (int curve = 0; curve < 4; ++curve)
            stream << qint32(2) << float(0) << float(0) << float(1) << float(1);
        stream << int(3) << QStringLiteral("green") << true << true << true;
        if (version >= 5)
            stream << QString() << QString() << QString() << QString() << false << QString();
        if (version >= 6)
            stream << QString() << QString() << false << false << false;
        if (stream.status() != QDataStream::Ok)
            return false;
        sidecar.close();
        if (!awaitLoad(editor, QUrl::fromLocalFile(legacyPath)))
            return false;
        for (int band = 0; band < 8; ++band) {
            for (int component = 0; component < 3; ++component) {
                const int oldBand = version >= 4 ? band : version == 3 ? (band == 7 ? 6 : band)
                    : (band == 0 ? 0 : (band == 7 ? 5 : band - 1));
                const int expected = (version < 4 && band == 6) || (version < 3 && band == 1)
                    ? 0 : 11 + 10 * component + oldBand;
                if (!check(editor.colorMixerValue(band, component) == expected,
                           "legacy mixer channels should migrate without shifting colors"))
                    return false;
            }
        }
        if (!check(editor.colorGradingValue(2, 0) == 202 &&
                   editor.colorGradingValue(2, 1) == 6 &&
                   editor.adjustmentValue(QStringLiteral("denoiseLuminance")) == (version >= 2 ? 12 : 0) &&
                   editor.colorGradingBlending() == 50 && editor.colorGradingBalance() == 0,
                   "legacy migration should preserve following fields, noise settings and default blending"))
            return false;
    }
    editor.resetAdjustments();
    return true;
}

bool verifyEffects(ImageEditor &editor, const QString &directory)
{
    QImage source(96, 64, QImage::Format_RGBA8888);
    cv::RNG noise(812);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const int gradient = 70 + x * 95 / (source.width() - 1);
            const int detail = ((x / 4 + y / 4) % 2) == 0 ? 18 : -18;
            // The old fixture contained only a clean ramp/checkerboard. A noise
            // estimator must not be required to mistake that texture for noise.
            // Add deterministic luminance/chroma noise and keep the pattern so
            // this test still exercises all effects through the actual export.
            const int grain = static_cast<int>(std::lround(noise.gaussian(5.0)));
            source.setPixelColor(x, y, QColor(
                qBound(0, gradient + detail + y / 5 + grain + static_cast<int>(std::lround(noise.gaussian(3.0))), 255),
                qBound(0, gradient + detail + 25 + grain + static_cast<int>(std::lround(noise.gaussian(3.0))), 255),
                qBound(0, gradient + detail + 50 + grain + static_cast<int>(std::lround(noise.gaussian(3.0))), 255)));
        }
    }
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/effects-source.png");
    if (!source.save(inputPath, "PNG"))
        return false;
    if (!check(awaitLoad(editor, QUrl::fromLocalFile(inputPath)), "effects source should load"))
        return false;

    const QString baselinePath = directory + QStringLiteral("/effects-base.png");
    const QString baselineStatus = awaitExport(
        editor, QUrl::fromLocalFile(baselinePath), QStringLiteral("PNG"));
    const QImage baseline(baselinePath);
    if (!check(baselineStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !baseline.isNull(),
               "effects baseline export should succeed"))
        return false;

    const auto verifyEffect = [&](const QString &name,
                                  void (ImageEditor::*setter)(int)) {
        (editor.*setter)(100);
        const QString path = directory + QStringLiteral("/effects-%1.png").arg(name);
        const QString status = awaitExport(
            editor, QUrl::fromLocalFile(path), QStringLiteral("PNG"));
        const QImage effect(path);
        editor.resetAdjustments();
        return check(status.startsWith(QStringLiteral("Image exportée")) &&
                         !effect.isNull() && imagesDiffer(baseline, effect),
                     qPrintable(name + QStringLiteral(
                         " should change the rendered export")));
    };

    return verifyEffect(QStringLiteral("texture"), &ImageEditor::setTexture) &&
           verifyEffect(QStringLiteral("clarity"), &ImageEditor::setClarity) &&
           verifyEffect(QStringLiteral("dehaze"), &ImageEditor::setDehaze) &&
           verifyEffect(QStringLiteral("grain"), &ImageEditor::setGrain) &&
           verifyEffect(QStringLiteral("denoise-luminance"),
                        &ImageEditor::setDenoiseLuminance) &&
           verifyEffect(QStringLiteral("denoise-chrominance"),
                        &ImageEditor::setDenoiseChrominance) &&
           verifyEffect(QStringLiteral("sharpening"),
                        &ImageEditor::setSharpeningAmount);
}

bool verifyDngOutput(ImageEditor &editor, const QString &directory)
{
    QImage input(64, 64, QImage::Format_RGBA8888);
    input.fill(QColor(96, 144, 200, 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/dng-source.png");
    if (!input.save(inputPath, "PNG"))
        return false;
    if (!awaitLoad(editor, QUrl::fromLocalFile(inputPath)))
        return false;

    const QString requestedPath = directory + QStringLiteral("/linear.jpeg");
    const QString expectedPath = directory + QStringLiteral("/linear.dng");
    const QString status = awaitExport(
        editor, QUrl::fromLocalFile(requestedPath), QStringLiteral("DNG"));
    if (!check(status.startsWith(QStringLiteral("Image exportée")) &&
                   QFileInfo::exists(expectedPath) &&
                   QFileInfo(expectedPath).size() > 0,
               "DNG export should write a nonempty .dng file"))
        return false;

#ifdef Q_OS_WIN
    TIFF *tiff = TIFFOpenW(reinterpret_cast<const wchar_t *>(expectedPath.utf16()), "r");
#else
    TIFF *tiff = TIFFOpen(expectedPath.toUtf8().constData(), "r");
#endif
    if (!check(tiff != nullptr, "DNG IFD should open")) return false;
    uint32_t count = 0, profileBytes = 0;
    float *matrix = nullptr;
    void *profile = nullptr;
    const bool hasMatrix = TIFFGetField(tiff, TIFFTAG_COLORMATRIX1, &count, &matrix) == 1 && count == 9;
    const bool hasIcc = TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &profileBytes, &profile) == 1;
    const QColorSpace embedded = hasIcc ? QColorSpace::fromIccProfile(
        QByteArray(static_cast<const char *>(profile), int(profileBytes))) : QColorSpace{};
    const cv::Matx33d expectedMatrix = WorkingColor::srgbToXyz().inv();
    bool consistent = hasMatrix && embedded == QColorSpace(QColorSpace::SRgbLinear);
    if (hasMatrix) for (int i=0; i<9; ++i) consistent &= std::abs(matrix[i]-expectedMatrix.val[i]) < 1e-6;
    TIFFClose(tiff);
    if (!check(consistent, "DNG linear samples need linear ICC and XYZ-to-stored-RGB matrix")) return false;

#ifdef IPR_WITH_LIBRAW
    QFile firstDng(expectedPath);
    if (!firstDng.open(QIODevice::ReadOnly)) return false;
    const QByteArray firstDngBytes = firstDng.readAll();
    firstDng.close();
    const QString repeatedStatus = awaitExport(editor, QUrl::fromLocalFile(requestedPath), QStringLiteral("DNG"));
    if (!firstDng.open(QIODevice::ReadOnly)) return false;
    if (!check(repeatedStatus.endsWith(QStringLiteral("/linear-2.dng")) &&
               firstDng.readAll() == firstDngBytes && QFileInfo::exists(directory + QStringLiteral("/linear-2.dng")),
               "the rendered DNG writer must also preserve an occupied normalized filename")) return false;
    firstDng.close();
    const auto imported = ImageImport::load(
        expectedPath,
        RawDecoder::Options{
            true, {directory + QStringLiteral("/empty-profile-directory")}});
    if (imported.image.isNull())
        std::cerr << "DNG decode error: " << imported.error.toStdString() << '\n';
    return check(!imported.image.isNull() && imported.capabilities.cameraDevelopment &&
                     imported.image.colorSpace == ColorPipeline::workingColorSpace() &&
                     imported.image.linearRgba.size() == cv::Size(64, 64),
                 "exported linear DNG should use the common import contract through LibRaw");
#else
    return true;
#endif
}

bool verifyHighlightRecovery(ImageEditor &editor, const QString &directory)
{
    QImage input(2, 1, QImage::Format_RGBA8888);
    input.setPixelColor(0, 0, QColor(240, 240, 240, 255));
    input.setPixelColor(1, 0, QColor(40, 40, 40, 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/tonal-source.png");
    if (!input.save(inputPath, "PNG"))
        return false;

    if (!awaitLoad(editor, QUrl::fromLocalFile(inputPath)))
        return false;
    editor.setBrightness(-0.49f);
    editor.setContrast(41);
    editor.setHighlights(-100);
    const QString exportPath = directory + QStringLiteral("/tonal.jpg");
    const QString status = awaitExport(
        editor, QUrl::fromLocalFile(exportPath), QStringLiteral("JPEG"));
    const QImage output(exportPath);
    if (!check(status.startsWith(QStringLiteral("Image exportée")) &&
                   !output.isNull(),
               "JPEG export should succeed with tonal adjustments"))
        return false;

    const QColor highlight = output.pixelColor(0, 0);
    const QColor shadow = output.pixelColor(1, 0);
    std::cout << "Tonal combination: highlight " << highlight.red() << ',' << highlight.green() << ','
              << highlight.blue() << " shadow " << shadow.red() << ',' << shadow.green() << ','
              << shadow.blue() << '\n';
    // Guards against inversion to black, not a calibrated level: the perceptual
    // contrast curve yields 157 here (the former linear curve gave >= 160).
    if (!check(highlight.red() >= 140 && highlight.green() >= 140 &&
                   highlight.blue() >= 140 && highlight.red() > shadow.red() + 100,
               "highlight recovery should not turn bright pixels black") ||
        !check(shadow.red() > 0 && shadow.green() > 0 && shadow.blue() > 0,
               "contrast should preserve nonzero shadow detail"))
        return false;

    editor.resetAdjustments();
    editor.setWhites(-100);
    const QString whitesPath = directory + QStringLiteral("/whites.jpg");
    const QString whitesStatus = awaitExport(
        editor, QUrl::fromLocalFile(whitesPath), QStringLiteral("JPEG"));
    const QImage whitesImage(whitesPath);
    if (!check(whitesStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !whitesImage.isNull() &&
                   whitesImage.pixelColor(0, 0).red() > 160,
               "white recovery should not turn bright pixels black"))
        return false;

    editor.resetAdjustments();
    editor.setShadows(-100);
    const QString shadowsPath = directory + QStringLiteral("/shadows.jpg");
    const QString shadowsStatus = awaitExport(
        editor, QUrl::fromLocalFile(shadowsPath), QStringLiteral("JPEG"));
    const QImage shadowsImage(shadowsPath);
    if (!check(shadowsStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !shadowsImage.isNull() &&
                   shadowsImage.pixelColor(1, 0).red() > 0,
               "shadow darkening should preserve nonzero shadow detail"))
        return false;

    editor.resetAdjustments();
    editor.setBlacks(-100);
    const QString blacksPath = directory + QStringLiteral("/blacks.png");
    const QString blacksStatus = awaitExport(
        editor, QUrl::fromLocalFile(blacksPath), QStringLiteral("PNG"));
    const QImage blacksImage(blacksPath);
    // -100 raises a true black point, like Lightroom: this dark pixel may reach 0.
    if (!check(blacksStatus.startsWith(QStringLiteral("Image exportée")) && !blacksImage.isNull(),
               "black point export should succeed"))
        return false;
    editor.resetAdjustments();
    editor.setBlacks(-50);
    const QString halfBlacksPath = directory + QStringLiteral("/blacks-half.png");
    const QString halfBlacksStatus = awaitExport(
        editor, QUrl::fromLocalFile(halfBlacksPath), QStringLiteral("PNG"));
    const QImage halfBlacks(halfBlacksPath);
    if (!check(halfBlacksStatus.startsWith(QStringLiteral("Image exportée")) && !halfBlacks.isNull() &&
                   halfBlacks.pixelColor(1, 0).red() > 0 &&
                   halfBlacks.pixelColor(1, 0).red() >= blacksImage.pixelColor(1, 0).red(),
               "moderate black adjustment should preserve nonzero dark detail"))
        return false;

    editor.resetAdjustments();
    editor.setShadows(-100);
    editor.setBlacks(-100);
    const QString darkTonesPath = directory + QStringLiteral("/dark-tones.png");
    const QString darkTonesStatus = awaitExport(
        editor, QUrl::fromLocalFile(darkTonesPath), QStringLiteral("PNG"));
    const QImage darkTonesImage(darkTonesPath);
    return check(darkTonesStatus.startsWith(QStringLiteral("Image exportée")) &&
                     !darkTonesImage.isNull() &&
                     darkTonesImage.pixelColor(1, 0).red() >=
                         blacksImage.pixelColor(1, 0).red() - 2,
                 "combined black and shadow adjustments should not compound");
}

// Graded-zone rule for Lumières tone controls (18 October 2026): as in
// Lightroom, a control may reach the whole tonal range, but its effect, in EV
// of linear light, is weakest at the opposite end of the histogram. Tones next
// to white, in the soft shoulder, and black samples are left out: their
// shift is capped by headroom. Shadows, Highlights and positive Whites adapt to the image, so
// minimum strengths hold for a bright synthetic image too.
double evShift(const QImage &base, const QImage &variant, int x)
{
    const auto linear = [](int code) {
        const double v = code / 255.0;
        return std::max(1e-6, v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
    };
    return std::log2(linear(variant.pixelColor(x, 0).red()) / linear(base.pixelColor(x, 0).red()));
}

bool oppositeEndMovesLeast(const QImage &base, const QImage &variant, int opposite,
                           std::initializer_list<int> others)
{
    const double far = std::abs(evShift(base, variant, opposite));
    for (int x : others) {
        const int code = variant.pixelColor(x, 0).red();
        // Black, or inside the white shoulder (linear > 0.70, code 219): the
        // shift is capped by headroom, not by the control's zone.
        if (code == 0 || code >= 219) continue;
        if (far > std::abs(evShift(base, variant, x)) + 0.03) return false;
    }
    return true;
}

bool verifyBlackShadowSeparation(ImageEditor &editor, const QString &directory)
{
    QImage input(3, 1, QImage::Format_RGBA8888);
    input.setPixelColor(0, 0, QColor(30, 30, 30, 255));
    input.setPixelColor(1, 0, QColor(110, 110, 110, 255));
    input.setPixelColor(2, 0, QColor(220, 220, 220, 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/black-shadow-source.bmp");
    if (!input.save(inputPath, "BMP") ||
        !awaitLoad(editor, QUrl::fromLocalFile(inputPath)))
        return false;

    const auto exportVariant = [&](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name +
            QStringLiteral(".jpg");
        const QString status = awaitExport(
            editor, QUrl::fromLocalFile(path), QStringLiteral("JPEG"));
        return std::pair<QString, QImage>{status, QImage(path)};
    };

    editor.resetAdjustments();
    const auto [baseStatus, base] = exportVariant(QStringLiteral("black-shadow-base"));
    if (!check(!baseStatus.isEmpty() &&
                   !base.isNull(),
               "baseline export should succeed for black/shadow separation"))
        return false;

    editor.resetAdjustments();
    editor.setBlacks(100);
    const auto [blackStatus, blackLift] =
        exportVariant(QStringLiteral("black-lift"));
    if (!check(!blackStatus.isEmpty() &&
                   !blackLift.isNull() &&
                   blackLift.pixelColor(0, 0).red() >
                       base.pixelColor(0, 0).red() + 5 &&
                   blackLift.pixelColor(1, 0).red() >= base.pixelColor(1, 0).red() &&
                   oppositeEndMovesLeast(base, blackLift, 2, {0, 1}),
               "black lift should be strongest on dark tones and weakest on highlights"))
        return false;

    editor.resetAdjustments();
    editor.setBlacks(-100);
    const auto [blackDarkStatus, blackDark] =
        exportVariant(QStringLiteral("black-darkening"));
    if (!check(!blackDarkStatus.isEmpty() && !blackDark.isNull() &&
                   blackDark.pixelColor(0, 0).red() + 5 < base.pixelColor(0, 0).red() &&
                   blackDark.pixelColor(2, 0).red() > blackDark.pixelColor(1, 0).red() &&
                   oppositeEndMovesLeast(base, blackDark, 2, {0, 1}),
               "black darkening should be strongest on dark tones and weakest on highlights"))
        return false;

    editor.resetAdjustments();
    editor.setShadows(-100);
    const auto [shadowDarkStatus, shadowDark] =
        exportVariant(QStringLiteral("shadow-darkening"));
    if (!check(!shadowDarkStatus.isEmpty() &&
                   !shadowDark.isNull() &&
                   shadowDark.pixelColor(0, 0).red() + 5 <
                       base.pixelColor(0, 0).red() &&
                   shadowDark.pixelColor(1, 0).red() + 5 <
                       base.pixelColor(1, 0).red() &&
                   oppositeEndMovesLeast(base, shadowDark, 2, {0, 1}),
               "shadow darkening should be strongest on dark tones and weakest on highlights"))
        return false;

    editor.resetAdjustments();
    editor.setShadows(100);
    const auto [shadowStatus, shadowLift] =
        exportVariant(QStringLiteral("shadow-lift"));
    return check(!shadowStatus.isEmpty() &&
                     !shadowLift.isNull() &&
                     shadowLift.pixelColor(0, 0).red() >
                         base.pixelColor(0, 0).red() + 5 &&
                     shadowLift.pixelColor(1, 0).red() >= base.pixelColor(1, 0).red() &&
                     oppositeEndMovesLeast(base, shadowLift, 2, {0, 1}),
                 "shadow lift should be strongest on dark tones and weakest on highlights");
}

bool verifyPositiveShadowRamp(ImageEditor &editor, const QString &directory)
{
    QImage input(256, 1, QImage::Format_RGBA8888);
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int x=0;x<256;++x) input.setPixelColor(x,0,QColor(x,x,x));
    const QString path=directory+QStringLiteral("/shadow-ramp.png");
    if (!input.save(path) || !awaitLoad(editor,QUrl::fromLocalFile(path))) return false;
    editor.resetAdjustments();
    editor.setShadows(100);
    const QString target=directory+QStringLiteral("/shadow-ramp.tiff");
    if (!awaitExport(editor,QUrl::fromLocalFile(target),QStringLiteral("TIFF"))
            .startsWith(QStringLiteral("Image exportée"))) return false;
    const QImage output(target);
    if (!check(!output.isNull(),"Shadow ramp TIFF unreadable")) return false;
    if (!check(output.pixelColor(0,0).red()==0,"Shadow ramp lifts absolute black")) return false;
    for (int x=1;x<256;++x) {
        if (!check(output.pixelColor(x,0).red()>=output.pixelColor(x-1,0).red(),
                   "Production shadow ramp reverses dark tone ordering")) return false;
    }
    // A pedestal is a jump from black to the first level; Lightroom itself lifts
    // the deepest tones by 2.3-3 EV at +100, so only the step shape is checked.
    const int first=output.pixelColor(1,0).red(), eighth=output.pixelColor(8,0).red();
    return check(first-output.pixelColor(0,0).red()<=eighth-first,"Production shadow ramp creates black pedestal");
}

bool verifyPositiveHighlightColour(ImageEditor &editor, const QString &directory)
{
    QImage input(3,1,QImage::Format_RGBA8888);
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    input.setPixelColor(0,0,QColor(0,0,240));
    input.setPixelColor(1,0,QColor(240,100,30));
    input.setPixelColor(2,0,QColor(120,30,220));
    const QString path=directory+QStringLiteral("/highlight-colour.png");
    if (!input.save(path) || !awaitLoad(editor,QUrl::fromLocalFile(path))) return false;
    editor.resetAdjustments(); editor.setHighlights(100);
    const QString target=directory+QStringLiteral("/highlight-colour.tiff");
    if (!awaitExport(editor,QUrl::fromLocalFile(target),QStringLiteral("TIFF"))
            .startsWith(QStringLiteral("Image exportée"))) return false;
    const QImage output(target);
    if (!check(!output.isNull(),"Highlight colour TIFF unreadable")) return false;
    for (int x=0;x<3;++x) {
        const QColor colour=output.pixelColor(x,0);
        if (!check(std::max({colour.red(),colour.green(),colour.blue()})<255,
                   "Production positive highlights clip saturated colour")) return false;
    }
    return true;
}

bool verifyHighlightWhiteSeparation(ImageEditor &editor, const QString &directory)
{
    QImage input(4, 1, QImage::Format_RGBA8888);
    input.setPixelColor(0, 0, QColor(35, 35, 35, 255));
    input.setPixelColor(1, 0, QColor(120, 120, 120, 255));
    input.setPixelColor(2, 0, QColor(190, 190, 190, 255));
    input.setPixelColor(3, 0, QColor(235, 235, 235, 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory +
        QStringLiteral("/highlight-white-source.bmp");
    if (!input.save(inputPath, "BMP") ||
        !awaitLoad(editor, QUrl::fromLocalFile(inputPath)))
        return false;

    const auto exportVariant = [&](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name +
            QStringLiteral(".jpg");
        const QString status = awaitExport(
            editor, QUrl::fromLocalFile(path), QStringLiteral("JPEG"));
        return std::pair<QString, QImage>{status, QImage(path)};
    };

    editor.resetAdjustments();
    const auto [baseStatus, base] = exportVariant(QStringLiteral("highlight-white-base"));
    if (!check(!baseStatus.isEmpty() && !base.isNull(),
               "baseline export should succeed for highlight/white separation"))
        return false;

    editor.resetAdjustments();
    editor.setHighlights(100);
    const auto [highlightLiftStatus, highlightLift] =
        exportVariant(QStringLiteral("highlight-lift"));
    if (!check(!highlightLiftStatus.isEmpty() && !highlightLift.isNull() &&
                   oppositeEndMovesLeast(base, highlightLift, 0, {1, 2}) &&
                   highlightLift.pixelColor(2, 0).red() >
                       base.pixelColor(2, 0).red() + 8,
               "highlight lift should be strongest on upper tones and weakest on blacks"))
        return false;

    editor.resetAdjustments();
    editor.setHighlights(-100);
    const auto [highlightDarkStatus, highlightDark] =
        exportVariant(QStringLiteral("highlight-darkening"));
    if (!check(!highlightDarkStatus.isEmpty() && !highlightDark.isNull() &&
                   oppositeEndMovesLeast(base, highlightDark, 0, {1, 2}) &&
                   highlightDark.pixelColor(2, 0).red() + 8 <
                       base.pixelColor(2, 0).red(),
               "highlight recovery should be strongest on upper tones and weakest on blacks"))
        return false;

    editor.resetAdjustments();
    editor.setWhites(100);
    const auto [whiteLiftStatus, whiteLift] =
        exportVariant(QStringLiteral("white-lift"));
    if (!check(!whiteLiftStatus.isEmpty() && !whiteLift.isNull() &&
                   oppositeEndMovesLeast(base, whiteLift, 0, {1, 2, 3}) &&
                   whiteLift.pixelColor(2, 0).red() >
                       base.pixelColor(2, 0).red() + 15 &&
                   whiteLift.pixelColor(3, 0).red() >=
                       whiteLift.pixelColor(2, 0).red(),
               "white lift should be strongest near white and weakest on blacks"))
        return false;

    editor.resetAdjustments();
    editor.setWhites(-100);
    const auto [whiteDarkStatus, whiteDark] =
        exportVariant(QStringLiteral("white-darkening"));
    return check(!whiteDarkStatus.isEmpty() && !whiteDark.isNull() &&
                     oppositeEndMovesLeast(base, whiteDark, 0, {1, 2}) &&
                     whiteDark.pixelColor(2, 0).red() + 8 <
                         base.pixelColor(2, 0).red(),
                 "white recovery should be strongest near white and weakest on blacks");
}

bool verifyHistogramValues(ImageEditor &editor, const QString &directory)
{
    // Known pixels: the histogram counts the displayed sRGB codes per channel,
    // and bins luminance (linear Y of those codes) in sRGB encoding.
    QImage input(2, 2, QImage::Format_RGBA8888);
    input.setPixelColor(0, 0, QColor(255, 0, 0));
    input.setPixelColor(1, 0, QColor(0, 255, 0));
    input.setPixelColor(0, 1, QColor(0, 0, 255));
    input.setPixelColor(1, 1, QColor(128, 128, 128));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/histogram-source.png");
    editor.resetAdjustments();
    if (!input.save(inputPath, "PNG") || !awaitLoad(editor, QUrl::fromLocalFile(inputPath)) ||
        !awaitHistogram(editor))
        return check(false, "histogram source should load");
    const QVariantList histogram = editor.histogram();
    const auto channel = [&](int index) { return histogram.at(index).toList(); };
    const auto count = [&](int index, int from, int to) {
        double total = 0;
        const QVariantList values = channel(index);
        for (int bin = std::max(0, from); bin <= std::min(255, to); ++bin) total += values.at(bin).toDouble();
        return total;
    };
    double pixels = 0;
    for (const QVariant &value : channel(1)) pixels += value.toDouble();
    // Y of pure red/green/blue: 0.2126/0.7152/0.0722 -> sRGB codes 127/220/76.
    return check(pixels == 4 &&
                     count(1, 254, 255) == 1 && count(1, 127, 129) == 1 && count(1, 0, 1) == 2 &&
                     count(2, 254, 255) == 1 && count(3, 254, 255) == 1 &&
                     count(0, 76, 76) == 1 && count(0, 127, 127) == 1 &&
                     count(0, 128, 128) == 1 && count(0, 220, 220) == 1,
                 "histogram must count displayed sRGB codes and bin luminance in sRGB encoding");
}

bool verifyContrastResponse(ImageEditor &editor, const QString &directory)
{
    // Black, deep shadow, shadow, pivot (0.32 linear ~ sRGB 153), highlight,
    // white and an orange: per-channel perceptual S curve with fixed endpoints.
    const QList<QColor> colors{QColor(0, 0, 0), QColor(30, 30, 30), QColor(105, 105, 105),
        QColor(153, 153, 153), QColor(210, 210, 210), QColor(255, 255, 255), QColor(200, 150, 50)};
    QImage input(int(colors.size()), 1, QImage::Format_RGBA8888);
    for (int x = 0; x < colors.size(); ++x) input.setPixelColor(x, 0, colors[x]);
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/contrast-source.png");
    if (!input.save(inputPath, "PNG") ||
        !awaitLoad(editor, QUrl::fromLocalFile(inputPath)))
        return false;

    const auto exportVariant = [&](const QString &name) {
        const QString path = directory + QLatin1Char('/') + name + QStringLiteral(".png");
        const QString status = awaitExport(editor, QUrl::fromLocalFile(path), QStringLiteral("PNG"));
        return std::pair<QString, QImage>{status, QImage(path)};
    };
    const auto red = [](const QImage &image, int x) { return image.pixelColor(x, 0).red(); };
    const auto hueShift = [](const QImage &a, const QImage &b) {
        const int delta = std::abs(a.pixelColor(6, 0).hsvHue() - b.pixelColor(6, 0).hsvHue());
        return std::min(delta, 360 - delta);
    };
    const auto saturation = [](const QImage &image) { return image.pixelColor(6, 0).hsvSaturation(); };

    editor.resetAdjustments();
    const auto [baseStatus, base] = exportVariant(QStringLiteral("contrast-base"));
    if (!check(!baseStatus.isEmpty() && !base.isNull(),
               "baseline export should succeed for contrast response"))
        return false;

    editor.resetAdjustments();
    editor.setContrast(100);
    const auto [strongStatus, strong] = exportVariant(QStringLiteral("contrast-strong"));
    if (!check(!strongStatus.isEmpty() && !strong.isNull() &&
                   red(strong, 0) <= red(base, 0) + 1 && red(strong, 5) >= red(base, 5) - 1 &&
                   red(strong, 1) + 5 < red(base, 1) && red(strong, 2) + 5 < red(base, 2) &&
                   std::abs(red(strong, 3) - red(base, 3)) <= 6 &&
                   red(strong, 4) > red(base, 4) + 5 && hueShift(strong, base) <= 8 &&
                   saturation(strong) > saturation(base),
               "positive contrast darkens below and brightens above the pivot, fixed endpoints, saturates colour"))
        return false;
    for (int x = 1; x < 5; ++x)
        if (!check(red(strong, x) < red(strong, x + 1), "positive contrast keeps tonal order"))
            return false;

    editor.resetAdjustments();
    editor.setContrast(-100);
    const auto [flatStatus, flat] = exportVariant(QStringLiteral("contrast-flat"));
    return check(!flatStatus.isEmpty() && !flat.isNull() &&
                     red(flat, 0) <= red(base, 0) + 1 && red(flat, 5) >= red(base, 5) - 1 &&
                     red(flat, 1) > red(base, 1) + 5 && red(flat, 2) > red(base, 2) + 5 &&
                     std::abs(red(flat, 3) - red(base, 3)) <= 6 &&
                     red(flat, 4) + 5 < red(base, 4) && hueShift(flat, base) <= 8 &&
                     saturation(flat) < saturation(base) &&
                     red(flat, 1) < red(flat, 2) && red(flat, 3) < red(flat, 4),
                 "negative contrast lifts below and lowers above the pivot, fixed endpoints, desaturates colour");
}

bool awaitPreview(ImageEditor &editor)
{
    // busy includes the render debounce now; awaitLoad can already have waited
    // for the completed preview, so do not wait for an event that has fired.
    if (!editor.busy() && !editor.imageSource().isEmpty()) return true;
    QEventLoop loop;
    bool rendered = false;
    const auto connection = QObject::connect(
        &editor, &ImageEditor::previewChanged, &loop, [&] {
            if (!editor.imageSource().isEmpty()) {
                rendered = true;
                loop.quit();
            }
        });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    return rendered;
}

bool verifyOriginalPreview(ImageEditor &editor, ImageProvider &provider,
                           const QString &directory)
{
    QImage input(80, 60, QImage::Format_RGBA8888);
    input.fill(QColor(70, 110, 150));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString path = directory + QStringLiteral("/comparison-source.png");
    if (!input.save(path) || !awaitLoad(editor, QUrl::fromLocalFile(path)) || !awaitPreview(editor)) return false;
    const QImage baseline = provider.originalImage();
    const QString originalUrl = editor.originalImageSource();
    if (!check(!baseline.isNull() && baseline == provider.image() && !originalUrl.isEmpty(),
               "neutral source and current preview should initially match")) return false;
    QSize requested;
    const QImage originRequest = provider.requestImage(originalUrl.section(QLatin1Char('/'), 3), &requested, {});
    if (!check(originRequest == baseline && requested == baseline.size(),
               "origin provider URL should return the baseline, not the current image")) return false;
    editor.setBrightness(1);
    if (!check(awaitPreview(editor) && provider.image() != baseline && provider.originalImage() == baseline
            && editor.originalImageSource() == originalUrl,
               "adjustments must not alter or reissue the origin preview")) return false;
    // Changing photo saves the edited sidecar. The next open restores +1 EV
    // while its origin remains unedited; no user photos or settings are used.
    const QString other = directory + QStringLiteral("/comparison-other.png");
    input.fill(QColor(20, 35, 50));
    if (!input.save(other)) return false;
    editor.loadImage(QUrl::fromLocalFile(other));
    if (!check(!provider.image().isNull() && provider.originalImage() == baseline
            && editor.originalImageSource() == originalUrl,
               "changing source preserves the displayed pair until replacement is ready")) return false;
    if (!awaitPreview(editor) || !awaitLoad(editor, QUrl::fromLocalFile(path)) || !awaitPreview(editor)) return false;
    if (!check(editor.adjustmentValue(QStringLiteral("brightness")) == 1
            && provider.originalImage() == baseline && provider.image() != baseline,
               "origin should ignore restored sidecar settings")) return false;
    editor.resetAdjustments();
    return check(awaitPreview(editor) && provider.image() == baseline && provider.originalImage() == baseline,
                 "reset should restore the baseline without modifying it");
}

bool verifyEditProxyDownscaling(ImageEditor &editor, ImageProvider &provider,
                                const QString &directory)
{
    // Larger than the 2048 px edit-proxy cap on its long side, so loading it
    // exercises ImageEditor::buildEditProxy()'s cv::resize() path.
    constexpr int width = 2200;
    constexpr int height = 1600;
    QImage input(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        uchar *line = input.scanLine(y);
        for (int x = 0; x < width; ++x) {
            line[x * 4 + 0] = static_cast<uchar>(x * 255 / width);
            line[x * 4 + 1] = static_cast<uchar>(y * 255 / height);
            line[x * 4 + 2] = 128;
            line[x * 4 + 3] = 255;
        }
    }
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory + QStringLiteral("/edit-proxy-source.png");
    if (!input.save(inputPath, "PNG"))
        return false;

    if (!check(awaitLoad(editor, QUrl::fromLocalFile(inputPath)),
               "oversized source image should load"))
        return false;
    if (!check(awaitPreview(editor),
               "preview should render for the oversized image"))
        return false;

    const QImage preview = provider.image();
    const int previewLongSide = std::max(preview.width(), preview.height());
    if (!check(!preview.isNull() && previewLongSide <= 2048,
               "interactive preview should be capped to the edit proxy resolution"))
        return false;
    if (!check(provider.originalImage().size() == preview.size(),
               "origin comparison should use the same capped resolution as current preview")) return false;

    const QString exportPath = directory + QStringLiteral("/edit-proxy-export.png");
    const QString status = awaitExport(
        editor, QUrl::fromLocalFile(exportPath), QStringLiteral("PNG"));
    const QImage exported(exportPath);
    return check(status.startsWith(QStringLiteral("Image exportée")) &&
                     exported.width() == width && exported.height() == height,
                 "export should keep the full source resolution regardless of "
                 "the edit proxy");
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Optional destination: verify the exact user-reported directory using an
    // isolated temporary child. Never read or overwrite the real photo files.
    const QDir parent(argc == 2 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath());
    if (!check(parent.exists(), "test destination must be an existing directory")) return 1;
    QTemporaryDir directory(parent.filePath(QStringLiteral(".ipr-export-test-XXXXXX")));
    if (!check(directory.isValid(), "temporary output directory should be created"))
        return 1;
    if (!verifyDecodedImageCache())
        return 1;

    QImage input(3, 2, QImage::Format_RGBA8888);
    input.fill(QColor(96, 144, 200, 255));
    input.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const QString inputPath = directory.filePath(QStringLiteral("input.png"));
    if (!check(input.save(inputPath, "PNG"), "source image should be written"))
        return 1;

    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    if (!check(awaitLoad(editor, QUrl::fromLocalFile(inputPath)) && editor.hasImage(),
               "editor should load the source image"))
        return 1;
    const QUrl defaultExport = editor.defaultExportFile(QStringLiteral("JPEG"));
    if (!check(defaultExport.isLocalFile() &&
                   defaultExport.fileName() == QStringLiteral("input-IPR.jpg"),
               "default export name should derive from the source name"))
        return 1;
    if (!verifyToneCurvesAndHistogram(editor, directory.path()))
        return 1;
    if (!awaitLoad(editor, QUrl::fromLocalFile(inputPath))) return 1;
    editor.resetAdjustments();
    if (!verifyUniqueExportNames(editor, directory.path())) return 1;

    if (!verifyOutput(editor, directory.path(), QStringLiteral("PNG"),
                      QStringLiteral("jpg"), QStringLiteral("png")) ||
        !verifyOutput(editor, directory.path(), QStringLiteral("PNG"),
                      QString(), QStringLiteral("png")) ||
        !verifyOutput(editor, directory.path(), QStringLiteral("JPEG"),
                      QStringLiteral("png"), QStringLiteral("jpg")))
        return 1;

    const QString nativePath = directory.filePath(QStringLiteral("native-path"));
    const QString nativeStatus = awaitExport(
        editor, QUrl(nativePath), QStringLiteral("PNG"));
    if (!check(nativeStatus.startsWith(QStringLiteral("Image exportée")) &&
                   QFileInfo::exists(nativePath + QStringLiteral(".png")),
               "absolute paths without a file URL scheme should export"))
        return 1;

    editor.setTemperature(50000);
    const QString warmPath = directory.filePath(QStringLiteral("warm.png"));
    const QString warmStatus = awaitExport(
        editor, QUrl::fromLocalFile(warmPath), QStringLiteral("PNG"));
    const QImage warmImage(warmPath);
    if (!check(warmStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !warmImage.isNull() &&
                   warmImage.pixelColor(0, 0).red() > 96 &&
                   warmImage.pixelColor(0, 0).blue() < 200,
               "temperature control should affect the exported image"))
        return 1;

    editor.resetAdjustments();
    editor.setTint(150);
    const QString tintPath = directory.filePath(QStringLiteral("tint.png"));
    const QString tintStatus = awaitExport(
        editor, QUrl::fromLocalFile(tintPath), QStringLiteral("PNG"));
    const QImage tintImage(tintPath);
    if (!check(tintStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !tintImage.isNull() &&
                   tintImage.pixelColor(0, 0).red() > 96 &&
                   tintImage.pixelColor(0, 0).green() < 144 &&
                   tintImage.pixelColor(0, 0).blue() > 200,
               "tint control should affect the exported image"))
        return 1;

    editor.resetAdjustments();
    editor.setSaturation(-100);
    const QString desaturatedPath =
        directory.filePath(QStringLiteral("desaturated.png"));
    const QString desaturatedStatus = awaitExport(
        editor, QUrl::fromLocalFile(desaturatedPath), QStringLiteral("PNG"));
    const QImage desaturatedImage(desaturatedPath);
    if (!check(desaturatedStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !desaturatedImage.isNull() &&
                   std::abs(desaturatedImage.pixelColor(0, 0).red() -
                            desaturatedImage.pixelColor(0, 0).green()) <= 1 &&
                   std::abs(desaturatedImage.pixelColor(0, 0).green() -
                            desaturatedImage.pixelColor(0, 0).blue()) <= 1,
               "saturation control should remove chrominance at -100"))
        return 1;

    editor.resetAdjustments();
    editor.setVibrance(100);
    const QString vibrantPath = directory.filePath(QStringLiteral("vibrant.png"));
    const QString vibrantStatus = awaitExport(
        editor, QUrl::fromLocalFile(vibrantPath), QStringLiteral("PNG"));
    const QImage vibrantImage(vibrantPath);
    if (!check(vibrantStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !vibrantImage.isNull() &&
                   vibrantImage.pixelColor(0, 0).blue() >
                       200,
               "vibrance control should increase the chrominance of the export"))
        return 1;

    editor.resetAdjustments();
    editor.setVibrance(-100);
    const QString reducedVibrancePath = directory.filePath(QStringLiteral("reduced-vibrance.png"));
    const QString reducedVibranceStatus = awaitExport(
        editor, QUrl::fromLocalFile(reducedVibrancePath), QStringLiteral("PNG"));
    const QImage reducedVibranceImage(reducedVibrancePath);
    if (!check(reducedVibranceStatus.startsWith(QStringLiteral("Image exportée")) &&
                   !reducedVibranceImage.isNull(), "negative vibrance must export successfully"))
        return 1;
    const QColor reducedColour = reducedVibranceImage.pixelColor(0, 0);
    const int reducedSpread = std::max({reducedColour.red(), reducedColour.green(), reducedColour.blue()}) -
                              std::min({reducedColour.red(), reducedColour.green(), reducedColour.blue()});
    // Lightroom keeps a residual on saturated colours at -100 (input spread 104).
    if (!check(reducedSpread > 0 && reducedSpread < 104 / 3,
               "negative vibrance must strongly reduce export chroma with a residual"))
        return 1;

    editor.resetAdjustments();
    const QList<QByteArray> writerFormats = QImageWriter::supportedImageFormats();
    if ((writerFormats.contains("tiff") || writerFormats.contains("tif")) &&
        !verifyOutput(editor, directory.path(), QStringLiteral("TIFF"),
                      QStringLiteral("jpeg"), QStringLiteral("tiff")))
        return 1;

    if (!verifyColorMixer(editor, directory.path()))
        return 1;

    if (!verifyEffects(editor, directory.path()))
        return 1;

    if (!verifyDngOutput(editor, directory.path()))
        return 1;

    if (!verifyHighlightRecovery(editor, directory.path()))
        return 1;

    if (!verifyBlackShadowSeparation(editor, directory.path()))
        return 1;
    if (!verifyPositiveShadowRamp(editor, directory.path()))
        return 1;
    if (!verifyPositiveHighlightColour(editor, directory.path()))
        return 1;

    if (!verifyHighlightWhiteSeparation(editor, directory.path()))
        return 1;

    if (!verifyContrastResponse(editor, directory.path()))
        return 1;
    if (!verifyHistogramValues(editor, directory.path()))
        return 1;

    if (!verifyEditProxyDownscaling(editor, provider, directory.path()))
        return 1;
    if (!verifyOriginalPreview(editor, provider, directory.path()))
        return 1;

    const QString unsupportedStatus =
        awaitExport(editor, QUrl::fromLocalFile(
                                directory.filePath(QStringLiteral("unsupported.png"))),
                    QStringLiteral("BMP"));
    if (!check(unsupportedStatus.startsWith(
                   QStringLiteral("Format d'export non pris en charge")),
               "unsupported export format should be reported"))
        return 1;

    const QString ioErrorStatus = awaitExport(
        editor, QUrl::fromLocalFile(directory.filePath(
                    QStringLiteral("missing/export.png"))),
        QStringLiteral("PNG"));
    if (!check(ioErrorStatus.startsWith(QStringLiteral("Échec de l'export")),
               "file write errors should be reported to the user"))
        return 1;

    return 0;
}
