#include "balcony/core/NetworkMonitor.h"

#include <wlanapi.h>

namespace balcony::core
{
    NetworkMonitor::NetworkMonitor()
    {
        DWORD negotiatedVersion = 0;
        WlanOpenHandle(2, nullptr, &negotiatedVersion, &_handle);
    }

    NetworkMonitor::~NetworkMonitor()
    {
        if (_handle)
            WlanCloseHandle(_handle, nullptr);
    }

    void NetworkMonitor::Update()
    {
        _connected = false;
        _signalQuality = 0;
        _ssid.clear();

        if (!_handle)
            return;

        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(_handle, nullptr, &interfaces) != ERROR_SUCCESS)
            return;

        for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
        {
            const WLAN_INTERFACE_INFO& info = interfaces->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected)
                continue;

            DWORD dataSize = 0;
            PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
            WLAN_OPCODE_VALUE_TYPE opcodeType = wlan_opcode_value_type_invalid;
            const DWORD result = WlanQueryInterface(
                _handle, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                nullptr, &dataSize, reinterpret_cast<PVOID*>(&connection), &opcodeType);

            if (result == ERROR_SUCCESS)
            {
                _connected = true;
                _signalQuality = static_cast<int>(connection->wlanAssociationAttributes.wlanSignalQuality);

                const DOT11_SSID& ssid = connection->wlanAssociationAttributes.dot11Ssid;
                if (ssid.uSSIDLength > 0)
                {
                    const int wideLength = MultiByteToWideChar(
                        CP_UTF8, 0, reinterpret_cast<const char*>(ssid.ucSSID),
                        static_cast<int>(ssid.uSSIDLength), nullptr, 0);
                    if (wideLength > 0)
                    {
                        _ssid.resize(wideLength);
                        MultiByteToWideChar(
                            CP_UTF8, 0, reinterpret_cast<const char*>(ssid.ucSSID),
                            static_cast<int>(ssid.uSSIDLength), _ssid.data(), wideLength);
                    }
                }

                WlanFreeMemory(connection);
            }
            break;
        }

        WlanFreeMemory(interfaces);
    }
}
