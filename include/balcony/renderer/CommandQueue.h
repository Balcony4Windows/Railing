#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace balcony::renderer
{
    // A command queue plus the fence used to track GPU completion on it.
    class CommandQueue
    {
    public:
        void Create(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

        ID3D12CommandQueue* Get() const { return _queue.Get(); }

        // Inserts a fence signal and returns the value to wait on for this point.
        uint64_t Signal();
        bool IsFenceComplete(uint64_t value) const;
        void WaitForFenceValue(uint64_t value);
        // Blocks until every command submitted so far has completed on the GPU.
        void Flush();

    private:
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> _queue;
        Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
        HANDLE _fenceEvent = nullptr;
        uint64_t _fenceValue = 0;
    };
}
