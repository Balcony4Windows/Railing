#include "balcony/core/CpuMonitor.h"

#include <algorithm>
#include <cstdint>

namespace balcony::core
{
    namespace
    {
        uint64_t ToUInt64(const FILETIME& ft)
        {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        }
    }

    CpuMonitor::CpuMonitor()
    {
        GetSystemTimes(&_prevIdle, &_prevKernel, &_prevUser);
    }

    void CpuMonitor::Update()
    {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (!GetSystemTimes(&idle, &kernel, &user))
            return;

        const uint64_t idleDelta = ToUInt64(idle) - ToUInt64(_prevIdle);
        const uint64_t kernelDelta = ToUInt64(kernel) - ToUInt64(_prevKernel);
        const uint64_t userDelta = ToUInt64(user) - ToUInt64(_prevUser);

        _prevIdle = idle;
        _prevKernel = kernel;
        _prevUser = user;

        const uint64_t totalDelta = kernelDelta + userDelta;
        if (totalDelta == 0)
            return;
        const uint64_t busyDelta = totalDelta - idleDelta;
        _usagePercent = std::clamp(static_cast<float>(busyDelta) * 100.0f / static_cast<float>(totalDelta), 0.0f, 100.0f);
    }
}
