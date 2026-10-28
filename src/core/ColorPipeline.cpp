#include "ColorPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace ColorPipeline {

namespace {

bool convertSourceToWorking(QImage &image, QString *error)
{
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("L'image source est vide.");
        return false;
    }

    if (!image.colorSpace().isValid())
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));

    QImage highPrecision = image.convertToFormat(QImage::Format_RGBA32FPx4);
    if (highPrecision.isNull()) {
        if (error)
            *error = QStringLiteral("Impossible de convertir l'image en RGBA flottant.");
        return false;
    }

    const QColorSpace working = workingColorSpace();
    const bool srgb = image.colorSpace() == QColorSpace(QColorSpace::SRgb);
    const bool linearSrgb = image.colorSpace() == QColorSpace(QColorSpace::SRgbLinear);
    if (srgb || linearSrgb || image.colorSpace() == working) {
        cv::Mat rgb(highPrecision.height(), highPrecision.width(), CV_32FC4,
                    highPrecision.bits(), highPrecision.bytesPerLine());
        if (srgb) {
            for (int y = 0; y < rgb.rows; ++y) {
                auto *row = rgb.ptr<cv::Vec4f>(y);
                for (int x = 0; x < rgb.cols; ++x) for (int c = 0; c < 3; ++c) {
                    const double v = std::abs(double(row[x][c]));
                    const double decoded = WorkingColor::srgbDecode(v);
                    row[x][c] = float(std::copysign(decoded, row[x][c]));
                }
            }
        }
        if (srgb || linearSrgb) WorkingColor::transform(rgb, WorkingColor::fromSrgb());
        highPrecision.setColorSpace(working);
        image = std::move(highPrecision);
        return true;
    }
    QImage converted = highPrecision.convertedToColorSpace(working);
    if (converted.isNull()) {
        if (error)
            *error = QStringLiteral("Impossible de convertir l'image vers Rec.2020 linéaire.");
        return false;
    }

    converted.setColorSpace(working);
    image = std::move(converted);
    return true;
}

cv::Vec3f mapRgbToSrgbGamut(const cv::Vec3f &source)
{
    cv::Vec3f rgb{
        std::max(0.0f, source[0]),
        std::max(0.0f, source[1]),
        std::max(0.0f, source[2]),
    };
    float luminance = 0.2126f * rgb[0] + 0.7152f * rgb[1] +
                      0.0722f * rgb[2];
    if (luminance > 1.0f) {
        rgb *= 1.0f / luminance;
        luminance = 1.0f;
    }

    const cv::Vec3f neutral{luminance, luminance, luminance};
    const cv::Vec3f chroma = rgb - neutral;
    float chromaScale = 1.0f;
    for (int channel = 0; channel < 3; ++channel) {
        if (chroma[channel] > 0.0f) {
            chromaScale = std::min(
                chromaScale, (1.0f - luminance) / chroma[channel]);
        } else if (chroma[channel] < 0.0f) {
            chromaScale = std::min(
                chromaScale, luminance / -chroma[channel]);
        }
    }
    return neutral + chroma * std::clamp(chromaScale, 0.0f, 1.0f);
}

QImage toEncodedSrgbRgba64(const cv::Mat &linearRgba)
{
    // Encode before quantization. Qt's integer color-space LUT loses low
    // linear16 codes; this buffer is already in sRGB primaries, so only
    // the analytic sRGB transfer is needed. Alpha is straight and linear.
    cv::Mat rgba16(linearRgba.rows, linearRgba.cols, CV_16UC4);
    for (int y = 0; y < linearRgba.rows; ++y) {
        const cv::Vec4f *source = linearRgba.ptr<cv::Vec4f>(y);
        cv::Vec<quint16, 4> *destination = rgba16.ptr<cv::Vec<quint16, 4>>(y);
        for (int x = 0; x < linearRgba.cols; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                const double linear = std::clamp(double(source[x][channel]), 0.0, 1.0);
                const double encoded = WorkingColor::srgbEncode(linear);
                destination[x][channel] = static_cast<quint16>(std::lround(encoded * 65535.0));
            }
            destination[x][3] = static_cast<quint16>(std::lround(
                std::clamp(source[x][3], 0.0f, 1.0f) * 65535.0f));
        }
    }

    QImage image(rgba16.cols, rgba16.rows, QImage::Format_RGBA64);
    for (int y = 0; y < rgba16.rows; ++y)
        std::memcpy(image.scanLine(y), rgba16.ptr(y), rgba16.cols * 8);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

}

