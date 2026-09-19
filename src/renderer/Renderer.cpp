#include "balcony/renderer/Renderer.h"

#include "D3DUtils.h"

namespace balcony::renderer
{
    Renderer::~Renderer()
    {
        Shutdown();
    }

    void Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height)
    {
        _device.Create();
        _directQueue.Create(_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
        _swapChain.Create(_device, _directQueue, hwnd, width, height);
        _primitives.Initialize(_device);

        for (uint32_t i = 0; i < FrameCount; ++i)
            ThrowIfFailed(_device.Get()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocators[i])));

        ThrowIfFailed(_device.Get()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocators[0].Get(), nullptr,
            IID_PPV_ARGS(&_commandList)));
        ThrowIfFailed(_commandList->Close());

        _initialized = true;
    }

    void Renderer::Resize(uint32_t width, uint32_t height)
    {
        if (!_initialized)
            return;

        _swapChain.Resize(_device, _directQueue, width, height);
    }

    void Renderer::BeginFrame()
    {
        if (!_initialized)
            return;

        _frameIndex = _swapChain.CurrentBackBufferIndex();
        _directQueue.WaitForFenceValue(_frameFenceValues[_frameIndex]);

        auto& allocator = _commandAllocators[_frameIndex];
        ThrowIfFailed(allocator->Reset());
        ThrowIfFailed(_commandList->Reset(allocator.Get(), nullptr));

        ID3D12Resource* backBuffer = _swapChain.BackBuffer(_frameIndex);

        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = backBuffer;
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &toRenderTarget);

        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = _swapChain.BackBufferRtv(_frameIndex);
        _commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        constexpr float clearColor[4] = {0.02f, 0.02f, 0.03f, 1.0f};
        _commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

        D3D12_VIEWPORT viewport{
            0.0f, 0.0f,
            static_cast<float>(_swapChain.Width()), static_cast<float>(_swapChain.Height()),
            0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, static_cast<LONG>(_swapChain.Width()), static_cast<LONG>(_swapChain.Height())};
        _commandList->RSSetViewports(1, &viewport);
        _commandList->RSSetScissorRects(1, &scissor);

        _primitives.Begin(_commandList.Get(), _frameIndex, _swapChain.Width(), _swapChain.Height());
    }

    void Renderer::EndFrame()
    {
        if (!_initialized)
            return;

        _primitives.End();

        ID3D12Resource* backBuffer = _swapChain.BackBuffer(_frameIndex);
        D3D12_RESOURCE_BARRIER toPresent{};
        toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toPresent.Transition.pResource = backBuffer;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _commandList->ResourceBarrier(1, &toPresent);

        ThrowIfFailed(_commandList->Close());

        ID3D12CommandList* commandLists[] = {_commandList.Get()};
        _directQueue.Get()->ExecuteCommandLists(1, commandLists);

        _swapChain.Present();

        _frameFenceValues[_frameIndex] = _directQueue.Signal();
    }

    void Renderer::Shutdown()
    {
        if (!_initialized)
            return;

        _directQueue.Flush();
        _initialized = false;
    }
}
