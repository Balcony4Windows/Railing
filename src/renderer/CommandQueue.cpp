#include "balcony/renderer/CommandQueue.h"

#include "D3DUtils.h"

namespace balcony::renderer
{
    void CommandQueue::Create(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = type;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue)));
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));

        _fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!_fenceEvent)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    uint64_t CommandQueue::Signal()
    {
        const uint64_t valueToSignal = ++_fenceValue;
        ThrowIfFailed(_queue->Signal(_fence.Get(), valueToSignal));
        return valueToSignal;
    }

    bool CommandQueue::IsFenceComplete(uint64_t value) const
    {
        return _fence->GetCompletedValue() >= value;
    }

    void CommandQueue::WaitForFenceValue(uint64_t value)
    {
        if (IsFenceComplete(value))
        {
            return;
        }

        ThrowIfFailed(_fence->SetEventOnCompletion(value, _fenceEvent));
        WaitForSingleObject(_fenceEvent, INFINITE);
    }

    void CommandQueue::Flush()
    {
        WaitForFenceValue(Signal());
    }
}
