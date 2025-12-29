#include "SystemStats.h"
#include <iostream>

SystemStats::SystemStats() {
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
}

SystemStats::~SystemStats() {
    if (cpuQuery) PdhCloseQuery(cpuQuery);
}

void SystemStats::Initialize() {
    // 1. Initialize CPU Query
    // We query "\Processor(_Total)\% Processor Time"
    if (PdhOpenQuery(NULL, NULL, &cpuQuery) == ERROR_SUCCESS) {
        // Add the counter. Note: This path is locale-dependent on older Windows,
        // but PdhAddEnglishCounter works on Vista+ for locale independence.
        PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);

        // Prime the counter (first read is always 0 or invalid)
        PdhCollectQueryData(cpuQuery);
    }
}

int SystemStats::GetCpuUsage() {
    if (!cpuQuery) return 0;

    // Collect new data point
    PDH_STATUS status = PdhCollectQueryData(cpuQuery);
    if (status != ERROR_SUCCESS) return 0;

    // Format the value
    PDH_FMT_COUNTERVALUE counterVal;
    status = PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);

    if (status == ERROR_SUCCESS) {
        return (int)counterVal.doubleValue;
    }
    return 0;
}

int SystemStats::GetRamUsage() {
    if (GlobalMemoryStatusEx(&memInfo)) {
        return (int)memInfo.dwMemoryLoad; // 0 to 100
    }
    return 0;
}

std::wstring SystemStats::GetRamText() {
    if (GlobalMemoryStatusEx(&memInfo)) {
        // Calculate Used RAM in GB
        double totalGB = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        double freeGB = memInfo.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
        double usedGB = totalGB - freeGB;

        wchar_t buf[64];
        swprintf_s(buf, L"%.1f GB", usedGB);
        return std::wstring(buf);
    }
    return L"-- GB";
}