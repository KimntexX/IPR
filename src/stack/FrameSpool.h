#ifndef IPR_STACK_FRAMESPOOL_H
#define IPR_STACK_FRAMESPOOL_H

#include "Image.h"

#include <QFile>
#include <QString>

// One image on disk, row after row, in the scratch folder of a job. What a job cannot keep
// in memory for every frame goes here, and comes back by bands.
class FrameSpool
{
public:
    explicit FrameSpool(const QString &path) : m_file(path) {}

    // False on a full disk. written grows by what reached the file.
    bool write(const ipr::Image &image, quint64 &written);
    // rows rows from row, of an image of this size, depth and channels.
    bool read(const ipr::Size &size, ipr::Depth depth, int channels, int row, int rows, ipr::Image &band);
    // Gives the disk space back at once.
    void remove();

private:
    QFile m_file;
};

#endif // IPR_STACK_FRAMESPOOL_H
