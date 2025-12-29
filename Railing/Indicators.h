#pragma once
#include <windows.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <d2d1.h>
#include <dwrite.h>
#include <initguid.h>
#include <dxcore.h>

#pragma comment(lib, "ws2_32.lib")

struct RenderContext;

class Indicators {
public:
    Indicators();
    ~Indicators();

    void InitCPU();
    void InitAudio();
    void InitGPU();

    void Draw(const struct RenderContext &ctx, int cpu, int ram, float rightLimitX);
    bool GetMuteState();
    void ToggleMute();
    void OpenNetworkSettings() { ShellExecuteW(NULL, L"open", L"ms-settings:network-wifi", NULL, NULL, SW_SHOWNORMAL); }

    D2D1_RECT_F GetVolumeRect() { return lastVolumeRect; }
    D2D1_RECT_F GetCpuRect() { return lastCpuRect; }
    D2D1_RECT_F GetRamRect() { return lastRamRect; }
    D2D1_RECT_F GetGpuRect() { return lastGpuRect; }
    D2D1_RECT_F GetPingRect() { return lastPingRect; }

private:
    void UpdateMetrics();
    int CalculateCPUUsage();
    int GetAudioLevel();
    int CalculateGPUTemperature();
    int CalculatePing();

    // CPU Members
    FILETIME preIdleTime = { 0 }, preKernelTime = { 0 }, preUserTime = { 0 };
    int cpuUsage = 0;

    // RAM Members
    int ramUsage = 0;

    // Audio Members
    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    IMMDevice *defaultDevice = nullptr;
    IAudioEndpointVolume *endpointVolume = nullptr;
    int volumeLevel = 0;

    // GPU Members (DXCore)
    IDXCoreAdapterFactory *dxFactory = nullptr;
    IDXCoreAdapter *dxAdapter = nullptr;
    int gpuTemp = 0;

    // Ping
    int pingMs = 0;

    ULONGLONG lastUpdateTick = 0;

    // Hit Rects
    D2D1_RECT_F lastVolumeRect = {};
    D2D1_RECT_F lastCpuRect = {};
    D2D1_RECT_F lastRamRect = {};
    D2D1_RECT_F lastGpuRect = {};
    D2D1_RECT_F lastPingRect = {};
};

class AudioCallback : public IAudioEndpointVolumeCallback {
public:
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
};