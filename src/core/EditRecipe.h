#ifndef IPR_CORE_EDITRECIPE_H
#define IPR_CORE_EDITRECIPE_H

#include "ColorMixer.h"
#include "PointColor.h"
#include "ToneCurve.h"

#include <QString>

#include <array>

// Data of an image edit, as saved in its .ipr sidecar: adjustment settings,
// RAW development recipe and user metadata. Plain values, no behaviour.
namespace EditRecipe {
struct Settings
{
    float brightness = 0; // gestion de l'exposition
    int contrast = 0;   // gestion du contraste
    int blur = 0;
    bool grayscale = false;
    int shadows = 0;    // gestion des ombres
    int blacks = 0;     // gestion des noirs
    int highlights = 0; // gestion des hautes lumières
    int whites = 0;     // gestion des blancs
    int temperature = 5200; // température cible en kelvins (As Shot typique Canon)
    int tint = 0;       // nuance relative de -150 (vert) à +150 (magenta)
    int vibrance = 0;   // priorité aux couleurs peu saturées
    int saturation = 0; // intensité uniforme de la chrominance
    int texture = 0;    // micro-contraste local
    int clarity = 0;    // contraste local à plus grande échelle
    int dehaze = 0;     // récupération ou ajout de voile
    int grain = 0;      // grain monochrome déterministe
    int denoiseLuminance = 0;
    int denoiseChrominance = 0;
    // Experimental: a RAW is developed signed (values below the black kept) and the
    // sliders act on its camera signal before the lens and colour profiles. A session
    // switch of the editor, not stored in the sidecar.
    bool denoiseBeforeProfile = false;
    int sharpeningAmount = 0;
    float sharpeningRadius = 1.0f;
    int sharpeningMasking = 0;
    // Red, orange, yellow, green, cyan, blue, violet, magenta (sidecar v4).
    std::array<int, ColorMixer::BandCount> mixerHue{};
    std::array<int, ColorMixer::BandCount> mixerSaturation{};
    std::array<int, ColorMixer::BandCount> mixerLuminance{};
    // Shadows, midtones, highlights and the global wheel (global and luminances: sidecar v8).
    std::array<int, 4> gradingHue{};
    std::array<int, 4> gradingSaturation{};
    std::array<int, 4> gradingLuminance{};
    int gradingBlending = 50;   // Lightroom's defaults (sidecar v7)
    int gradingBalance = 0;
    PointColor::Parameters pointColor;   // picked colours, in picking order (sidecar v9)
    bool automaticWhiteBalance = false;
    ToneCurve::Curves toneCurves;
};

struct RawDevelopment
{
    QString profile;
    QString profileSha256;
    QString manifest;
    QString manifestSha256;
    bool normalizationEnabled = false;
    QString engineVersion;
    QString lensProfile;
    QString lensProfileSha256;
    bool lensProfileResolved = false;
    bool lensCorrectionEnabled = true;
    bool chromaticAberrationEnabled = true;
};

struct UserImageMetadata
{
    int rating = 0;
    QString colorLabel;
    bool flagged = false;
    bool ratingSet = false;
    bool colorLabelSet = false;
};
}

#endif // IPR_CORE_EDITRECIPE_H
