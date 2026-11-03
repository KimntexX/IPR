#include "imageeditor.h"
#include "CameraSignal.h"
#include "RecipeCodec.h"
#include "RenderStageCache.h"
#include "Sidecar.h"
#include "controlled_memory.h"
#include "imageprovider.h"
#include "synthetic_dcp.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

// Friend of ImageEditor, as in the other tests of its private render.
class IprTestImageGenerator
{
public:
    using Settings = ImageEditor::Settings;
    static ColorPipeline::WorkingImage render(const ColorPipeline::WorkingImage &source, const Settings &settings,
                                              double pixelScale = 1.0, RenderStageCache *cache = nullptr)
    {
        return ImageEditor::renderWorkingImage(source, settings, pixelScale, cache);
    }
};

namespace {

using Settings = IprTestImageGenerator::Settings;
using namespace LocalAdjustments;

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 60000)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    poll.start(10);
    if (!done()) loop.exec();
    return done();
}

cv::Mat noisyRgb(const cv::Size &size)
{
    cv::Mat rgb(size, CV_32FC3);
    cv::RNG random(5);
    for (int y = 0; y < rgb.rows; ++y)
        for (int x = 0; x < rgb.cols; ++x) {
            const float shared = float(random.gaussian(0.01));
            const float u = float(x) / float(rgb.cols), v = float(y) / float(rgb.rows);
            rgb.at<cv::Vec3f>(y, x) = cv::Vec3f(std::max(0.002f, 0.05f + 0.6f * u + shared + float(random.gaussian(0.006))),
                                                std::max(0.002f, 0.06f + 0.5f * v + shared + float(random.gaussian(0.006))),
                                                std::max(0.002f, 0.30f - 0.2f * u + shared + float(random.gaussian(0.006))));
        }
    return rgb;
}

ColorPipeline::WorkingImage rasterSource(const cv::Size &size)
{
    std::vector<cv::Mat> channels;
    cv::split(noisyRgb(size), channels);
    channels.emplace_back(size, CV_32FC1, cv::Scalar(1));
    ColorPipeline::WorkingImage image;
    cv::merge(channels, image.linearRgba);
    image.referenceWhiteBalanceKelvin = 5200.0;
    return image;
}

ColorPipeline::WorkingImage cameraSource(const cv::Size &size, const QString &profile)
{
    CameraSignal::Source signal;
    signal.rgb = noisyRgb(size);
    signal.cameraMake = QStringLiteral("Canon");
    signal.cameraModel = QStringLiteral("EOS R6 Mark III");
    signal.whiteBalanceKelvin = 5200.0;
    signal.whiteBalanceApplied = true;
    signal.cameraNeutral = cv::Vec3d(0.6, 1.0, 0.8);
    QString error;
    ColorPipeline::WorkingImage image = CameraSignal::render(std::move(signal), profile, {}, &error);
    if (image.isNull())
        std::cerr << "camera source: " << error.toStdString() << '\n';
    image.referenceWhiteBalanceKelvin = 5200.0;
    return image;
}

Shape wholeShape()
{
    Shape shape;
    shape.kind = Kind::Radial;
    shape.radiusX = 4.0f;
    shape.radiusY = 4.0f;
    shape.feather = 0.0f;
    return shape;
}

Mask wholeMask(LocalValues::Parameter parameter, float value)
{
    Mask mask;
    mask.shapes.push_back(wholeShape());
    mask.values[size_t(parameter)] = value;
    return mask;
}

double worstDifference(const ColorPipeline::WorkingImage &a, const ColorPipeline::WorkingImage &b)
{
    if (a.isNull() || b.isNull() || a.linearRgba.size() != b.linearRgba.size())
        return std::numeric_limits<double>::infinity();
    cv::Mat difference;
    cv::absdiff(a.linearRgba, b.linearRgba, difference);
    double worst = 0.0;
    cv::minMaxLoc(difference.reshape(1), nullptr, &worst);
    return worst;
}

bool sameBytes(const ColorPipeline::WorkingImage &a, const ColorPipeline::WorkingImage &b)
{
    return worstDifference(a, b) == 0.0;
}

