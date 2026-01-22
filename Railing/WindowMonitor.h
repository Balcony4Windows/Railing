#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <Types.h>
class WindowMonitor
{
public:
	/// <summary>
	/// Return a filtered list of all relevant windows + pinned apps.
	/// </summary>
	/// <param name="outWindows">Windows to search</param>
	/// <param name="pinnedApps">Pinned apps</param>
	/// <param name="ignoreBar">The taskbar itself</param>
	static void GetTopLevelWindows(std::vector<WindowInfo> &outWindows, const std::vector<std::wstring> &pinnedApps, HWND ignoreBar);

	/// <summary>
	/// The "Magic" function to determine if a window should be shown.
	/// </summary>
	/// <param name="hwnd">Window to determine</param>
	/// <param name="barWindow">The taskbar itself</param>
	/// <returns>Is a showable window?</returns>
	static BOOL IsAppWindow(HWND hwnd, HWND barWindow);

	/// <summary>
	/// Helper to get executable path.
	/// </summary>
	/// <param name="hwnd">Handle to executable window.</param>
	/// <returns>Executable path if exists, else ""</returns>
	static std::wstring GetWindowExePath(HWND hwnd);
};