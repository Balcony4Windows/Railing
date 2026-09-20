#include "balcony/core/NetworkBackend.h"

#include <algorithm>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "ole32.lib")

namespace balcony::core
{
    NetworkBackend::NetworkBackend()
    {
        EnsureHandle();
    }

    NetworkBackend::~NetworkBackend()
    {
        // Blocks briefly if a connection attempt is still in flight --
        // same accepted shutdown-wait pattern as e.g. Renderer::Shutdown
        // flushing the GPU queue -- rather than detaching a thread that
        // would otherwise go on touching a HANDLE this destructor is
        // about to close.
        if (_connectThread.joinable())
        {
            _connectThread.join();
        }

        if (_handle)
        {
            WlanCloseHandle(_handle, nullptr);
        }
    }

    bool NetworkBackend::EnsureHandle()
    {
        if (_handle)
        {
            return true;
        }
        const DWORD result = WlanOpenHandle(2, nullptr, &_negotiatedVersion, &_handle);
        return result == ERROR_SUCCESS && _handle != nullptr;
    }

    void NetworkBackend::RequestScan()
    {
        if (!EnsureHandle())
        {
            return;
        }

        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(_handle, nullptr, &interfaces) == ERROR_SUCCESS)
        {
            if (interfaces->dwNumberOfItems > 0)
            {
                _interfaceGuid = interfaces->InterfaceInfo[0].InterfaceGuid;
                WlanScan(_handle, &_interfaceGuid, nullptr, nullptr, nullptr);
            }
            WlanFreeMemory(interfaces);
        }
    }

    NetworkStatus NetworkBackend::GetCurrentStatus() const
    {
        NetworkStatus status;
        if (!_handle)
        {
            return status;
        }

        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(_handle, nullptr, &interfaces) != ERROR_SUCCESS)
        {
            return status;
        }

        for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
        {
            const WLAN_INTERFACE_INFO& info = interfaces->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected)
            {
                continue;
            }

            status.connected = true;
            _interfaceGuid = info.InterfaceGuid;

            DWORD dataSize = 0;
            PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
            WLAN_OPCODE_VALUE_TYPE opcodeType = wlan_opcode_value_type_invalid;
            if (WlanQueryInterface(_handle, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                    nullptr, &dataSize, reinterpret_cast<PVOID*>(&connection), &opcodeType) == ERROR_SUCCESS)
            {
                status.signalQuality = static_cast<int>(connection->wlanAssociationAttributes.wlanSignalQuality);
                WlanFreeMemory(connection);
            }
            break;
        }

        WlanFreeMemory(interfaces);
        return status;
    }

    std::vector<WifiNetworkInfo> NetworkBackend::ScanNetworks() const
    {
        std::vector<WifiNetworkInfo> networks;
        if (!_handle)
        {
            return networks;
        }

        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(_handle, nullptr, &interfaces) != ERROR_SUCCESS)
        {
            return networks;
        }

        if (interfaces && interfaces->dwNumberOfItems > 0)
        {
            const GUID interfaceGuid = interfaces->InterfaceInfo[0].InterfaceGuid;
            _interfaceGuid = interfaceGuid;

            PWLAN_AVAILABLE_NETWORK_LIST bssList = nullptr;
            WlanGetAvailableNetworkList(_handle, &interfaceGuid, 0, nullptr, &bssList);
            if (bssList)
            {
                for (DWORD i = 0; i < bssList->dwNumberOfItems; ++i)
                {
                    const WLAN_AVAILABLE_NETWORK& entry = bssList->Network[i];
                    if (entry.dot11Ssid.uSSIDLength == 0)
                    {
                        continue;
                    }

                    WifiNetworkInfo net;
                    const std::string ssidUtf8(reinterpret_cast<const char*>(entry.dot11Ssid.ucSSID), entry.dot11Ssid.uSSIDLength);
                    net.ssid.assign(ssidUtf8.begin(), ssidUtf8.end());
                    net.signalQuality = static_cast<int>(entry.wlanSignalQuality);
                    net.connected = (entry.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
                    net.secure = entry.bSecurityEnabled != 0;
                    net.authAlgorithm = entry.dot11DefaultAuthAlgorithm;
                    net.cipherAlgorithm = entry.dot11DefaultCipherAlgorithm;

                    const bool exists = std::any_of(networks.begin(), networks.end(),
                        [&net](const WifiNetworkInfo& n) { return n.ssid == net.ssid; });
                    if (!exists)
                    {
                        networks.push_back(std::move(net));
                    }
                }
                WlanFreeMemory(bssList);
            }
        }
        WlanFreeMemory(interfaces);

        std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b)
        {
            if (a.connected != b.connected)
            {
                return a.connected;
            }
            return a.signalQuality > b.signalQuality;
        });
        return networks;
    }

    bool NetworkBackend::HasSavedProfile(const std::wstring& ssid) const
    {
        if (!_handle)
        {
            return false;
        }

        PWLAN_PROFILE_INFO_LIST list = nullptr;
        if (WlanGetProfileList(_handle, &_interfaceGuid, nullptr, &list) != ERROR_SUCCESS)
        {
            return false;
        }

        bool found = false;
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
        {
            if (ssid == list->ProfileInfo[i].strProfileName)
            {
                found = true;
                break;
            }
        }
        WlanFreeMemory(list);
        return found;
    }

    namespace
    {
        // The ported blocking logic, unchanged in substance from the
        // original ConnectTo() -- runs entirely on the background
        // thread NetworkBackend::ConnectTo() spawns below. Takes
        // everything it needs by value (handle, interface GUID,
        // network, password) rather than reading NetworkBackend's own
        // _interfaceGuid member, which the main thread can be
        // concurrently rewriting via RequestScan()/ScanNetworks()/
        // GetCurrentStatus() -- this is what keeps the two threads from
        // racing on that field.
        std::wstring ConnectBlocking(HANDLE handle, GUID interfaceGuid, WifiNetworkInfo network, std::wstring password, bool hasSavedProfile)
        {
            if (!handle)
            {
                return L"Interface Error";
            }

            if (!hasSavedProfile && !password.empty())
            {
                const std::wstring authStr = (network.authAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK) ? L"WPA2PSK" : L"WPAPSK";
                const std::wstring cipherStr = (network.cipherAlgorithm == DOT11_CIPHER_ALGO_CCMP) ? L"AES" : L"TKIP";
                const std::wstring xml =
                    L"<?xml version=\"1.0\"?>\n<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
                    L"<name>" + network.ssid + L"</name>\n<SSIDConfig><SSID><name>" + network.ssid + L"</name></SSID></SSIDConfig>\n"
                    L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode><MSM><security>\n"
                    L"<authEncryption><authentication>" + authStr + L"</authentication><encryption>" + cipherStr + L"</encryption><useOneX>false</useOneX></authEncryption>\n"
                    L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + password + L"</keyMaterial></sharedKey>\n"
                    L"</security></MSM></WLANProfile>";

                DWORD reason = 0;
                const DWORD result = WlanSetProfile(handle, &interfaceGuid, 0, xml.c_str(), nullptr, TRUE, nullptr, &reason);
                if (result != ERROR_SUCCESS)
                {
                    return (reason == WLAN_REASON_CODE_INVALID_PROFILE_SCHEMA) ? L"Invalid Password Format" : L"Profile Creation Failed";
                }
            }

            WLAN_CONNECTION_PARAMETERS params{};
            params.wlanConnectionMode = wlan_connection_mode_profile;
            params.strProfile = network.ssid.c_str();
            params.dot11BssType = dot11_BSS_type_infrastructure;

            if (WlanConnect(handle, &interfaceGuid, &params, nullptr) != ERROR_SUCCESS)
            {
                return L"Connection Start Failed.";
            }

            const ULONGLONG startTime = GetTickCount64();
            while (GetTickCount64() - startTime < 10000) // 10s timeout, same as the original.
            {
                PVOID stateData = nullptr;
                DWORD dataSize = 0;
                if (WlanQueryInterface(handle, &interfaceGuid, wlan_intf_opcode_interface_state, nullptr, &dataSize, &stateData, nullptr) == ERROR_SUCCESS)
                {
                    const WLAN_INTERFACE_STATE state = *static_cast<WLAN_INTERFACE_STATE*>(stateData);
                    WlanFreeMemory(stateData);

                    if (state == wlan_interface_state_connected)
                    {
                        PVOID connectionData = nullptr;
                        if (WlanQueryInterface(handle, &interfaceGuid, wlan_intf_opcode_current_connection, nullptr, &dataSize, &connectionData, nullptr) == ERROR_SUCCESS)
                        {
                            const WLAN_CONNECTION_ATTRIBUTES* connection = static_cast<WLAN_CONNECTION_ATTRIBUTES*>(connectionData);
                            const std::string connectedSsidUtf8(
                                reinterpret_cast<const char*>(connection->wlanAssociationAttributes.dot11Ssid.ucSSID),
                                connection->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                            const std::wstring connectedSsid(connectedSsidUtf8.begin(), connectedSsidUtf8.end());
                            WlanFreeMemory(connectionData);

                            if (connectedSsid == network.ssid)
                            {
                                return L"Connected!";
                            }
                        }
                    }
                    if (state == wlan_interface_state_disconnected && (GetTickCount64() - startTime > 2000))
                    {
                        return L"Failed.";
                    }
                }
                Sleep(250); // Don't hammer the CPU -- fine here, this is the background thread, not the main loop.
            }

            return L"Timed Out!";
        }
    }

    void NetworkBackend::ConnectTo(const WifiNetworkInfo& network, const std::wstring& password)
    {
        if (_connecting || !_handle)
        {
            return;
        }

        if (_connectThread.joinable())
        {
            _connectThread.join(); // Previous attempt's thread, already finished (see IsConnecting()).
        }

        const bool hasSavedProfile = HasSavedProfile(network.ssid);
        const HANDLE handle = _handle;
        const GUID interfaceGuid = _interfaceGuid;

        _connecting = true;
        _connectThread = std::thread([this, handle, interfaceGuid, network, password, hasSavedProfile]()
        {
            std::wstring result = ConnectBlocking(handle, interfaceGuid, network, password, hasSavedProfile);
            {
                std::lock_guard<std::mutex> lock(_resultMutex);
                _connectResult = std::move(result);
            }
            _connecting = false;
        });
    }

    std::optional<std::wstring> NetworkBackend::TryTakeConnectResult()
    {
        std::lock_guard<std::mutex> lock(_resultMutex);
        if (!_connectResult)
        {
            return std::nullopt;
        }
        std::optional<std::wstring> result = std::move(_connectResult);
        _connectResult.reset();
        return result;
    }

    void NetworkBackend::Disconnect()
    {
        if (_handle)
        {
            WlanDisconnect(_handle, &_interfaceGuid, nullptr);
        }
    }
}