// A whole-zone mask at +X renders as the global setting at +X: the ADR's promise.
void sameAsGlobal(const ColorPipeline::WorkingImage &source, const char *name)
{
    struct Case
    {
        const char *name;
        LocalValues::Parameter parameter;
        float value;
        std::function<void(Settings &)> global;
        double tolerance;
    };
    const std::vector<Case> cases{
        {"exposure", LocalValues::Exposure, 1.0f, [](Settings &s) { s.brightness = 1.0f; }, 1e-4},
        {"contrast", LocalValues::Contrast, 40.0f, [](Settings &s) { s.contrast = 40; }, 1e-4},
        {"temperature", LocalValues::Temperature, 800.0f, [](Settings &s) { s.temperature = 6000; }, 1e-3},
        {"tint", LocalValues::Tint, 30.0f, [](Settings &s) { s.tint = 30; }, 1e-3},
        {"saturation", LocalValues::Saturation, 40.0f, [](Settings &s) { s.saturation = 40; }, 1e-3},
        {"vibrance", LocalValues::Vibrance, 40.0f, [](Settings &s) { s.vibrance = 40; }, 1e-3},
        {"shadows", LocalValues::Shadows, 50.0f, [](Settings &s) { s.shadows = 50; }, 1e-3},
        {"blacks", LocalValues::Blacks, -40.0f, [](Settings &s) { s.blacks = -40; }, 1e-3},
        {"highlights", LocalValues::Highlights, -50.0f, [](Settings &s) { s.highlights = -50; }, 1e-3},
        {"whites", LocalValues::Whites, 40.0f, [](Settings &s) { s.whites = 40; }, 1e-3},
        {"texture", LocalValues::Texture, 50.0f, [](Settings &s) { s.texture = 50; }, 1e-3},
        {"clarity", LocalValues::Clarity, 50.0f, [](Settings &s) { s.clarity = 50; }, 1e-3},
        {"dehaze", LocalValues::Dehaze, 40.0f, [](Settings &s) { s.dehaze = 40; }, 1e-3},
        {"denoise luminance", LocalValues::DenoiseLuminance, 40.0f, [](Settings &s) { s.denoiseLuminance = 40; }, 1e-3},
        {"denoise colour", LocalValues::DenoiseChrominance, 40.0f, [](Settings &s) { s.denoiseChrominance = 40; }, 1e-3},
        {"sharpening", LocalValues::Sharpening, 60.0f, [](Settings &s) { s.sharpeningAmount = 60; }, 1e-3},
    };
    for (const Case &entry : cases) {
        Settings global;
        entry.global(global);
        Settings local;
        local.localMasks.push_back(wholeMask(entry.parameter, entry.value));
        const double worst = worstDifference(IprTestImageGenerator::render(source, global), IprTestImageGenerator::render(source, local));
        if (!(worst <= entry.tolerance))
            std::cerr << name << ' ' << entry.name << ": worst " << worst << '\n';
        check(worst <= entry.tolerance, "a whole mask renders as the global setting");
    }
}

