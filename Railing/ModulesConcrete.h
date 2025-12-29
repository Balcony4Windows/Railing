#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <string>
#include <algorithm>
#include "Module.h"
#include <ctime>
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "ws2_32.lib")

// Helper to format output strings
inline std::wstring FormatOutput(std::string fmt, std::string token, std::wstring value) {
	if (fmt.empty()) return value;

	if (fmt.empty()) return L"";
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &fmt[0], (int)fmt.size(), NULL, 0);
	std::wstring wfmt(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &fmt[0], (int)fmt.size(), &wfmt[0], size_needed);

	std::wstring wtoken(token.begin(), token.end());

	size_t pos = wfmt.find(wtoken);
	if (pos != std::wstring::npos) {
		wfmt.replace(pos, wtoken.length(), value);
	}
	return wfmt;
}

// Helper to draw a progress bar background
inline void DrawProgressBar(RenderContext &ctx, float x, float y, float w, float h, float pct, const D2D1_COLOR_F color)
{
	if (pct > 0.0f) {
		if (pct > 1.0f) pct = 1.0f;
		D2D1_RECT_F fillRect = D2D1::RectF(x, y + h - 4.0f, x + (w*pct), y + h);
		D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(fillRect, 2.0f, 2.0f);
		ctx.bgBrush->SetColor(color);
		ctx.rt->FillRoundedRectangle(roundedBar, ctx.bgBrush);
	}
}

class ClockModule : public Module
{
public:
	ClockModule(const ModuleConfig &cfg) : Module(cfg) {}

	float GetContentWidth(RenderContext &ctx) override
	{
		std::wstring text = GetTimeStr();
		IDWriteTextLayout *layout = nullptr;
		ctx.writeFactory->CreateTextLayout(text.c_str(),(UINT32)text.length(),ctx.textFormat,1000.0f, 1000.0f,&layout);
		DWRITE_TEXT_METRICS metrics;
		layout->GetMetrics(&metrics);
		float w = metrics.width + 3.0f;
		layout->Release();
		return w;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		std::wstring text = GetTimeStr();
		D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
		ctx.textBrush->SetColor(config.baseStyle.fg); // NOTE: You may need to offset Y for font size
		ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), ctx.textFormat, rect, ctx.textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
private:
	std::wstring GetTimeStr()
	{
		time_t now = time(0);
		struct tm tstruct;
		localtime_s(&tstruct, &now);
		wchar_t buf[128];
		std::string fmt = config.format.empty() ? "%H:%M" : config.format;

		if (fmt.front() == '{' && fmt.back() == '}') {
			fmt = fmt.substr(1, fmt.size() - 2);
		}
		if (fmt.find(":%") == 0) {
			fmt = fmt.substr(1);
		}

		std::wstring wfmt(fmt.begin(), fmt.end());
		wcsftime(buf, 128, wfmt.c_str(), &tstruct);
		return buf;
	}
};

class GroupModule : public Module
{
public:
	std::vector<Module *> children;
	GroupModule(const ModuleConfig &cfg) : Module(cfg) {}
	~GroupModule() { for (auto m : children) delete m; }
	void AddChild(Module *m) { children.push_back(m); }
	
	float GetContentWidth(RenderContext &ctx) override
	{
		float totalW = 0.0f;
		for (auto child : children) {
			child->CalculateWidth(ctx);
			totalW += child->width;
		}
		return totalW;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		float cursorX = x;
		float cursorY = y;
		for (auto child : children) { // Important: We tell children to draw at currentX. 
			// Child's Draw will apply its own margins, but usually group items have transparent BG so it will be seamless.
			float childSize = child->width;
			if (!ctx.isVertical) {
				child->Draw(ctx, cursorX, y, h);
				cursorX += childSize;
			}
			else {
				child->Draw(ctx, x, cursorY, w);
				cursorY += childSize;
			}
		}
	}
};

class WorkspacesModule : public Module
{
public:
	float itemWidth = 20.0f;
	float itemPadding = 0.0f;
	int activeIndex = 0;
	int count = 5; // default

	WorkspacesModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override
	{
		itemPadding = config.baseStyle.padding.left + config.baseStyle.padding.right;
		if (itemPadding == 0.0f) itemPadding = 8.0f;
		return (itemPadding + itemWidth) * count;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{ // Draw 1 2 3 4 5
		float cursor = (ctx.isVertical) ? y : x;
		for (int i = 0; i < count; i++) {
			Style s = config.itemStyle;
			if (i == activeIndex && config.states.count("active")) {
				Style activeS = config.states.at("active");
				if (activeS.has_bg) s.bg = activeS.bg;
				s.fg = activeS.fg;
			}
			D2D1_RECT_F itemRect = D2D1::RectF(cursor, y, cursor + itemWidth + itemPadding, y + h);
			if (!ctx.isVertical) itemRect = D2D1::RectF(cursor, y, cursor + itemWidth + itemPadding, y + h);
			else itemRect = D2D1::RectF(x, cursor, x + w, cursor + itemWidth + itemPadding);

			if (i == activeIndex || s.has_bg) {
				D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(itemRect, config.baseStyle.radius, config.baseStyle.radius);
				ctx.bgBrush->SetColor(s.bg);
				ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
			}
			wchar_t buf[4]; // Draw 1 2 3 4 5
			swprintf_s(buf, L"%d", i + 1);
			ctx.textBrush->SetColor(s.fg);
			ctx.rt->DrawTextW(buf, (UINT32)wcslen(buf), ctx.textFormat, itemRect, ctx.textBrush);
			cursor += (itemWidth + itemPadding);
		}
	}

	void SetActiveIndex(int index) { activeIndex = index; }
};

class IconModule : public Module
{ // Generic Icon Module (Network, Battery, etc. Placeholder)
public:
	IconModule(const ModuleConfig &cfg) : Module(cfg) {}

	float GetContentWidth(RenderContext &ctx) override
	{ // Fixed width for icon
		return ctx.iconFormat->GetFontSize();
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		std::wstring text = L"\uE774"; // Placeholder icon
		if (config.type == "network") text = L"\uE774"; /* globe */
		if (config.type == "audio") text = L"\uE767"; /* volume */
		if (config.type == "battery") text = L"\uE83F"; /* battery */
		if (config.type == "tray") text = L"\uE70E"; /* Chevron Up */
		if (config.type == "notification") text = L"\uEA8F"; /* bell */

		D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
		ctx.textBrush->SetColor(config.baseStyle.fg);
		ctx.rt->DrawTextW(text.c_str(), 1, ctx.iconFormat, rect, ctx.textBrush);
	}
};

class CpuModule : public Module
{
public:
	CpuModule(const ModuleConfig &cfg) : Module(cfg) {}
	void Update() override { } // placeholder
	float GetContentWidth(RenderContext &ctx) override { return 80.0f; } // Approx width for "CPU: 100%"

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		D2D1_COLOR_F color = config.baseStyle.fg;
		for (const auto &th : config.thresholds) {
			if (ctx.cpuUsage >= th.val) color = th.style.fg;
		}
		DrawProgressBar(ctx, x, y, w, h, ctx.cpuUsage / 100.0f, color);
		std::string fmt = config.format.empty() ? "CPU: {usage}%" : config.format;
		std::wstring text = FormatOutput(fmt, "{usage}", std::to_wstring(ctx.cpuUsage));

		ctx.textBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
		ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), ctx.textFormat, D2D1::RectF(x, y, x + w, y + h), ctx.textBrush);
	}
};

class RamModule : public Module
{
public:
	RamModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override { return 80.0f; }
	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		D2D1_COLOR_F color = config.baseStyle.fg;
		if (ctx.ramUsage > 80) color = D2D1::ColorF(D2D1::ColorF::OrangeRed);
		DrawProgressBar(ctx, x, y, w, h, ctx.ramUsage / 100.0f, color);

		std::string fmt = config.format.empty() ? "RAM: {usage}%" : config.format;
		std::wstring text = FormatOutput(fmt, "{usage}", std::to_wstring(ctx.ramUsage));

		ctx.textBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
		ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), ctx.textFormat, D2D1::RectF(x, y, x + w, y + h), ctx.textBrush);
	}
};

