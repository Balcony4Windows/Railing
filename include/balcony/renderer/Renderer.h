#pragma once

#include "balcony/renderer/CommandQueue.h"
#include "balcony/renderer/GraphicsDevice.h"
#include "balcony/renderer/PrimitiveRenderer.h"
#include "balcony/renderer/SwapChain.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <Windows.h>

#include <array>
#include <cstdint>

// D3D12 renderer: device/queue/heap management, frame pacing.
// See CLAUDE.md section 4.
namespace balcony::renderer
{
    class Renderer
    {
    public:
        ~Renderer();

        void Initialize(HWND hwnd, uint32_t width, uint32_t height);
        void Resize(uint32_t width, uint32_t height);

        // Frame is split so callers can submit primitive draws in between.
        void BeginFrame();
        void EndFrame();

        void Shutdown();

        GraphicsDevice& Device() { return _device; }
        CommandQueue& Queue() { return _directQueue; }
        PrimitiveRenderer& Primitives() { return _primitives; }

    private:
        GraphicsDevice _device;
        CommandQueue _directQueue;
        SwapChain _swapChain;
        PrimitiveRenderer _primitives;

        std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> _commandAllocators;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
        std::array<uint64_t, FrameCount> _frameFenceValues{};

        uint32_t _frameIndex = 0;
        bool _initialized = false;
    };
}
