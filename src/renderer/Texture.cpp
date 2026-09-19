#include "balcony/renderer/Texture.h"

#include "balcony/renderer/GraphicsDevice.h"
#include "balcony/renderer/CommandQueue.h"
#include "balcony/renderer/PrimitiveRenderer.h"

#include "D3DUtils.h"

#include <wincodec.h>

#include <vector>

namespace balcony::renderer
{
    void Texture::CreateFromPixels(GraphicsDevice& device, CommandQueue& queue, PrimitiveRenderer& renderer,
                                    uint32_t width, uint32_t height, const uint8_t* rgba8)
    {
        // If this Texture already owns a GPU resource (i.e. this call is
        // replacing content that may already be on screen -- e.g. a live
        // clock re-calling Text::SetText every second), the assignment
        // into _resource below releases that old resource. Flushing
        // first guarantees the GPU has actually finished any previously
        // submitted frame that might still be reading it via its SRV;
        // without this, a frame still in flight from the triple-buffered
        // swap chain can sample a texture whose backing memory has
        // already been freed.
        if (_resource)
        {
            queue.Flush();
        }

        ID3D12Device* d3dDevice = device.Get();

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        ThrowIfFailed(d3dDevice->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_resource)));

        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT numRows = 0;
        d3dDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, nullptr, &uploadSize);

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = uploadSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
        ThrowIfFailed(d3dDevice->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

        uint8_t* mapped = nullptr;
        ThrowIfFailed(uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
        const UINT64 srcRowPitch = static_cast<UINT64>(width) * 4;
        for (UINT row = 0; row < numRows; ++row)
        {
            memcpy(mapped + row * footprint.Footprint.RowPitch, rgba8 + row * srcRowPitch, srcRowPitch);
        }
        uploadBuffer->Unmap(0, nullptr);

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
        ThrowIfFailed(d3dDevice->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = _resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = _resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(commandList->Close());
        ID3D12CommandList* lists[] = {commandList.Get()};
        queue.Get()->ExecuteCommandLists(1, lists);
        queue.Flush(); // Asset loading isn't per-frame-hot; a blocking wait is fine here.

        _width = width;
        _height = height;
        _srvIndex = renderer.RegisterTexture(device, _resource.Get());
    }

    bool Texture::CreateFromFile(GraphicsDevice& device, CommandQueue& queue, PrimitiveRenderer& renderer,
                                  std::wstring_view path)
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            return false;
        }

        const std::wstring pathStr(path);
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(pathStr.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr))
        {
            return false;
        }

        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            return false;
        }

        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        if (width == 0 || height == 0)
        {
            return false;
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr))
        {
            return false;
        }

        CreateFromPixels(device, queue, renderer, width, height, pixels.data());
        return true;
    }
}
