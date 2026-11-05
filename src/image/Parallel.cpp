#include "Parallel.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace ipr {

int workerThreads()
{
    constexpr int budget = 8;
    const int host = int(std::thread::hardware_concurrency());
    return std::clamp(host - 1, 1, budget);
}

void parallelRows(int count, const std::function<void(int first, int last)> &work, int threads)
{
    if (count <= 0)
        return;
    const int workers = std::clamp(threads > 0 ? threads : workerThreads(), 1, count);
    if (workers == 1) {
        work(0, count);
        return;
    }
    // More parts than threads: an uneven part does not hold the others back. Which thread takes
    // which part does not matter to the result, as each row is computed alone.
    const int parts = std::min(count, workers * 4);
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
    std::mutex errorLock;
    std::exception_ptr error;
    const auto run = [&] {
        for (int part = next.fetch_add(1); part < parts && !failed.load(); part = next.fetch_add(1)) {
            try {
                work(int(std::int64_t(count) * part / parts), int(std::int64_t(count) * (part + 1) / parts));
            } catch (...) {
                const std::lock_guard<std::mutex> lock(errorLock);
                if (!error)
                    error = std::current_exception();
                failed.store(true);
            }
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(size_t(workers - 1));
    try {
        for (int i = 1; i < workers; ++i)
            pool.emplace_back(run);
    } catch (...) {
        // No more threads to be had: those already there, and this one, do the work.
    }
    run();
    for (std::thread &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}

} // namespace ipr
