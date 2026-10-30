#ifndef IPR_TOOLS_TESTRECIPES_H
#define IPR_TOOLS_TESTRECIPES_H

#include "imageeditor.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// Test states shared by the TIFF generator (visual review) and the render
// server (in-memory analysis): one catalogue of controls and values, one
// development recipe, one reference rule. Both tools render through the same
// full-resolution path as the IHM export.
namespace TestRecipes {
using Settings = ImageEditor::Settings;
using LoadResult = ImageEditor::LoadResult;

struct Feature {
    QString name;
    QList<double> values;
};

// Decode options a state needs; a state that differs from the default needs
// its own decode (a second master).
struct Decode {
    bool automaticWhiteBalance = false;
    bool lensCorrection = false;
    bool chromaticAberration = false;
    // The editor's experimental camera-signal denoise: signed development, signal kept
    // before the lens corrections.
    bool signedCameraSignal = false;
    bool isDefault() const
    {
        return !automaticWhiteBalance && !lensCorrection && !chromaticAberration && !signedCameraSignal;
    }
};

// The state a variant is measured against: the neutral ("neutral", 0) or a
// support state of the same control (temperature 5200, sharpening radius 1...).
struct Reference {
    QString feature;
    double value = 0;
};

const QStringList &groups();
// Supported images at the root of base, without recursion, sorted by name.
QFileInfoList sources(const QString &base);
QList<Feature> features();
QString featureGroup(const QString &name);
QString featureFolder(const QString &name);
QString fileName(const QString &stem, const QString &feature, double value);
// Controls that only exist for RAW sources (WB, optics).
bool rawOnly(const QString &name);
bool eligible(const QString &feature, const QString &sourcePath);

Decode decodeFor(const QString &feature, double value);
Reference referenceFor(const QString &feature);
// Same RAW recipe as the IHM default: Camera Neutral and the RAW calibration
// (ADR-0007); optics off outside the optique series, like the Lightroom references.
LoadResult load(const QString &path, const Decode &decode = {});
Settings neutral(const LoadResult &loaded);
Settings adjusted(Settings settings, const QString &name, double value);
// Complete settings of a state ("neutral" included) on a decoded source.
Settings settingsFor(const LoadResult &loaded, const QString &feature, double value);

QJsonObject declaredSettings(const Settings &settings);
QString sha256(const QString &path);
// Provenance of a rendered state, embedded in the TIFF or returned by the
// render server; `reference` names the reference file or state.
QJsonObject declaration(const LoadResult &source, const Settings &settings, const QString &input,
                        const QString &group, const QString &feature, double value,
                        const QString &reference);
}

#endif // IPR_TOOLS_TESTRECIPES_H
