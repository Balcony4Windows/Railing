#include <WinSock2.h> // Must be first
#include "Railing.h"
#include "InputManager.h"
#include "WindowMonitor.h"
#include "ThemeLoader.h"
#include "ModulesConcrete.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <cmath>
#include "TrayBackend.h"
#include "TrayFlyout.h"
#include <BarInstance.h>

#pragma comment(lib, "dwmapi.lib") 
#pragma comment(lib, "d2d1")
#pragma comment(lib, "shell32.lib")

Railing *Railing::instance = nullptr;

Railing::Railing()
    : lastCpuUpdate(0), lastRamUpdate(0), lastGpuUpdate(0),
    cachedCpuUsage(0), cachedRamUsage(0), cachedGpuTemp(0),
    cachedVolume(0.0f), cachedMute(false),
    cachedWifiSignal(0), cachedWifiState(false)
{}

Railing::~Railing() {
    if (!bars.empty() && bars[0]->GetHwnd())
        KillTimer(bars[0]->GetHwnd(), 1);

    for (auto *bar : bars) {
        delete bar;
    }
    bars.clear();

    if (titleHook) UnhookWinEvent(titleHook);
    if (focusHook) UnhookWinEvent(focusHook);
    if (windowLifecycleHook) UnhookWinEvent(windowLifecycleHook);
    CoUninitialize();
}

void Railing::CreateNewBar(const std::string &configName) {
    BarInstance *bar = new BarInstance(configName);
    bool isPrimary = bars.empty(); // First bar is primary

    if (!bar->Initialize(hInst, isPrimary)) {
        delete bar;
        return;
    }

    bars.push_back(bar);
    SaveSession();
}

void Railing::DuplicateBar(BarInstance *source) {
    if (!source) return;
    CreateNewBar(source->configFileName);
}

BarInstance *Railing::FindBar(HWND hwnd) {
    for (BarInstance *bar : bars) {
        if (bar->GetHwnd() == hwnd) return bar;
    }
    return nullptr;
}

void Railing::DeleteBar(BarInstance *target) {
    if (!target || bars.size() <= 1) {
        MessageBox(nullptr,
            L"Cannot delete this item.\n\n"
            L"- No bar is currently selected, or\n"
            L"- At least one bar must remain in the project.",
            L"Delete Bar Failed",
            MB_OK | MB_ICONWARNING);
        return;
    }

    auto it = std::find(bars.begin(), bars.end(), target);
    if (it != bars.end()) {
        bars.erase(it);
        delete target;
        SaveSession();
    }
}

bool Railing::Initialize(HINSTANCE hInstance) {
    instance = this;
    hInst = hInstance;

    // Initialize global resources
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    TrayBackend::Get(); // Initialize tray

    // Start global backends
    stats.GetCpuUsage(); // Prime the pump
    gpuStats.Initialize();
    visualizerBackend.Start();

    // Register global hooks (apply to all bars)
    titleHook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    focusHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    windowLifecycleHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Create primary bar
    Railing::instance->LoadSession();
    if (bars.empty()) return false;

    SetTimer(bars[0]->GetHwnd(), 1, 1000, NULL);

    return true;
}

void Railing::RunMessageLoop()
{
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CALLBACK Railing::WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
    LONG idObject, LONG idChild,
    DWORD dwEventThread, DWORD dwmsEventTime) {
    if (!instance) return;

    // Broadcast to all bars
    for (auto *bar : instance->bars) {
        InvalidateRect(bar->GetHwnd(), nullptr, FALSE);
        if (event == EVENT_SYSTEM_FOREGROUND) bar->workspaces.AddWindow(hwnd);
    }
}

void Railing::SaveSession() {
    std::ofstream out(SESSION_FILE);
	out << "{ \"bars\": [\n";

    for (size_t i = 0; i < bars.size(); i++) {
		std::string path = bars[i]->configFileName;
        out << "    \"" << path << "\"" << (i < bars.size() - 1 ? "," : "") << "\n";
    }
    out << "  ]}\n}";
    out.close();
}

void Railing::LoadSession() {
	std::ifstream in(SESSION_FILE);
    if (!in.is_open()) {
        CreateNewBar("config.json");
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
		size_t quoteStart = line.find('\"');
		size_t quoteEnd = line.rfind('\"');
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos && quoteEnd > quoteStart) {
            std::string path = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            if (path != "bars") CreateNewBar(path); // Skip key name
        }
    }
}

void Railing::UpdateGlobalStats() {
    ULONGLONG now = GetTickCount64();
    bool updated = false;

    if (now - lastCpuUpdate >= 1000) {
        cachedCpuUsage = stats.GetCpuUsage();
        lastCpuUpdate = now;
        updated = true;
    }

    if (now - lastRamUpdate >= 1000) {
        cachedRamUsage = stats.GetRamUsage();
        lastRamUpdate = now;
        updated = true;
    }

    if (now - lastGpuUpdate >= 1000) {
        cachedGpuTemp = gpuStats.GetGpuTemp();
        lastGpuUpdate = now;
        updated = true;
    }

    // Broadcast to all bars
    if (updated) {
        SystemStatusData statsData;
        statsData.cpuUsage = cachedCpuUsage;
        statsData.ramUsage = cachedRamUsage;
        statsData.gpuTemp = cachedGpuTemp;
        statsData.volume = cachedVolume;
        statsData.isMuted = cachedMute;
        statsData.wifiSignal = cachedWifiSignal;
        statsData.isWifiConnected = cachedWifiState;

        for (auto *bar : bars) {
            bar->UpdateStats(statsData);
        }
    }
}
