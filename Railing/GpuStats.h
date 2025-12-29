#pragma once
#include <windows.h>
#include <initguid.h>
#include <dxcore.h>
#include <cstdio> // for sprintf

#pragma comment(lib, "dxcore.lib")

class GpuStats {
public:
    GpuStats() {} // Constructor does nothing. Wait for Initialize().

    ~GpuStats() {
        if (dxAdapter) dxAdapter->Release();
        if (dxFactory) dxFactory->Release();
    }

    void Initialize() {
        if (dxAdapter) return; // Prevent double init

        if (FAILED(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&dxFactory)))) return;

        IDXCoreAdapterList *list = nullptr;
        const GUID attribs[] = { DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU };

        // Ask for Hardware GPUs
        dxFactory->CreateAdapterList(1, attribs, IID_PPV_ARGS(&list));

        if (list) {
            uint32_t count = list->GetAdapterCount();
            for (uint32_t i = 0; i < count; i++) {
                IDXCoreAdapter *candidate = nullptr;
                if (SUCCEEDED(list->GetAdapter(i, IID_PPV_ARGS(&candidate)))) {

                    // TEST: Does this GPU support temperature?
                    DXCoreAdapterState state = DXCoreAdapterState::AdapterTemperatureCelsius;
                    uint32_t sensorIndex = 0;
                    float tempCheck = 0.0f;

                    HRESULT hr = candidate->QueryState(state, &sensorIndex, &tempCheck);

                    if (SUCCEEDED(hr)) {
                        // Found a working one! Use it.
                        dxAdapter = candidate;
                        char buf[100];
                        sprintf_s(buf, "Railing: Found GPU with Temp Support at Index %d\n", i);
                        OutputDebugStringA(buf);
                        break;
                    }
                    else {
                        // Keep looking
                        candidate->Release();
                    }
                }
            }
            list->Release();
        }
    }

    int GetGpuTemp() {
        if (!dxAdapter) return 0;

        DXCoreAdapterState state = DXCoreAdapterState::AdapterTemperatureCelsius;
        uint32_t sensorIndex = 0;
        float tempCelsius = 0.0f;

        if (SUCCEEDED(dxAdapter->QueryState(state, &sensorIndex, &tempCelsius))) {
            return (int)tempCelsius;
        }
        return 0;
    }

private:
    IDXCoreAdapterFactory *dxFactory = nullptr;
    IDXCoreAdapter *dxAdapter = nullptr;
};