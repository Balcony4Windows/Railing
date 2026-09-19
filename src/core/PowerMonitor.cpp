#include "balcony/core/PowerMonitor.h"

#include <Windows.h>

namespace balcony::core
{
    void PowerMonitor::Update()
    {
        SYSTEM_POWER_STATUS status{};
        if (!GetSystemPowerStatus(&status))
            return;

        // BatteryFlag 128 = no battery, 255 = unknown.
        _hasBattery = status.BatteryFlag != 128 && status.BatteryFlag != 255;
        _charging = _hasBattery && status.ACLineStatus == 1;
        _batteryPercent = (_hasBattery && status.BatteryLifePercent != 255)
            ? static_cast<int>(status.BatteryLifePercent)
            : -1;
    }
}
