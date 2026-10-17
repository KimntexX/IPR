#pragma once
#include "SystemMemory.h"
// Small synthetic editor fixtures must not depend on unrelated user/CI RAM use.
// Real corpus probes deliberately retain the native sampler.
inline SystemMemory::Snapshot controlledMemory()
{
    return {32ULL * 1024 * 1024 * 1024, 24ULL * 1024 * 1024 * 1024, 0, true, false};
}
