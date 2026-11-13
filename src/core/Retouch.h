#ifndef IPR_CORE_RETOUCH_H
#define IPR_CORE_RETOUCH_H

#include "LocalAdjustments.h"
#include "Types.h"

#include <QDataStream>
#include <QVariantList>
#include "Image.h"

#include <vector>

// Removing what should not be in a photo (lot B3). A retouch is a zone painted on the normalized
// creation canvas, attached to the photo as a mask is, and what fills it is worked out at each render: the source never changes,
// and a retouch can be turned off, changed or removed at any time. What it puts in the zone is
// reconstructed, not recovered: the recipe says where.
namespace Retouch {

enum class Kind {
    Remove,  // the zone is invented from what surrounds it
    Heal,    // the texture of another place, at the level of the zone's surroundings
    Clone    // another place, as it is
};

struct Operation
{
    Geometry::Settings frame; // fixed creation canvas, attached to the oriented source
    Kind kind = Kind::Remove;
    bool visible = true;
    float opacity = 1.0f;       // in [0, 1]
    std::vector<LocalAdjustments::BrushStroke> strokes;
    ipr::Point2f source{0.0f, 0.0f}; // Heal and Clone: from a zone's pixel to its source, shares of the canvas's sides
};

using Operations = std::vector<Operation>;

bool operator==(const Operation &a, const Operation &b);

// Clamps every field to its range; false on a non-finite value, or beyond the limits a recipe
// keeps its size within.
bool bound(Operations &operations);

// Whether these retouches change anything at all.
bool active(const Operations &operations);

// The zone, F32 of one channel in [0, 1], of this size; opacity and visibility apart.
ipr::Image coverage(const Operation &operation, const ipr::Size &canvas);

// Applies the visible retouches in order, each seeing the result of the ones before, to a linear
// F32 of three channels image on the canvas. Pixels outside the zones do not change. An empty pixel, exactly 0
// where a transform left nothing, is never taken as a source. Returns how many could not be done:
// a zone that takes too much of what surrounds it to be invented is left as it is.
int apply(ipr::Image &rgb, const Operations &operations);
// Apply fixed-frame operations on the current canvas, preserving pixels outside
// the projected correction. The raw source size defines normalized geometry.
int applyOnCanvas(ipr::Image &rgb, const Operations &operations, const Geometry::Settings &geometry,
                  const ipr::Size &source);
ipr::Image canvasCoverage(const Operation &operation, const Geometry::Settings &geometry, const ipr::Size &source);
ipr::Point2f suggestSourceOnCanvas(const ipr::Image &rgb, const Operation &operation,
                                  const Geometry::Settings &geometry, const ipr::Size &source);
QVariantList describeOnCanvas(const Operations &operations, const Geometry::Settings &geometry, const ipr::Size &source);

// Where to take a zone from: the place nearby whose surroundings best match the zone's. Shares of
// the canvas's sides, or (0, 0) when there is none. rgb: linear F32 of three channels, on the canvas.
ipr::Point2f suggestSource(const ipr::Image &rgb, const Operation &operation);

// What a render's result depends on, for the cache keys. Adds nothing without a retouch.
void appendKey(QDataStream &stream, const Operations &operations);

QString kindName(Kind kind);
bool kindNamed(const QString &name, Kind &kind);

// The retouches as the panel reads them, strokes as a count, and the zone's centre.
QVariantList describe(const Operations &operations);

} // namespace Retouch

#endif // IPR_CORE_RETOUCH_H
