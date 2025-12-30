#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include "ThemeLoader.h"
#include "RailingRenderer.h"
#include "ModuleFactory.h"
#include "resource.h"
#include <dwmapi.h>
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "user32.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

RailingRenderer::RailingRenderer(HWND hwnd) : hwnd(hwnd), dock(nullptr)
{
	theme = ThemeLoader::Load("config.json");
    if (theme.global.blur) theme.global.radius = 8.0f; // This is fixed by Windows :(
    char debugBuf[256];
    sprintf_s(debugBuf, "DEBUG: Loaded Config. Layout sizes: L=%d, C=%d, R=%d\n",
        (int)theme.layout.left.size(),
        (int)theme.layout.center.size(),
        (int)theme.layout.right.size());
    OutputDebugStringA(debugBuf);
    if (theme.global.blur && theme.global.background.a < 1.0f) {
		EnableBlur(hwnd, D2D1ColorFToBlurColor(theme.global.background));
    }
    else {
        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), reinterpret_cast<void **>(&pFactory));
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&pWriteFactory));

    std::wstring fontName(theme.global.font.begin(), theme.global.font.end());
    pWriteFactory->CreateTextFormat(
        fontName.c_str(), NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        theme.global.fontSize,
        L"en-us",
		&pTextFormat);
	pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    pWriteFactory->CreateTextFormat(
		L"Segoe MDL2 Assets", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 
		theme.global.fontSize + 2.0f, L"en-us", &pIconFormat);
	pIconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	pIconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	RECT rc; GetClientRect(hwnd, &rc);
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    pFactory->CreateHwndRenderTarget(props, hwndProps, &pRenderTarget);

    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pTextBrush);
    pRenderTarget->CreateSolidColorBrush(theme.global.background, &pBgBrush);
    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush); // Temp

    CoInitialize(NULL);
    CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWICFactory));
    dock.SetWICFactory(pWICFactory);
    LoadAppIcon();

    // Build module list
    for (const auto &id : theme.layout.left) {
        Module *m = ModuleFactory::Create(id, theme);
        if (m) leftModules.push_back(m);
    }
    for (const auto &id : theme.layout.center) {
        Module *m = ModuleFactory::Create(id, theme);
        if (m) centerModules.push_back(m);
    }
    for (const auto &id : theme.layout.right) {
        Module *m = ModuleFactory::Create(id, theme);
        if (m) rightModules.push_back(m);
    }
    UpdateBlurRegion();
}

RailingRenderer::~RailingRenderer()
{
    for (Module *m : leftModules) delete m;
    for (Module *m : centerModules) delete m;
    for (Module *m : rightModules) delete m;

    if (pWICFactory) pWICFactory->Release();
    if (pTextBrush) pTextBrush->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pBorderBrush) pBorderBrush->Release();
    if (pTextFormat) pTextFormat->Release();
    if (pIconFormat) pIconFormat->Release();
    if (pRenderTarget) pRenderTarget->Release();
    if (pFactory) pFactory->Release();
    if (pWriteFactory) pWriteFactory->Release();
}

