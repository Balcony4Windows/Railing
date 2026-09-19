// initguid.h must precede the FIRST inclusion of dxcore's headers in this
// translation unit (including transitively, via GpuMonitor.h) so that
// DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU is actually defined here rather than
// just declared extern.
#include <initguid.h>

#include "balcony/core/GpuMonitor.h"

namespace balcony::core
{
    GpuMonitor::GpuMonitor()
    {
        InitializePdhCounters();
        InitializeTemperatureAdapter();
    }

    GpuMonitor::~GpuMonitor()
    {
        if (_query)
        {
            PdhCloseQuery(_query);
        }
    }

    void GpuMonitor::InitializePdhCounters()
    {
        if (PdhOpenQuery(nullptr, 0, &_query) != ERROR_SUCCESS)
        {
            _query = nullptr;
            return;
        }

        constexpr wchar_t kWildcardPath[] = L"\\GPU Engine(*)\\Utilization Percentage";

        DWORD pathListSize = 0;
        PdhExpandWildCardPathW(nullptr, kWildcardPath, nullptr, &pathListSize, 0);
        if (pathListSize == 0)
            return;

        std::vector<wchar_t> pathList(pathListSize);
        if (PdhExpandWildCardPathW(nullptr, kWildcardPath, pathList.data(), &pathListSize, 0) != ERROR_SUCCESS)
            return;

        for (const wchar_t* path = pathList.data(); *path != L'\0'; path += wcslen(path) + 1)
        {
            PDH_HCOUNTER counter = nullptr;
            if (PdhAddEnglishCounterW(_query, path, 0, &counter) == ERROR_SUCCESS)
                _counters.push_back(counter);
        }
    }

    void GpuMonitor::InitializeTemperatureAdapter()
    {
        Microsoft::WRL::ComPtr<IDXCoreAdapterFactory> factory;
        if (FAILED(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&factory))))
            return;

        Microsoft::WRL::ComPtr<IDXCoreAdapterList> adapterList;
        const GUID attributes[] = {DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU};
        if (FAILED(factory->CreateAdapterList(1, attributes, IID_PPV_ARGS(&adapterList))))
            return;

        const uint32_t count = adapterList->GetAdapterCount();
        for (uint32_t i = 0; i < count; ++i)
        {
            Microsoft::WRL::ComPtr<IDXCoreAdapter> candidate;
            if (FAILED(adapterList->GetAdapter(i, IID_PPV_ARGS(&candidate))))
                continue;

            // Probe: does this adapter actually support a temperature read?
            // Not every GPU/driver does, and DXCore has no "is this the
            // active display GPU" attribute, so probing is the only way.
            uint32_t sensorIndex = 0;
            float probeTemp = 0.0f;
            if (SUCCEEDED(candidate->QueryState(DXCoreAdapterState::AdapterTemperatureCelsius, &sensorIndex, &probeTemp)))
            {
                _temperatureAdapter = candidate;
                break;
            }
        }
    }

    void GpuMonitor::Update()
    {
        if (_query && !_counters.empty())
        {
            if (PdhCollectQueryData(_query) == ERROR_SUCCESS)
            {
                double total = 0.0;
                for (PDH_HCOUNTER counter : _counters)
                {
                    PDH_FMT_COUNTERVALUE value{};
                    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS)
                        total += value.doubleValue;
                }
                // Summing every engine instance is an approximation of
                // Task Manager's per-adapter aggregation, not an exact
                // match -- good enough for a taskbar-style readout.
                _usagePercent = static_cast<float>(total > 100.0 ? 100.0 : total);
            }
        }

        if (_temperatureAdapter)
        {
            uint32_t sensorIndex = 0;
            float tempCelsius = 0.0f;
            if (SUCCEEDED(_temperatureAdapter->QueryState(DXCoreAdapterState::AdapterTemperatureCelsius, &sensorIndex, &tempCelsius)))
                _temperatureCelsius = static_cast<int>(tempCelsius);
        }
    }
}
