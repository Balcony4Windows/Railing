#pragma once

#include "balcony/renderer/DescriptorHeap.h"
#include "balcony/renderer/SwapChain.h" // FrameCount

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace balcony::renderer
{
    class GraphicsDevice;

    struct RectF
    {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

        bool Contains(float px, float py) const
        {
            return px >= x && px < x + width && py >= y && py < y + height;
        }
    };

    struct ColorRGBA
    {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    };

    // Shared immediate-mode quad batcher. Every visual primitive (Image,
    // Text, Animation, ...) draws through this one pipeline/root signature
    // instead of owning its own -- see CLAUDE.md sections 4 and 5.
    class PrimitiveRenderer
    {
    public:
        void Initialize(GraphicsDevice& device);

        // Creates an SRV for `resource` in the shared heap and returns its
        // index. Used internally by Texture -- primitives never call this.
        uint32_t RegisterTexture(GraphicsDevice& device, ID3D12Resource* resource);

        void Begin(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex, uint32_t screenWidth, uint32_t screenHeight);
        void DrawQuad(const RectF& rect, uint32_t textureSrvIndex, const RectF& uv = RectF{0.0f, 0.0f, 1.0f, 1.0f}, const ColorRGBA& tint = ColorRGBA{});
        void End();

    private:
        struct Vertex
        {
            float x, y, u, v, r, g, b, a;
        };

        void Flush();

        Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipelineState;
        DescriptorHeap _srvHeap;
        uint32_t _nextSrvIndex = 0;

        static constexpr uint32_t MaxQuadsPerFrame = 4096;
        struct FrameBuffer
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
            Vertex* mapped = nullptr;
            D3D12_VERTEX_BUFFER_VIEW view{};
        };
        std::array<FrameBuffer, FrameCount> _frames;

        ID3D12GraphicsCommandList* _commandList = nullptr;
        uint32_t _frameIndex = 0;
        uint32_t _writeOffset = 0;
        uint32_t _pendingTextureIndex = UINT32_MAX;
        uint32_t _pendingStart = 0;
        uint32_t _pendingCount = 0;
    };
}
