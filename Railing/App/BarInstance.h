#pragma once
#include <RailingRenderer.h>
class BarInstance
{
public:
	HWND hwnd = NULL;
	ThemeConfig config;
	std::string configFileName;

	RailingRenderer *renderer = nullptr;
	std::unique_ptr<InputManager> inputManager;

	TooltipHandler tooltips;
	WorkspaceManager workspaces;

	bool isHidden = false;
	float showProgress = 0.0f;
	ULONGLONG lastInteractionTime = 0;

	BarInstance(const std::string &configFile);
	~BarInstance();

	void Initialize(HINSTANCE hInstance);
	void ReloadConfig();
	void UpdateStats(const RailingRenderer::SystemStatusData &stats);
	
	static LRESULT CALLBACK BarWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void OnPaint();
	void OnTimer(UINT_PTR id);
};

