#pragma once
#include "Module.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

class WeatherModule : public Module {
    std::atomic<double> currentTemp = -999.0;
    std::atomic<int> weatherCode = -1;
    std::wstring cachedDisplayStr;
    std::atomic<bool> needsUpdate = true;
    std::atomic<bool> stopThread = false;
    std::thread worker;

    std::wstring ToWide(const std::string &str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    // Mapping WMO codes to Emojis (Open-Meteo standard)
    std::string GetWeatherIcon(int code) {
        if (code == 0) return (const char *)u8"☀️";
        if (code == 1 || code == 2 || code == 3) return (const char *)u8"⛅";
        if (code == 45 || code == 48) return (const char *)u8"🌫️";
        if (code >= 51 && code <= 55) return (const char *)u8"🌧️";
        if (code >= 56 && code <= 57) return (const char *)u8"🌨️";
        if (code >= 61 && code <= 65) return (const char *)u8"🌧️";
        if (code >= 66 && code <= 67) return (const char *)u8"🌨️";
        if (code >= 71 && code <= 77) return (const char *)u8"❄️";
        if (code >= 80 && code <= 82) return (const char *)u8"🌦️";
        if (code >= 85 && code <= 86) return (const char *)u8"❄️";
        if (code >= 95 && code <= 99) return (const char *)u8"⛈️";
        return (const char *)u8"❓";
    }

    // Native WinHTTP Request (No external CURL dependency)
    std::string HttpGet(const std::wstring &server, const std::wstring &path) {
        HINTERNET hSession = WinHttpOpen(L"RailingWeather/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

        std::string response;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL)) {

            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;

                char *pszOutBuffer = new char[dwSize + 1];
                if (!pszOutBuffer) break;

                ZeroMemory(pszOutBuffer, dwSize + 1);
                if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                    response.append(pszOutBuffer, dwDownloaded);
                }
                delete[] pszOutBuffer;
            } while (dwSize > 0);
        }
        else {
            OutputDebugStringA("DEBUG: Weather HTTP Request Failed.\n");
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

public:
    WeatherModule(const ModuleConfig &cfg) : Module(cfg) {
        worker = std::thread([this]() {
            std::string lat = config.latitude.empty() ? "40.7128" : config.latitude; // Default NYC
            std::string lon = config.longitude.empty() ? "-74.0060" : config.longitude;
            std::string unit = config.tempFormat;
            if (unit.empty()) unit = "fahrenheit";

            std::wstring server = L"api.open-meteo.com";
            std::string pathStr = "/v1/forecast?latitude=" + lat +
                "&longitude=" + lon +
                "&current_weather=true&temperature_unit=" + unit;

            std::wstring path = std::wstring(pathStr.begin(), pathStr.end());

            while (!stopThread) {
                std::string jsonStr = HttpGet(server, path);

                if (!jsonStr.empty()) {
                    try {
                        auto j = json::parse(jsonStr);
                        if (j.contains("error") && j["error"].get<bool>() == true) {
                            // Log error or fallback? For now just retry later.
                        }
                        else if (j.contains("current_weather")) {
                            currentTemp = j["current_weather"]["temperature"].get<double>();
                            weatherCode = j["current_weather"]["weathercode"].get<int>();
                            needsUpdate = true;
                        }
                    }
                    catch (...) {}
                }
                int sleepTime = config.interval > 0 ? config.interval : 900000;
                for (int i = 0; i < sleepTime / 100; i++) {
                    if (stopThread) return;
                    Sleep(100);
                }
            }
            });
        worker.detach();
    }

    ~WeatherModule() { stopThread = true; }

    float GetContentWidth(RenderContext &ctx) override {
        if (needsUpdate) {
            int code = weatherCode;
            double temp = currentTemp;
            std::string icon = GetWeatherIcon(code);
            std::string fmt = config.format.empty() ? (const char *)u8"{icon} {temp}°C" : config.format;
            std::string tempStr = std::to_string((int)currentTemp);

            // Simple string replace helper
            auto ReplaceAll = [&](std::string &str, const std::string &from, const std::string &to) {
                size_t start_pos = 0;
                while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                    str.replace(start_pos, from.length(), to);
                    start_pos += to.length();
                }
                };

            ReplaceAll(fmt, "{icon}", icon);
            ReplaceAll(fmt, "{temp}", tempStr);

            cachedDisplayStr = ToWide(fmt);
            needsUpdate = false;
        }

        Style s = GetEffectiveStyle();
        IDWriteTextFormat *fmt = ctx.emojiFormat ? ctx.emojiFormat : ctx.textFormat;
        IDWriteTextLayout *layout = GetLayout(ctx, cachedDisplayStr, fmt);

        DWRITE_TEXT_METRICS m;
        layout->GetMetrics(&m);
        return m.width + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
    }

    void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
        Style s = GetEffectiveStyle();
        if (s.has_bg) {
            D2D1_RECT_F bgRect = D2D1::RectF(
                x + s.margin.left,
                y + s.margin.top,
                x + w - s.margin.right,
                y + h - s.margin.bottom
            );
            D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(bgRect, s.radius, s.radius);
            ctx.bgBrush->SetColor(s.bg);
            ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
        }

        ctx.textBrush->SetColor(s.fg);
        D2D1_RECT_F textRect = D2D1::RectF(
            x + s.margin.left + s.padding.left,
            y + s.margin.top + s.padding.top,
            x + w - s.margin.right - s.padding.right,
            y + h - s.margin.bottom - s.padding.bottom
        );
        IDWriteTextFormat *fmt = ctx.emojiFormat ? ctx.emojiFormat : ctx.textFormat;
        ctx.rt->DrawTextW(
            cachedDisplayStr.c_str(),
            (UINT32)cachedDisplayStr.length(),
            fmt,
            textRect,
            ctx.textBrush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
        );
    }
};