#pragma once
// Experimental mathematical scaffold only. Not linked into the production renderer.
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace RawHighlightStudy {
using Rgb = std::array<double, 3>;

inline void requireFinite(const Rgb &rgb)
{
    for (double channel : rgb)
        if (!std::isfinite(channel))
            throw std::invalid_argument("Nonfinite highlight signal");
}

// F receives bounded lookup coordinates, never the unbounded original signal.
// s=max(1,max(p)), q=clamp(p/s,0,1), output=p+s*(F(q)-q).
// Preserves the signed residual; this is a numerical extension, not a physical
// reconstruction of negative light and not a certified DNG rendering rule.
template<class Table>
Rgb extendTableHeadroom(const Rgb &signal, Table table)
{
    requireFinite(signal);
    const double scale = std::max(1.0, *std::max_element(signal.begin(), signal.end()));
    Rgb query;
    for (int c = 0; c < 3; ++c)
        query[c] = std::clamp(signal[c] / scale, 0.0, 1.0);
    const Rgb transformed = table(query);
    requireFinite(transformed);
    Rgb result;
    for (int c = 0; c < 3; ++c)
        result[c] = signal[c] + scale * (transformed[c] - query[c]);
    requireFinite(result);
    return result;
}

// Scalar C1 shoulder for a later, independent output compression experiment.
// Identity through knee; strictly increasing and asymptotic to 1 afterwards.
// knee is an experimental parameter, never a camera-name dependent constant.
inline double softShoulder(double value, double knee)
{
    if (!std::isfinite(value) || !std::isfinite(knee) || knee <= 0 || knee >= 1)
        throw std::invalid_argument("Invalid highlight shoulder input");
    if (value <= knee)
        return value;
    return knee - (1.0 - knee) * std::expm1(-(value - knee) / (1.0 - knee));
}

inline double highlightWeight(double luminance, double begin, double end)
{
    if(!std::isfinite(luminance)||!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<=begin||end>1)
        throw std::invalid_argument("Invalid highlight transition");
    const double t=std::clamp((luminance-begin)/(end-begin),0.0,1.0);
    return t*t*(3-2*t);
}
}
