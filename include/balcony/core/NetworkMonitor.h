#pragma once

#include <Windows.h>

#include <string>

// Wireless connectivity state (System.NetworkState). Only reports status;
// scanning/connecting is a separate future settings feature, not state
// tracking. See CLAUDE.md section 6.
namespace balcony::core
{
    class NetworkMonitor
    {
    public:
        NetworkMonitor();
        ~NetworkMonitor();

        void Update();

        bool IsConnected() const { return _connected; }
        int SignalQualityPercent() const { return _signalQuality; }
        const std::wstring& Ssid() const { return _ssid; }

    private:
        HANDLE _handle = nullptr;
        bool _connected = false;
        int _signalQuality = 0;
        std::wstring _ssid;
    };
}
