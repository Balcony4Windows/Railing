#pragma once
#include <d3d11.h>
#include <d2d1.h>
#include <atlbase.h>

class GraphicsDevice
{
public:
	CComPtr<ID3D11Device> device;
	CComPtr<ID3D11DeviceContext> context;
	CComPtr<ID2D1Factory> d2dFactory;

	// We could use shared resources here,
	// IE CComPtr<ID3D11ShaderResourceView> sharedIconAtlas;

	bool Initialize();
};

