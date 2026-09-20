#include "balcony/core/AudioBackend.h"

#include <combaseapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <initguid.h>
#include <mmreg.h>
#include <propidl.h>

#pragma comment(lib, "Mmdevapi.lib")
#pragma comment(lib, "Propsys.lib")

namespace balcony::core
{
    namespace
    {
        // Undocumented -- there is no public Windows SDK header for
        // this. Interface layout and GUIDs are widely reverse-engineered
        // and used by essentially every third-party volume-control tool
        // (Railing included, where these came from); this is the only
        // programmatic way to change the system's default audio device.
        enum DeviceShareMode
        {
            DeviceShareModeShared,
            DeviceShareModeExclusive
        };

        struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : public IUnknown
        {
            virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
            virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode*) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode) = 0;
            virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
            virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
        };

        constexpr GUID kClsidPolicyConfig = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
    }

    AudioBackend::~AudioBackend()
    {
        Cleanup();
    }

    void AudioBackend::EnsureInitialized()
    {
        if (_initialized)
        {
            return;
        }

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_enumerator));
        if (FAILED(hr))
        {
            // COM may not have been initialized on this thread yet by
            // the time a Lua script first touches Audio.* -- main.cpp
            // already calls CoInitializeEx before creating the window,
            // but this mirrors the original's own defensive retry.
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_enumerator));
        }

        if (_enumerator)
        {
            _initialized = true;
            UpdateEndpoint();
        }
    }

    void AudioBackend::UpdateEndpoint()
    {
        if (!_enumerator)
        {
            return;
        }

        if (_volume)
        {
            _volume->Release();
            _volume = nullptr;
        }
        if (_device)
        {
            _device->Release();
            _device = nullptr;
        }

        if (FAILED(_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &_device)))
        {
            return;
        }

        _device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&_volume));
    }

    float AudioBackend::GetVolume() const
    {
        if (!_volume)
        {
            return 0.0f;
        }
        float volume = 0.0f;
        _volume->GetMasterVolumeLevelScalar(&volume);
        return volume;
    }

    void AudioBackend::SetVolume(float volume)
    {
        if (!_volume)
        {
            return;
        }
        volume = (volume < 0.0f) ? 0.0f : (volume > 1.0f ? 1.0f : volume);
        _volume->SetMasterVolumeLevelScalar(volume, nullptr);
    }

    bool AudioBackend::GetMute() const
    {
        if (!_volume)
        {
            return false;
        }
        BOOL mute = FALSE;
        _volume->GetMute(&mute);
        return mute == TRUE;
    }

    void AudioBackend::ToggleMute()
    {
        if (!_volume)
        {
            return;
        }
        BOOL mute = FALSE;
        _volume->GetMute(&mute);
        _volume->SetMute(!mute, nullptr);
    }

    std::wstring AudioBackend::GetCurrentDeviceName() const
    {
        if (!_device)
        {
            return L"No Device";
        }

        IPropertyStore* props = nullptr;
        if (FAILED(_device->OpenPropertyStore(STGM_READ, &props)))
        {
            return L"Unknown Device";
        }

        PROPVARIANT nameVariant;
        PropVariantInit(&nameVariant);
        props->GetValue(PKEY_Device_FriendlyName, &nameVariant);
        std::wstring name = nameVariant.pwszVal ? nameVariant.pwszVal : L"Unknown Device";

        PropVariantClear(&nameVariant);
        props->Release();
        return name;
    }

    std::vector<AudioDeviceInfo> AudioBackend::EnumerateOutputDevices() const
    {
        std::vector<AudioDeviceInfo> devices;
        if (!_enumerator)
        {
            return devices;
        }

        IMMDeviceCollection* collection = nullptr;
        if (FAILED(_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) || !collection)
        {
            return devices;
        }

        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || !device)
            {
                continue;
            }

            AudioDeviceInfo info;

            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id)) && id)
            {
                info.id = id;
                CoTaskMemFree(id);
            }

            IPropertyStore* props = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)))
            {
                PROPVARIANT nameVariant;
                PropVariantInit(&nameVariant);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &nameVariant)) && nameVariant.pwszVal)
                {
                    info.name = nameVariant.pwszVal;
                }
                PropVariantClear(&nameVariant);
                props->Release();
            }

            device->Release();

            if (!info.id.empty())
            {
                devices.push_back(std::move(info));
            }
        }

        collection->Release();
        return devices;
    }

    void AudioBackend::SetDefaultDevice(const std::wstring& deviceId)
    {
        IPolicyConfig* policyConfig = nullptr;
        const HRESULT hr = CoCreateInstance(kClsidPolicyConfig, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), reinterpret_cast<void**>(&policyConfig));

        if (SUCCEEDED(hr) && policyConfig)
        {
            policyConfig->SetDefaultEndpoint(deviceId.c_str(), eConsole);
            policyConfig->SetDefaultEndpoint(deviceId.c_str(), eMultimedia);
            policyConfig->Release();
            UpdateEndpoint();
        }
    }

    void AudioBackend::Cleanup()
    {
        if (_volume)
        {
            _volume->Release();
            _volume = nullptr;
        }
        if (_device)
        {
            _device->Release();
            _device = nullptr;
        }
        if (_enumerator)
        {
            _enumerator->Release();
            _enumerator = nullptr;
        }
    }
}
