#pragma once
#include <map>
#include <Windows.h>

class WorkspaceManager
{
public:
	int currentWorkspace = 0;
	std::map<HWND, int> managedWindows;
	std::map<int, std::vector<HWND>> savedZOrders;

	void AddWindow(HWND hwnd) {
		if (managedWindows.find(hwnd) == managedWindows.end()) managedWindows[hwnd] = currentWorkspace;
	}

	static BOOL CALLBACK CaptureZOrderProc(HWND hwnd, LPARAM lParam) {
		auto *params = (std::pair<WorkspaceManager *, std::vector<HWND>*>*)lParam;
		WorkspaceManager *self = params->first;
		std::vector<HWND> *list = params->second;

		if (self->managedWindows.count(hwnd) && self->managedWindows[hwnd] == self->currentWorkspace) {
			list->push_back(hwnd);
		}
		return TRUE;
	}

    void PruneGhosts(int workspaceIdx) {
        auto &list = savedZOrders[workspaceIdx];
        list.erase(std::remove_if(list.begin(), list.end(),
            [](HWND h) { return !IsWindow(h); }),
            list.end());
    }

    void SwitchWorkspace(int newIndex) {
        if (newIndex == currentWorkspace) return;

        // SAVE Z-ORDER (Current Workspace)
        std::vector<HWND> currentOrder;
        std::pair<WorkspaceManager *, std::vector<HWND> *> params(this, &currentOrder);
        EnumWindows(CaptureZOrderProc, (LPARAM)&params);
        savedZOrders[currentWorkspace] = currentOrder;

        for (auto it = managedWindows.begin(); it != managedWindows.end(); ) {
            HWND hwnd = it->first;
            int wksp = it->second;

            if (!IsWindow(hwnd)) {
                it = managedWindows.erase(it);
                continue;
            }

            if (wksp == currentWorkspace) {
                ShowWindow(hwnd, SW_HIDE);
            }
            ++it;
        }

        currentWorkspace = newIndex;
        PruneGhosts(newIndex);

        std::vector<HWND> &zList = savedZOrders[newIndex];
        std::vector<HWND> processed; // Keep track of what we touched

        if (!zList.empty()) {
            for (auto it = zList.rbegin(); it != zList.rend(); it++) {
                HWND hwnd = *it;
                if (IsWindow(hwnd)) {
                    ShowWindow(hwnd, SW_SHOWNA);
                    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    processed.push_back(hwnd);
                }
            }
        }

        for (auto &[hwnd, wksp] : managedWindows) {
            if (wksp == currentWorkspace) {
                bool alreadyProcessed = false;
                for (HWND h : processed) if (h == hwnd) alreadyProcessed = true;

                if (!alreadyProcessed && IsWindow(hwnd)) {
                    ShowWindow(hwnd, SW_SHOWNA);
                    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }
        if (!zList.empty()) {
            HWND topWin = zList[0];
            if (IsWindow(topWin)) SetForegroundWindow(topWin);
        }
    }
};

