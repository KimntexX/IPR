#ifndef IPR_IMAGE_PARALLEL_H
#define IPR_IMAGE_PARALLEL_H

#include <functional>

namespace ipr {

// The host's threads, one left for the interface, at most 8: the budget the studies settled on.
int workerThreads();

// Calls work(first, last) over parts of [0, count), each row once, from at most `threads` threads
// including the caller's, and returns once all have ended. threads <= 0 means workerThreads(). An
// exception stops the parts not started, and the first one is thrown again on the caller's thread.
// The helper threads of all calls together stay within workerThreads() - 1: a call made while the
// others hold them, as from tiles already spread over threads, runs on its caller alone.
void parallelRows(int count, const std::function<void(int first, int last)> &work, int threads = 0);

} // namespace ipr

#endif // IPR_IMAGE_PARALLEL_H
