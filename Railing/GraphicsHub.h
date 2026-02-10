#pragma once
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

class GraphicsHub
{
public:
	static GraphicsHub &Get() {
		static GraphicsHub instance;
		return instance;
	}

	bool Initialize();
	void Shutdown();

    // The Shared GPU Resources
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIDevice> dxgiDevice;

    // The Shared Factories
    ComPtr<ID2D1Factory1> d2dFactory; // Note: Factory1!!
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<IWICImagingFactory> wicFactory;

private:
    GraphicsHub() = default;
    ~GraphicsHub() { Shutdown(); }
};