class AppIconModule : public Module
{
public:
	AppIconModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override { return 24.0f; }

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		if (ctx.appIcon) {
			float iconSize = h * 0.8f; // STATIC: icon size is 80% of taskbar height
			float offX = x + (w - iconSize) / 2.0f;
			float offY = y + (h - iconSize) / 2.0f;
			D2D1_RECT_F dest = D2D1::RectF(offX, offY, offX + iconSize, offY + iconSize);
			ctx.rt->DrawBitmap(ctx.appIcon, dest);
		}
	}
};

class PingModule : public Module {
	std::atomic<int> lastPing = 0;
	std::atomic<bool> stopThread = false;
	std::thread worker;

public:
	PingModule(const ModuleConfig &cfg) : Module(cfg) 
	{
		worker = std::thread([this]() {
			HANDLE hIcmpFile;
			unsigned long ipaddr = INADDR_NONE;
			DWORD dwRetVal = 0;
			char SendData[32] = "Data Buffer";
			LPVOID ReplyBuffer = nullptr;
			DWORD ReplySize = 0;

			inet_pton(AF_INET, "8.8.8.8", &ipaddr); // Sent to Google DNS
			hIcmpFile = IcmpCreateFile();
			if (hIcmpFile == INVALID_HANDLE_VALUE) return;
			ReplySize = sizeof(ICMP_ECHO_REPLY) + sizeof(SendData);
			ReplyBuffer = (VOID *)malloc(ReplySize);
			while (!stopThread) {
				if (hIcmpFile && ReplyBuffer) {
					dwRetVal = IcmpSendEcho(hIcmpFile, ipaddr, SendData, sizeof(SendData),
						NULL, ReplyBuffer, ReplySize, 1000);
					if (dwRetVal != 0) {
						PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
						lastPing = (int)pEchoReply->RoundTripTime;
					}
					else lastPing = -1; // Timeout/error
				}
				int sleepTime = config.interval > 0 ? config.interval : 2000;
				std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
			}
			if (ReplyBuffer) free(ReplyBuffer);
			IcmpCloseHandle(hIcmpFile);
			});
		worker.detach();
	}
	~PingModule() { stopThread = true; }
	float GetContentWidth(RenderContext &ctx) override { return 100.0f; }

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
		int ms = lastPing;
		std::wstring valStr = (ms < 0) ? L"---" : std::to_wstring(ms);

		std::string fmt = config.format.empty() ? "PING: {ping}ms" : config.format;
		std::wstring text = FormatOutput(fmt, "{ping}", valStr);

		D2D1_COLOR_F color = config.baseStyle.fg;
		if (ms > 150) color = D2D1::ColorF(D2D1::ColorF::OrangeRed);
		else if (ms > 100) color = D2D1::ColorF(D2D1::ColorF::Yellow);
		DrawProgressBar(ctx, x, y, w, h, (float)ms / 200.0f, color);

		for (const auto &th : config.thresholds) {
			if (ms >= th.val) color = th.style.fg;
		}

		D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
		ctx.textBrush->SetColor(color);
		ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), ctx.textFormat, rect, ctx.textBrush);
	}
};

class GpuModule : public Module
{
public:
	GpuModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override { return 80.0f; }
	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		D2D1_COLOR_F color = config.baseStyle.fg;
		// Thresholds: Orange > 75C, Red > 85C
		if (ctx.gpuTemp > 85) color = D2D1::ColorF(D2D1::ColorF::OrangeRed);
		else if (ctx.gpuTemp > 75) color = D2D1::ColorF(D2D1::ColorF::Gold);

		DrawProgressBar(ctx, x, y, w, h, ctx.gpuTemp / 100.0f, color);
		std::string fmt = config.format.empty() ? "GPU: {temp}\u00B0C" : config.format;
		std::wstring text = FormatOutput(fmt, "{temp}", std::to_wstring(ctx.gpuTemp));

		ctx.textBrush->SetColor(D2D1::ColorF(1, 1, 1, 1)); // White text overlay
		ctx.rt->DrawTextW(
			text.c_str(),
			(UINT32)text.length(),
			ctx.textFormat,
			D2D1::RectF(x, y, x + w, y + h),
			ctx.textBrush
		);
	}
};