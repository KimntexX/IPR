#include <libraw/libraw.h>
#include <QColorSpace>
#include <QImage>
#include <QString>
#include <cstdint>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const QString dir = QStringLiteral(
        "/mnt/c/Users/victo/Documents/Projet/Programmation/IPR/tests/rendered_compare/");
    for (int outputColor : {0, 1, 2, 3, 4, 5, 6, 7}) {
        LibRaw raw;
        int result = raw.open_file(argv[1]);
        if (result != LIBRAW_SUCCESS)
            return 3;
        raw.imgdata.params.use_camera_wb = 0;
        raw.imgdata.params.use_auto_wb = 1;
        raw.imgdata.params.output_color = outputColor;
        raw.imgdata.params.output_bps = 16;
        raw.imgdata.params.no_auto_bright = 1;
        raw.imgdata.params.gamm[0] = 1.0;
        raw.imgdata.params.gamm[1] = 1.0;
        if ((result = raw.unpack()) != LIBRAW_SUCCESS ||
            (result = raw.dcraw_process()) != LIBRAW_SUCCESS)
            continue;
        auto *data = raw.dcraw_make_mem_image(&result);
        if (!data || result != LIBRAW_SUCCESS || data->colors != 3 ||
            data->bits != 16) {
            if (data)
                LibRaw::dcraw_clear_mem(data);
            continue;
        }
        const auto *src = reinterpret_cast<const std::uint16_t *>(data->data);
        QImage image(data->width, data->height, QImage::Format_RGBX64);
        for (int y = 0; y < data->height; ++y) {
            auto *dst = reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for (int x = 0; x < data->width; ++x) {
                const int i = (y * data->width + x) * 3;
                dst[x] = QRgba64::fromRgba64(src[i], src[i + 1], src[i + 2], 65535);
            }
        }
        image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        image = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb))
                    .convertToFormat(QImage::Format_RGB888);
        const QString path = dir + QStringLiteral("output%1.jpg").arg(outputColor);
        image.save(path, "JPEG", 90);
        std::cout << "output=" << outputColor << " mean="
                  << src[0] << ',' << src[1] << ',' << src[2]
                  << " file=" << path.toStdString() << '\n';
        LibRaw::dcraw_clear_mem(data);
    }
}
