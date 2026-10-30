#include "noise_bench_control.h"

namespace IprNoiseBench {
namespace {
thread_local Operation current = Operation::Bypass;
}
void operation(Operation value) { current = value; }
Operation operation() { return current; }
}