void rendering(const ColorPipeline::WorkingImage &source, const char *name)
{
    std::cerr << "-- " << name << '\n';
    const Settings neutral;
    const ColorPipeline::WorkingImage plain = IprTestImageGenerator::render(source, neutral);

    // Without an active mask, the render does not change by a bit.
    for (const std::function<void(Mask &)> &change : std::vector<std::function<void(Mask &)>>{
             [](Mask &m) { m.visible = false; }, [](Mask &m) { m.opacity = 0.0f; }, [](Mask &m) { m.values = {}; }, [](Mask &m) { m.shapes.clear(); }}) {
        Settings idle;
        Mask mask = wholeMask(LocalValues::Exposure, 1.0f);
        change(mask);
        idle.localMasks.push_back(mask);
        check(sameBytes(plain, IprTestImageGenerator::render(source, idle)), "an idle mask changes no bit");
    }

    sameAsGlobal(source, name);

    // Opacity weighs the value.
    Settings half;
    half.localMasks.push_back(wholeMask(LocalValues::Exposure, 1.0f));
    half.localMasks[0].opacity = 0.5f;
    Settings globalHalf;
    globalHalf.brightness = 0.5f;
    check(worstDifference(IprTestImageGenerator::render(source, half), IprTestImageGenerator::render(source, globalHalf)) <= 1e-4, "an opacity of half is half the value");

    // Masks add up: two of 0.5 EV are one of 1 EV; the order changes nothing.
    Settings two;
    two.localMasks = {wholeMask(LocalValues::Exposure, 0.5f), wholeMask(LocalValues::Exposure, 0.5f)};
    Settings one;
    one.brightness = 1.0f;
    check(worstDifference(IprTestImageGenerator::render(source, two), IprTestImageGenerator::render(source, one)) <= 1e-4, "masks add up");
    Settings withGlobal;
    withGlobal.brightness = 0.25f;
    withGlobal.localMasks = {wholeMask(LocalValues::Exposure, 0.75f)};
    check(worstDifference(IprTestImageGenerator::render(source, withGlobal), IprTestImageGenerator::render(source, one)) <= 1e-4, "and add to the global value");

    Settings ab, ba;
    Mask a = wholeMask(LocalValues::Exposure, 0.4f), b = wholeMask(LocalValues::Contrast, 30.0f);
    a.shapes[0].radiusX = a.shapes[0].radiusY = 0.3f;
    a.shapes[0].feather = 0.5f;
    ab.localMasks = {a, b};
    ba.localMasks = {b, a};
    check(sameBytes(IprTestImageGenerator::render(source, ab), IprTestImageGenerator::render(source, ba)), "their order changes nothing");

    // A zone acts in it and only there: the top half here, away from its soft edge.
    Settings zone;
    Mask top;
    Shape gradient;
    gradient.kind = Kind::Linear;
    gradient.start = {0.5f, 0.45f};
    gradient.end = {0.5f, 0.55f};
    top.shapes.push_back(gradient);
    top.values[size_t(LocalValues::Exposure)] = 1.0f;
    zone.localMasks.push_back(top);
    Settings oneEv;
    oneEv.brightness = 1.0f;
    const ColorPipeline::WorkingImage zoned = IprTestImageGenerator::render(source, zone);
    const ColorPipeline::WorkingImage lifted = IprTestImageGenerator::render(source, oneEv);
    const int rows = zoned.linearRgba.rows, cols = zoned.linearRgba.cols;
    const cv::Rect upper(0, 0, cols, rows * 2 / 5), lower(0, rows * 3 / 5, cols, rows - rows * 3 / 5);
    cv::Mat difference;
    double worst = 0.0;
    cv::absdiff(zoned.linearRgba(upper), lifted.linearRgba(upper), difference);
    cv::minMaxLoc(difference.reshape(1), nullptr, &worst);
    check(worst <= 1e-4, "inside its zone, the whole effect");
    cv::absdiff(zoned.linearRgba(lower), plain.linearRgba(lower), difference);
    cv::minMaxLoc(difference.reshape(1), nullptr, &worst);
    check(worst <= 1e-4, "outside it, nothing");

    // A cached render resumes rightly: a change of mask is never served from the cache. The
    // cache keeps the tones' stage first: the mask must change a value that lies in it.
    RenderStageCache cache;
    Settings first;
    first.localMasks = {wholeMask(LocalValues::Exposure, 1.0f)};
    Settings second = first;
    second.localMasks[0].values[size_t(LocalValues::Exposure)] = -1.0f;
    IprTestImageGenerator::render(source, first, 1.0, &cache);
    const ColorPipeline::WorkingImage cached = IprTestImageGenerator::render(source, second, 1.0, &cache);
    check(sameBytes(cached, IprTestImageGenerator::render(source, second)), "a changed mask is not served from the cache");
    const ColorPipeline::WorkingImage again = IprTestImageGenerator::render(source, second, 1.0, &cache);
    check(sameBytes(again, cached), "and a same one renders the same from it");
    Settings removed;
    check(sameBytes(IprTestImageGenerator::render(source, removed, 1.0, &cache), plain), "removing it comes back to the plain render");
}

