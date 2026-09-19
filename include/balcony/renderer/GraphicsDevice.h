#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace balcony::renderer
{
    // Owns the DXGI factory, chosen adapter, and D3D12 device. Intended to be
    // shared by every renderer/swap chain in the process (see CLAUDE.md §3/§4).
    class GraphicsDevice
    {
    public:
        void Create();

        IDXGIFactory6* Factory() const { return _factory.Get(); }
        ID3D12Device* Get() const { return _device.Get(); }

    private:
        Microsoft::WRL::ComPtr<IDXGIFactory6> _factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> _adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> _device;
    };
}
