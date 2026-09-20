#pragma once

#include "balcony/core/CpuMonitor.h"
#include "balcony/core/GpuMonitor.h"
#include "balcony/core/MemoryMonitor.h"
#include "balcony/core/NetworkMonitor.h"
#include "balcony/core/PowerMonitor.h"

#include <string>

// Aggregates the individual monitors (CLAUDE.md section 6's "persistent/
// current state") and exposes them as display-ready strings -- the
// natural shape for something like a Lua `label:SetText(System.Time())`
// call.
//
// Update() throttles the underlying monitors to roughly once a second
// internally: polling PDH/WLAN every frame at 60fps would be wasteful
// for values that don't meaningfully change that often. Time is the
// exception -- TimeString() reads the clock fresh on every call, since
// throttling "the current time" would just make it wrong.
namespace balcony::core
{
    class SystemMonitors
    {
    public:
        void Update(float deltaSeconds);

        std::string TimeString() const;
        std::string CpuUsageString() const;
        std::string MemoryUsageString() const;
        std::string GpuUsageString() const;
        std::string GpuTemperatureString() const;
        std::string NetworkStatusString() const;
        std::string BatteryStatusString() const;

        // Raw 0-100 readings alongside the display-ready strings above --
        // a script rendering its own custom visual (a bar chart, a
        // gauge, ...) needs the number, not a string it would have to
        // parse back out. See CLAUDE.md section 6: System exposes
        // information, the UI (here, a Canvas-based plugin) decides how
        // to represent it.
        float CpuUsagePercent() const { return _cpu.UsagePercent(); }
        float MemoryUsagePercent() const { return _memory.UsagePercent(); }
        float GpuUsagePercent() const { return _gpu.UsagePercent(); }

    private:
        CpuMonitor _cpu;
        MemoryMonitor _memory;
        GpuMonitor _gpu;
        NetworkMonitor _network;
        PowerMonitor _power;

        float _elapsedSinceUpdate = 1.0f; // Forces an immediate first update.
    };
}
