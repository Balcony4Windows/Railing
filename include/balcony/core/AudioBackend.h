#pragma once

#include <Windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <string>
#include <vector>

// Default audio output device: get/set volume, mute, and which device is
// the system default. Ported from Balcony4Windows/Railing's
// Services/AudioBackend.h (pure Win32 Core Audio COM, no Direct2D
// dependency) with two deliberate changes:
//  - No IAudioEndpointVolumeCallback push notifications (Railing posted a
//    custom window message on every change). This polls instead, like
//    every other live value in this codebase (System.Time, the taskbar
//    refresh, ...) -- see scripts/plugins/volume_flyout.lua.
//  - No OpenSoundSettings() (ms-settings:sound): that URI is registered
//    to launch via explorer.exe specifically, which this project intends
//    to not be running eventually. See CLAUDE.md section 2.
// See CLAUDE.md section 6: this is an action surface (Windows.*/Shell.*
// shaped), not read-only System state, since it can change the system's
// actual audio configuration.
namespace balcony::core
{
    struct AudioDeviceInfo
    {
        std::wstring id;
        std::wstring name;
    };

    class AudioBackend
    {
    public:
        ~AudioBackend();

        // Must be called once with the DE's own HWND before anything
        // else here does anything -- COM activation doesn't need a
        // window, but this mirrors the constructor-does-nothing/
        // EnsureInitialized(hwnd) split the original has, and keeps
        // this class's lifetime independent of any particular HWND
        // being ready yet (same reasoning as WindowEnumerator's
        // selfWindowId). Idempotent.
        void EnsureInitialized();

        float GetVolume() const;
        void SetVolume(float volume);

        bool GetMute() const;
        void ToggleMute();

        std::wstring GetCurrentDeviceName() const;

        // Not present in the original -- VolumeFlyout.cpp built this
        // list itself there. Standard IMMDeviceEnumerator::
        // EnumAudioEndpoints, same PKEY_Device_FriendlyName lookup
        // GetCurrentDeviceName() already uses per device.
        std::vector<AudioDeviceInfo> EnumerateOutputDevices() const;

        // `id` from EnumerateOutputDevices() -- via the undocumented
        // IPolicyConfig interface (GUIDs below), the same mechanism the
        // Windows volume flyout itself uses.
        void SetDefaultDevice(const std::wstring& deviceId);

    private:
        void UpdateEndpoint();
        void Cleanup();

        IMMDeviceEnumerator* _enumerator = nullptr;
        IMMDevice* _device = nullptr;
        IAudioEndpointVolume* _volume = nullptr;
        bool _initialized = false;
    };
}
