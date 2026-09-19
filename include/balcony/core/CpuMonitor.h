#pragma once

#include <Windows.h>

// Total system CPU usage (System.CPUUsage), sampled as a delta between
// successive Update() calls. See CLAUDE.md section 6.
namespace balcony::core
{
    class CpuMonitor
    {
    public:
        CpuMonitor();

        void Update();
        float UsagePercent() const { return _usagePercent; }

    private:
        FILETIME _prevIdle{};
        FILETIME _prevKernel{};
        FILETIME _prevUser{};
        float _usagePercent = 0.0f;
    };
}
