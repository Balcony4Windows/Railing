// Config.cpp
#include "Config.h"
#include <vector>

AppConfig ConfigLoader::Load() {
    AppConfig config;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring configPath = path;
    size_t pos = configPath.find_last_of(L"\\/");
    configPath = configPath.substr(0, pos + 1) + L"railing.ini";

    auto ReadString = [&](const wchar_t *sec, const wchar_t *key, const wchar_t *def) {
        wchar_t buf[128];
        GetPrivateProfileStringW(sec, key, def, buf, 128, configPath.c_str());
        return std::wstring(buf);
        };
    auto ReadInt = [&](const wchar_t *sec, const wchar_t *key, int def) {
        return (int)GetPrivateProfileIntW(sec, key, def, configPath.c_str());
        };
    auto ReadFloat = [&](const wchar_t *sec, const wchar_t *key, float def) {
        wchar_t buf[64];
        GetPrivateProfileStringW(sec, key, L"", buf, 64, configPath.c_str());
        if (wcslen(buf) == 0) return def;
        return std::stof(buf);
        };

    // [General]
    config.barHeight = ReadInt(L"General", L"BarHeight", 48);
    config.screenMargin = ReadInt(L"General", L"ScreenMargin", 12);
    config.scale = ReadFloat(L"General", L"Scale", 1.0f);

    // [Decoration]
    config.rounding = ReadFloat(L"Decoration", L"Rounding", 6.0f);
    config.barOpacity = ReadFloat(L"Decoration", L"BarOpacity", 0.75f);
    config.pillOpacity = ReadFloat(L"Decoration", L"PillOpacity", 0.3f);
    config.enableBlur = ReadInt(L"Decoration", L"EnableBlur", 1) == 1;

    // [Layout]
    config.moduleGap = ReadFloat(L"Layout", L"ModuleGap", 10.0f);
    config.innerPadding = ReadFloat(L"Layout", L"InnerPadding", 8.0f);

    // [Typography]
    config.fontFamily = ReadString(L"Typography", L"FontFamily", L"JetBrains Mono");
    config.fontSize = ReadFloat(L"Typography", L"FontSize", 14.0f);
    config.iconSize = ReadFloat(L"Typography", L"IconSize", 16.0f);

    // [Colors]
    std::wstring bgHex = ReadString(L"Colors", L"BarBackground", L"#1a1a1a");
    config.barBackgroundColor = AppConfig::FromHex(bgHex, config.barOpacity);

    config.accentColor = AppConfig::FromHex(ReadString(L"Colors", L"Accent", L"#00BFFF"));
    config.textColor = AppConfig::FromHex(ReadString(L"Colors", L"Text", L"#FFFFFF"));
    config.urgentColor = AppConfig::FromHex(ReadString(L"Colors", L"Urgent", L"#FF5555"));
    config.inactiveTextColor = AppConfig::FromHex(ReadString(L"Colors", L"InactiveText", L"#888888"));

    // [Input]
    config.use24HourTime = ReadInt(L"Input", L"Use24Hour", 0) == 1;

    return config;
}