#include "Analysis.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace ipr;

namespace {

void check(bool condition, const char *what)
{
    if (!condition) throw std::runtime_error(what);
}

bool near(double a, double b, double tolerance = 1e-12)
{
    return std::abs(a - b) <= tolerance;
}

Image filled(Size size, const std::function<float(int x, int y)> &value)
{
    Image image = Image::create(size, Depth::F32, 1);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
            image.mutableRow<float>(y)[x] = value(x, y);
    return image;
}

// A texture of values from 0 to 1, the same on every run.
std::vector<float> noise(int count)
{
    std::vector<float> values(static_cast<size_t>(count));
    uint32_t state = 12345u;
    for (float &value : values) {
        state = state * 1664525u + 1013904223u;
        value = float(state >> 8) / float(1u << 24);
    }
    return values;
}

void eigenvectors()
{
    std::vector<double> values, vectors;
    check(symmetricEigen({2, 0, 0, 0, 3, 0, 0, 0, 1}, 3, values, vectors), "a diagonal matrix");
    check(near(values[0], 3) && near(values[1], 2) && near(values[2], 1), "its values, largest first");
    check(near(std::abs(vectors[1]), 1) && near(std::abs(vectors[3]), 1) && near(std::abs(vectors[8]), 1), "its axes, in the same order");
    check(symmetricEigen({2, 1, 1, 2}, 2, values, vectors) && near(values[0], 3) && near(values[1], 1), "a coupled pair");
    check(near(std::abs(vectors[0]), std::sqrt(0.5)) && near(vectors[0], vectors[1]), "the diagonal first");
    check(!symmetricEigen({1, 2, 3}, 2, values, vectors), "the size of the matrix");
    check(!symmetricEigen({1, std::nan(""), 0, 1}, 2, values, vectors), "finite values only");
}

void cholesky()
{
    std::vector<double> x;
    check(solveCholesky({4, 2, 2, 3}, {2, 1}, 2, x) && near(x[0], 0.5) && near(x[1], 0.0), "a positive definite system");
    check(!solveCholesky({1, 2, 2, 1}, {1, 1}, 2, x), "an indefinite matrix is refused");
    check(!solveCholesky({1, 0, 0, 0}, {1, 1}, 2, x), "a singular one too");
}

void singularValueDecomposition()
{
    std::vector<double> w, vt;
    check(singularValues({3, 0, 0, 4, 0, 0}, 3, 2, w, vt) && near(w[0], 4) && near(w[1], 3), "values largest first");
    check(near(std::abs(vt[1]), 1) && near(std::abs(vt[2]), 1), "the right vectors as rows, in that order");
    check(singularValues({1, 2, 2, 4}, 2, 2, w, vt) && near(w[1], 0.0, 1e-12), "a rank short by one");
    check(near(std::abs(2.0 * vt[2] - vt[3]), std::sqrt(5.0), 1e-9), "its null vector along (2, -1)");
    check(!singularValues({1, 2, 3}, 2, 2, w, vt), "the size of the matrix");
    check(!singularValues({1, std::numeric_limits<double>::infinity(), 0, 1}, 2, 2, w, vt), "finite values only");
}

void rotations()
{
    const double c = std::cos(0.5), s = std::sin(0.5);
    const Matrix33d turn{{c, -s, 0, s, c, 0, 0, 0, 1}};
    Matrix33d scaled = turn;
    for (double &value : scaled.values)
        value *= 2.0;
    check(near(determinant(scaled), 8.0), "the determinant of a scaled rotation");
    const Matrix33d back = nearestRotation(scaled);
    for (size_t i = 0; i < 9; ++i)
        check(near(back.values[i], turn.values[i], 1e-12), "a scaled rotation gives the rotation back");
    const Matrix33d mirrored = nearestRotation(Matrix33d{{1, 0, 0, 0, 1, 0, 0, 0, -1}});
    check(near(determinant(mirrored), 1.0), "a mirror is turned into a rotation");
}

void hannWindows()
{
    Image window = Image::create({3, 3}, Depth::F32, 1);
    hannWindowInto(window);
    check(window.row<float>(1)[1] == 1.0f && window.row<float>(0)[0] == 0.0f && window.row<float>(2)[1] == 0.0f, "one in the middle, zero on the edges");
    Image wide = Image::create({5, 3}, Depth::F32, 1);
    hannWindowInto(wide);
    check(near(wide.row<float>(1)[1], std::sqrt(0.5f), 1e-7), "the root of the product of the two curves");
    bool refused = false;
    try {
        Image line = Image::create({4, 1}, Depth::F32, 1);
        hannWindowInto(line);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "two values or more each way");
}

void fourier()
{
    // Against the direct sum, on a size made of 2, 3 and 5 and on one that is not.
    for (const Size size : {Size(12, 10), Size(7, 11)}) {
        const std::vector<float> values = noise(size.area());
        const Image plane = filled(size, [&](int x, int y) { return values[size_t(y * size.width + x)]; });
        const std::vector<std::complex<double>> found = fourierTransform(plane);
        double worst = 0.0;
        for (int v = 0; v < size.height; ++v)
            for (int u = 0; u < size.width; ++u) {
                std::complex<double> sum = 0.0;
                for (int y = 0; y < size.height; ++y)
                    for (int x = 0; x < size.width; ++x)
                        sum += double(values[size_t(y * size.width + x)]) *
                               std::polar(1.0, -2.0 * pi * (double(u * x) / size.width + double(v * y) / size.height));
                worst = std::max(worst, std::abs(found[size_t(v * size.width + u)] - sum));
            }
        check(found.size() == size_t(size.area()) && worst < 1e-9, "the transform is the direct sum");
    }
    check(fourierTransform(Image()).empty(), "no plane, no transform");
    bool refused = false;
    try {
        fourierTransform(Image::create({4, 4}, Depth::F32, 2));
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "one channel of floats");
}

void phase()
{
    // A texture and the same moved round by (5, -3): the shift found whole.
    const int width = 64, height = 48;
    const std::vector<float> values = noise(width * height);
    const auto texture = [&](int x, int y) { return values[size_t(((y % height + height) % height) * width + (x % width + width) % width)]; };
    const Image a = filled({width, height}, texture);
    const Image b = filled({width, height}, [&](int x, int y) { return texture(x - 5, y + 3); });
    const Point<double> shift = phaseCorrelation(a, b, Image());
    check(near(shift.x, 5.0, 1e-6) && near(shift.y, -3.0, 1e-6), "a whole shift found exactly");
    // A size that is not made of 2, 3 and 5 is padded; the window given is applied.
    const Image c = filled({61, 47}, texture), d = filled({61, 47}, [&](int x, int y) { return texture(x - 2, y - 1); });
    Image window = Image::create({61, 47}, Depth::F32, 1);
    hannWindowInto(window);
    const Point<double> padded = phaseCorrelation(c, d, window);
    check(near(padded.x, 2.0, 0.25) && near(padded.y, 1.0, 0.25), "a shift found through padding and window");
    bool refused = false;
    try {
        phaseCorrelation(a, c, Image());
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "two images of one size");
}

void templateMatching()
{
    const std::vector<float> values = noise(40 * 30);
    const Image image = filled({40, 30}, [&](int x, int y) { return values[size_t(y * 40 + x)]; });
    const Image block = filled({9, 7}, [&](int x, int y) { return values[size_t((y + 11) * 40 + x + 17)]; });
    Image scores = Image::create({32, 24}, Depth::F32, 1);
    matchTemplateInto(image, block, scores);
    int bestX = 0, bestY = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 32; ++x)
            if (scores.row<float>(y)[x] > scores.row<float>(bestY)[bestX])
                bestX = x, bestY = y;
    check(bestX == 17 && bestY == 11 && near(scores.row<float>(11)[17], 1.0, 1e-4), "the block found where it was cut");
    bool refused = false;
    try {
        Image wrong = Image::create({31, 24}, Depth::F32, 1);
        matchTemplateInto(image, block, wrong);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "a buffer of the size of the places");
}

} // namespace

int main()
{
    try {
        eigenvectors();
        cholesky();
        singularValueDecomposition();
        rotations();
        hannWindows();
        fourier();
        phase();
        templateMatching();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "image analysis: all passed\n";
    return 0;
}
