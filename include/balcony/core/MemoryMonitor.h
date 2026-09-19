#pragma once

#include <cstdint>

// System-wide RAM usage (System.MemoryUsage). See CLAUDE.md section 6.
namespace balcony::core
{
    class MemoryMonitor
    {
    public:
        void Update();

        float UsagePercent() const { return _usagePercent; }
        uint64_t UsedBytes() const { return _usedBytes; }
        uint64_t TotalBytes() const { return _totalBytes; }

    private:
        float _usagePercent = 0.0f;
        uint64_t _usedBytes = 0;
        uint64_t _totalBytes = 0;
    };
}
