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

namespace {

// The helper threads not running for any call, shared by all of them.
std::atomic<int> &spareHelpers()
{
    static std::atomic<int> spare{workerThreads() - 1};
    return spare;
}

int takeHelpers(int wanted)
{
    std::atomic<int> &spare = spareHelpers();
    int available = spare.load();
    while (available > 0) {
        const int taken = std::min(available, wanted);
        if (spare.compare_exchange_weak(available, available - taken))
            return taken;
    }
    return 0;
}

// Gives the helpers back on every path, exceptions included.
struct Helpers
{
    int count;
    explicit Helpers(int wanted) : count(wanted > 0 ? takeHelpers(wanted) : 0) {}
    ~Helpers() { give(count); }
    void give(int helpers)
    {
        if (helpers > 0)
            spareHelpers().fetch_add(helpers);
        count -= helpers;
    }
    Helpers(const Helpers &) = delete;
    Helpers &operator=(const Helpers &) = delete;
};

} // namespace

void parallelRows(int count, const std::function<void(int first, int last)> &work, int threads)
{
    if (count <= 0)
        return;
    const int wanted = std::clamp(threads > 0 ? threads : workerThreads(), 1, count);
    Helpers helpers(wanted - 1);
    const int workers = helpers.count + 1;
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
    helpers.give(helpers.count - int(pool.size()));
    run();
    for (std::thread &thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}

} // namespace ipr
