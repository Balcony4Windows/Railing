#pragma once
#include <windows.h>
#include <string>

class SystemStats {
public:
    SystemStats();
    ~SystemStats();

    int GetCpuUsage();
    int GetRamUsage();
    std::wstring GetRamText();

private:
    FILETIME prevSysIdle;
    FILETIME prevSysKernel;
    FILETIME prevSysUser;

    MEMORYSTATUSEX memInfo;
};