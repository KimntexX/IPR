#include "imageeditor.h"
#include "image_testing.h"
#include "RenderChain.h"
#include "DetailProcessing.h"
#include "Pixels.h"
#include <QCoreApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

class IprTestImageGenerator {
public:
    static void exercise(const ColorPipeline::WorkingImage &source) {
        const auto check = [](bool ok) { if (!ok) throw std::runtime_error("render contract"); };
        ImageEditor::Settings neutral;
        auto result = RenderChain::workingImage(source, neutral);
        check(!result.isNull() && result.colorSpace == ColorPipeline::workingColorSpace());
        check(ipr::largestDifference(result.linearRgba, source.linearRgba) == 0);
        for (int group = 0; group < 14; ++group) {
            for (int sign : {-1, 1}) {
                auto settings = neutral;
                switch (group) {
                case 0: settings.brightness = float(sign); break;
                case 1: settings.shadows = sign * 100; break;
                case 2: settings.highlights = sign * 100; break;
                case 3: settings.whites = sign * 100; settings.blacks = sign * 100; break;
                case 4: settings.contrast = sign * 100; break;
                case 5: settings.saturation = sign * 100; break;
                case 6: settings.vibrance = sign * 100; break;
                case 7: settings.mixerHue[1] = sign * 100; settings.mixerLuminance[1] = sign * 100; break;
                case 8: settings.gradingHue[0] = 30; settings.gradingSaturation[0] = 60; break;
                case 9: settings.texture = sign * 100; settings.clarity = sign * 100; break;
                case 10: settings.dehaze = sign * 100; break;
                case 11: settings.denoiseChrominance = 100; settings.denoiseLuminance = 50; break;
                case 12: settings.sharpeningAmount = 100; break;
                case 13: settings.grayscale = true; break;
                }
                result = RenderChain::workingImage(source, settings);
                check(!result.isNull() && result.colorSpace == ColorPipeline::workingColorSpace());
                check(ipr::allFinite(result.linearRgba));
                for (int y = 0; y < source.linearRgba.height(); ++y)
                    for (int x = 0; x < source.linearRgba.width(); ++x)
                        check(imagetest::at<ipr::Vec4f>(result.linearRgba, y,x)[3] == imagetest::at<ipr::Vec4f>(source.linearRgba, y,x)[3]);
            }
        }
        const auto preview = RenderChain::preview(source, neutral, true);
        check(!preview.preview.isNull() && !preview.originalPreview.isNull());
        for (int channel = 0; channel < 3; ++channel) {
            std::array<quint64,256> expected{};
            for (int y=0; y<preview.preview.height(); ++y) {
                const auto *row = preview.preview.constScanLine(y);
                for (int x=0; x<preview.preview.width(); ++x) ++expected[row[4*x+channel]];
            }
            const auto bins = preview.histogram[channel+1].toList();
            check(bins.size() == 256);
            for (int i=0; i<256; ++i) check(bins[i].toULongLong() == expected[i]);
        }
        auto invalid = ColorPipeline::sharedCopy(source);
        invalid.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
        check(RenderChain::workingImage(invalid, neutral).isNull());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto check = [](bool ok, const char *why) { if (!ok) throw std::runtime_error(why); };
    try {
        QString error;
        check(ColorPipeline::workingColorSpace().isValid(), "Rec.2020 linear Qt descriptor");
        ipr::Image extended = ipr::Image::create({3, 1}, ipr::Depth::F32, 4);
        imagetest::at<ipr::Vec4f>(extended, 0,0) = {-0.25f, .18f, 2.f, .37f};
        imagetest::at<ipr::Vec4f>(extended, 0,1) = {4.f, -1.f, .01f, 0.f};
        imagetest::at<ipr::Vec4f>(extended, 0,2) = {.5f, .5f, .5f, 1.f};
        const auto original = extended.clone();
        WorkingColor::transform(extended, WorkingColor::fromSrgb());
        WorkingColor::transform(extended, WorkingColor::toSrgb());
        check(ipr::largestDifference(extended, original) < 5e-6, "unbounded matrix round trip");
        for (int x=0; x<3; ++x) check(imagetest::at<ipr::Vec4f>(extended, 0,x)[3] == imagetest::at<ipr::Vec4f>(original, 0,x)[3], "alpha unchanged");

        // Floating source in the master: signed/HDR values must survive import.
        QImage floating(3, 1, QImage::Format_RGBA32FPx4);
        std::memcpy(floating.bits(), original.row<float>(0), 3 * 4 * sizeof(float));
        floating.setColorSpace(ColorPipeline::workingColorSpace());
        const auto imported = ColorPipeline::fromSourceImage(floating, &error);
        check(!imported.isNull() && ipr::largestDifference(imported.linearRgba, original) == 0, "float import must not quantize or clip");
        floating.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        const auto converted = ColorPipeline::fromSourceImage(floating, &error);
        auto expected = original.clone(); WorkingColor::transform(expected, WorkingColor::fromSrgb());
        if (!converted.isNull() && ipr::largestDifference(converted.linearRgba, expected) >= 5e-6)
            std::cerr << "ICC float actual differs from expected by "
                      << ipr::largestDifference(converted.linearRgba, expected) << '\n';
        check(!converted.isNull() && ipr::largestDifference(converted.linearRgba, expected) < 5e-6, "float sRGB must actually convert to Rec.2020");

        // ProPhoto red is outside the Rec.2020 cube; no integer bridge is allowed.
        QImage wide(1, 1, QImage::Format_RGBA32FPx4);
        auto *p = reinterpret_cast<float *>(wide.bits()); p[0]=1; p[1]=p[2]=0; p[3]=.5f;
        wide.setColorSpace(QColorSpace(QColorSpace::Primaries::ProPhotoRgb, QColorSpace::TransferFunction::Linear));
        const auto wideMaster = ColorPipeline::fromSourceImage(wide, &error);
        check(!wideMaster.isNull() && imagetest::at<ipr::Vec4f>(wideMaster.linearRgba, 0,0)[1] < 0, "wide ICC import retains negative coordinates");

        for (const QByteArray format : {QByteArray("png"), QByteArray("jpeg"), QByteArray("tiff"), QByteArray("bmp")}) {
            check(QImageWriter::supportedImageFormats().contains(format), "required image writer plugin unavailable");
            QImage image(8,8,QImage::Format_RGB32); image.fill(QColor(220,70,35));
            image.setColorSpace(QColorSpace(QColorSpace::SRgb));
            QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::ReadWrite);
            QImageWriter writer(&buffer, format); check(writer.write(image), "encode fixture"); buffer.seek(0);
            QImageReader reader(&buffer, format); const auto decoded = reader.read();
            const auto master = ColorPipeline::fromSourceImage(decoded, &error);
            check(!master.isNull() && master.colorSpace == ColorPipeline::workingColorSpace(), "decoded format master domain");
            check(!ColorPipeline::toSrgb(master, ColorPipeline::OutputFormat::HighBitDepth, &error).isNull(), "decoded format export");
        }
        ColorPipeline::WorkingImage source;
        source.linearRgba = ipr::Image::create({64,64},ipr::Depth::F32,4);
        for (int y=0; y<64; ++y) for (int x=0; x<64; ++x)
            imagetest::at<ipr::Vec4f>(source.linearRgba, y,x) = {x/32.f-.1f, y/48.f, .2f, .37f};
        IprTestImageGenerator::exercise(source);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
