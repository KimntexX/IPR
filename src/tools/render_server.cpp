#include "TestRecipes.h"

#include <QCoreApplication>
#include <QColorSpace>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// In-memory renders of the test states for the analysers, without TIFF files.
// Protocol: one JSON request per line on stdin; one JSON header line on stdout,
// followed for a render by `bytes` bytes of little-endian uint16 RGB, row-major.
//   {"op":"list","base":DIR,"group":G}  -> {"ok":true,"states":[...]}
//   {"op":"render","source":FILE,"feature":F,"value":V[,"highlight_mode":M]}  -> header + pixels
//   {"op":"quit"}
// An error answers {"ok":false,"error":...} and the server keeps serving.
// Diagnostics go to stderr; stdout carries only the protocol.
namespace {
using namespace TestRecipes;

void require(bool condition, const QString &message)
{
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}

void send(const QJsonObject &header, const char *data = nullptr, size_t size = 0)
{
    const QByteArray line = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
    const bool written = std::fwrite(line.constData(), 1, size_t(line.size()), stdout) == size_t(line.size())
        && (size == 0 || std::fwrite(data, 1, size, stdout) == size) && std::fflush(stdout) == 0;
    if (!written) throw std::runtime_error("stdout closed");
}

QJsonObject list(const QString &base, const QString &group)
{
    require(group == "all" || groups().contains(group), "Unknown group: " + group);
    QJsonArray states;
    for (const QFileInfo &source : sources(base))
        for (const Feature &feature : features()) {
            if ((group != "all" && featureGroup(feature.name) != group) || !eligible(feature.name, source.filePath()))
                continue;
            const Reference reference = referenceFor(feature.name);
            for (double value : feature.values)
                states.append(QJsonObject{{"scene", source.completeBaseName()},
                    {"source", source.absoluteFilePath()}, {"group", featureGroup(feature.name)},
                    {"feature", feature.name}, {"value", value},
                    {"reference", QJsonObject{{"feature", reference.feature}, {"value", reference.value}}}});
        }
    return {{"ok", true}, {"states", states}};
}

// The default decode of the last source stays in memory; a state with its own
// decode options (auto WB, optics) decodes once and is released after use.
class Masters {
public:
    const LoadResult &get(const QString &source, const Decode &decode)
    {
        if (!decode.isDefault()) {
            extra_.reset();
            extra_ = load(source, decode);
            return *extra_;
        }
        extra_.reset();
        if (!primary_ || primarySource_ != source) {
            primary_.reset();
            primary_ = load(source);
            primarySource_ = source;
        }
        return *primary_;
    }

private:
    QString primarySource_;
    std::optional<LoadResult> primary_, extra_;
};

void render(Masters &masters, const QJsonObject &request)
{
    const QString source = request.value("source").toString();
    const QString feature = request.value("feature").toString();
    const double value = request.value("value").toDouble();
    require(!source.isEmpty() && QFileInfo(source).isFile(), "Missing source: " + source);
    const auto catalogue = features();
    require(feature == "neutral" || std::any_of(catalogue.cbegin(), catalogue.cend(),
                                                [&](const Feature &known) { return known.name == feature; }),
            "Unknown feature: " + feature);
    require(feature == "neutral" || eligible(feature, source), "RAW source required for " + feature);
    Decode decode = feature == "neutral" ? Decode{} : decodeFor(feature, value);
    decode.highlightMode = request.value("highlight_mode").toInt(0);
    require(decode.highlightMode >= 0 && decode.highlightMode <= 9, "highlight_mode must be 0-9");
    const LoadResult &master = masters.get(source, decode);
    const Settings settings = settingsFor(master, feature, value);
    QString error;
    const QImage image = ImageEditor::renderSrgb16(master.image, settings, &error);
    require(!image.isNull() && image.format() == QImage::Format_RGBA64, "Render failed: " + error);
    const int width = image.width(), height = image.height();
    std::vector<quint16> rgb(size_t(width) * size_t(height) * 3);
    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const quint16 *>(image.constScanLine(y));
        quint16 *out = rgb.data() + size_t(y) * size_t(width) * 3;
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) out[size_t(x) * 3 + size_t(c)] = row[size_t(x) * 4 + size_t(c)];
    }
    static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "The protocol sends little-endian samples");
    const QString group = feature == "neutral" ? QStringLiteral("neutral") : featureGroup(feature);
    const Reference reference = feature == "neutral" ? Reference{"neutral", 0} : referenceFor(feature);
    const QJsonObject declared = declaration(master, settings, source, group, feature, value,
        "state:" + reference.feature + ':' + QString::number(reference.value, 'g', 12));
    send({{"ok", true}, {"width", width}, {"height", height}, {"channels", 3}, {"dtype", "<u2"},
          {"bytes", qint64(rgb.size() * sizeof(quint16))},
          {"icc_base64", QString::fromLatin1(image.colorSpace().iccProfile().toBase64())},
          {"declaration", declared}},
         reinterpret_cast<const char *>(rgb.data()), rgb.size() * sizeof(quint16));
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("IPR");
    QCoreApplication::setApplicationName("IPR");
#ifdef _WIN32
    // Pixels are binary: no newline translation on stdout.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Masters masters;
    std::string line;
    for (int c; (c = std::getchar()) != EOF;) {
        if (c != '\n') { line.push_back(char(c)); continue; }
        try {
            QJsonParseError parse{};
            const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parse).object();
            line.clear();
            require(parse.error == QJsonParseError::NoError, "Invalid JSON request: " + parse.errorString());
            const QString op = request.value("op").toString();
            if (op == "quit") return 0;
            if (op == "list") send(list(request.value("base").toString(), request.value("group").toString()));
            else if (op == "render") render(masters, request);
            else require(false, "Unknown op: " + op);
        } catch (const std::exception &error) {
            line.clear();
            try { send({{"ok", false}, {"error", QString::fromUtf8(error.what())}}); }
            catch (const std::exception &) { return 1; }
        }
    }
    return 0;
}
