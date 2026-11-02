#ifndef IPR_CORE_RECIPECODEC_H
#define IPR_CORE_RECIPECODEC_H

#include "EditRecipe.h"

#include <QCborMap>

// CBOR form of an edit recipe, shared by the .ipr sidecar and the edit history:
// one named field per value. Decoding skips unknown fields and leaves the target's
// value where a field is absent or of another type, so the format can grow.
namespace RecipeCodec {
QCborMap encodeSettings(const EditRecipe::Settings &settings);
// False when a value is not a finite number or a list exceeds its bound; the
// values are then kept within the ranges of the editor's controls.
bool decodeSettings(const QCborMap &map, EditRecipe::Settings &settings);
// Keeps the settings within the ranges of the editor's controls, so a damaged or
// hand-edited file cannot reach the render outside them. False on a non-finite value.
bool bound(EditRecipe::Settings &settings);

QCborMap encodeMetadata(const EditRecipe::UserImageMetadata &metadata);
void decodeMetadata(const QCborMap &map, EditRecipe::UserImageMetadata &metadata);
QCborMap encodeRaw(const EditRecipe::RawDevelopment &raw);
void decodeRaw(const QCborMap &map, EditRecipe::RawDevelopment &raw);

// A complete recipe as the history keeps it: the settings and, in one field, the
// development choices (not the engine's version, which no edit changes).
QCborMap encodeRecipe(const EditRecipe::Settings &settings, const EditRecipe::RawDevelopment &raw);
// A recipe without development choices leaves raw as it is.
bool decodeRecipe(const QCborMap &map, EditRecipe::Settings &settings, EditRecipe::RawDevelopment &raw);
// A recipe's settings, for comparing two recipes whatever their development.
QCborMap settingsOf(QCborMap recipe);
}

#endif // IPR_CORE_RECIPECODEC_H
