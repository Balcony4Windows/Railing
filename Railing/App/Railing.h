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
#include "MainMenu.h"
#include "BarInstance.h"

#define HOTKEY_KILL_THIS 9001
#define WM_RAILING_CMD (WM_APP+1)
#define CMD_SWITCH_WORKSPACE 1
#define CMD_RELOAD_CONFIG 2

#define ANIMATION_TIMER_ID 200

class BarInstance;

class Railing {
public:
    static Railing *instance;

    // Bar management
    std::vector<BarInstance *> bars;
    BarInstance *primaryBar = nullptr;

    // Global resources
    SystemStats stats;
    GpuStats gpuStats;
    AudioCapture visualizerBackend;
    NetworkBackend networkBackend;

    // Cached global stats
    int cachedCpuUsage = 0;
    int cachedRamUsage = 0;
    int cachedGpuTemp = 0;
    float cachedVolume = 0.0f;
    bool cachedMute = false;
    int cachedWifiSignal = 0;
    bool cachedWifiState = false;

    HINSTANCE hInst = nullptr;

    Railing();
    ~Railing();

    bool Initialize(HINSTANCE hInstance);
    void RunMessageLoop();
    void CreateNewBar(const std::string &configName);
    void DuplicateBar(BarInstance *source);
	BarInstance *FindBar(HWND hwnd);
    void DeleteBar(BarInstance *target);
    void UpdateGlobalStats();

private:
    HWINEVENTHOOK titleHook = nullptr;
    HWINEVENTHOOK focusHook = nullptr;
    HWINEVENTHOOK windowLifecycleHook = nullptr;

    ULONGLONG lastCpuUpdate = 0;
    ULONGLONG lastRamUpdate = 0;
    ULONGLONG lastGpuUpdate = 0;

    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event,
        HWND hwnd, LONG idObject, LONG idChild,
        DWORD dwEventThread, DWORD dwmsEventTime);

    const char *SESSION_FILE = "session.json";
    void SaveSession();
    void LoadSession();
};