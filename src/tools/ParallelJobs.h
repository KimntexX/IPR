#ifndef IPR_TOOLS_PARALLELJOBS_H
#define IPR_TOOLS_PARALLELJOBS_H

#include <functional>
#include <thread>
#include <vector>

namespace ParallelJobs {
using Job = std::function<void()>;
using ThreadStarter = std::function<std::thread(Job)>;

// The caller is one worker. Stop scheduling after the first job failure and
// join every started thread before returning or propagating an exception.
// An optional starter makes partial thread-creation failure testable.
void run(const std::vector<Job> &jobs, int workers, const ThreadStarter &starter = {});
}

#endif // IPR_TOOLS_PARALLELJOBS_H
