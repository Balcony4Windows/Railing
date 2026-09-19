#pragma once

// Battery/power state (System.Battery). See CLAUDE.md section 6.
namespace balcony::core
{
    class PowerMonitor
    {
    public:
        void Update();

        bool HasBattery() const { return _hasBattery; }
        bool IsCharging() const { return _charging; }
        int BatteryPercent() const { return _batteryPercent; } // -1 if HasBattery() is false.

    private:
        bool _hasBattery = false;
        bool _charging = false;
        int _batteryPercent = -1;
    };
}
