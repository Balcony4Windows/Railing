#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string_view>

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
    class PrimitiveRenderer;

    // A GPU-resident RGBA8 texture registered with a PrimitiveRenderer's
    // shared SRV heap so it can be drawn. Copyable: copies share the same
    // GPU resource and SRV slot (see CLAUDE.md section 3 on shared resources).
    class Texture
    {
    public:
        void CreateFromPixels(GraphicsDevice& device, CommandQueue& queue, PrimitiveRenderer& renderer,
                               uint32_t width, uint32_t height, const uint8_t* rgba8);

        // Decodes a common image format (PNG/JPEG/BMP/ICO/...) via WIC.
        bool CreateFromFile(GraphicsDevice& device, CommandQueue& queue, PrimitiveRenderer& renderer,
                             std::wstring_view path);

        uint32_t Width() const { return _width; }
        uint32_t Height() const { return _height; }
        uint32_t SrvIndex() const { return _srvIndex; }
        bool IsValid() const { return _resource != nullptr; }

        // Sentinel meaning "this Texture has never been registered with a
        // PrimitiveRenderer's SRV heap yet" -- distinct from a real index
        // (0 is a valid slot), so CreateFromPixels can tell a first-time
        // upload (needs a new slot) apart from replacing content in place
        // (reuse the slot it already has). See CreateFromPixels.
        static constexpr uint32_t kInvalidSrvIndex = UINT32_MAX;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
        uint32_t _width = 0;
        uint32_t _height = 0;
        uint32_t _srvIndex = kInvalidSrvIndex;
    };
}
