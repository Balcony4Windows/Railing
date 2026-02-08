#pragma once
#include <Windows.h>
#include <string>
#include "RailingRenderer.h"
#include "TooltipHandler.h"
#include "NetworkFlyout.h"
#include "TrayFlyout.h"
#include "VolumeFlyout.h"

class Railing;


class InputManager
{
	BarInstance *barInstance;
	RailingRenderer *renderer;

	int lastHoveredDockIndex = -1;
	bool isTrackingMouse = false;
	std::wstring lastTooltipText;

public:

	InputManager(BarInstance *bar, RailingRenderer *rr, TooltipHandler *t);

	void HandleMouseMove(HWND hwnd, int x, int y);
	void HandleLeftClick(HWND hwnd, int x, int y);
	void HandleRightClick(HWND hwnd, int x, int y);
	void HandleScroll(HWND hwnd, short delta);
	void OnMouseLeave(HWND hwnd);

	TooltipHandler *tooltips;

private:
	Module *HitTest(int x, int y, D2D1_RECT_F &outRect);
};

