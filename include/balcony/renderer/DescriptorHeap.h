#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace balcony::renderer
{
    // Thin wrapper around a single ID3D12DescriptorHeap with handle-offset math.
    class DescriptorHeap
    {
    public:
        void Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible = false);

        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT index) const;

        ID3D12DescriptorHeap* Get() const { return _heap.Get(); }

    private:
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
        UINT _descriptorSize = 0;
    };
}