void withGeometry(const ColorPipeline::WorkingImage &source)
{
    // Masks live on the canvas: a mask on its top stays there, whatever the transform,
    // including for the settings that act on the camera signal before the geometry.
    for (const std::function<void(Geometry::Settings &)> &transform : std::vector<std::function<void(Geometry::Settings &)>>{
             [](Geometry::Settings &g) { g.rotation = 6.0f; }, [](Geometry::Settings &g) { g.quarterTurns = 1; },
             [](Geometry::Settings &g) { g.flipVertical = true; }, [](Geometry::Settings &g) { g.vertical = 20; }}) {
        Settings framed;
        transform(framed.geometry);
        const ColorPipeline::WorkingImage plain = IprTestImageGenerator::render(source, framed);
        Settings masked = framed;
        Mask top;
        Shape gradient;
        gradient.kind = Kind::Linear;
        gradient.start = {0.5f, 0.45f};
        gradient.end = {0.5f, 0.55f};
        top.shapes.push_back(gradient);
        top.values[size_t(LocalValues::Exposure)] = 1.0f;
        masked.localMasks.push_back(top);
        const ColorPipeline::WorkingImage zoned = IprTestImageGenerator::render(source, masked);
        check(zoned.linearRgba.size() == plain.linearRgba.size(), "the canvas stays the same");
        const int rows = plain.linearRgba.rows, cols = plain.linearRgba.cols;
        const auto meanOf = [](const cv::Mat &image, const cv::Rect &area) {
            const cv::Scalar mean = cv::mean(image(area));
            return (mean[0] + mean[1] + mean[2]) / 3.0;
        };
        // Inside the image, away from the zones a transform empties.
        const cv::Rect upper(cols / 4, rows / 8, cols / 2, rows / 4), lower(cols / 4, rows * 5 / 8, cols / 2, rows / 4);
        const double gain = meanOf(zoned.linearRgba, upper) / std::max(1e-6, meanOf(plain.linearRgba, upper));
        const double unchanged = meanOf(zoned.linearRgba, lower) / std::max(1e-6, meanOf(plain.linearRgba, lower));
        if (gain < 1.3 || std::abs(unchanged - 1.0) > 0.002)
            std::cerr << "gain " << gain << " unchanged " << unchanged << '\n';
        check(gain >= 1.3, "the canvas's top is lifted");
        check(std::abs(unchanged - 1.0) <= 0.002, "and its bottom is not");
    }
}

void resolutions(const ColorPipeline::WorkingImage &source)
{
    // A preview shows at its size what the export shows at its own: same zone, same effect.
    Settings settings;
    Mask mask;
    Shape round;
    round.kind = Kind::Radial;
    round.center = {0.4f, 0.6f};
    round.radiusX = 0.3f;
    round.radiusY = 0.3f;
    round.feather = 0.7f;
    mask.shapes.push_back(round);
    mask.values[size_t(LocalValues::Exposure)] = 1.0f;
    settings.localMasks.push_back(mask);
    ColorPipeline::WorkingImage small = source;
    cv::resize(source.linearRgba, small.linearRgba, cv::Size(), 0.5, 0.5, cv::INTER_AREA);
    const ColorPipeline::WorkingImage full = IprTestImageGenerator::render(source, settings);
    const ColorPipeline::WorkingImage preview = IprTestImageGenerator::render(small, settings, 0.5);
    cv::Mat reduced;
    cv::resize(full.linearRgba, reduced, preview.linearRgba.size(), 0, 0, cv::INTER_AREA);
    cv::Mat difference;
    cv::absdiff(reduced, preview.linearRgba, difference);
    double worst = 0.0;
    cv::minMaxLoc(difference.reshape(1), nullptr, &worst);
    if (worst > 0.02)
        std::cerr << "resolution worst " << worst << '\n';
    check(worst <= 0.02, "the zone is the same at both resolutions");
}

