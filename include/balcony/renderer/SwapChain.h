#pragma once

#include "balcony/renderer/DescriptorHeap.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <Windows.h>

#include <array>
#include <cstdint>

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;

    // Triple-buffered by default.
    inline constexpr uint32_t FrameCount = 3;

    class SwapChain
    {
    public:
        void Create(GraphicsDevice& device, CommandQueue& presentQueue, HWND hwnd, uint32_t width, uint32_t height);
        void Resize(GraphicsDevice& device, CommandQueue& presentQueue, uint32_t width, uint32_t height);
        void Present();

        ID3D12Resource* BackBuffer(uint32_t index) const { return _backBuffers[index].Get(); }
        D3D12_CPU_DESCRIPTOR_HANDLE BackBufferRtv(uint32_t index) const { return _rtvHeap.CpuHandle(index); }
        uint32_t CurrentBackBufferIndex() const;

        uint32_t Width() const { return _width; }
        uint32_t Height() const { return _height; }

    private:
        void CreateRenderTargetViews(ID3D12Device* device);
        void ReleaseBackBuffers();

        Microsoft::WRL::ComPtr<IDXGISwapChain3> _swapChain;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> _backBuffers;
        DescriptorHeap _rtvHeap;
        uint32_t _width = 0;
        uint32_t _height = 0;
    };
}
