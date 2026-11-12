#include "LocalContrast.h"
#include <random>
#include "image_testing.h"
#include "WorkingColorSpace.h"
#include "Filters.h"
#include "Resize.h"
#include "Pixels.h"


#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// Grey image whose log luminance carries random detail at every scale.
ipr::Image texturedGrey(int size)
{
    ipr::Image detail = ipr::Image::create({size, size}, ipr::Depth::F32, 1);
    std::mt19937 random(5);
    std::normal_distribution<float> normal(0.f, 1.f);
    ipr::mapInPlace(detail, [&](float) { return normal(random); });
    ipr::Image log2Luminance = ipr::filled({size, size}, ipr::Depth::F32, 1, -3.0);   // around 0.125
    for (double sigma : {0.7, 3.0, 24.0}) {
        const ipr::Image band = ipr::gaussianBlurred(detail, sigma);
        std::vector<double> mean, deviation;
        ipr::meanAndDeviationOf(band, mean, deviation);
        const float weight = float(0.25 / deviation[0]);
        log2Luminance = ipr::mapped(log2Luminance, band, [weight](float l, float b) { return l + b * weight; });
    }
    ipr::Image rgb = ipr::Image::create({size, size}, ipr::Depth::F32, 3);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float v = std::exp2(imagetest::at<float>(log2Luminance, y, x));
            imagetest::at<ipr::Vec3f>(rgb, y, x) = ipr::Vec3f(v, v, v);
        }
    return rgb;
}

// Least-squares gain of the variant's band (G_a - G_b of the log luminance) on the original's.
double bandGain(const ipr::Image &original, const ipr::Image &variant, double a, double b)
{
    const auto band = [&](const ipr::Image &rgb) {
        ipr::Image l = ipr::Image::create(rgb.size(), ipr::Depth::F32, 1);
        for (int y = 0; y < rgb.height(); ++y)
            for (int x = 0; x < rgb.width(); ++x)
                imagetest::at<float>(l, y, x) = std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(rgb, y, x)) + 1e-3f);
        const ipr::Image ga = a > 0 ? ipr::gaussianBlurred(l, a, ipr::BlurBorder::MirrorEdge) : l.clone();
        const ipr::Image gb = ipr::gaussianBlurred(l, b, ipr::BlurBorder::MirrorEdge);
        return ipr::mapped(ga, gb, [](float x, float y) { return x - y; });
    };
    const ipr::Image o = band(original), v = band(variant);
    const auto dot = [](const ipr::Image &p, const ipr::Image &q) {
        double sum = 0;
        for (int y = 0; y < p.height(); ++y)
            for (int x = 0; x < p.width(); ++x)
                sum += double(p.row<float>(y)[x]) * double(q.row<float>(y)[x]);
        return sum;
    };
    return dot(o, v) / dot(o, o);
}

} // namespace

