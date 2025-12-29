#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <mmsystem.h> 

#include <chrono>
#include <cwchar>
#include <initguid.h>
#include <dxcore.h>
#include "Indicators.h"
#include "Railing.h"
#include "RenderContext.h"

#pragma comment(lib, "ws2_32.lib") // For Winsock
#pragma comment(lib, "dxcore.lib") // For GPU
#pragma comment(lib, "winmm.lib")  // For Multimedia

Indicators::Indicators()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    InitCPU();
    InitAudio();
    InitGPU();
    pingMs = CalculatePing();
}

Indicators::~Indicators()
{
    if (endpointVolume) endpointVolume->Release();
    if (defaultDevice) defaultDevice->Release();
    if (deviceEnumerator) deviceEnumerator->Release();
    if (dxAdapter) dxAdapter->Release();
    if (dxFactory) dxFactory->Release();
    WSACleanup();
}

int Indicators::CalculatePing()
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return -1;

    // Non-blocking mode
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(53); // DNS Port
    InetPton(AF_INET, L"8.8.8.8", &server.sin_addr); // Google DNS

    auto start = std::chrono::high_resolution_clock::now();
    connect(sock, (sockaddr *)&server, sizeof(server));

    // Wait up to 500ms
    TIMEVAL timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    // Wait for connection result
    int result = select(0, NULL, &writeSet, NULL, &timeout);

    if (result > 0) {
        // CRITICAL FIX: Check if it actually connected or just errored out
        int so_error = 0;
        int len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

        if (so_error == 0) {
            // Real Success!
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            closesocket(sock);
            return (duration < 1) ? 1 : (int)duration;
        }
    }

    closesocket(sock);
    return -1; // Offline or Timeout
}

void Indicators::InitGPU()
{
    if (FAILED(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&dxFactory)))) return;

    IDXCoreAdapterList *list = nullptr;
    const GUID attribs[] = { DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU };

    // Sort by preference (usually dedicated GPU comes first)
    dxFactory->CreateAdapterList(1, attribs, IID_PPV_ARGS(&list));

    if (list) {
        if (list->GetAdapterCount() > 0) {
            list->GetAdapter(0, IID_PPV_ARGS(&dxAdapter));
        }
        list->Release();
    }
}

int Indicators::CalculateGPUTemperature()
{
    if (!dxAdapter) return 0;
    DXCoreAdapterState state = DXCoreAdapterState::AdapterTemperatureCelsius;

    uint32_t sensorIndex = 0;
    float tempCelsius = 0.0f;

    HRESULT hr = dxAdapter->QueryState(state, &sensorIndex, &tempCelsius);

    if (SUCCEEDED(hr)) {
        return (int)tempCelsius;
    }
    return 0;
}

void Indicators::InitCPU()
{
    GetSystemTimes(&preIdleTime, &preKernelTime, &preUserTime);
}

int Indicators::CalculateCPUUsage() {
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) return 0;

    auto FileTimeToInt64 = [](const FILETIME &ft) {
        return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };

    uint64_t nowIdle = FileTimeToInt64(idleTime);
    uint64_t nowKernel = FileTimeToInt64(kernelTime);
    uint64_t nowUser = FileTimeToInt64(userTime);

    uint64_t diffIdle = nowIdle - FileTimeToInt64(preIdleTime);
    uint64_t diffKernel = nowKernel - FileTimeToInt64(preKernelTime);
    uint64_t diffUser = nowUser - FileTimeToInt64(preUserTime);
    uint64_t total = diffKernel + diffUser;

    preIdleTime = idleTime;
    preKernelTime = kernelTime;
    preUserTime = userTime;

    if (total == 0) return 0;
    return (int)((total - diffIdle) * 100 / total);
}

AudioCallback *g_AudioCB = new AudioCallback();

void Indicators::InitAudio()
{
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator), (void **)&deviceEnumerator);
    if (deviceEnumerator) {
        deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
        if (defaultDevice) defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void **)&endpointVolume);
    }
    if (endpointVolume) endpointVolume->RegisterControlChangeNotify(g_AudioCB);
}

int Indicators::GetAudioLevel()
{
    if (!endpointVolume) return 0;
    float currentVolume = 0;
    endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
    return (int)(currentVolume * 100);
}

void Indicators::UpdateMetrics()
{
    // CPU
    cpuUsage = CalculateCPUUsage();

    // RAM
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    ramUsage = (int)memInfo.dwMemoryLoad;

    // GPU
    gpuTemp = CalculateGPUTemperature();
    // Ping
    pingMs = CalculatePing();
}