WorkingImage fromSourceImage(const QImage &source, QString *error)
{
    WorkingImage result;
    const bool hasEmbeddedProfile = source.colorSpace().isValid();
    QString sourceProfile = hasEmbeddedProfile
                                ? source.colorSpace().description()
                                : QStringLiteral("sRGB supposé (profil ICC absent ou invalide)");
    if (sourceProfile.isEmpty())
        sourceProfile = QStringLiteral("Profil ICC embarqué");

    QImage linear = source;
    if (!convertSourceToWorking(linear, error))
        return result;

    cv::Mat rgbaFloat(linear.height(), linear.width(), CV_32FC4,
                   const_cast<uchar *>(linear.constBits()), linear.bytesPerLine());
    result.linearRgba = rgbaFloat.clone();
    if (!cv::checkRange(result.linearRgba, true)) {
        if (error) *error = QStringLiteral("L'image source contient des valeurs non finies.");
        return {};
    }
    result.colorSpace = workingColorSpace();
    result.sourceProfile = sourceProfile;
    return result;
}

WorkingImage fromLinearSrgb(const cv::Mat &rgb, QString *error)
{
    WorkingImage result;
    if (rgb.empty() || rgb.type() != CV_32FC3) {
        if (error)
            *error = QStringLiteral("Les données RAW RGB linéaires sont invalides.");
        return result;
    }

    std::vector<cv::Mat> channels;
    cv::split(rgb, channels);
    channels.push_back(cv::Mat(rgb.size(), CV_32FC1, cv::Scalar(1.0f)));
    cv::merge(channels, result.linearRgba);
    WorkingColor::transform(result.linearRgba, WorkingColor::fromSrgb());
    result.colorSpace = workingColorSpace();
    result.sourceProfile = QStringLiteral("RAW — sRGB linéaire");
    return result;
}

WorkingImage mapToSrgbGamut(const WorkingImage &source, QString *error)
{
    if (source.isNull() || source.colorSpace != workingColorSpace() ||
        source.linearRgba.type() != CV_32FC4 ||
        !cv::checkRange(source.linearRgba, true)) {
        if (error)
            *error = QStringLiteral(
                "La compression de gamut attend une image RGBA linéaire finie.");
        return {};
    }

    WorkingImage result = source;
    result.linearRgba = source.linearRgba.clone();
    WorkingColor::transform(result.linearRgba, WorkingColor::toSrgb());
    if (!cv::checkRange(result.linearRgba, true)) {
        if (error) *error = QStringLiteral("La conversion de sortie a produit des valeurs non finies.");
        return {};
    }
    result.colorSpace = QColorSpace(QColorSpace::SRgbLinear);
    result.dcpContext.reset(); // This is an output derivative, not a reeditable source.
    for (int y = 0; y < result.linearRgba.rows; ++y) {
        cv::Vec4f *row = result.linearRgba.ptr<cv::Vec4f>(y);
        for (int x = 0; x < result.linearRgba.cols; ++x) {
            const cv::Vec3f rgb = mapRgbToSrgbGamut(
                {row[x][0], row[x][1], row[x][2]});
            row[x][0] = rgb[0];
            row[x][1] = rgb[1];
            row[x][2] = rgb[2];
        }
    }
    return result;
}

namespace {

// Correlated color temperature (CCT) of a light source is converted to its
// Planckian-locus chromaticity using Krystek's rational approximation (valid
// over the whole 2000-50000 K slider range used here), the same curve the
// DNG specification uses to define "Temperature" independently of any
// specific camera. See docs/imageeditor-render-working-image.md §4 for the
// rationale behind this model.
cv::Point2d planckianLocusChromaticity(double kelvinTemperature)
{
    const double t = kelvinTemperature;
    const double t2 = t * t;
    const double u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) /
                      (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2);
    const double v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) /
                      (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2);
    const double denominator = 2.0 * u - 8.0 * v + 4.0;
    return {3.0 * u / denominator, 2.0 * v / denominator};
}

