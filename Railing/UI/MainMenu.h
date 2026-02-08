#pragma once
#include <Windows.h>
#include <commdlg.h>
#include <BarInstance.h>

class MainMenu
{
public:
	enum MenuCommand : UINT {
		CMD_LAYOUT_TOP = 1000,
		CMD_LAYOUT_BOTTOM,
		CMD_LAYOUT_LEFT,
		CMD_LAYOUT_RIGHT,
		CMD_LAYOUT_FLOATING,
		CMD_LAYOUT_LOCK,

		CMD_BAR_NEW = 1100,
		CMD_BAR_DUPLICATE,
		CMD_BAR_DELETE,

		CMD_CONFIG_RELOAD = 1200,
		CMD_CONFIG_LOAD,
		CMD_CONFIG_RELOAD_MODULES,
		CMD_CONFIG_EDIT,

		CMD_INTERACT_MOVE = 1300,
		CMD_INTERACT_RESIZE,
		CMD_INTERACT_SNAP,

		CMD_SYSTEM_RESTART_BAR = 1400,
		CMD_SYSTEM_RESTART_SHELL,
		CMD_SYSTEM_EXIT_TO_EXPLORER,

		CMD_ABOUT = 1500,
		CMD_AUTOHIDE
	};
	/// <summary>
	/// Creates the context menu for the taskbar
	/// </summary>
	/// <returns>context menu</returns>
	static HMENU CreateTaskbarContextMenu(BarInstance *bar);
	/// <summary>
	/// Shows the menu at the specified screen point
	/// </summary>
	/// <param name="hwnd">Window that calls thsi</param>
	/// <param name="screenPt">Where to show this</param>
	static void Show(HWND hwnd, POINT screenPt);
	/// <summary>
	/// Handles a menu command
	/// </summary>
	/// <param name="hwnd">Parent hwnd</param>
	/// <param name="cmd">CMD to handle</param>
	static void HandleMenuCmd(HWND hwnd, UINT cmd);
};

