#include "Dehaze.h"
#include "image_testing.h"
#include "Resize.h"
#include "Pixels.h"


#include <cmath>
#include <iostream>

namespace {

bool check(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// A scene seen through haze on its upper half (I = J t + A (1 - t)), clear below,
// under a sky at the airlight (the top rows), as in a hazy landscape.
ipr::Image hazyScene()
{
    ipr::Image scene = ipr::Image::create({512, 512}, ipr::Depth::F32, 3);
    for (int y = 0; y < scene.height(); ++y)
        for (int x = 0; x < scene.width(); ++x) {
            const float detail = 0.08f + 0.06f * float(((x / 16) + (y / 16)) % 2);
            const float t = y < 32 ? 0.0f : y < 256 ? 0.35f : 1.0f;
            const float airlight = 0.8f;
            const ipr::Vec3f j(detail * 1.2f, detail, detail * 0.8f);
            imagetest::at<ipr::Vec3f>(scene, y, x) = j * t + ipr::Vec3f(airlight, airlight, airlight) * (1 - t);
        }
    return scene;
}

// Detail contrast: mean step of green between neighbouring squares of a row, free
// of the smooth haze gradient.
double contrast(const ipr::Image &rgb, int y0, int y1)
{
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 64; x < 448 - 16; ++x, ++n)
            sum += std::abs(imagetest::at<ipr::Vec3f>(rgb, y, x)[1] - imagetest::at<ipr::Vec3f>(rgb, y, x + 16)[1]);
    return sum / n;
}

double mean(const ipr::Image &rgb, int y0, int y1)
{
    return ipr::meanOf(rgb.copy(ipr::Recti(64, y0, 448 - 64, y1 - y0)))[1];
}

} // namespace

int main()
{
    const ipr::Image scene = hazyScene();

    ipr::Image same = scene.clone();
    Dehaze::apply(same, 0);
    if (!check(ipr::largestDifference(same, scene) == 0, "zero leaves the image unchanged")) return 1;

    ipr::Image clearer = scene.clone();
    Dehaze::apply(clearer, 100);
    if (!check(contrast(clearer, 64, 192) > 1.3 * contrast(scene, 64, 192) && mean(clearer, 64, 192) < mean(scene, 64, 192),
               "+100 removes haze: more contrast and a darker veil in the hazy half")) return 1;

    ipr::Image veiled = scene.clone();
    Dehaze::apply(veiled, -100);
    if (!check(contrast(veiled, 64, 192) < contrast(scene, 64, 192) && mean(veiled, 64, 192) > mean(scene, 64, 192),
               "-100 adds a veil: less contrast and brighter in the hazy half")) return 1;

    bool valid = true;
    for (const ipr::Image *m : {&clearer, &veiled})
        for (int y = 0; y < m->height(); ++y)
            for (int x = 0; x < m->width(); ++x)
                for (int c = 0; c < 3; ++c) {
                    const float v = m->row<ipr::Vec3f>(y)[x][c];
                    valid = valid && std::isfinite(v) && v >= 0;
                }
    if (!check(valid, "output stays finite and nonnegative")) return 1;

    // On a 1/4 proxy, the haze is measured over the same full-image radii.
    {
        const ipr::Image expected = ipr::resized(clearer, ipr::Size(128, 128), ipr::Interpolation::Area);
        const ipr::Image reduced = ipr::resized(scene, ipr::Size(128, 128), ipr::Interpolation::Area);
        ipr::Image proxy = reduced.clone();
        Dehaze::apply(proxy, 100, 0.25);
        const double change = imagetest::distance(expected, reduced), error = imagetest::distance(proxy, expected);
        if (!check(error < 0.25 * change, "on a 1/4 proxy the dehaze matches the reduced full-resolution one")) return 1;
    }

    ipr::Image empty;
    Dehaze::apply(empty, 50);
    if (!check(empty.empty(), "an empty image is left alone")) return 1;
    return 0;
}
