#pragma once
#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlGuid.h>
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

	static inline std::wstring ResolveShortcut(const std::wstring &path) {
		if (path.length() < 4 || path.substr(path.length() - 4) != L".lnk")
			return path;

		std::wstring result = path;
		IShellLink *psl;
		if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)&psl))) {
			IPersistFile *ppf;
			if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf))) {
				if (SUCCEEDED(ppf->Load(path.c_str(), STGM_READ))) {
					wchar_t target[MAX_PATH];
					if (SUCCEEDED(psl->GetPath(target, MAX_PATH, NULL, 0))) result = target;
				}
				ppf->Release();
			}
			psl->Release();
		}
		return result;
	}
};