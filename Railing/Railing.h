#pragma once
#include <Windows.h>
#include <vector>
#include <shellapi.h> // For SHAppBarMessage
#include "resource.h"
#include "Types.h"
#include <dwmapi.h>
#include "RailingRenderer.h"
#include "VolumeFlyout.h"
#include "TrayFlyout.h"
#include "SystemStats.h"
#include "TooltipHandler.h"
#include "GpuStats.h"
#include "AppBarRegistration.h"
#include <Psapi.h>

#define HOTKEY_KILL_THIS 9001
#define WM_RAILING_CMD (WM_APP+1)
#define CMD_SWITCH_WORKSPACE 1
#define CMD_RELOAD_CONFIG 2

void RegisterAppBar(HWND hwnd);
void UnregisterAppBar(HWND hwnd);
void UpdateAppBarPosition(HWND hwnd);

class VolumeFlyout;
class RailingRenderer;

class Railing
{
public:
	Railing();
	~Railing();
	static Railing *instance;
	HWND hwndBar = nullptr;
	TooltipHandler tooltips;
	ThemeConfig cachedConfig;
	FILETIME lastConfigWriteTime = { 0 };
	void CheckForConfigUpdate();

	bool Initialize(HINSTANCE hInstance);
	void RunMessageLoop();
	BOOL IsAppWindow(HWND hwnd);
	void GetTopLevelWindows(std::vector<WindowInfo> &outWindows);

	RailingRenderer *renderer = nullptr;
	VolumeFlyout *flyout = nullptr;
	TrayFlyout *trayFlyout;
private:
	HWND CreateBarWindow(HINSTANCE hInstance, const ThemeConfig &config);
	void DrawBar(HWND hwnd); 

	static inline std::wstring GetWindowExePath(HWND hwnd) {
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
		if (!hProcess) return L"";

		wchar_t path[MAX_PATH];
		if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH) == 0) {
			DWORD size = MAX_PATH;
			QueryFullProcessImageNameW(hProcess, 0, path, &size);
		}
		CloseHandle(hProcess);
		return std::wstring(path);
	}

	bool needsWindowRefresh = true;

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static void CALLBACK WinEventProc(
		HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
	inline ULONGLONG GetInterval(std::string type, int def);
	HINSTANCE hInst = nullptr;
	HWINEVENTHOOK titleHook = nullptr;
	UINT shellMsgId = 0;
	std::vector <WindowInfo> windows;
	std::vector <WindowInfo> allWindows;
	std::vector<std::wstring> pinnedApps;

	inline void SavePinnedApps() {
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(NULL, exePath, MAX_PATH);
		std::wstring path(exePath);
		path = path.substr(0, path.find_last_of(L"\\/"));
		std::wstring fullPathW = path + L"\\config.json";

		nlohmann::json j;
		std::ifstream i(fullPathW);
		if (i.is_open()) {
			i >> j;
			i.close();
		}

		j["pinned"] = nlohmann::json::array();
		for (const auto &pPath : pinnedApps) {
			std::string p(pPath.begin(), pPath.end());
			j["pinned"].push_back(p);
		}

		std::ofstream o(fullPathW);
		if (o.is_open()) {
			o << j.dump(2);
			o.close();
		}
	}

	ULONGLONG lastCpuUpdate = 0;
	ULONGLONG lastRamUpdate = 0;
	ULONGLONG lastGpuUpdate = 0;
	ULONGLONG lastNetUpdate = 0;
	ULONGLONG lastClockUpdate = 0;
	float cachedVolume = 0;
	bool cachedMute = false;
	SystemStats stats;
	GpuStats gpuStats;
	int cachedGpuTemp = 0;
	int cachedCpuUsage = 0;
	int cachedRamUsage = 0;
	void UpdateSystemStats();

	HWND hwndTooltip = nullptr;
	std::wstring lastTooltipText = L"";
	bool isTrackingMouse = false;
};
