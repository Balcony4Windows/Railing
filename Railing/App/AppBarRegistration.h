#pragma once
#include <windows.h>
#include "ThemeTypes.h"
#include "ThemeLoader.h"
#ifndef WM_RAILING_APPBAR
#define WM_RAILING_APPBAR (WM_USER + 101)
#endif
void RegisterAppBar(HWND hwnd);
void UnregisterAppBar(HWND hwnd);
void UpdateAppBarPosition(HWND hwnd, ThemeConfig &theme);