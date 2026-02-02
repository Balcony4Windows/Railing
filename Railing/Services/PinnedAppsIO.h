#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <Windows.h>

struct PinnedAppEntry {
    std::wstring path;
    std::wstring args;
    std::wstring name;
    std::wstring iconPath;
    int iconIndex = 0;
};

class PinnedAppsIO {
public:
    static std::wstring GetConfigPath() {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(NULL, buffer, MAX_PATH);
        std::wstring path(buffer);
        return path.substr(0, path.find_last_of(L"\\/")) + L"\\pinned.dat";
    }

    static bool Save(const std::vector<PinnedAppEntry> &apps) {
        std::ofstream out(GetConfigPath(), std::ios::binary);
        if (!out.is_open()) return false;

        const char magic[] = "RAIL";
        out.write(magic, 4);

        // FIX: Version bumped to 3 to match the Load logic
        int version = 3;
        out.write(reinterpret_cast<const char *>(&version), sizeof(version));

        int count = (int)apps.size();
        out.write(reinterpret_cast<const char *>(&count), sizeof(count));

        for (const auto &app : apps) {
            WriteString(out, app.path);
            WriteString(out, app.args);
            WriteString(out, app.name);
            WriteString(out, app.iconPath);
            out.write(reinterpret_cast<const char *>(&app.iconIndex), sizeof(app.iconIndex));
        }
        out.close();
        return true;
    }

    static std::vector<PinnedAppEntry> Load() {
        std::vector<PinnedAppEntry> apps;
        std::ifstream in(GetConfigPath(), std::ios::binary);
        if (!in.is_open()) return apps;

        char magic[4];
        in.read(magic, 4);
        if (strncmp(magic, "RAIL", 4) != 0) return apps;

        int version = 0;
        in.read(reinterpret_cast<char *>(&version), sizeof(version));

        int count = 0;
        in.read(reinterpret_cast<char *>(&count), sizeof(count));

        for (int i = 0; i < count; i++) {
            PinnedAppEntry app;
            app.path = ReadString(in);
            app.args = ReadString(in);
            app.name = ReadString(in);

            // FIX: This check now works because Save writes version 3
            if (version >= 3) {
                app.iconPath = ReadString(in);
                in.read(reinterpret_cast<char *>(&app.iconIndex), sizeof(app.iconIndex));
            }
            apps.push_back(app);
        }

        return apps;
    }

private:
    static void WriteString(std::ofstream &out, const std::wstring &str) {
        int len = (int)str.length();
        out.write(reinterpret_cast<const char *>(&len), sizeof(len));
        if (len > 0) out.write(reinterpret_cast<const char *>(str.data()), len * sizeof(wchar_t));
    }
    static std::wstring ReadString(std::ifstream &in) {
        int len = 0;
        in.read(reinterpret_cast<char *>(&len), sizeof(len));
        if (len <= 0) return L"";

        std::wstring str(len, L'\0');
        in.read(reinterpret_cast<char *>(&str[0]), len * sizeof(wchar_t));
        return str;
    }
};