//========= Copyright Valve Corporation, All rights reserved. ============//
// Purpose: D3D11 device and swap-chain implementation of IRenderBackend.

#include "pch_materialsystem.h"
#include "renderbackend.h"

#if defined( _WIN32 )
#include <d3d11.h>
#include <dxgi.h>

template <typename T> static void ReleaseD3D11( T *&p ) { if ( p ) { p->Release(); p = NULL; } }

class CD3D11RenderBackend : public IRenderBackend
{
public:
	CD3D11RenderBackend() : m_device(NULL), m_context(NULL), m_swapChain(NULL), m_rtv(NULL), m_depth(NULL), m_dsv(NULL), m_vsync(false), m_featureLevel(D3D_FEATURE_LEVEL_9_1) {}
	virtual ~CD3D11RenderBackend() { Shutdown(); }
	virtual RenderBackendType_t GetType() const { return RENDER_BACKEND_D3D11; }
	virtual const char *GetName() const { return "d3d11"; }
	virtual RenderBackendCaps_t GetCaps() const
	{
		RenderBackendCaps_t caps = { m_device != NULL, m_device != NULL, m_device != NULL, false, false, m_device != NULL, false, 1, (unsigned int)m_featureLevel };
		if ( !m_device ) return caps;
		caps.m_bComputeShaders = m_featureLevel >= D3D_FEATURE_LEVEL_11_0;
		UINT quality = 0;
		for ( unsigned int samples = 8; samples > 1; samples >>= 1 )
			if ( SUCCEEDED( m_device->CheckMultisampleQualityLevels( DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality ) ) && quality ) { caps.m_nMaxMSAASamples = samples; break; }
		caps.m_bMSAA = caps.m_nMaxMSAASamples > 1;
		return caps;
	}
	virtual bool Initialize( const RenderBackendInit_t &init )
	{
		Shutdown();
		if ( !init.m_pNativeWindow || !init.m_nWidth || !init.m_nHeight ) return false;
		DXGI_SWAP_CHAIN_DESC desc; memset(&desc, 0, sizeof(desc));
		desc.BufferDesc.Width=init.m_nWidth; desc.BufferDesc.Height=init.m_nHeight; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.OutputWindow=(HWND)init.m_pNativeWindow; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		HRESULT hr=D3D11CreateDeviceAndSwapChain(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&desc,&m_swapChain,&m_device,&m_featureLevel,&m_context);
		if (FAILED(hr)) { Shutdown(); return false; }
		m_vsync=init.m_bVSync;
		return CreateTargets(init.m_nWidth, init.m_nHeight);
	}
	virtual void Shutdown() { ReleaseTargets(); if(m_context) m_context->ClearState(); ReleaseD3D11(m_context); ReleaseD3D11(m_swapChain); ReleaseD3D11(m_device); }
	virtual bool Resize(unsigned int width,unsigned int height) { if(!m_swapChain || !width || !height) return false; ReleaseTargets(); if(FAILED(m_swapChain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0))) return false; return CreateTargets(width,height); }
	virtual void Present() { if(m_swapChain) m_swapChain->Present(m_vsync ? 1 : 0,0); }
	virtual IShaderAPI *GetLegacyShaderAPI() const { return NULL; }
	virtual IShaderDevice *GetLegacyShaderDevice() const { return NULL; }
	virtual IShaderDeviceMgr *GetLegacyShaderDeviceMgr() const { return NULL; }
private:
	bool CreateTargets(unsigned int width,unsigned int height)
	{
		ID3D11Texture2D *backBuffer=NULL; if(FAILED(m_swapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&backBuffer)) || FAILED(m_device->CreateRenderTargetView(backBuffer,NULL,&m_rtv))) { ReleaseD3D11(backBuffer); ReleaseTargets(); return false; } ReleaseD3D11(backBuffer);
		D3D11_TEXTURE2D_DESC d; memset(&d,0,sizeof(d)); d.Width=width; d.Height=height; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
		if(FAILED(m_device->CreateTexture2D(&d,NULL,&m_depth)) || FAILED(m_device->CreateDepthStencilView(m_depth,NULL,&m_dsv))) { ReleaseTargets(); return false; }
		m_context->OMSetRenderTargets(1,&m_rtv,m_dsv); return true;
	}
	void ReleaseTargets() { if(m_context) m_context->OMSetRenderTargets(0,NULL,NULL); ReleaseD3D11(m_dsv); ReleaseD3D11(m_depth); ReleaseD3D11(m_rtv); }
	ID3D11Device *m_device; ID3D11DeviceContext *m_context; IDXGISwapChain *m_swapChain; ID3D11RenderTargetView *m_rtv; ID3D11Texture2D *m_depth; ID3D11DepthStencilView *m_dsv; bool m_vsync; D3D_FEATURE_LEVEL m_featureLevel;
};
IRenderBackend *CreateD3D11RenderBackend() { return new CD3D11RenderBackend(); }
#else
IRenderBackend *CreateD3D11RenderBackend() { return NULL; }
#endif
