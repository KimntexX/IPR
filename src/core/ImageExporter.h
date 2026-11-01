#ifndef IPR_CORE_IMAGEEXPORTER_H
#define IPR_CORE_IMAGEEXPORTER_H

#include "ColorPipeline.h"

#include <QString>
#include <QUrl>

// Writes a rendered working image to JPEG, PNG, TIFF (16-bit sRGB) or linear
// DNG. The file is encoded beside the destination, then published under the
// first free name (name-2, name-3, ...): an existing file is never replaced.
namespace ImageExporter {
struct Result
{
    bool success = false;
    QString message;
};

// "jpg" -> "JPEG"; other names upper-cased.
QString normalizedFormat(QString format);
// File suffix of a normalized format, empty when it is not supported.
QString suffix(const QString &normalizedFormat);
bool supports(const QString &format);
// Local file path of a URL or of a native absolute path, empty otherwise.
QString localPath(const QUrl &target);
// Target with the suffix of the format (tif/jpeg accepted as such).
QString normalizedPath(const QString &target, const QString &normalizedFormat);
// The target, or the first free numbered variant of it; empty if none.
QString availablePath(const QString &target);

Result write(const ColorPipeline::WorkingImage &rendered, const QString &target,
             const QString &format, int quality);
}

#endif // IPR_CORE_IMAGEEXPORTER_H
