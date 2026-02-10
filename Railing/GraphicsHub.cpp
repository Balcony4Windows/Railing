#include "GraphicsHub.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

bool GraphicsHub::Initialize()
{
	HRESULT hr = S_OK;

	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

	hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, creationFlags,
		featureLevels, ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext);
	if (FAILED(hr)) return false;

	hr = d3dDevice.As(&dxgiDevice);
	if (FAILED(hr)) return false;

	D2D1_FACTORY_OPTIONS options = {};
	hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
		&options, &d2dFactory);
	if (FAILED(hr)) return false;

	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &writeFactory);
	if (FAILED(hr)) return false;
	hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	if (FAILED(hr)) return false;
	
	return true;
}

void GraphicsHub::Shutdown()
{
	// ComPtrs will auto-release automatically, but we can explicitly clear them if needed
}