void codec()
{
    Settings settings;
    Mask mask;
    mask.name = QStringLiteral("Ciel é");
    mask.opacity = 0.75f;
    mask.visible = false;
    Shape radial;
    radial.kind = Kind::Radial;
    radial.center = {0.3f, 0.7f};
    radial.radiusX = 0.2f;
    radial.radiusY = 0.4f;
    radial.angle = 30.0f;
    radial.feather = 0.6f;
    radial.inverted = true;
    Shape stroked;
    stroked.kind = Kind::Brush;
    stroked.combine = Combine::Subtract;
    BrushStroke stroke;
    stroke.points = {{0.1f, 0.2f}, {0.3f, 0.4f}, {0.5f, 0.6f}};
    stroke.size = 0.08f;
    stroke.feather = 0.3f;
    stroke.flow = 0.9f;
    stroked.strokes.push_back(stroke);
    Shape linear;
    linear.combine = Combine::Intersect;
    linear.start = {-0.5f, 0.1f};
    linear.end = {1.5f, 0.9f};
    mask.shapes = {radial, stroked, linear};
    mask.values[size_t(LocalValues::Exposure)] = 0.5f;
    mask.values[size_t(LocalValues::Sharpening)] = 60.0f;
    settings.localMasks.push_back(mask);

    const QCborMap map = RecipeCodec::encodeSettings(settings);
    Settings read;
    read.localMasks.push_back(Mask{});
    check(RecipeCodec::decodeSettings(map, read), "a recipe with masks reads back");
    check(read.localMasks.size() == 1 && read.localMasks[0] == mask, "the same masks");

    // Through bytes: CBOR keeps floats exactly enough for a same render.
    const QByteArray bytes = QCborValue(map).toCbor();
    Settings throughBytes;
    check(RecipeCodec::decodeSettings(QCborValue::fromCbor(bytes).toMap(), throughBytes) && throughBytes.localMasks == settings.localMasks, "through bytes too");

    // A recipe without masks: no section, and reading one clears what was there.
    const QCborMap plain = RecipeCodec::encodeSettings(Settings{});
    check(!plain.contains(QStringLiteral("localMasks")), "no masks write nothing");
    Settings cleared;
    cleared.localMasks.push_back(mask);
    check(RecipeCodec::decodeSettings(plain, cleared) && cleared.localMasks.empty(), "an older recipe has no masks");

    // What cannot be masks is refused, not guessed; a setting from a later version is skipped.
    QCborMap broken = map;
    broken.insert(QStringLiteral("localMasks"), QCborArray{QStringLiteral("not a mask")});
    Settings refused;
    check(!RecipeCodec::decodeSettings(broken, refused), "a mask of the wrong type is refused");
    QCborMap unknownKind = RecipeCodec::encodeSettings(settings);
    QCborArray masks = unknownKind.value(QStringLiteral("localMasks")).toArray();
    QCborMap first = masks.at(0).toMap();
    QCborArray shapes = first.value(QStringLiteral("shapes")).toArray();
    QCborMap shape = shapes.at(0).toMap();
    shape.insert(QStringLiteral("kind"), QStringLiteral("hexagon"));
    shapes[0] = shape;
    first.insert(QStringLiteral("shapes"), shapes);
    QCborMap values = first.value(QStringLiteral("values")).toMap();
    values.insert(QStringLiteral("fromTheFuture"), 3.0);
    first.insert(QStringLiteral("values"), values);
    masks[0] = first;
    unknownKind.insert(QStringLiteral("localMasks"), masks);
    check(!RecipeCodec::decodeSettings(unknownKind, refused), "an unknown shape is refused");
    shape.insert(QStringLiteral("kind"), QStringLiteral("radial"));
    shapes[0] = shape;
    first.insert(QStringLiteral("shapes"), shapes);
    masks[0] = first;
    unknownKind.insert(QStringLiteral("localMasks"), masks);
    Settings future;
    check(RecipeCodec::decodeSettings(unknownKind, future) && future.localMasks.size() == 1, "an unknown setting is skipped");
    check(future.localMasks[0].values[size_t(LocalValues::Exposure)] == 0.5f, "the others stay");
}

QString writePng(const QString &directory, const QString &name, int width, int height)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(x, y, QColor(60 + 100 * x / width, 60 + 100 * y / height, 110, 255));
    const QString path = QDir(directory).filePath(name);
    return image.save(path, "PNG") ? QFileInfo(path).canonicalFilePath() : QString();
}

