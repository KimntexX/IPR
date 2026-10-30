#ifndef IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H
#define IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H
#include <array>

// Only the mechanically generated diagnostic target includes this interface.
// The actual engine source and public Context are not changed.
namespace IprNoiseBench {
enum class Operation { Bypass, PooledBlind, CapturePooled, FrozenPooled, ExternalCovariance, Zero };
using Covariances = std::array<std::array<float,9>,5>;
void operation(Operation value);
Operation operation();
void covariances(const Covariances &value);
const Covariances &covariances();
}

#endif // IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H
