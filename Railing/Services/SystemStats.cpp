#include "SystemStats.h"
#include <iostream>

inline ULONGLONG FT2ULL(const FILETIME &ft) {
	return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

SystemStats::SystemStats() : prevSysIdle(NULL), prevSysKernel(NULL), prevSysUser(NULL), memInfo(NULL)
{
	RtlZeroMemory(&prevSysIdle, sizeof(FILETIME));
	RtlZeroMemory(&prevSysKernel, sizeof(FILETIME));
	RtlZeroMemory(&prevSysUser, sizeof(FILETIME));

	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	GetSystemTimes(&prevSysIdle, &prevSysKernel, &prevSysUser);
}

SystemStats::~SystemStats() {}

int SystemStats::GetCpuUsage() {
    FILETIME sysIdle, sysKernel, sysUser;

    if (!GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) return 0;

    ULONGLONG idle = FT2ULL(sysIdle) - FT2ULL(prevSysIdle);
    ULONGLONG kernel = FT2ULL(sysKernel) - FT2ULL(prevSysKernel);
    ULONGLONG user = FT2ULL(sysUser) - FT2ULL(prevSysUser);

    ULONGLONG total = kernel + user;
    if (total == 0) return 0;

    prevSysIdle = sysIdle;
    prevSysKernel = sysKernel;
    prevSysUser = sysUser;

    ULONGLONG used = total - idle;

    int percent = (int)((used * 100) / total);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    return percent;
}

int SystemStats::GetRamUsage() {
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return (int)memInfo.dwMemoryLoad;
    }
    return 0;
}

std::wstring SystemStats::GetRamText() {
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {

        double usedBytes = (double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys);
        double usedGB = usedBytes / (1024.0 * 1024.0 * 1024.0);

        wchar_t buf[64];
        swprintf_s(buf, 64, L"%.1f GB", usedGB);
        return std::wstring(buf);
    }
    return L"-- GB";
}