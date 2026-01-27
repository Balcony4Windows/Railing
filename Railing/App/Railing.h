#pragma once
#include <winsock2.h>
#include <Windows.h>
#include <vector>
#include <shellapi.h>
#include <dwmapi.h>
#include <Psapi.h>
#include "Types.h"
#include "resource.h"
#include "RailingRenderer.h"
#include "VolumeFlyout.h"
#include "TrayFlyout.h"
#include "SystemStats.h"
#include "TooltipHandler.h"
#include "GpuStats.h"
#include "AppBarRegistration.h"
#include "DropTarget.h"
#include "WorkspaceManager.h"
#include "AudioCapture.h"
#include "NetworkFlyout.h"

class InputManager;

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
	HINSTANCE hInst = nullptr;
	TooltipHandler tooltips;
	ThemeConfig cachedConfig;
	FILETIME lastConfigWriteTime = { 0 };
	void CheckForConfigUpdate();

	std::unique_ptr<InputManager> inputManager;
	IDropTarget *pDropTarget = nullptr;

	bool Initialize(HINSTANCE hInstance);
	void RunMessageLoop();

	std::vector <WindowInfo> allWindows;
	std::vector<std::wstring> pinnedApps;

	RailingRenderer *renderer = nullptr;
	VolumeFlyout *flyout = nullptr;
	TrayFlyout *trayFlyout;
	NetworkFlyout *networkFlyout;
	WorkspaceManager workspaces;
	AudioCapture visualizerBackend;
	NetworkBackend networkBackend;

	float cachedVolume = 0.0f;
	int cachedWifiSignal = 0;
	bool cachedWifiState = false;
	bool cachedMute = false;

	static std::string ToUtf8(const std::wstring &wstr) {
		if (wstr.empty()) return std::string();
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string strTo(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
		return strTo;
	}

	inline void SavePinnedApps() {
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(NULL, exePath, MAX_PATH);
		std::wstring path(exePath);
		path = path.substr(0, path.find_last_of(L"\\/"));
		std::wstring fullPathW = path + L"\\config.json";

		nlohmann::json j;
		std::ifstream i(fullPathW);
		if (i.is_open()) {
			try { i >> j; }
			catch (...) {}
			i.close();
		}

		j["pinned"] = nlohmann::json::array();
		for (const auto &pPath : pinnedApps) {
			j["pinned"].push_back(ToUtf8(pPath));
		}

		std::ofstream o(fullPathW);
		if (o.is_open()) {
			o << j.dump(2);
			o.close();
		}
	}
private:
	HWND CreateBarWindow(HINSTANCE hInstance, const ThemeConfig &config);
	void DrawBar(HWND hwnd);

	static std::wstring GetWindowExePath(HWND hwnd) {
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid == 0) return L"";

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
		if (!hProcess) return L"";

		wchar_t path[MAX_PATH];
		if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH)) {
			CloseHandle(hProcess);
			return path;
		}

		CloseHandle(hProcess);
		return L"";
	}

	bool needsWindowRefresh = true;

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static void CALLBACK WinEventProc(
		HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
	inline ULONGLONG GetInterval(std::string type, int def);

	HWINEVENTHOOK titleHook = nullptr;
	UINT shellMsgId = 0;
	std::vector <WindowInfo> windows;

	ULONGLONG lastCpuUpdate = 0;
	ULONGLONG lastRamUpdate = 0;
	ULONGLONG lastGpuUpdate = 0;
	ULONGLONG lastNetUpdate = 0;
	ULONGLONG lastClockUpdate = 0;

	SystemStats stats;
	GpuStats gpuStats;
	int cachedGpuTemp = 0;
	int cachedCpuUsage = 0;
	int cachedRamUsage = 0;
	void UpdateSystemStats();

	bool isHidden = false;
	bool isHoveringBar = false;
	float showProgress = 1.0f;
	ULONGLONG lastInteractionTime = 0;
	bool IsMouseAtEdge();

	HWND hwndTooltip = nullptr;
	std::wstring lastTooltipText = L"";
	bool isTrackingMouse = false;
};