int main()
{
    const ipr::Image original = texturedGrey(512);

    ipr::Image same = original.clone();
    LocalContrast::apply(same, 0, 0);
    if (!check(ipr::largestDifference(same, original) == 0, "zero texture and clarity leave the image unchanged")) return 1;

    // Lightroom's fitted response (etude-amplitude-effets-v001): small variations of
    // the fine bands are strongly amplified by texture, which fades by 16-32 pixels.
    ipr::Image texture = original.clone();
    LocalContrast::apply(texture, 100, 0);
    const double fine = bandGain(original, texture, 0, 1), wide = bandGain(original, texture, 16, 32);
    if (!(fine > 1.3 && fine < 1.65 && wide > 1.0 && wide - 1.0 < 0.5 * (fine - 1.0)))
        std::cerr << "texture +100: fine " << fine << ", 16-32 px " << wide << "\n";
    if (!check(fine > 1.3 && fine < 1.65 && wide > 1.0 && wide - 1.0 < 0.5 * (fine - 1.0),
               "texture +100 amplifies fine detail and fades at wide scales")) return 1;
    ipr::Image smoother = original.clone();
    LocalContrast::apply(smoother, -100, 0);
    const double reduced = bandGain(original, smoother, 0, 1);
    if (!check(reduced > 0.4 && reduced < 0.7, "texture -100 reduces the fine band")) return 1;

    // A 4 EV step with faint detail: clarity +100 amplifies the detail but
    // preserves the edge (no strong halo, flat sides kept).
    ipr::Image step = ipr::Image::create({512, 256}, ipr::Depth::F32, 3);
    for (int y = 0; y < step.height(); ++y)
        for (int x = 0; x < step.width(); ++x) {
            const float base = x < 256 ? 0.05f : 0.8f;
            const float v = base * std::exp2(0.04f * std::sin(0.4f * float(y)));
            imagetest::at<ipr::Vec3f>(step, y, x) = ipr::Vec3f(v, v, v);
        }
    step = ipr::gaussianBlurred(step, 1.5, ipr::BlurBorder::MirrorEdge);   // edge softened as by optics
    ipr::Image clear = step.clone();
    LocalContrast::apply(clear, 0, 100);
    const auto stripe = [](const ipr::Image &m, int x0, int x1) {
        double lo = 1e9, hi = -1e9;
        for (int y = 64; y < 192; ++y)
            for (int x = x0; x < x1; ++x) {
                const double l = std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(m, y, x)));
                lo = std::min(lo, l);
                hi = std::max(hi, l);
            }
        return std::make_pair(lo, hi);
    };
    const auto [stepLo, stepHi] = stripe(step, 60, 120);
    const auto [clearLo, clearHi] = stripe(clear, 60, 120);
    if (!check((clearHi - clearLo) > 1.5 * (stepHi - stepLo), "clarity +100 amplifies faint detail")) return 1;
    // wide halo: change of the flat sides from 8 to 56 pixels off the edge
    double halo = 0;
    for (int x = 200; x < 312; ++x) {
        if (std::abs(x - 256) < 8) continue;
        const double before = std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(step, 128, x)));
        const double after = std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(clear, 128, x)));
        halo = std::max(halo, std::abs(after - before));
    }
    if (!(halo < 1.2)) std::cerr << "clarity +100: largest change 8-56 px off the edge " << halo << " EV\n";
    // Lightroom's own rings at +100 reach 0.7-1.0 EV beside strong edges (6V1A9947):
    // the bound guards against a regression, not against Lightroom's behaviour.
    if (!check(halo < 1.2, "clarity +100 keeps a 4 EV edge without a strong halo")) return 1;

    // Colours keep their channel ratios and stay finite and nonnegative.
    ipr::Image colour = original.clone();
    for (int y = 0; y < colour.height(); ++y)
        for (int x = 0; x < colour.width(); ++x) {
            auto &p = imagetest::at<ipr::Vec3f>(colour, y, x);
            p = ipr::Vec3f(p[0] * 1.4f, p[1], p[2] * 0.5f);
        }
    const ipr::Image before = colour.clone();
    LocalContrast::apply(colour, 60, 60);
    bool ratios = true, finite = true;
    for (int y = 0; y < colour.height(); ++y)
        for (int x = 0; x < colour.width(); ++x) {
            const auto &a = imagetest::at<ipr::Vec3f>(before, y, x), &b = imagetest::at<ipr::Vec3f>(colour, y, x);
            finite = finite && std::isfinite(b[0]) && std::isfinite(b[1]) && std::isfinite(b[2]) && b[0] >= 0 && b[2] >= 0;
            ratios = ratios && std::abs(b[0] / b[1] - a[0] / a[1]) < 1e-3f && std::abs(b[2] / b[1] - a[2] / a[1]) < 1e-3f;
        }
    if (!check(finite && ratios, "colours keep their ratios and stay finite and nonnegative")) return 1;

    // On the preview proxy, the effect is what the reduction leaves of the full image's.
    {
        const ipr::Image full = texturedGrey(768);
        const ipr::Image reduced = ipr::resized(full, ipr::Size(256, 256), ipr::Interpolation::Area);
        const auto logChange = [&](const ipr::Image &rgb) {
            ipr::Image change = ipr::Image::create(reduced.size(), ipr::Depth::F32, 1);
            for (int y = 0; y < reduced.height(); ++y)
                for (int x = 0; x < reduced.width(); ++x)
                    imagetest::at<float>(change, y, x) = std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(rgb, y, x)) + 1e-3f)
                        - std::log2(WorkingColor::nonNegativeLuminance(imagetest::at<ipr::Vec3f>(reduced, y, x)) + 1e-3f);
            return change;
        };
        for (const auto &[texture, clarity] : {std::pair{100, 0}, std::pair{0, 100}}) {
            ipr::Image master = full.clone();
            LocalContrast::apply(master, texture, clarity);
            const ipr::Image expected = ipr::resized(master, reduced.size(), ipr::Interpolation::Area);
            ipr::Image scaled = reduced.clone(), unscaled = reduced.clone();
            LocalContrast::apply(scaled, texture, clarity, 1.0 / 3.0);
            LocalContrast::apply(unscaled, texture, clarity);
            const ipr::Image target = logChange(expected);
            const ipr::Image nothing = ipr::filled(target.size(), ipr::Depth::F32, 1, 0.0);
            const double scaledError = imagetest::distance(logChange(scaled), target) / imagetest::distance(target, nothing);
            const double unscaledError = imagetest::distance(logChange(unscaled), target) / imagetest::distance(target, nothing);
            // Texture lives at scales the proxy cannot show unscaled (error 0.49 -> 0.11);
            // clarity's large bands survive the reduction better (0.18 -> 0.13).
            if (!check(scaledError < 0.35 && scaledError < unscaledError,
                       "on a 1/3 proxy the effect matches the reduced full-resolution effect")) return 1;
        }
    }

    ipr::Image empty;
    LocalContrast::apply(empty, 50, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