void RailingRenderer::Draw(const std::vector<WindowInfo> &windows, HWND activeWindow, std::vector<Dock::ClickTarget> &outTargets)
{
    if (!pRenderTarget) return;

    for (Module *m : leftModules) m->Update();
    for (Module *m : centerModules) m->Update();
    for (Module *m : rightModules) m->Update();

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    pRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    RenderContext ctx;

    ThemeConfig theme = ThemeLoader::Load("config.json");
    ctx.dpi = GetDpiForWindow(hwnd);
	float scale = (float)ctx.dpi / 96.0f;
    RECT rc; GetClientRect(hwnd, &rc);
    float physicalW = (float)(rc.right - rc.left);
    float physicalH = (float)(rc.bottom - rc.top);
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    float idealW = (float)(mi.rcMonitor.right - mi.rcMonitor.left);
    float idealH = (float)(theme.global.height * scale);
    float animScaleX = physicalW / idealW;
    float animScaleY = physicalH / idealH;
    D2D1::Matrix3x2F translationToOrigin = D2D1::Matrix3x2F::Translation(-idealW / 2.0f, -idealH / 2.0f);
    D2D1::Matrix3x2F scaleMatrix = D2D1::Matrix3x2F::Scale(animScaleX, animScaleY);
    D2D1::Matrix3x2F translationToCenter = D2D1::Matrix3x2F::Translation(physicalW / 2.0f, physicalH / 2.0f);
    pRenderTarget->SetTransform(translationToOrigin * scaleMatrix * translationToCenter);

    ctx.rt = pRenderTarget;
    ctx.writeFactory = pWriteFactory;
    ctx.textFormat = pTextFormat;
    ctx.iconFormat = pIconFormat;
    ctx.textBrush = pTextBrush;
    ctx.bgBrush = pBgBrush;
    ctx.borderBrush = pBorderBrush;
    ctx.cpuUsage = currentStats.cpuUsage;
	ctx.gpuTemp = currentStats.gpuTemp;
    ctx.ramUsage = currentStats.ramUsage;
	ctx.volume = currentStats.volume;
	ctx.isMuted = currentStats.isMuted;
    ctx.appIcon = pAppIcon;
    pRenderTarget->SetDpi((float)ctx.dpi, (float)ctx.dpi);
    ctx.scale = ctx.dpi / 96.0f;
    ctx.hwnd = hwnd;
    ctx.logicalHeight = idealH / ctx.scale;
    ctx.logicalWidth = idealW / ctx.scale;

    std::string pos = theme.global.position;
    ctx.isVertical = (pos == "left" || pos == "right");

    if (!theme.global.blur) {
        D2D1_RECT_F mainRect = D2D1::RectF(0.0f, 0.0f, ctx.logicalWidth, ctx.logicalHeight);
        float r = theme.global.radius;
        D2D1_ROUNDED_RECT globalShape = D2D1::RoundedRect(mainRect, r, r);

        if (theme.global.background.a > 0.0f) {
            ctx.bgBrush->SetColor(theme.global.background);
            if (r > 0) ctx.rt->FillRoundedRectangle(globalShape, ctx.bgBrush);
            else ctx.rt->FillRectangle(mainRect, ctx.bgBrush);
        }
    }
    if (theme.global.borderWidth > 0.0f) {
        float r = theme.global.radius;
        float inset = theme.global.borderWidth / 2.0f;
        D2D1_RECT_F borderRect = D2D1::RectF(inset, inset, ctx.logicalWidth - inset, ctx.logicalHeight - inset);
        D2D1_ROUNDED_RECT borderShape = D2D1::RoundedRect(borderRect, r, r);
        ctx.borderBrush->SetColor(theme.global.borderColor);
        if (r > 0) ctx.rt->DrawRoundedRectangle(borderShape, ctx.borderBrush, theme.global.borderWidth);
        else ctx.rt->DrawRectangle(borderRect, ctx.borderBrush, theme.global.borderWidth);
    }

    for (Module *m : leftModules) m->CalculateWidth(ctx);
    for (Module *m : centerModules) m->CalculateWidth(ctx);
    for (Module *m : rightModules) m->CalculateWidth(ctx);

    float startX = 0.0f;
    float startY = 0.0f;
    float endX = ctx.logicalWidth;
    float endY = ctx.logicalHeight;

    // Helper to register keys
    auto RegisterKeys = [&](Module *m) {
        moduleRects[m->config.type] = m->cachedRect; // Register "audio"
        if (!m->config.id.empty()) moduleRects[m->config.id] = m->cachedRect; // Register "volume"

        if (m->config.type == "group") {
            GroupModule *g = static_cast<GroupModule *>(m);
            for (auto *c : g->children) {
                moduleRects[c->config.type] = c->cachedRect;
                if (!c->config.id.empty()) moduleRects[c->config.id] = c->cachedRect;
            }
        }
        };

    if (!ctx.isVertical) {
        // Horizontal
        float barHeight = ctx.logicalHeight;
        float cursorX = startX;
        for (Module *m : leftModules) {
            float modW = m->width;
            m->Draw(ctx, cursorX, 0.0f, barHeight);
            RegisterKeys(m);
            cursorX += modW;
        }
        float rightCursor = endX;
        for (auto it = rightModules.rbegin(); it != rightModules.rend(); ++it) {
            Module *m = *it;
            float modW = m->width;
            rightCursor -= modW;
            m->Draw(ctx, rightCursor, 0.0f, barHeight);
            RegisterKeys(m);
        }
        float totalCenterW = 0;
        for (Module *m : centerModules) totalCenterW += m->width;
        float centerX = (ctx.logicalWidth - totalCenterW) / 2.0f;
        for (Module *m : centerModules) {
            float modW = m->width;
            m->Draw(ctx, centerX, 0.0f, barHeight);
            RegisterKeys(m);
            centerX += modW;
        }
    }
    else {
        // Vertical
        float barWidth = ctx.logicalWidth;
        float cursorY = startY;
        for (Module *m : leftModules) {
            float modH = m->width;
            m->Draw(ctx, 0.0f, cursorY, barWidth);
            RegisterKeys(m);
            cursorY += modH;
        }
        float bottomCursor = endY;
        for (auto it = rightModules.rbegin(); it != rightModules.rend(); ++it) {
            Module *m = *it;
            float modH = m->width;
            bottomCursor -= modH;
            m->Draw(ctx, 0.0f, bottomCursor, barWidth);
            RegisterKeys(m);
        }
        float totalCenterH = 0;
        for (Module *m : centerModules) totalCenterH += m->width;
        float centerY = (ctx.logicalHeight - totalCenterH) / 2.0f;
        for (Module *m : centerModules) {
            float modH = m->width;
            m->Draw(ctx, 0.0f, centerY, barWidth);
            RegisterKeys(m);
            centerY += modH;
        }
    }

    outTargets.clear();
    pRenderTarget->EndDraw();
}

void RailingRenderer::Resize()
{
    if (!pRenderTarget) return;
    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U newSize = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    pRenderTarget->Resize(newSize);

    UpdateBlurRegion();
}

