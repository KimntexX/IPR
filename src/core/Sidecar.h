#ifndef IPR_CORE_SIDECAR_H
#define IPR_CORE_SIDECAR_H

#include "EditRecipe.h"

#include <QString>

// The .ipr sidecar beside an image (format version 6): settings, user metadata
// and RAW development recipe, tied to the source file's size and date. Older
// versions are read; mixer bands of versions 1-3 keep their hue identity.
namespace Sidecar {
QString path(const QString &fileName);
bool read(const QString &fileName, EditRecipe::Settings *settings,
          EditRecipe::UserImageMetadata *metadata, EditRecipe::RawDevelopment *raw = nullptr);
bool write(const QString &fileName, const EditRecipe::Settings &settings,
           const EditRecipe::UserImageMetadata &metadata, const EditRecipe::RawDevelopment &raw);
}

#endif // IPR_CORE_SIDECAR_H
