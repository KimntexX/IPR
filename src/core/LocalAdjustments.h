#ifndef IPR_CORE_LOCALADJUSTMENTS_H
#define IPR_CORE_LOCALADJUSTMENTS_H

#include "Geometry.h"
#include "LocalValues.h"

#include <QDataStream>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/core.hpp>

#include <array>
#include <vector>

// The masks of an image (ADR-0021, B2). A mask is not an image laid over another: it is a
// zone, made of shapes, that carries its own values of the existing settings. They act in
// the processing chain: at each stage, a pixel takes the global value plus the masks' values
// weighted by their coverage. Masks have neither order nor blend mode: they add up.
//
// Shapes are vectors, in the normalized canvas of Geometry: the image as straightened, before
// the crop, u and v in [0, 1] across the pixels' outer edges. They render the same at every
// resolution.
namespace LocalAdjustments {

enum class Kind { Linear, Radial, Brush, LuminanceRange };
enum class Combine { Add, Subtract, Intersect };

struct BrushStroke
{
    std::vector<cv::Point2f> points;
    float size = 0.05f;      // radius, as a share of the canvas's long side
    float feather = 0.5f;    // share of the radius that fades, in [0, 1]
    float flow = 1.0f;       // coverage at the stroke's centre, in [0, 1]
};

struct Shape
{
    Kind kind = Kind::Linear;
    Combine combine = Combine::Add;
    bool inverted = false;
    // Linear: whole effect before start, none after end.
    cv::Point2f start{0.5f, 0.25f};
    cv::Point2f end{0.5f, 0.75f};
    // Radial: an ellipse, its radii as shares of the canvas's width and height.
    cv::Point2f center{0.5f, 0.5f};
    float radiusX = 0.25f;
    float radiusY = 0.25f;
    float angle = 0.0f;      // degrees, clockwise
    float feather = 0.5f;    // share of the radius that fades, in [0, 1]
    std::vector<BrushStroke> strokes;
    // Luminance range: what the image shows before any edit, encoded as the eye reads it,
    // whole between low and high, fading over feather on each side. Alone it selects tones;
    // intersected with another shape, the tones of a zone.
    float rangeLow = 0.0f;
    float rangeHigh = 1.0f;
    float rangeFeather = 0.1f;
};

// In the units of the global sliders; see LocalValues.
using Values = std::array<float, LocalValues::ParameterCount>;

struct Mask
{
    QString name;
    bool visible = true;
    float opacity = 1.0f;    // in [0, 1]
    std::vector<Shape> shapes;
    Values values{};
};

using Masks = std::vector<Mask>;

bool operator==(const BrushStroke &a, const BrushStroke &b);
bool operator==(const Shape &a, const Shape &b);
bool operator==(const Mask &a, const Mask &b);

// Clamps every field to its range; false on a non-finite value, or beyond the limits a
// recipe keeps its size within.
bool bound(Masks &masks);

// Whether these masks change anything at all.
bool active(const Masks &masks);

// Whether a shape reads the image: the caller then prepares its luminance.
bool usesLuminance(const Masks &masks);
// The luminance a range reads, from linear RGB (CV_32FC3): CV_32FC1, encoded in [0, 1].
cv::Mat rangeLuminance(const cv::Mat &linearRgb);

// Coverage in [0, 1] of a mask's shapes, CV_32FC1 of this size; opacity and visibility apart.
// luminance: from rangeLuminance, of this size. Without it, a range covers nothing.
cv::Mat coverage(const Mask &mask, const cv::Size &canvas, const cv::Mat &luminance = {});

// What the masks add per setting, for an image of this size. A setting no mask touches stays
// an empty map, so that its module keeps its global path.
LocalValues::Maps valueMaps(const Masks &masks, const cv::Size &canvas, const cv::Mat &luminance = {});

// The maps of the settings that act on the camera signal, before the geometry's resampling:
// a canvas map brought to the source's pixels. Without a transform, returns it as is.
cv::Mat toSource(const cv::Mat &canvasMap, const Geometry::Settings &geometry, const cv::Size &source);

// What a render's result depends on, for the cache keys.
void appendKey(QDataStream &stream, const Masks &masks);

// The names the recipe and the panel use. -1 for an unknown one.
QString parameterName(int parameter);
int parameterNamed(const QString &name);
QString kindName(Kind kind);
QString combineName(Combine combine);
bool kindNamed(const QString &name, Kind &kind);
bool combineNamed(const QString &name, Combine &combine);

// The masks as the panel reads them, strokes as a count.
QVariantList describe(const Masks &masks);
// Applies the fields present in values to a shape; false on a field of the wrong type.
bool setShapeFields(Shape &shape, const QVariantMap &values);

} // namespace LocalAdjustments

#endif // IPR_CORE_LOCALADJUSTMENTS_H
