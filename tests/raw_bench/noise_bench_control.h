#ifndef IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H
#define IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H

// Only the mechanically generated diagnostic target includes this interface.
// The actual engine source and public Context are not changed.
namespace IprNoiseBench {
enum class Operation { Bypass, PooledBlind, CapturePooled, FrozenPooled, Zero };
void operation(Operation value);
Operation operation();
}

#endif // IPR_TESTS_RAW_BENCH_NOISE_BENCH_CONTROL_H
