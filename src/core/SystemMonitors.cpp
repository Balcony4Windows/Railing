#include "balcony/core/SystemMonitors.h"

#include "balcony/core/Clock.h"

#include <Windows.h>

#include <cstdio>

namespace balcony::core
{
    namespace
    {
        std::string WideToUtf8(const std::wstring& wide)
        {
            if (wide.empty())
            {
                return {};
            }

            const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            std::string narrow(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), length, nullptr, nullptr);
            return narrow;
        }
    }

    void SystemMonitors::Update(float deltaSeconds)
    {
        _elapsedSinceUpdate += deltaSeconds;
        if (_elapsedSinceUpdate < 1.0f)
        {
            return;
        }
        _elapsedSinceUpdate = 0.0f;

        _cpu.Update();
        _memory.Update();
        _gpu.Update();
        _network.Update();
        _power.Update();
    }

    std::string SystemMonitors::TimeString() const
    {
        const TimeOfDay time = Clock::Now();
        char buffer[16];
        sprintf_s(buffer, "%02d:%02d:%02d", time.hour, time.minute, time.second);
        return buffer;
    }

    std::string SystemMonitors::CpuUsageString() const
    {
        char buffer[16];
        sprintf_s(buffer, "%.0f%%", _cpu.UsagePercent());
        return buffer;
    }

    std::string SystemMonitors::MemoryUsageString() const
    {
        char buffer[16];
        sprintf_s(buffer, "%.0f%%", _memory.UsagePercent());
        return buffer;
    }

    std::string SystemMonitors::GpuUsageString() const
    {
        char buffer[16];
        sprintf_s(buffer, "%.0f%%", _gpu.UsagePercent());
        return buffer;
    }

    std::string SystemMonitors::GpuTemperatureString() const
    {
        if (_gpu.TemperatureCelsius() < 0)
        {
            return "N/A";
        }

        char buffer[16];
        sprintf_s(buffer, "%d\xC2\xB0" "C", _gpu.TemperatureCelsius()); // \xC2\xB0 = UTF-8 for U+00B0 (degree sign).
        return buffer;
    }

    std::string SystemMonitors::NetworkStatusString() const
    {
        if (!_network.IsConnected())
        {
            return "Disconnected";
        }

        char buffer[128];
        sprintf_s(buffer, "%s (%d%%)", WideToUtf8(_network.Ssid()).c_str(), _network.SignalQualityPercent());
        return buffer;
    }

    std::string SystemMonitors::BatteryStatusString() const
    {
        if (!_power.HasBattery())
        {
            return "No Battery";
        }

        char buffer[32];
        sprintf_s(buffer, "%d%%%s", _power.BatteryPercent(), _power.IsCharging() ? " (Charging)" : "");
        return buffer;
    }
}
