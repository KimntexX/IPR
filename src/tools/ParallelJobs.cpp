#include "ParallelJobs.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>

namespace ParallelJobs {
namespace {
struct JoinAll {
    std::vector<std::thread> &threads;
    ~JoinAll()
    {
        for (auto &thread : threads)
            if (thread.joinable()) thread.join();
    }
};
}

void run(const std::vector<Job> &jobs, int workers, const ThreadStarter &starter)
{
    if (jobs.empty()) return;
    std::atomic<size_t> next{0};
    std::atomic_bool failed{false};
    std::exception_ptr error;
    std::mutex errorLock;
    const auto work = [&] {
        for (size_t i = 0; !failed && (i = next++) < jobs.size();) {
            try { jobs[i](); }
            catch (...) {
                const std::lock_guard<std::mutex> guard(errorLock);
                if (!error) error = std::current_exception();
                failed = true;
            }
        }
    };
    std::vector<std::thread> threads;
    const JoinAll joinAll{threads};
    // Allocate before creating any temporary joinable std::thread: a failing
    // vector growth must not destroy that temporary without a join.
    threads.reserve(size_t(std::max(1, workers) - 1));
    try {
        for (int t = 1; t < std::max(1, workers); ++t)
            threads.push_back(starter ? starter(work) : std::thread(work));
    } catch (...) {
        failed = true;
        throw; // JoinAll remains alive until all started workers are joined.
    }
    work();
    for (auto &thread : threads) thread.join();
    if (error) std::rethrow_exception(error);
}
}
