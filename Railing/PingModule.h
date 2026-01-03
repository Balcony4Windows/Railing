#pragma once
#define WIN32_LEAN_AND_MEAN
#include "Module.h"
#include <thread>
#include <WS2tcpip.h>
#include <IPExport.h>
#include <IcmpAPI.h>
#include <mutex>
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "ws2_32.lib")

struct NetTask {
	void *ownerId;
	std::string ip;
	std::atomic<int> *resultStore;
	int interval;
	ULONGLONG lastRun;
};

class NetworkPoller
{
public:
	static std::vector<NetTask> tasks;
	static std::mutex taskMutex;
	static bool isRunning;
	static void AddTask(void *owner, std::string ip, std::atomic<int> *store, int interval) {
		tasks.push_back({ owner, ip, store, interval, 0 });
		if (!isRunning) Start();
	}
	static void RemoveTask(void *owner) {
		std::lock_guard<std::mutex> lock(taskMutex);
		tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
			[owner](const NetTask &t) { return t.ownerId == owner; }),
			tasks.end());
	}
private:
	static void Start() {
		if (isRunning) return;
		isRunning = true;

		std::thread([]() {
			SetThreadDescription(GetCurrentThread(), L"Railing_PingWorker");

			HANDLE hIcmp = IcmpCreateFile();
			if (hIcmp == INVALID_HANDLE_VALUE) return;

			char SendData[32] = "RailingPing";
			DWORD ReplySize = sizeof(ICMP_ECHO_REPLY) + sizeof(SendData);
			LPVOID ReplyBuffer = malloc(ReplySize);

			while (isRunning) {
				ULONGLONG now = GetTickCount64();

				std::vector<NetTask> localTasks;
				{
					std::lock_guard<std::mutex> lock(taskMutex);
					localTasks = tasks;
				}

				for (auto &task : localTasks) {
					if (now - task.lastRun > task.interval) {

						unsigned long ipaddr = INADDR_NONE;
						inet_pton(AF_INET, task.ip.c_str(), &ipaddr);

						DWORD dwRet = IcmpSendEcho(hIcmp, ipaddr, SendData, sizeof(SendData),
							NULL, ReplyBuffer, ReplySize, 1000);

						int result = -1;
						if (dwRet != 0 && ReplyBuffer) {
							PICMP_ECHO_REPLY pEcho = (PICMP_ECHO_REPLY)ReplyBuffer;
							result = (int)pEcho->RoundTripTime;
						}

						std::lock_guard<std::mutex> lock(taskMutex);
						for (auto &realTask : tasks) {
							if (realTask.ownerId == task.ownerId) {
								*realTask.resultStore = result;
								realTask.lastRun = now;
								break;
							}
						}
					}
				}
				Sleep(100);
			}
			if (ReplyBuffer) free(ReplyBuffer);
			IcmpCloseHandle(hIcmp);
			}).detach();
	}
};

inline std::vector<NetTask> NetworkPoller::tasks;
inline std::mutex NetworkPoller::taskMutex;
inline bool NetworkPoller::isRunning = false;

class PingModule : public Module {
	int lastRenderedPing = -999;
	std::wstring cachedStr;
public:
	std::string targetIP;
	std::atomic<int> lastPing = 0;
	PingModule(const ModuleConfig &cfg) : Module(cfg)
	{
		targetIP = config.target.empty() ? "8.8.8.8" : config.target;
		int interval = config.interval > 0 ? config.interval : 2000;
		NetworkPoller::AddTask(this, targetIP, &lastPing, interval);
	}

	~PingModule() { NetworkPoller::RemoveTask(this); }

	float GetContentWidth(RenderContext &ctx) override {
		int currentPing = lastPing;
		if (currentPing != lastRenderedPing) {
			lastRenderedPing = currentPing;
			std::wstring valStr = (currentPing < 0) ? L"---" : std::to_wstring(currentPing);
			std::string fmt = config.format.empty() ? "PING: {ping}ms" : config.format;
			size_t pos = fmt.find("{ping}");
			std::wstring wFmt(fmt.begin(), fmt.end());
			if (pos != std::string::npos) {
				cachedStr = wFmt;
				cachedStr = FormatOutput(fmt, "{ping}", valStr);
			}
			else cachedStr = wFmt;
		}
		Style s = GetEffectiveStyle();
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		IDWriteTextLayout *layout = GetLayout(ctx, cachedStr, fmt);
		DWRITE_TEXT_METRICS m; layout->GetMetrics(&m);
		return m.width + 10.0f + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
		Style s = GetEffectiveStyle();
		int ms = lastPing;
		std::wstring valStr = (ms < 0) ? L"---" : std::to_wstring(ms);

		std::string txt = config.format.empty() ? "PING: {ping}ms" : config.format;
		std::wstring text = FormatOutput(txt, "{ping}", valStr);

		D2D1_COLOR_F color = s.fg;
		for (const auto &th : config.thresholds) {
			if (lastRenderedPing >= th.val) {
				color = th.style.fg;
				if (th.style.has_bg) s.bg = th.style.bg;
			}
		}

		float pct = (lastRenderedPing < 0) ? 0.0f : (float)lastRenderedPing / 200.0f;
		DrawProgressBar(ctx, x, y, w, h, pct, color);

		D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		ctx.rt->DrawTextW(cachedStr.c_str(), (UINT32)cachedStr.length(), fmt, rect, ctx.textBrush);
	}
};