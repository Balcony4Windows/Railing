#include "balcony/renderer/PrimitiveRenderer.h"

#include "balcony/renderer/GraphicsDevice.h"

#include "D3DUtils.h"

#include <d3dcompiler.h>

namespace balcony::renderer
{
    namespace
    {
        constexpr char kShaderSource[] = R"(
cbuffer ScreenConstants : register(b0)
{
    float2 ScreenSize;
};

struct VSInput
{
    float2 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float2 ndc = float2(
        (input.Position.x / ScreenSize.x) * 2.0 - 1.0,
        1.0 - (input.Position.y / ScreenSize.y) * 2.0);
    output.Position = float4(ndc, 0.0, 1.0);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}

Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
    return g_Texture.Sample(g_Sampler, input.TexCoord) * input.Color;
}
)";

        Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target)
        {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            UINT flags = 0;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            const HRESULT hr = D3DCompile(
                kShaderSource, sizeof(kShaderSource) - 1, nullptr, nullptr, nullptr,
                entryPoint, target, flags, 0, &blob, &errors);
            if (FAILED(hr))
            {
                throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "shader compile failed");
            }
            return blob;
        }
    }

    void PrimitiveRenderer::Initialize(GraphicsDevice& device)
    {
        ID3D12Device* d3dDevice = device.Get();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 2;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.NumParameters = 2;
        rootSigDesc.pParameters = params;
        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = &sampler;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> signatureErrors;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &signatureErrors));
        ThrowIfFailed(d3dDevice->CreateRootSignature(
            0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)));

        const auto vsBlob = CompileShader("VSMain", "vs_5_0");
        const auto psBlob = CompileShader("PSMain", "ps_5_0");

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = _rootSignature.Get();
        psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
        psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
        psoDesc.InputLayout = {inputLayout, _countof(inputLayout)};
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = UINT_MAX;

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;

        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[0] = blend;

        ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pipelineState)));

        _srvHeap.Create(d3dDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCapacity, true);

        const UINT vertexBufferSize = MaxQuadsPerFrame * 6 * sizeof(Vertex);
        for (auto& frame : _frames)
        {
            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = vertexBufferSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ThrowIfFailed(d3dDevice->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.vertexBuffer)));

            ThrowIfFailed(frame.vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&frame.mapped)));

            frame.view.BufferLocation = frame.vertexBuffer->GetGPUVirtualAddress();
            frame.view.SizeInBytes = vertexBufferSize;
            frame.view.StrideInBytes = sizeof(Vertex);
        }
    }

    uint32_t PrimitiveRenderer::RegisterTexture(GraphicsDevice& device, ID3D12Resource* resource, uint32_t existingIndex)
    {
        uint32_t index = existingIndex;
        if (index == UINT32_MAX)
        {
            // Never hand out (or write to) a slot past the heap's actual
            // backing storage -- doing so doesn't fail visibly, it
            // silently corrupts whatever the process heap put next to it.
            // Clamping to the last valid slot means the newest texture
            // past capacity draws wrong instead of crashing the process;
            // kSrvCapacity has enough headroom that this should never
            // actually trigger in normal use.
            index = (_nextSrvIndex < kSrvCapacity) ? _nextSrvIndex++ : (kSrvCapacity - 1);
        }

        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        device.Get()->CreateShaderResourceView(resource, &srvDesc, _srvHeap.CpuHandle(index));
        return index;
    }

    void PrimitiveRenderer::Begin(ID3D12GraphicsCommandList* commandList, uint32_t frameIndex, uint32_t screenWidth, uint32_t screenHeight)
    {
        _commandList = commandList;
        _frameIndex = frameIndex;
        _writeOffset = 0;
        _pendingStart = 0;
        _pendingCount = 0;
        _pendingTextureIndex = UINT32_MAX;

        ID3D12DescriptorHeap* heaps[] = {_srvHeap.Get()};
        _commandList->SetDescriptorHeaps(1, heaps);
        _commandList->SetGraphicsRootSignature(_rootSignature.Get());
        _commandList->SetPipelineState(_pipelineState.Get());

        const float screenSize[2] = {static_cast<float>(screenWidth), static_cast<float>(screenHeight)};
        _commandList->SetGraphicsRoot32BitConstants(0, 2, screenSize, 0);

        _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        _commandList->IASetVertexBuffers(0, 1, &_frames[_frameIndex].view);
    }

    void PrimitiveRenderer::DrawQuad(const RectF& rect, uint32_t textureSrvIndex, const RectF& uv, const ColorRGBA& tint)
    {
        if (textureSrvIndex != _pendingTextureIndex)
        {
            Flush();
            _commandList->SetGraphicsRootDescriptorTable(1, _srvHeap.GpuHandle(textureSrvIndex));
            _pendingTextureIndex = textureSrvIndex;
            _pendingStart = _writeOffset;
        }

        if (_writeOffset + 6 > MaxQuadsPerFrame * 6)
        {
            return; // Frame's quad budget exhausted; drop silently for now.
        }

        const float x0 = rect.x, y0 = rect.y;
        const float x1 = rect.x + rect.width, y1 = rect.y + rect.height;
        const float u0 = uv.x, v0 = uv.y;
        const float u1 = uv.x + uv.width, v1 = uv.y + uv.height;

        Vertex* v = _frames[_frameIndex].mapped + _writeOffset;
        v[0] = {x0, y0, u0, v0, tint.r, tint.g, tint.b, tint.a};
        v[1] = {x1, y0, u1, v0, tint.r, tint.g, tint.b, tint.a};
        v[2] = {x0, y1, u0, v1, tint.r, tint.g, tint.b, tint.a};
        v[3] = {x1, y0, u1, v0, tint.r, tint.g, tint.b, tint.a};
        v[4] = {x1, y1, u1, v1, tint.r, tint.g, tint.b, tint.a};
        v[5] = {x0, y1, u0, v1, tint.r, tint.g, tint.b, tint.a};

        _writeOffset += 6;
        _pendingCount += 6;
    }

    void PrimitiveRenderer::Flush()
    {
        if (_pendingCount == 0)
        {
            return;
        }

        _commandList->DrawInstanced(_pendingCount, 1, _pendingStart, 0);
        _pendingCount = 0;
    }

    void PrimitiveRenderer::End()
    {
        Flush();
        _commandList = nullptr;
    }
}
