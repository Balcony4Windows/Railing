#pragma once
#include <windows.h>
#include <pdh.h>
#include <string>

#pragma comment(lib, "pdh.lib")

class SystemStats {
public:
    SystemStats();
    ~SystemStats();

    void Initialize();

    // Call these based on your timers (e.g. every 1s, 3s)
    int GetCpuUsage();
    int GetRamUsage(); // Returns percentage (0-100)

    // Optional: Returns formatted string "16.4 GB"
    std::wstring GetRamText();

private:
    // PDH Queries for CPU
    PDH_HQUERY cpuQuery = NULL;
    PDH_HCOUNTER cpuTotal = NULL;

    // Cache for RAM (MEMORYSTATUSEX is fast, but good to wrap)
    MEMORYSTATUSEX memInfo;
};