#include "FrameSpool.h"

bool FrameSpool::write(const cv::Mat &image, quint64 &written)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 rowBytes = qint64(image.cols) * qint64(image.elemSize());
    for (int y = 0; y < image.rows; ++y) {
        if (m_file.write(reinterpret_cast<const char *>(image.ptr(y)), rowBytes) != rowBytes)
            return false; // a full disk shows here
        written += quint64(rowBytes);
    }
    m_file.close();
    return m_file.error() == QFile::NoError && m_file.open(QIODevice::ReadOnly);
}

bool FrameSpool::read(const cv::Size &size, int type, int row, int rows, cv::Mat &band)
{
    band.create(rows, size.width, type);
    const qint64 rowBytes = qint64(size.width) * qint64(band.elemSize());
    if (!m_file.seek(qint64(row) * rowBytes))
        return false;
    for (int y = 0; y < rows; ++y)
        if (m_file.read(reinterpret_cast<char *>(band.ptr(y)), rowBytes) != rowBytes)
            return false;
    return true;
}

void FrameSpool::remove()
{
    m_file.close();
    m_file.remove();
}
