#pragma once

#include <Windows.h>
#include <wlanapi.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Wi-Fi scan/connect, ported from Balcony4Windows/Railing's
// Services/NetworkBackend.h (pure WLAN API, no Direct2D dependency).
//
// One deliberate change: the original's ConnectTo() blocks the calling
// thread for up to 10 seconds, polling WlanQueryInterface every 250ms
// while waiting for the connection to complete. Fine on a worker
// thread; Balcony is single-threaded end to end (Lua, input, and
// rendering all run on the one main loop), so calling that directly
// would freeze the entire desktop -- mouse, keyboard, everything -- for
// up to 10 seconds on every connection attempt. ConnectTo here instead
// starts that same blocking logic on a background thread and returns
// immediately; poll TryTakeConnectResult() (from Balcony.OnUpdate, same
// as every other live value in this codebase) for the outcome. See
// CLAUDE.md section 6: this is an action surface, not read-only System
// state.
namespace balcony::core
{
    struct WifiNetworkInfo
    {
        std::wstring ssid;
        int signalQuality = 0;
        bool connected = false;
        bool secure = false;
        DOT11_AUTH_ALGORITHM authAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
        DOT11_CIPHER_ALGORITHM cipherAlgorithm = DOT11_CIPHER_ALGO_NONE;
    };

    struct NetworkStatus
    {
        bool connected = false;
        int signalQuality = 0;
    };

    class NetworkBackend
    {
    public:
        NetworkBackend();
        ~NetworkBackend();

        // Fire-and-forget: WlanScan doesn't block waiting for results --
        // it kicks off a scan the radio performs in the background.
        // ScanNetworks() picks up whatever it's found so far/since.
        void RequestScan();

        // Sorted connected-first, then by signal strength (matches the
        // real Windows network flyout's own ordering). Deduplicated by
        // SSID.
        std::vector<WifiNetworkInfo> ScanNetworks() const;

        NetworkStatus GetCurrentStatus() const;

        // Relies on an interface GUID already discovered by a prior
        // RequestScan()/ScanNetworks()/GetCurrentStatus() call, same as
        // the original this was ported from -- an existing quirk of the
        // source, not introduced here.
        bool HasSavedProfile(const std::wstring& ssid) const;

        // Starts connecting on a background thread and returns
        // immediately. `password` empty connects using an existing
        // saved profile (or an open network); non-empty creates a new
        // WPA2PSK/WPAPSK profile first. Does nothing if a connection
        // attempt is already in progress -- check IsConnecting() first.
        void ConnectTo(const WifiNetworkInfo& network, const std::wstring& password);
        bool IsConnecting() const { return _connecting; }

        // Non-blocking. Returns the result string ("Connected!",
        // "Failed.", "Timed Out!", ...) exactly once -- the first call
        // after a background ConnectTo() finishes -- and std::nullopt
        // every other time, including while still connecting.
        std::optional<std::wstring> TryTakeConnectResult();

        void Disconnect();

    private:
        bool EnsureHandle();

        HANDLE _handle = nullptr;
        DWORD _negotiatedVersion = 0;

        // Main-thread-only -- see ConnectTo()'s .cpp comment on why the
        // background thread never touches this. `mutable` because
        // GetCurrentStatus()/ScanNetworks() cache the interface GUID
        // they discover as a side effect while otherwise being pure
        // queries from the caller's perspective (same as the original
        // this was ported from, which wasn't const-qualified at all).
        mutable GUID _interfaceGuid{};

        std::thread _connectThread;
        std::atomic<bool> _connecting{false};
        std::mutex _resultMutex;
        std::optional<std::wstring> _connectResult;
    };
}
