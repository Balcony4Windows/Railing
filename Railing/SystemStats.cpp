#include "SystemStats.h"
#include <iostream>

inline ULONGLONG FT2ULL(const FILETIME &ft)
{
	return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

SystemStats::SystemStats() : prevSysIdle(NULL), prevSysKernel(NULL), prevSysUser(NULL), memInfo(NULL)
{
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	GetSystemTimes(&prevSysIdle, &prevSysKernel, &prevSysUser);
}

SystemStats::~SystemStats() {}

int SystemStats::GetCpuUsage()
{
	FILETIME sysIdle, sysKernel, sysUser;

	if (!GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) return 0;

	ULONGLONG idle = FT2ULL(sysIdle) - FT2ULL(prevSysIdle);
	ULONGLONG kernel = FT2ULL(sysKernel) - FT2ULL(prevSysKernel);
	ULONGLONG user = FT2ULL(sysUser) - FT2ULL(prevSysUser);

	prevSysIdle = sysIdle;
	prevSysKernel = sysKernel;
	prevSysUser = sysUser;

	ULONGLONG total = kernel + user;
	ULONGLONG used = total - idle;
	if (total == 0) return 0;

	return (int)((used * 100) / total); // percent
}

int SystemStats::GetRamUsage()
{
	return ((GlobalMemoryStatusEx(&memInfo)) ? (int)memInfo.dwMemoryLoad : 0);
}

std::wstring SystemStats::GetRamText()
{
	if (GlobalMemoryStatusEx(&memInfo)) {
		double totalGB = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
		double freeGB = memInfo.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
		double usedGB = totalGB - freeGB;

		wchar_t buf[64];
		swprintf_s(buf, L"%.1f GB", usedGB);
		return std::wstring(buf);
	}
	return L"-- GB";
}