// LMS (long/medium/short cone) response of the Planckian white point for a
// given temperature, via the Bradford chromatic adaptation basis. Comparing
// this response between two temperatures yields the per-cone gain a von
// Kries adaptation transform must apply to make a pixel neutral at the
// reference temperature appear as if it were neutral at the target
// temperature instead — which is exactly what the "Temperature" slider asks
// for.
cv::Vec3d planckianLocusLms(double kelvinTemperature)
{
    const cv::Point2d xy = planckianLocusChromaticity(kelvinTemperature);
    const cv::Vec3d xyz(xy.x / xy.y, 1.0, (1.0 - xy.x - xy.y) / xy.y);
    // Bradford cone-response matrix (XYZ -> LMS).
    const cv::Matx33d bradford(
        0.8951, 0.2664, -0.1614,
        -0.7502, 1.7135, 0.0367,
        0.0389, -0.0685, 1.0296);
    return bradford * xyz;
}

// Builds the 3x3 linear-sRGB matrix that performs a von Kries chromatic
// adaptation from referenceTemperature to temperature: it pre-converts RGB
// to XYZ then to LMS, applies the per-cone gain derived from the Planckian
// locus, then converts back to XYZ and RGB. Combining every step into one
// matrix keeps the per-pixel cost identical to the previous diagonal gain
// (a single 3x3 multiply-add), while being colorimetrically correct instead
// of an arbitrary, overly flat log(Kelvin) gain curve.  The cold branch also
// receives a corpus-calibrated diagonal compensation.  Bradford gives the
// physical direction, while that compensation corrects the under-attenuated
// red response measured against the Lightroom TIFF reference at 2000 K.
cv::Matx33d temperatureAdaptationMatrix(int temperature, double referenceTemperature)
{
    const cv::Matx33d rgbToXyz(
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041);
    const cv::Matx33d xyzToRgb = rgbToXyz.inv();
    const cv::Matx33d bradford(
        0.8951, 0.2664, -0.1614,
        -0.7502, 1.7135, 0.0367,
        0.0389, -0.0685, 1.0296);
    const cv::Matx33d bradfordInv = bradford.inv();

    const cv::Vec3d referenceLms = planckianLocusLms(referenceTemperature);
    const cv::Vec3d targetLms = planckianLocusLms(static_cast<double>(temperature));
    // The green/M channel is left untouched (gain = 1): luminance and the
    // green/magenta balance stay governed solely by the Tint control, so the
    // Temperature slider only redistributes energy between red and blue.
    const cv::Vec3d gain(referenceLms[0] / targetLms[0], 1.0,
                         referenceLms[2] / targetLms[2]);
    const cv::Matx33d gainMatrix(
        gain[0], 0.0, 0.0,
        0.0, gain[1], 0.0,
        0.0, 0.0, gain[2]);
    const cv::Matx33d bradfordAdaptation =
        xyzToRgb * bradfordInv * gainMatrix * bradford * rgbToXyz;

    // The Lightroom/IPR eight-scene corpus showed that the uncorrected cold
    // endpoint retained too much red (0.526 after v2 vs. 0.432) and slightly
    // too much blue (4.691 vs. 4.535).  Blend the empirical endpoint
    // progressively in
    // log-Kelvin space below the source reference only; warm temperatures keep
    // the pure Bradford result already validated by the corpus.
    if (static_cast<double>(temperature) >= referenceTemperature ||
        referenceTemperature <= 2000.0)
        return bradfordAdaptation;

    const double coldRange = std::log(referenceTemperature / 2000.0);
    const double coldStrength = std::clamp(
        std::log(referenceTemperature / static_cast<double>(temperature)) / coldRange,
        0.0, 1.0);
    const cv::Matx33d coldCalibration(
        1.0 - 0.65 * coldStrength, 0.0, 0.0,
        0.0, 1.0 + 0.19 * coldStrength, 0.0,
        0.0, 0.0, 1.0 - 0.16 * coldStrength);
    return coldCalibration * bradfordAdaptation;
}

}

