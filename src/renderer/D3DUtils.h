#pragma once

#include <Windows.h>
#include <stdexcept>

namespace balcony::renderer
{
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw std::runtime_error("D3D12/DXGI call failed");
        }
    }
}
