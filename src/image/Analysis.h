#ifndef IPR_IMAGE_ANALYSIS_H
#define IPR_IMAGE_ANALYSIS_H

#include "Image.h"
#include "Types.h"

#include <vector>

namespace ipr {

// Small linear algebra, in double. Matrices are given by rows.

// The eigenvalues of a symmetric n x n matrix, largest first, and its unit eigenvectors as the rows
// of `vectors` in the same order; each vector's sign is arbitrary. False when the matrix is not n x n
// or not finite.
bool symmetricEigen(const std::vector<double> &matrix, int n, std::vector<double> &values, std::vector<double> &vectors);

// Solves a x = b for a symmetric positive definite a, by Cholesky. False when a is not positive
// definite enough to factor.
bool solveCholesky(const std::vector<double> &a, const std::vector<double> &b, int n, std::vector<double> &x);

// The singular values of a rows x cols matrix, largest first, one per column, and the right singular
// vectors as the rows of vt (cols x cols) in the same order, by one-sided Jacobi: as precise as
// the matrix, where the eigenvectors of its square would lose half the digits. False when not finite.
bool singularValues(const std::vector<double> &a, int rows, int cols, std::vector<double> &w, std::vector<double> &vt);

// The rotation nearest to m, u * vt of its singular value decomposition, turned so that its
// determinant is one.
Matrix33d nearestRotation(const Matrix33d &m);

// The Hann window of this size, as OpenCV's createHanningWindow: the square root of the product of
// a Hann curve across and one down. F32, one channel, two values or more each way.
void hannWindowInto(Image &out);

// The translation of b against a, by phase correlation of the two windowed images, as OpenCV's
// phaseCorrelate: both F32 of one channel and of one size, the window too.
Point<double> phaseCorrelation(const Image &a, const Image &b, const Image &window);

// For each place of templ over image, its normalized correlation with the window there (OpenCV's
// TM_CCOEFF_NORMED), from -1 to 1. Both F32 of one channel; out is
// (image.width - templ.width + 1, image.height - templ.height + 1).
void matchTemplateInto(const Image &image, const Image &templ, Image &out);

} // namespace ipr

#endif // IPR_IMAGE_ANALYSIS_H
