#include "balcony/core/MemoryMonitor.h"

#include <Windows.h>

namespace balcony::core
{
    void MemoryMonitor::Update()
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
            return;

        _usagePercent = static_cast<float>(status.dwMemoryLoad);
        _totalBytes = status.ullTotalPhys;
        _usedBytes = status.ullTotalPhys - status.ullAvailPhys;
    }
}
