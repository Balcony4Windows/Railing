#pragma once
#include "Types.h"
#include <vector>

class WindowManager
{
public:
	static std::vector<WindowInfo> GetTopLevelWindows();
	static bool IsAppWindow(HWND hwnd);
};

