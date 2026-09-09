//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Private backend boundary for the material system.
//
//===========================================================================//

#ifndef MATERIALSYSTEM_RENDERBACKEND_H
#define MATERIALSYSTEM_RENDERBACKEND_H

#if defined( _WIN32 )
#pragma once
#endif

class IShaderAPI;
class IShaderDevice;
class IShaderDeviceMgr;

// Keep this contract private until all legacy shader API consumers can be
// served by it. Public Source rendering interfaces remain the compatibility
// surface for engine and game code.
enum RenderBackendType_t
{
	RENDER_BACKEND_LEGACY_SHADER_API = 0,
	RENDER_BACKEND_D3D11,
	RENDER_BACKEND_VULKAN,
	RENDER_BACKEND_OPENGL,
};

struct RenderBackendCaps_t
{
	bool m_bGraphics;
	bool m_bTextureDownload;
	bool m_bShadowDepthTextures;
	bool m_bFetch4;
	bool m_bMSAA;
	bool m_bInstancing;
	bool m_bComputeShaders;
	unsigned int m_nMaxMSAASamples;
	unsigned int m_nFeatureLevel;
};

struct RenderBackendInit_t
{
	void *m_pNativeWindow;
	unsigned int m_nWidth;
	unsigned int m_nHeight;
	unsigned int m_nMSAASamples;
	bool m_bVSync;
};

abstract_class IRenderBackend
{
public:
	virtual ~IRenderBackend() {}

	virtual RenderBackendType_t GetType() const = 0;
	virtual const char *GetName() const = 0;
	virtual RenderBackendCaps_t GetCaps() const = 0;
	virtual bool Initialize( const RenderBackendInit_t &init ) = 0;
	virtual void Shutdown() = 0;
	virtual bool Resize( unsigned int nWidth, unsigned int nHeight ) = 0;
	virtual void Present() = 0;

	// These accessors are deliberately temporary compatibility hooks. New
	// backends must implement resources and submission behind this boundary;
	// callers must not receive native graphics API objects.
	virtual IShaderAPI *GetLegacyShaderAPI() const = 0;
	virtual IShaderDevice *GetLegacyShaderDevice() const = 0;
	virtual IShaderDeviceMgr *GetLegacyShaderDeviceMgr() const = 0;
};

IRenderBackend *CreateLegacyShaderAPIBackend( IShaderAPI *pShaderAPI, IShaderDevice *pShaderDevice, IShaderDeviceMgr *pShaderDeviceMgr );
IRenderBackend *CreateD3D11RenderBackend();
void DestroyRenderBackend( IRenderBackend *pBackend );

#endif // MATERIALSYSTEM_RENDERBACKEND_H
