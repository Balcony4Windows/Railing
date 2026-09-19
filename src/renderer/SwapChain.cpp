#include "balcony/renderer/SwapChain.h"

#include "balcony/renderer/CommandQueue.h"
#include "balcony/renderer/GraphicsDevice.h"

#include "D3DUtils.h"

namespace balcony::renderer
{
    void SwapChain::Create(GraphicsDevice& device, CommandQueue& presentQueue, HWND hwnd, uint32_t width, uint32_t height)
    {
        _width = width;
        _height = height;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = FrameCount;
        desc.SampleDesc.Count = 1;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(device.Factory()->CreateSwapChainForHwnd(
            presentQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1));

        // We drive resizing ourselves; opt out of DXGI's alt-enter handling.
        ThrowIfFailed(device.Factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

        ThrowIfFailed(swapChain1.As(&_swapChain));

        _rtvHeap.Create(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount);
        CreateRenderTargetViews(device.Get());
    }

    void SwapChain::CreateRenderTargetViews(ID3D12Device* device)
    {
        for (uint32_t i = 0; i < FrameCount; ++i)
        {
            ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_backBuffers[i])));
            device->CreateRenderTargetView(_backBuffers[i].Get(), nullptr, _rtvHeap.CpuHandle(i));
        }
    }

    void SwapChain::ReleaseBackBuffers()
    {
        for (auto& backBuffer : _backBuffers)
        {
            backBuffer.Reset();
        }
    }

    void SwapChain::Resize(GraphicsDevice& device, CommandQueue& presentQueue, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || (width == _width && height == _height))
        {
            return;
        }

        presentQueue.Flush();
        ReleaseBackBuffers();

        ThrowIfFailed(_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0));

        _width = width;
        _height = height;
        CreateRenderTargetViews(device.Get());
    }

    void SwapChain::Present()
    {
        // Vsync-locked for now; frame pacing beyond this is future work.
        ThrowIfFailed(_swapChain->Present(1, 0));
    }

    uint32_t SwapChain::CurrentBackBufferIndex() const
    {
        return _swapChain->GetCurrentBackBufferIndex();
    }
}
