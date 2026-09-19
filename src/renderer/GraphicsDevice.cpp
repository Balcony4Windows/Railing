#include "balcony/renderer/GraphicsDevice.h"

#include "D3DUtils.h"

#include <d3d12sdklayers.h>

namespace balcony::renderer
{
    namespace
    {
#if defined(_DEBUG)
        void EnableDebugLayer()
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                debugController->EnableDebugLayer();
        }
#endif

        Microsoft::WRL::ComPtr<IDXGIAdapter4> SelectAdapter(IDXGIFactory6* factory)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
            for (UINT index = 0;
                 factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
                 ++index)
            {
                DXGI_ADAPTER_DESC3 desc{};
                adapter->GetDesc3(&desc);
                if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
                    continue;

                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
                    return adapter;
            }

            throw std::runtime_error("No D3D12-capable adapter found");
        }
    }

    void GraphicsDevice::Create()
    {
#if defined(_DEBUG)
        EnableDebugLayer();
        UINT factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#else
        UINT factoryFlags = 0;
#endif

        ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&_factory)));

        _adapter = SelectAdapter(_factory.Get());

        ThrowIfFailed(D3D12CreateDevice(_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device)));
    }
}
