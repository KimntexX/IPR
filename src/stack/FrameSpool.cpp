#include "FrameSpool.h"
#include "Types.h"

bool FrameSpool::write(const ipr::Image &image, quint64 &written)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 rowBytes = qint64(image.width()) * qint64(image.pixelBytes());
    for (int y = 0; y < image.height(); ++y) {
        if (m_file.write(reinterpret_cast<const char *>(image.row<unsigned char>(y)), rowBytes) != rowBytes)
            return false; // a full disk shows here
        written += quint64(rowBytes);
    }
    m_file.close();
    return m_file.error() == QFile::NoError && m_file.open(QIODevice::ReadOnly);
}

bool FrameSpool::read(const ipr::Size &size, ipr::Depth depth, int channels, int row, int rows, ipr::Image &band)
{
    band = ipr::Image::create({size.width, rows}, depth, channels);
    const qint64 rowBytes = qint64(size.width) * qint64(band.pixelBytes());
    if (!m_file.seek(qint64(row) * rowBytes))
        return false;
    for (int y = 0; y < rows; ++y)
        if (m_file.read(reinterpret_cast<char *>(band.mutableRow<unsigned char>(y)), rowBytes) != rowBytes)
            return false;
    return true;
}

void FrameSpool::remove()
{
    m_file.close();
    m_file.remove();
}