void commands(const QDir &folder)
{
    const QString photo = writePng(folder.path(), QStringLiteral("photo.png"), 120, 80);
    const QString other = writePng(folder.path(), QStringLiteral("other.png"), 120, 80);
    ImageProvider provider;
    ImageEditor editor(&provider, nullptr, nullptr, controlledMemory);
    const auto settled = [&] { return waitFor([&] { return !editor.busy(); }); };
    const auto load = [&](const QString &path) {
        editor.loadImage(QUrl::fromLocalFile(path));
        return waitFor([&] { return editor.hasImage() && !editor.busy() && editor.imageSource().size() > 0; });
    };

    // Before any photo, nothing can be added.
    check(editor.addLocalMask(QStringLiteral("radial")) == -1, "no photo, no mask");
    check(load(photo), "the photo loads");
    check(editor.localMasks().isEmpty(), "it starts without masks");
    const QImage before = provider.image();

    // A mask without a value changes nothing on the image.
    check(editor.addLocalMask(QStringLiteral("hexagon")) == -1, "an unknown kind is refused");
    const int mask = editor.addLocalMask(QStringLiteral("radial"));
    check(mask == 0 && editor.localMasks().size() == 1, "a mask is added");
    check(settled(), "it settles");
    check(provider.image() == before, "without a value it changes no pixel");

    // Its value acts in its zone.
    editor.setLocalMaskValue(mask, QStringLiteral("exposure"), 1.0);
    check(settled(), "the value renders");
    QImage lifted = provider.image();
    check(qRed(lifted.pixel(60, 40)) > qRed(before.pixel(60, 40)) + 10, "the centre is lifted");
    check(qRed(lifted.pixel(2, 2)) == qRed(before.pixel(2, 2)), "outside the zone, nothing");

    // Each command can be undone, as one step of the history.
    const qsizetype steps = editor.historySteps().size();
    editor.setLocalMaskVisible(mask, false);
    check(settled() && provider.image() == before, "a hidden mask does not act");
    check(editor.historySteps().size() == steps + 1, "hiding it is one step");
    editor.undo();
    check(settled() && editor.localMasks().at(0).toMap().value(QStringLiteral("visible")).toBool(), "an undo brings it back");
    check(provider.image() == lifted, "with its effect");

    // Shape fields, bounded, and refused when they are not what they should be.
    editor.setLocalMaskShape(mask, 0, {{QStringLiteral("center"), QVariantList{0.1, 0.1}}, {QStringLiteral("radiusX"), 0.1}, {QStringLiteral("feather"), 5.0}});
    check(settled(), "a moved shape renders");
    const QVariantMap moved = editor.localMasks().at(0).toMap().value(QStringLiteral("shapes")).toList().at(0).toMap();
    check(moved.value(QStringLiteral("feather")).toDouble() == 1.0, "a value out of range is bounded");
    check(moved.value(QStringLiteral("center")).toList().at(0).toDouble() < 0.11, "the shape has moved");
    editor.setLocalMaskShape(mask, 0, {{QStringLiteral("center"), QStringLiteral("nowhere")}});
    editor.setLocalMaskShape(mask, 0, {{QStringLiteral("nothing"), 1}});
    check(editor.localMasks().at(0).toMap().value(QStringLiteral("shapes")).toList().at(0).toMap().value(QStringLiteral("center")).toList().at(0).toDouble() < 0.11, "a wrong field changes nothing");

    // Bad indexes and values do nothing.
    const QVariantList unchanged = editor.localMasks();
    editor.setLocalMaskValue(7, QStringLiteral("exposure"), 1.0);
    editor.setLocalMaskValue(mask, QStringLiteral("nothing"), 1.0);
    editor.setLocalMaskValue(mask, QStringLiteral("exposure"), std::numeric_limits<double>::quiet_NaN());
    editor.setLocalMaskOpacity(-1, 0.5);
    editor.setLocalMaskOpacity(mask, std::numeric_limits<double>::infinity());
    editor.removeLocalMask(9);
    editor.removeLocalMaskShape(mask, 9);
    editor.setLocalMaskShape(mask, 9, {{QStringLiteral("radiusX"), 0.2}});
    check(editor.localMasks() == unchanged, "bad commands change nothing");
    check(editor.duplicateLocalMask(9) == -1 && editor.addLocalMaskShape(9, QStringLiteral("radial"), QStringLiteral("add")) == -1, "and add nothing");
    check(editor.addLocalMaskShape(mask, QStringLiteral("radial"), QStringLiteral("nothing")) == -1, "an unknown combination is refused");

    // A brush: strokes on its shape only; a stroke is one step.
    const int brush = editor.addLocalMask(QStringLiteral("brush"));
    check(brush == 1, "a second mask");
    editor.setLocalMaskValue(brush, QStringLiteral("contrast"), 30.0);
    settled();
    const qsizetype beforeStroke = editor.historySteps().size();
    editor.addLocalMaskStroke(brush, 0, {0.2, 0.2, 0.8, 0.8}, 0.1, 0.5, 1.0);
    check(settled() && editor.historySteps().size() == beforeStroke + 1, "a stroke is one step");
    check(editor.localMasks().at(brush).toMap().value(QStringLiteral("shapes")).toList().at(0).toMap().value(QStringLiteral("strokes")).toInt() == 1, "the stroke is kept");
    editor.addLocalMaskStroke(brush, 0, {0.2, 0.2, 0.8}, 0.1, 0.5, 1.0);
    editor.addLocalMaskStroke(brush, 0, {}, 0.1, 0.5, 1.0);
    editor.addLocalMaskStroke(mask, 0, {0.5, 0.5}, 0.1, 0.5, 1.0);
    check(editor.localMasks().at(brush).toMap().value(QStringLiteral("shapes")).toList().at(0).toMap().value(QStringLiteral("strokes")).toInt() == 1, "malformed points add none");
    check(editor.localMasks().at(mask).toMap().value(QStringLiteral("shapes")).toList().at(0).toMap().value(QStringLiteral("strokes")).toInt() == 0, "nor a radial shape");

    // A duplicate, then a shape added to it, removed again.
    const int copy = editor.duplicateLocalMask(brush);
    check(copy == 2 && editor.localMasks().size() == 3, "a mask can be duplicated");
    const int added = editor.addLocalMaskShape(copy, QStringLiteral("linear"), QStringLiteral("subtract"));
    check(added == 1, "a shape can be added");
    editor.removeLocalMaskShape(copy, added);
    editor.removeLocalMask(copy);
    check(settled() && editor.localMasks().size() == 2, "and removed");
    editor.setLocalMaskName(brush, QStringLiteral("Visage"));
    check(editor.localMasks().at(brush).toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Visage"), "a mask can be named");

    // Saved with the photo, and back when it reopens; the other photo has its own.
    check(settled(), "the render settles");
    editor.saveCurrentAdjustments();
    const auto saved = Sidecar::read(photo);
    check(saved && saved->settings.localMasks.size() == 2, "the masks are saved in the sidecar");
    check(load(other) && editor.localMasks().isEmpty(), "another photo has its own");
    editor.setContrast(25);
    check(settled(), "the other photo renders");
    editor.copyAdjustments();
    check(load(photo) && editor.localMasks().size() == 2, "reopening a photo restores its masks");

    // Pasting edits keeps the target's zones, drawn for it.
    editor.pasteAdjustments();
    check(settled() && editor.localMasks().size() == 2 && editor.adjustmentValue(QStringLiteral("contrast")) == 25, "a paste keeps the target's masks");

    // The general reset removes them, like the other edits.
    editor.resetAdjustments();
    check(settled() && editor.localMasks().isEmpty(), "a reset clears them");
    check(provider.image() == before, "and the first image is back");
    editor.undo();
    check(settled() && editor.localMasks().size() == 2, "an undo restores them");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "a temporary folder");
    const QDir folder(directory.path());
    const QString dcp = folder.filePath(QStringLiteral("R6.dcp"));
    QFile file(dcp);
    check(file.open(QIODevice::WriteOnly) && file.write(SyntheticDcp::make(true)) > 0, "a synthetic profile");
    file.close();

    const cv::Size size(240, 160);
    const ColorPipeline::WorkingImage raster = rasterSource(size);
    rendering(raster, "raster");
    withGeometry(raster);
    resolutions(raster);
    const ColorPipeline::WorkingImage camera = cameraSource(size, dcp);
    check(!camera.isNull() && camera.dcpContext != nullptr, "a source with a camera signal");
    if (!camera.isNull()) {
        rendering(camera, "camera");
        withGeometry(camera);
    }
    codec();
    commands(folder);
    if (passed)
        std::cout << "editor masks: ok\n";
    return passed ? 0 : 1;
}
