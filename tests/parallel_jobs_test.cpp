#include "ParallelJobs.h"

#include <atomic>
#include <iostream>
#include <stdexcept>

int main()
{
    std::atomic_int completed{0};
    std::vector<ParallelJobs::Job> jobs;
    for (int i = 0; i < 24; ++i) jobs.push_back([&] { ++completed; });
    ParallelJobs::run(jobs, 4);
    if (completed != 24) return 1;
    completed = 0;
    ParallelJobs::run(jobs, 0);
    if (completed != 24) return 2;

    std::atomic_int live{0};
    int started = 0;
    bool caught = false;
    try {
        ParallelJobs::run(jobs, 4, [&](ParallelJobs::Job work) -> std::thread {
            if (++started == 2) throw std::runtime_error("synthetic thread creation failure");
            return std::thread([&, work] { ++live; work(); --live; });
        });
    } catch (const std::runtime_error &) { caught = true; }
    if (!caught || started != 2 || live != 0) return 3;

    caught = false;
    try {
        ParallelJobs::run({[] { throw std::runtime_error("synthetic job failure"); }}, 2);
    } catch (const std::runtime_error &) { caught = true; }
    if (!caught) return 4;
    ParallelJobs::run({}, 4);
    std::cout << "Parallel jobs: success, serial fallback, partial startup failure and job failure passed\n";
    return 0;
}
