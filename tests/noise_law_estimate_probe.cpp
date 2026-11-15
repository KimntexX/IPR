// Manual check only: the noise law IPR estimates from one RAW (NoiseLaw::estimate, read noise
// from RawBlackLevelEstimator::readNoise), printed per site of the 2x2 cell in raster order, to
// compare with tests/neural/estimate_single_raw_law.py (research branch patch_noise_neural) on
// the same files. One line per file.
#include "LibRawInput.h"
#include "NoiseLaw.h"
#include "RawBlackLevelEstimator.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <libraw/libraw.h>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

bool printLaw(const QString &path)
{
    LibRaw raw;
#ifdef _WIN32
    const int opened = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    const int opened = raw.open_file(QFile::encodeName(path).constData());
#endif
    if (opened != LIBRAW_SUCCESS || raw.unpack() != LIBRAW_SUCCESS) {
        std::cout << QFileInfo(path).fileName().toStdString() << " unreadable\n";
        return false;
    }
    auto &data = raw.imgdata;
    // The optical black as the decoder reads it (RawDecoder::readBlackLevel, Canon's left area).
    RawBlackLevelEstimator::Input black;
    black.mosaic = data.rawdata.raw_image;
    black.rawWidth = data.sizes.raw_width;
    black.rawHeight = data.sizes.raw_height;
    black.rowStrideSamples = int(data.sizes.raw_pitch / sizeof(std::uint16_t));
    const libraw_area_t &area = data.makernotes.canon.LeftOpticalBlack;
    if (area.r > area.l && area.b > area.t)
        black.maskedAreas.push_back({area.l, area.t, area.r - area.l + 1, area.b - area.t + 1});
    else
        for (const auto &m : data.sizes.mask)
            if (m[2] > m[0] && m[3] > m[1])
                black.maskedAreas.push_back({m[1], m[0], m[3] - m[1], m[2] - m[0]});
    black.maximum = data.color.maximum;
    black.marginsAreMasked = true;
    black.leftMargin = data.sizes.left_margin;
    black.topMargin = data.sizes.top_margin;
    black.activeWidth = data.sizes.width;
    black.activeHeight = data.sizes.height;
    black.hasCfa = data.idata.filters != 0 && data.idata.colors >= 3;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            black.cfaChannels[size_t(row * 2 + column)] = raw.COLOR(row, column);
    // The black as the decoder settles it: measured when LibRaw's deviates (R6 Mark III: zero).
    const auto measured = RawBlackLevelEstimator::measure(black);
    std::array<unsigned int, 4> decoder{};
    for (int channel = 0; channel < 4; ++channel)
        decoder[size_t(channel)] = data.color.black + data.color.cblack[channel];
    const bool pattern = data.color.cblack[4] != 0 || data.rawdata.color.cblack[4] != 0;
    if (RawBlackLevelEstimator::decide(decoder, pattern, measured, int(data.color.maximum)).source ==
        RawBlackLevelEstimator::Source::Measured) {
        for (int channel = 0; channel < 4; ++channel) {
            data.color.cblack[channel] = measured.levels[size_t(channel)];
            data.rawdata.color.cblack[channel] = measured.levels[size_t(channel)];
        }
        data.color.black = 0;
        data.rawdata.color.black = 0;
    }
    RawInput::Input input = LibRawInput::read(raw);
    input.readVariance = RawBlackLevelEstimator::readNoise(black);
    std::array<double, 4> siteBlack{}, siteRead{};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const int colour = input.pattern[size_t(y)][size_t(x)];
            siteBlack[size_t(y * 2 + x)] = input.black[size_t(colour)];
            siteRead[size_t(y * 2 + x)] = input.readVariance[size_t(colour)];
        }
    QString reason;
    const auto law = NoiseLaw::estimate(input.mosaic, siteBlack, siteRead, input.white, &reason);
    std::cout << QFileInfo(path).fileName().toStdString() << " iso " << input.iso;
    if (!law) {
        std::cout << " refused: " << reason.toStdString() << '\n';
        return false;
    }
    for (const auto &position : law->positions)
        std::cout << " (" << position.shotSlope << ", " << position.readVariance << ")";
    std::cout << '\n';
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList paths = app.arguments().mid(1);   // Unicode paths, whatever the code page
    bool all = !paths.isEmpty();
    for (const QString &path : paths)
        all = printLaw(path) && all;
    return all ? 0 : 1;
}
