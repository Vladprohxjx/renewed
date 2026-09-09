//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Compatibility backend for the existing Source shader API.
//
//===========================================================================//

#include "pch_materialsystem.h"
#include "renderbackend.h"
#include "shaderapi/ishaderapi.h"
#include "shaderapi/IShaderDevice.h"

class CLegacyShaderAPIBackend : public IRenderBackend
{
public:
	CLegacyShaderAPIBackend( IShaderAPI *pShaderAPI, IShaderDevice *pShaderDevice, IShaderDeviceMgr *pShaderDeviceMgr )
		: m_pShaderAPI( pShaderAPI ), m_pShaderDevice( pShaderDevice ), m_pShaderDeviceMgr( pShaderDeviceMgr )
	{
	}

	virtual RenderBackendType_t GetType() const { return RENDER_BACKEND_LEGACY_SHADER_API; }
	virtual const char *GetName() const { return "legacy-shaderapi"; }

	virtual RenderBackendCaps_t GetCaps() const
	{
		RenderBackendCaps_t caps = { false, false, false, false, false, false, false, 1, 0 };
		if ( !m_pShaderAPI || !m_pShaderDevice )
			return caps;

		caps.m_bGraphics = m_pShaderDevice->IsUsingGraphics();
		caps.m_bTextureDownload = m_pShaderAPI->CanDownloadTextures();
		caps.m_bShadowDepthTextures = m_pShaderAPI->SupportsShadowDepthTextures();
		caps.m_bFetch4 = m_pShaderAPI->SupportsFetch4();
		caps.m_bMSAA = m_pShaderAPI->SupportsMSAAMode( 2 );
		caps.m_bInstancing = true;
		caps.m_nMaxMSAASamples = caps.m_bMSAA ? 2 : 1;
		return caps;
	}
	virtual bool Initialize( const RenderBackendInit_t & ) { return true; }
	virtual void Shutdown() {}
	virtual bool Resize( unsigned int, unsigned int ) { return true; }
	virtual void Present() {}

	virtual IShaderAPI *GetLegacyShaderAPI() const { return m_pShaderAPI; }
	virtual IShaderDevice *GetLegacyShaderDevice() const { return m_pShaderDevice; }
	virtual IShaderDeviceMgr *GetLegacyShaderDeviceMgr() const { return m_pShaderDeviceMgr; }

private:
	IShaderAPI *m_pShaderAPI;
	IShaderDevice *m_pShaderDevice;
	IShaderDeviceMgr *m_pShaderDeviceMgr;
};

IRenderBackend *CreateLegacyShaderAPIBackend( IShaderAPI *pShaderAPI, IShaderDevice *pShaderDevice, IShaderDeviceMgr *pShaderDeviceMgr )
{
	if ( !pShaderAPI || !pShaderDevice || !pShaderDeviceMgr )
		return NULL;

	return new CLegacyShaderAPIBackend( pShaderAPI, pShaderDevice, pShaderDeviceMgr );
}

void DestroyRenderBackend( IRenderBackend *pBackend )
{
	delete pBackend;
}