void RailingRenderer::UpdateBlurRegion()
{
    if (!hwnd) return;
    if (theme.global.blur) {
        SetWindowRgn(hwnd, nullptr, TRUE); // Reset any existing region
        return;
    }
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    int radius = (int)(theme.global.radius * scale);
    HRGN hRgn = CreateRoundRectRgn(0, 0, w, h, radius * 2, radius * 2);
    SetWindowRgn(hwnd, hRgn, TRUE);
}

void RailingRenderer::EnableBlur(HWND hwnd, DWORD nColor) {
    // Force 8px Rounding for the blur surface
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    // Windows 10 Fallback (Windows 11 is too milky)
    HMODULE hUser = LoadLibrary(L"user32.dll");
    if (hUser) {
        auto pSetWindowCompositionAttribute = reinterpret_cast<BOOL(WINAPI *)(HWND, WINDOWCOMPOSITIONATTRIBDATA *)>(GetProcAddress(hUser, "SetWindowCompositionAttribute"));
        if (pSetWindowCompositionAttribute) {
            ACCENT_POLICY policy = {};
            policy.nAccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            policy.nColor = nColor; // Pure transparent
            WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
            pSetWindowCompositionAttribute(hwnd, &data);
        }
    }
}
bool HitTestRecursive(const std::vector<Module *> &list, POINT pt, float scale) {
    for (Module *m : list) {
        // Check the module itself
        RECT r = {
            (LONG)(m->cachedRect.left * scale),
            (LONG)(m->cachedRect.top * scale),
            (LONG)(m->cachedRect.right * scale),
            (LONG)(m->cachedRect.bottom * scale)
        };
        if (PtInRect(&r, pt)) return true;
        if (m->config.type == "group") { // If a group, check children
            GroupModule *group = static_cast<GroupModule *>(m);
            if (HitTestRecursive(group->children, pt, scale)) return true;
        }
    }
    return false;
}
bool RailingRenderer::HitTest(POINT pt, const std::vector<Dock::ClickTarget> &targets)
{
    for (const auto &target : targets) {
        if (PtInRect(&target.rect, pt)) return true;
    }
    // Check App Icon
    if (PtInRect(&iconClickRect, pt)) return true;

    // Check Modules (Recursive)
    float scale = GetDpiForWindow(hwnd) / 96.0f;

    if (HitTestRecursive(leftModules, pt, scale)) return true;
    if (HitTestRecursive(centerModules, pt, scale)) return true;
    if (HitTestRecursive(rightModules, pt, scale)) return true;

    return false;
}

D2D1_RECT_F RailingRenderer::GetModuleRect(std::string moduleId)
{
    if (moduleRects.count(moduleId)) return moduleRects[moduleId];
    return D2D1::RectF(0, 0, 0, 0);
}

Module *RailingRenderer::GetModule(std::string id)
{
    Module *m = FindModuleRecursive(leftModules, id);
    if (!m) m = FindModuleRecursive(centerModules, id);
    return m ? m : FindModuleRecursive(rightModules, id);
}

void RailingRenderer::LoadAppIcon()
{
    if (pAppIcon) return;
    if (!pWICFactory) return;

    HICON hIcon = nullptr;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring pathStr = exePath;
    size_t lastSlash = pathStr.find_last_of(L"\\/");
    std::wstring iconPath = pathStr.substr(0, lastSlash) + L"\\Balcony.ico";

    // Try to load from file first
    hIcon = (HICON)LoadImageW(
        NULL,
        iconPath.c_str(),
        IMAGE_ICON,
        32, 32,
        LR_LOADFROMFILE
    );

    // Fallback: Load from Resource if file missing
    if (!hIcon) {
        hIcon = (HICON)LoadImageW(
            GetModuleHandle(NULL),
            MAKEINTRESOURCE(IDI_ICON1),
            IMAGE_ICON,
            32, 32,
            0
        );
    }

    if (hIcon) {
        IWICBitmap *wicBitmap = nullptr;
        HRESULT hr = pWICFactory->CreateBitmapFromHICON(hIcon, &wicBitmap);

        if (SUCCEEDED(hr)) {
            IWICFormatConverter *converter = nullptr;
            pWICFactory->CreateFormatConverter(&converter);
            converter->Initialize(
                wicBitmap,
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,
                NULL,
                0.f,
                WICBitmapPaletteTypeMedianCut
            );

            pRenderTarget->CreateBitmapFromWicBitmap(converter, &pAppIcon);

            converter->Release();
            wicBitmap->Release();
        }
        DestroyIcon(hIcon);
    }
}

Module *RailingRenderer::FindModuleRecursive(const std::vector<Module *> &list, const std::string &id)
{
    for (Module *m : list) {
        if (m->config.id == id) return m;

        if (m->config.type == "group") {
            GroupModule *group = static_cast<GroupModule *>(m);
            Module *found = FindModuleRecursive(group->children, id);
            if (found) return found;
        }
    }
    return nullptr;
}