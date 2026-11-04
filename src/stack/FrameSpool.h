#ifndef IPR_STACK_FRAMESPOOL_H
#define IPR_STACK_FRAMESPOOL_H

#include <QFile>
#include <QString>
#include <opencv2/core.hpp>

// One image on disk, row after row, in the scratch folder of a job. What a job cannot keep
// in memory for every frame goes here, and comes back by bands.
class FrameSpool
{
public:
    explicit FrameSpool(const QString &path) : m_file(path) {}

    // False on a full disk. written grows by what reached the file.
    bool write(const cv::Mat &image, quint64 &written);
    // rows rows from row, of an image of this size and type.
    bool read(const cv::Size &size, int type, int row, int rows, cv::Mat &band);
    // Gives the disk space back at once.
    void remove();

private:
    QFile m_file;
};

#endif // IPR_STACK_FRAMESPOOL_H
