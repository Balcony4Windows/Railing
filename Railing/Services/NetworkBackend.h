#pragma once
#include <Windows.h>
#include <wlanapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <wlantypes.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "wlanapi.lib")

struct WifiNetwork {
	std::wstring ssid;
	int signalQuality;
	bool isConnected;
	bool isSecure;
	DOT11_AUTH_ALGORITHM authAlgo;
	DOT11_CIPHER_ALGORITHM cipherAlgo;
};

class NetworkBackend
{
public:
	HANDLE hClient = NULL;
	DWORD dwMaxClient = 2;
	DWORD dwCurVersion = 0;
	GUID interfaceGuid = { 0 };

	NetworkBackend() {
		WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
	}
	~NetworkBackend() { if (hClient) WlanCloseHandle(hClient, NULL); }

	void RequestScan() {
		if (!hClient) return;
		PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
		if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
			if (pIfList->dwNumberOfItems > 0) {
				this->interfaceGuid = pIfList->InterfaceInfo[0].InterfaceGuid;
				WlanScan(hClient, &this->interfaceGuid, NULL, NULL, NULL);
			}
			WlanFreeMemory(pIfList);
		}
	}

	std::vector<WifiNetwork> ScanNetworks() {
		std::vector<WifiNetwork> networks;
		if (!hClient) return networks;

		PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
		if (WlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS) return networks;

		if (pIfList && pIfList->dwNumberOfItems > 0) {
			this->interfaceGuid = pIfList->InterfaceInfo[0].InterfaceGuid;

			PWLAN_AVAILABLE_NETWORK_LIST pBssList = NULL;
			WlanGetAvailableNetworkList(hClient, &this->interfaceGuid, 0, NULL, &pBssList);
			if (pBssList) {
				for (DWORD i = 0; i < pBssList->dwNumberOfItems; i++) {
					WLAN_AVAILABLE_NETWORK *pEntry = &pBssList->Network[i];
					if (pEntry->dot11Ssid.uSSIDLength == 0) continue;

					WifiNetwork net;
					std::string tempSSID((char *)pEntry->dot11Ssid.ucSSID, pEntry->dot11Ssid.uSSIDLength);
					net.ssid = std::wstring(tempSSID.begin(), tempSSID.end());
					net.signalQuality = (int)pEntry->wlanSignalQuality;
					net.isConnected = (pEntry->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED);
					net.isSecure = (pEntry->bSecurityEnabled);
					net.authAlgo = pEntry->dot11DefaultAuthAlgorithm;
					net.cipherAlgo = pEntry->dot11DefaultCipherAlgorithm;

					bool exists = false;
					for (auto &n : networks) if (n.ssid == net.ssid) { exists = true; break; }
					if (!exists) networks.push_back(net);
				}
				WlanFreeMemory(pBssList);
			}
		}
		if (pIfList) WlanFreeMemory(pIfList);

		std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
			if (a.isConnected != b.isConnected) return a.isConnected;
			return a.signalQuality > b.signalQuality;
			});
		return networks;
	}

	bool ProfileExists(const std::wstring &profileName) {
		PWLAN_PROFILE_INFO_LIST pList = NULL;
		if (WlanGetProfileList(hClient, &interfaceGuid, NULL, &pList) != ERROR_SUCCESS) return false;

		bool found = false;
		for (DWORD i = 0; i < pList->dwNumberOfItems; i++) {
			if (pList->ProfileInfo[i].strProfileName == profileName) {
				found = true;
				break;
			}
		}
		if (pList) WlanFreeMemory(pList);
		return found;
	}

	std::wstring ConnectTo(WifiNetwork net, std::wstring password) {
		if (!hClient) return L"Interface Error";

		if (!ProfileExists(net.ssid) && !password.empty()) {
			std::wstring authStr = (net.authAlgo == DOT11_AUTH_ALGO_RSNA_PSK) ? L"WPA2PSK" : L"WPAPSK";
			std::wstring cipherStr = (net.cipherAlgo == DOT11_CIPHER_ALGO_CCMP) ? L"AES" : L"TKIP";
			std::wstring xml = L"<?xml version=\"1.0\"?>\n<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
				L"<name>" + net.ssid + L"</name>\n<SSIDConfig><SSID><name>" + net.ssid + L"</name></SSID></SSIDConfig>\n"
				L"<connectionType>ESS</connectionType><connectionMode>auto</connectionMode><MSM><security>\n"
				L"<authEncryption><authentication>" + authStr + L"</authentication><encryption>" + cipherStr + L"</encryption><useOneX>false</useOneX></authEncryption>\n"
				L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>" + password + L"</keyMaterial></sharedKey>\n"
				L"</security></MSM></WLANProfile>";

			DWORD reason;
			DWORD res = WlanSetProfile(hClient, &interfaceGuid, 0, xml.c_str(), NULL, TRUE, NULL, &reason);
			if (res != ERROR_SUCCESS) {
				if (reason == WLAN_REASON_CODE_INVALID_PROFILE_SCHEMA) return L"Invalid Password Format";
				return L"Profile Creation Failed";
			}
		}

		WLAN_CONNECTION_PARAMETERS wcp = { };
		wcp.wlanConnectionMode = wlan_connection_mode_profile;
		wcp.strProfile = net.ssid.c_str();
		wcp.dot11BssType = dot11_BSS_type_infrastructure;

		DWORD res = WlanConnect(hClient, &interfaceGuid, &wcp, NULL);
		if (res != ERROR_SUCCESS) return L"Connection Start Failed.";
		ULONGLONG startTime = GetTickCount64();
		while (GetTickCount64() - startTime < 10000) { // 10s timeout
			PVOID pData = NULL;
			DWORD dataSize = 0;

			if (WlanQueryInterface(hClient, &interfaceGuid, wlan_intf_opcode_interface_state, NULL, &dataSize, &pData, NULL) == ERROR_SUCCESS) {
				WLAN_INTERFACE_STATE state = *(WLAN_INTERFACE_STATE *)pData;
				WlanFreeMemory(pData);

				if (state == wlan_interface_state_connected) {
					PVOID pConnData = NULL;
					if (WlanQueryInterface(hClient, &interfaceGuid, wlan_intf_opcode_current_connection, NULL, &dataSize, &pConnData, NULL) == ERROR_SUCCESS) {
						WLAN_CONNECTION_ATTRIBUTES *pConn = (WLAN_CONNECTION_ATTRIBUTES *)pConnData;
						std::string connectedSSID((char *)pConn->wlanAssociationAttributes.dot11Ssid.ucSSID, pConn->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
						std::wstring wConnectedSSID(connectedSSID.begin(), connectedSSID.end());
						WlanFreeMemory(pConnData);

						if (wConnectedSSID == net.ssid) return L"Connected!";
					}
				}
				if (state == wlan_interface_state_disconnected && (GetTickCount64() - startTime > 2000)) {
					return L"Failed.";
				}
			}
			Sleep(250); // Don't hammer the CPU
		}

		return L"Timed Out!";
	}
	void Disconnect() { if (hClient) WlanDisconnect(hClient, &interfaceGuid, NULL); }
};