void Indicators::Draw(const RenderContext &ctx, int cpu, int ram, float rightLimitX)
{
    ULONGLONG currentTick = GetTickCount64();
    if (currentTick - lastUpdateTick > 2000) {
        UpdateMetrics();
        lastUpdateTick = currentTick;
    }
    volumeLevel = GetAudioLevel();

    // Layout Constants
    const float height = ctx.logicalHeight * 0.6f;
    const float y = (ctx.logicalHeight - height) / 2.0f;
    const float sectionGap = ctx.moduleGap;
    const float itemPadding = ctx.innerPadding;
    const float iconPillWidth = 36.0f;
    const float barHeight = 2.0f;
    float cpuW = (itemPadding * 2) + 34.0f + 45.0f;
    float ramW = (itemPadding * 2) + 34.0f + 45.0f;
    float gpuW = (itemPadding * 2) + 35.0f + 45.0f;
    float pingW = (itemPadding * 2) + 20.0f + 50.0f + 52.0f;
    float volW = iconPillWidth;

    // Sum them up + 4 gaps (CPU->RAM->GPU->Ping->Volume)
    float totalW = cpuW + ramW + gpuW + pingW + volW + (sectionGap * 4);
    float cursorX = rightLimitX - totalW;

    wchar_t buf[16];
    if (!ctx.borderBrush) ctx.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), (ID2D1SolidColorBrush **)&ctx.borderBrush);

    auto DrawPill = [&](const wchar_t *icon, const wchar_t *label, int val, int maxVal, const wchar_t *suffix,
        D2D1_COLOR_F color, float labelW, float valueW, D2D1_RECT_F &outRect)
        {
            float iconW = (icon) ? 20.0f : 0.0f;
            float totalW = itemPadding + iconW + labelW + valueW + itemPadding;

            D2D1_RECT_F rect = D2D1::RectF(cursorX, y, cursorX + totalW, y + height);
            outRect = rect;
            D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, ctx.rounding, ctx.rounding);

            ctx.bgBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, ctx.pillOpacity));
            ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
            ctx.rt->DrawRoundedRectangle(rounded, (ID2D1Brush *)ctx.borderBrush, 1.0f);

            if (maxVal > 0) {
                float pct = (float)val / (float)maxVal;
                if (pct > 1.0f) pct = 1.0f;
                if (pct < 0.0f) pct = 0.0f;

                ID2D1RoundedRectangleGeometry *pMask = nullptr;
                ID2D1Factory *pFactory = nullptr;
                ctx.rt->GetFactory(&pFactory);

                if (pFactory) {
                    pFactory->CreateRoundedRectangleGeometry(rounded, &pMask);
                    pFactory->Release();
                }

                if (pMask) {
                    ID2D1Layer *pLayer = nullptr;
                    ctx.rt->CreateLayer(NULL, &pLayer);

                    if (pLayer) {
                        // PUSH MASK: "Only draw inside the rounded pill shape"
                        ctx.rt->PushLayer(
                            D2D1::LayerParameters(D2D1::InfiniteRect(), pMask),
                            pLayer
                        );
                        float barWidth = totalW * pct;
                        D2D1_RECT_F barRect = D2D1::RectF(
                            rect.left,
                            rect.bottom - barHeight,
                            rect.left + barWidth,
                            rect.bottom
                        );

                        ctx.textBrush->SetColor(color);
                        ctx.rt->FillRectangle(barRect, ctx.textBrush);

                        // POP MASK: Stop clipping
                        ctx.rt->PopLayer();

                        pLayer->Release();
                    }
                    pMask->Release();
                }
            }

            // 4. Draw Text/Icon (Draw ON TOP of the bar so text is always readable)
            float currentX = cursorX + itemPadding;

            if (icon) {
                D2D1_RECT_F iconRect = D2D1::RectF(currentX, y - 2.0f, currentX + iconW, y + height - 2.0f);
                ctx.textBrush->SetColor(color);
                ctx.rt->DrawTextW(icon, (UINT32)wcslen(icon), ctx.iconFormat, iconRect, ctx.textBrush);
                currentX += iconW;
            }

            D2D1_RECT_F labelRect = D2D1::RectF(currentX, y, currentX + labelW, y + height);
            ctx.textBrush->SetColor(color);
            ctx.rt->DrawTextW(label, (UINT32)wcslen(label), ctx.textFormat, labelRect, ctx.textBrush);
            currentX += labelW;

            D2D1_RECT_F valueRect = D2D1::RectF(currentX, y, currentX + valueW, y + height);
            swprintf_s(buf, L"%d%s", val, suffix);
            ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
            ctx.rt->DrawTextW(buf, (UINT32)wcslen(buf), ctx.textFormat, valueRect, ctx.textBrush);

            cursorX += totalW + sectionGap;
        };

    auto GetStatusColor = [](int value, int threshold1, int threshold2) -> D2D1_COLOR_F {
        if (value >= threshold2) return D2D1::ColorF(1.0f, 0.2f, 0.2f); // Red
        if (value >= threshold1) return D2D1::ColorF(D2D1::ColorF::Gold);
        return D2D1::ColorF(D2D1::ColorF::LimeGreen);
        };

    // CPU: Max 100
    DrawPill(nullptr, L"CPU:", cpuUsage, 100, L"%",
        GetStatusColor(cpuUsage, 60, 90), 34.0f, 45.0f, lastCpuRect);

    // RAM: Max 100
    DrawPill(nullptr, L"RAM:", ramUsage, 100, L"%",
        GetStatusColor(ramUsage, 70, 90), 34.0f, 45.0f, lastRamRect);

    // GPU: Max 100°C
    DrawPill(nullptr, L"GPU:", gpuTemp, 100, L"°C",
        GetStatusColor(gpuTemp, 75, 87), 35.0f, 45.0f, lastGpuRect);

    const wchar_t *netIcon = L"\uE774";
    D2D1_COLOR_F netColor = D2D1::ColorF(D2D1::ColorF::Gray);

    // Ping thresholds for color
    if (pingMs != -1 && pingMs != 999) {
        if (pingMs < 60) { netIcon = L"\uE701"; netColor = D2D1::ColorF(D2D1::ColorF::SkyBlue); }
        else if (pingMs < 150) { netIcon = L"\uE874"; netColor = D2D1::ColorF(D2D1::ColorF::White); }
        else if (pingMs < 300) { netIcon = L"\uE873"; netColor = D2D1::ColorF(D2D1::ColorF::Gold); }
        else { netIcon = L"\uE872"; netColor = D2D1::ColorF(D2D1::ColorF::OrangeRed); }
    }

    // Ping: Max 200ms
    DrawPill(netIcon, L"PING:", (pingMs == -1) ? 0 : pingMs, 200, L"ms",
        netColor, 50.0f, 52.0f, lastPingRect);

    D2D1_RECT_F rect = D2D1::RectF(cursorX, y, cursorX + iconPillWidth, y + height);
    lastVolumeRect = rect;

    // Define the shape for background and mask
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, ctx.rounding, ctx.rounding);

    // 1. Draw Background
    ctx.bgBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, ctx.pillOpacity));
    ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
    ctx.rt->DrawRoundedRectangle(rounded, (ID2D1Brush *)ctx.borderBrush, 1.0f);

    // 2. Prepare Variables (Icon & Color)
    const wchar_t *volIcon = L"\uE74F";
    bool isMuted = GetMuteState();
    if (!isMuted && volumeLevel > 0) {
        if (volumeLevel < 33) volIcon = L"\uE993";
        else if (volumeLevel < 66) volIcon = L"\uE994";
        else volIcon = L"\uE995";
    }
    D2D1_COLOR_F volColor = (isMuted || volumeLevel == 0) ? D2D1::ColorF(D2D1::ColorF::Gray) : D2D1::ColorF(D2D1::ColorF::White);

    // 3. Draw Volume Bar WITH MASK (The Fix)
    if (!isMuted && volumeLevel > 0) {
        float volPct = (float)volumeLevel / 100.0f;

        // A. Create Geometry for Masking
        ID2D1RoundedRectangleGeometry *pMask = nullptr;
        ID2D1Factory *pFactory = nullptr;
        ctx.rt->GetFactory(&pFactory);

        if (pFactory) {
            pFactory->CreateRoundedRectangleGeometry(rounded, &pMask);
            pFactory->Release();
        }

        if (pMask) {
            ID2D1Layer *pLayer = nullptr;
            ctx.rt->CreateLayer(NULL, &pLayer);

            if (pLayer) {
                // B. Push Layer (Apply the Clip)
                // "Only draw pixels that fit inside the rounded pill"
                ctx.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), pMask), pLayer);

                // C. Draw the Fill
                // Note: Change 'rect.bottom - barHeight' to 'rect.top' if you want the whole pill to fill up like a progress bar
                D2D1_RECT_F barRect = D2D1::RectF(
                    rect.left,
                    rect.bottom - barHeight,
                    rect.left + (iconPillWidth * volPct),
                    rect.bottom
                );

                ctx.textBrush->SetColor(volColor);
                ctx.rt->FillRectangle(barRect, ctx.textBrush);

                // D. Pop Layer (Remove the Clip)
                ctx.rt->PopLayer();
                pLayer->Release();
            }
            pMask->Release();
        }
    }

    // 4. Draw Icon (Always on top of the bar)
    ctx.textBrush->SetColor(volColor);
    ctx.rt->DrawTextW(volIcon, (UINT32)wcslen(volIcon), ctx.iconFormat, rect, ctx.textBrush);

    cursorX += iconPillWidth + sectionGap;
}

bool Indicators::GetMuteState() {
    if (!endpointVolume) return false;
    BOOL isMuted = FALSE;
    endpointVolume->GetMute(&isMuted);
    return isMuted == TRUE;
}

void Indicators::ToggleMute()
{
    if (!endpointVolume) return;
    BOOL isMuted = FALSE;
    endpointVolume->GetMute(&isMuted);
    endpointVolume->SetMute(!isMuted, NULL);
    // Force a redraw immediately so the icon updates
    if (Railing::instance && Railing::instance->hwndBar) InvalidateRect(Railing::instance->hwndBar, NULL, FALSE);
}

STDMETHODIMP AudioCallback::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (Railing::instance && Railing::instance->hwndBar) {
        InvalidateRect(Railing::instance->hwndBar, NULL, FALSE);
    }
    return S_OK;
}

STDMETHODIMP AudioCallback::QueryInterface(REFIID riid, void **ppv) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppv = static_cast<IAudioEndpointVolumeCallback *>(this);
        return S_OK;
    }
    return E_NOINTERFACE;
}