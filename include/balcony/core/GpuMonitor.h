#pragma once

#include <pdh.h>
#include <dxcore.h>
#include <wrl/client.h>

#include <vector>

// GPU utilization (System.GPUUsage), via the same "GPU Engine" performance
// counters Task Manager uses, plus best-effort temperature via DXCore.
// See CLAUDE.md section 6. Not every adapter/driver exposes temperature;
// TemperatureCelsius() returns -1 when it doesn't.
namespace balcony::core
{
    class GpuMonitor
    {
    public:
        GpuMonitor();
        ~GpuMonitor();

        void Update();

        float UsagePercent() const { return _usagePercent; }
        int TemperatureCelsius() const { return _temperatureCelsius; }

    private:
        void InitializePdhCounters();
        void InitializeTemperatureAdapter();

        PDH_HQUERY _query = nullptr;
        std::vector<PDH_HCOUNTER> _counters;
        float _usagePercent = 0.0f;

        Microsoft::WRL::ComPtr<IDXCoreAdapter> _temperatureAdapter;
        int _temperatureCelsius = -1;
    };
}
