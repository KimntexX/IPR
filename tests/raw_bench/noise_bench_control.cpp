#include "noise_bench_control.h"

namespace IprNoiseBench {
namespace {
thread_local Operation current = Operation::Bypass;
thread_local Covariances external{};
}
void operation(Operation value) { current = value; }
Operation operation() { return current; }
void covariances(const Covariances &value) { external=value; }
const Covariances &covariances() { return external; }
}
