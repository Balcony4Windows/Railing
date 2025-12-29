#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <endpointvolume.h>
#include <vector>
#include <string>
#include <shellapi.h>

// Necessary for PKEY_Device_FriendlyName to be defined
#include <initguid.h> 
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "Mmdevapi.lib")
#pragma comment(lib, "Propsys.lib") // Required for Property Store

// Define DeviceShareMode
typedef enum DeviceShareMode {
    DeviceShareModeShared,
    DeviceShareModeExclusive
} DeviceShareMode;

// Define IPolicyConfig
interface IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

static const GUID CLSID_PolicyConfig = { 0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9} };
static const GUID IID_IPolicyConfig = { 0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8} };

class AudioBackend
{
public:
    IMMDeviceEnumerator *pEnumerator = nullptr;
    IMMDevice *pDevice = nullptr;
    IAudioEndpointVolume *pVolume = nullptr;

    AudioBackend() {
        // REMOVED CoInitialize. We assume the main thread (Railing.cpp) did this.
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);

        UpdateEndpoint();
    }

    ~AudioBackend() {
        if (pVolume) pVolume->Release();
        if (pDevice) pDevice->Release();
        if (pEnumerator) pEnumerator->Release();
    }

    void UpdateEndpoint() {
        if (!pEnumerator) return; // If this is null, CoCreateInstance failed.

        if (pDevice) { pDevice->Release(); pDevice = nullptr; }
        if (pVolume) { pVolume->Release(); pVolume = nullptr; }

        // eMultimedia is better for general volume control than eConsole
        pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);

        if (pDevice) {
            pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&pVolume);
        }
    }

    float GetVolume() {
        if (!pVolume) return 0.0f;
        float vol = 0.0f;
        pVolume->GetMasterVolumeLevelScalar(&vol);
        return vol;
    }

    void SetVolume(float vol) {
        if (!pVolume) return;
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        pVolume->SetMasterVolumeLevelScalar(vol, NULL);
    }

    void OpenSoundSettings() {
        ShellExecute(NULL, L"open", L"ms-settings:sound", NULL, NULL, SW_SHOWNORMAL);
    }

    std::wstring GetCurrentDeviceName() {
        if (!pDevice) return L"No Device";
        IPropertyStore *pProps = nullptr;

        // STGM_READ access is required
        HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (FAILED(hr)) return L"Unknown (PropStore Failed)";

        PROPVARIANT varName;
        PropVariantInit(&varName);

        // PKEY_Device_FriendlyName requires <initguid.h> and <functiondiscoverykeys_devpkey.h>
        pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        std::wstring name = varName.pwszVal ? varName.pwszVal : L"Unknown Device";

        PropVariantClear(&varName);
        pProps->Release();
        return name;
    }

    void SetDefaultDevice(const std::wstring &deviceId) {
        IPolicyConfig *pPolicyConfig = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_PolicyConfig, NULL, CLSCTX_ALL, IID_IPolicyConfig, (LPVOID *)&pPolicyConfig);

        if (SUCCEEDED(hr) && pPolicyConfig) {
            pPolicyConfig->SetDefaultEndpoint(deviceId.c_str(), eConsole);
            pPolicyConfig->SetDefaultEndpoint(deviceId.c_str(), eMultimedia);
            pPolicyConfig->Release();
            UpdateEndpoint();
        }
    }
};