bool applyTemperatureTint(cv::Mat &linearRgb, int temperature,
                          double referenceTemperature, int tint,
                          QString *error)
{
    if (linearRgb.empty() || linearRgb.type() != CV_32FC3) {
        if (error)
            *error = QStringLiteral("La balance température/nuance attend une image RGB flottante.");
        return false;
    }
    if (temperature < 2000 || temperature > 50000 || tint < -150 || tint > 150 ||
        !std::isfinite(referenceTemperature) ||
        referenceTemperature < 2000.0 || referenceTemperature > 50000.0) {
        if (error)
            *error = QStringLiteral("La température et sa référence doivent être comprises entre 2000 et 50000 K, et la nuance entre -150 et 150.");
        return false;
    }

    const float tintAmount = static_cast<float>(tint) / 150.0f;
    const float rawGreenGain = std::pow(2.0f, -tintAmount);
    const float rawMagentaGain = std::pow(2.0f, 0.5f * tintAmount);
    // A green/magenta diagonal must not behave like an exposure adjustment.
    // Normalize its gain on a neutral linear-sRGB color using Rec. 709
    // luminance coefficients.  This keeps the chromatic axis intact while
    // preserving the luminance of neutral tones for both tint directions.
    const float tintLuminanceGain = 0.2848f * rawMagentaGain +
                                    0.7152f * rawGreenGain;
    const float greenGain = rawGreenGain / tintLuminanceGain;
    const float magentaGain = rawMagentaGain / tintLuminanceGain;

    // Fast, exact no-op path: when the slider sits exactly on the detected
    // reference and tint is neutral, skip the matrix math entirely so the
    // pixels are left bit-identical (matrix round-trips through an inverse
    // can otherwise introduce negligible but non-zero floating-point noise).
    if (tint == 0 &&
        std::abs(static_cast<double>(temperature) - referenceTemperature) < 1e-6)
        return true;

    // The empirical tint/cold axes were calibrated in sRGB. Conjugate the
    // complete control matrix once; all pixel arithmetic stays in Rec.2020.
    const cv::Matx33d tintMatrix(magentaGain, 0, 0, 0, greenGain, 0, 0, 0, magentaGain);
    const cv::Matx33d adaptation = WorkingColor::fromSrgb() * tintMatrix *
        temperatureAdaptationMatrix(temperature, referenceTemperature) * WorkingColor::toSrgb();
    const cv::Matx33f adaptationF(
        static_cast<float>(adaptation(0, 0)), static_cast<float>(adaptation(0, 1)),
        static_cast<float>(adaptation(0, 2)),
        static_cast<float>(adaptation(1, 0)), static_cast<float>(adaptation(1, 1)),
        static_cast<float>(adaptation(1, 2)),
        static_cast<float>(adaptation(2, 0)), static_cast<float>(adaptation(2, 1)),
        static_cast<float>(adaptation(2, 2)));

    for (int y = 0; y < linearRgb.rows; ++y) {
        cv::Vec3f *row = linearRgb.ptr<cv::Vec3f>(y);
        for (int x = 0; x < linearRgb.cols; ++x) {
            const cv::Vec3f adapted = adaptationF * row[x];
            row[x] = adapted;
        }
    }
    return true;
}

QImage toSrgb(const WorkingImage &source, OutputFormat outputFormat, QString *error)
{
    if (source.isNull()) {
        if (error)
            *error = QStringLiteral("L'image de travail est vide.");
        return {};
    }

    if (source.linearRgba.type() != CV_32FC4) {
        if (error)
            *error = QStringLiteral("L'image de travail doit être en RGBA flottant 32 bits.");
        return {};
    }
    if (source.colorSpace != workingColorSpace()) {
        if (error)
            *error = QStringLiteral("L'image de travail n'est pas en Rec.2020 linéaire.");
        return {};
    }
    if (!cv::checkRange(source.linearRgba, true)) {
        if (error)
            *error = QStringLiteral("L'image de travail contient des valeurs non finies.");
        return {};
    }

    const WorkingImage gamutMapped = mapToSrgbGamut(source, error);
    if (gamutMapped.isNull())
        return {};

    QImage encoded = toEncodedSrgbRgba64(gamutMapped.linearRgba);
    if (encoded.isNull()) {
        if (error)
            *error = QStringLiteral("Impossible de quantifier l'image de travail.");
        return {};
    }

    switch (outputFormat) {
    case OutputFormat::Preview8Bit:
        encoded = encoded.convertToFormat(QImage::Format_RGBA8888);
        break;
    case OutputFormat::Jpeg8Bit:
        encoded = encoded.convertToFormat(QImage::Format_RGB888);
        break;
    case OutputFormat::HighBitDepth:
        encoded = encoded.convertToFormat(QImage::Format_RGBA64);
        break;
    }

    if (encoded.isNull()) {
        if (error)
            *error = QStringLiteral("Impossible de convertir l'image sRGB au format demandé.");
        return {};
    }

    encoded.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return encoded;
}

}
