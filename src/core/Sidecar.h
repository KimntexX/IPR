#ifndef IPR_CORE_SIDECAR_H
#define IPR_CORE_SIDECAR_H

#include "EditHistory.h"
#include "EditRecipe.h"

#include <QString>

#include <optional>

// The .ipr sidecar beside an image (ADR-0021): a self-described CBOR document
// whose named sections are the header ("ipr": format, writer, identity of the
// photo), the settings, the user metadata, the RAW development recipe and the
// compressed edit history.
// Unknown sections and fields are skipped, missing fields take their default.
// The former binary sidecars (versions 1 to 9) are read and converted; the first
// save rewrites them at the current format. A file of a newer format is read but
// never overwritten.
namespace Sidecar {
inline constexpr int FormatVersion = 13;

// Where a sidecar stands against this IPR.
enum class Format { Current, Older, Newer };

struct Document
{
    EditRecipe::Settings settings;
    EditRecipe::UserImageMetadata metadata;
    EditRecipe::RawDevelopment raw;
    EditHistory history;   // empty for a sidecar saved without one
    Format format = Format::Current;   // set by read(); write() always writes the current format
};

QString path(const QString &fileName);
// No document when the sidecar is absent, damaged or tied to another state of the photo.
std::optional<Document> read(const QString &fileName);
// False when writing fails or when the existing sidecar has a newer format.
bool write(const QString &fileName, const Document &document);
}

#endif // IPR_CORE_SIDECAR_H
