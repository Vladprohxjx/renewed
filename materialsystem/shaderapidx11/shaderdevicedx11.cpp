//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//
//===========================================================================//

#include <d3d11.h>
#include <d3dcompiler.h>
#include <initguid.h>
#include <d3d11shader.h>

#include "shaderdevicedx11.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapidx11.h"
#include "shadershadowdx11.h"
#include "meshdx11.h"
#include "shaderapidx11_global.h"
#include "tier1/KeyValues.h"
#include "tier2/tier2.h"
#include "tier0/icommandline.h"
#include "inputlayoutdx11.h"
#include "shaderapibase.h"

#include <math.h>
#include <malloc.h>

ConVar mat_hdr_level( "mat_hdr_level", "2", FCVAR_ARCHIVE );
ConVar mat_fastclip( "mat_fastclip", "0", FCVAR_CHEAT );

//-----------------------------------------------------------------------------
// Explicit instantiation of shader buffer implementation
//-----------------------------------------------------------------------------
template class CShaderBuffer< ID3DBlob >;


//-----------------------------------------------------------------------------
//
// Device manager
//
//-----------------------------------------------------------------------------
static CShaderDeviceMgrDx11 g_ShaderDeviceMgrDx11;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(
	CShaderDeviceMgrDx11,
	IShaderDeviceMgr,
	SHADER_DEVICE_MGR_INTERFACE_VERSION,
	g_ShaderDeviceMgrDx11
);

static CShaderDeviceDx11 g_ShaderDeviceDx11;
CShaderDeviceDx11* g_pShaderDeviceDx11 = &g_ShaderDeviceDx11;


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CShaderDeviceMgrDx11::CShaderDeviceMgrDx11()
{
	m_pDXGIFactory = NULL;
	m_bObeyDxCommandlineOverride = true;
}

CShaderDeviceMgrDx11::~CShaderDeviceMgrDx11()
{
}


//-----------------------------------------------------------------------------
// Connect, disconnect
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::Connect( CreateInterfaceFn factory )
{
	LOCK_SHADERAPI();

	if ( !BaseClass::Connect( factory ) )
		return false;

	HRESULT hr = CreateDXGIFactory(
		__uuidof( IDXGIFactory ),
		(void**)(&m_pDXGIFactory)
	);

	if ( FAILED( hr ) )
	{
		Warning( "Failed to create the DXGI Factory!\n" );
		return false;
	}

	InitAdapterInfo();
	return true;
}

void CShaderDeviceMgrDx11::Disconnect()
{
	LOCK_SHADERAPI();

	if ( m_pDXGIFactory )
	{
		m_pDXGIFactory->Release();
		m_pDXGIFactory = NULL;
	}

	BaseClass::Disconnect();
}


//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
InitReturnVal_t CShaderDeviceMgrDx11::Init()
{
	LOCK_SHADERAPI();

	return INIT_OK;
}


//-----------------------------------------------------------------------------
// Shutdown
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::Shutdown()
{
	LOCK_SHADERAPI();

	if ( g_pShaderDevice )
	{
		g_pShaderDevice->ShutdownDevice();
		g_pShaderDevice = NULL;
	}

	g_pShaderAPI = NULL;
	g_pShaderShadow = NULL;
}


//-----------------------------------------------------------------------------
// Initialize adapter information
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::InitAdapterInfo()
{
	m_Adapters.RemoveAll();

	IDXGIAdapter *pAdapter = NULL;

	for (
		UINT nCount = 0;
		m_pDXGIFactory->EnumAdapters( nCount, &pAdapter ) != DXGI_ERROR_NOT_FOUND;
		++nCount )
	{
		int j = m_Adapters.AddToTail();
		AdapterInfo_t &info = m_Adapters[j];

#ifdef _DEBUG
		memset( &info.m_ActualCaps, 0xDD, sizeof( info.m_ActualCaps ) );
#endif

		IDXGIOutput *pOutput = GetAdapterOutput( nCount );

		info.m_ActualCaps.m_bDeviceOk =
			ComputeCapsFromD3D( &info.m_ActualCaps, pAdapter, pOutput );

		if ( !info.m_ActualCaps.m_bDeviceOk )
		{
			pAdapter->Release();
			pAdapter = NULL;
			continue;
		}

		ReadDXSupportLevels( info.m_ActualCaps );

		ReadHardwareCaps(
			info.m_ActualCaps,
			info.m_ActualCaps.m_nMaxDXSupportLevel
		);

		const char *pShaderParam = CommandLine()->ParmValue( "-shader" );
		if ( pShaderParam && pShaderParam[0] )
		{
			Q_strncpy(
				info.m_ActualCaps.m_pShaderDLL,
				pShaderParam,
				sizeof( info.m_ActualCaps.m_pShaderDLL )
			);
		}

		pAdapter->Release();
		pAdapter = NULL;
	}
}


//-----------------------------------------------------------------------------
// Determines hardware caps from D3D
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::ComputeCapsFromD3D(
	HardwareCaps_t *pCaps,
	IDXGIAdapter *pAdapter,
	IDXGIOutput *pOutput )
{
	if ( !pCaps || !pAdapter )
		return false;

	HRESULT hr = pAdapter->CheckInterfaceSupport(
		__uuidof( ID3D11Device ),
		NULL
	);

	if ( hr != S_OK )
	{
		return false;
	}

	DXGI_ADAPTER_DESC desc;
	hr = pAdapter->GetDesc( &desc );

	Assert( !FAILED( hr ) );

	if ( FAILED( hr ) )
		return false;

	bool bForceFloatHDR =
		( CommandLine()->CheckParm( "-floathdr" ) != NULL );

	Q_UnicodeToUTF8(
		desc.Description,
		pCaps->m_pDriverName,
		MATERIAL_ADAPTER_NAME_LENGTH
	);

	pCaps->m_VendorID = desc.VendorId;
	pCaps->m_DeviceID = desc.DeviceId;
	pCaps->m_SubSysID = desc.SubSysId;
	pCaps->m_Revision = desc.Revision;

	pCaps->m_NumSamplers = 16;
	pCaps->m_NumTextureStages = 0;

	pCaps->m_HasSetDeviceGammaRamp = true;
	pCaps->m_bSoftwareVertexProcessing = false;

	pCaps->m_SupportsVertexShaders = true;
	pCaps->m_SupportsVertexShaders_2_0 = false;

	pCaps->m_SupportsPixelShaders = true;
	pCaps->m_SupportsPixelShaders_1_4 = false;
	pCaps->m_SupportsPixelShaders_2_0 = false;
	pCaps->m_SupportsPixelShaders_2_b = false;
	pCaps->m_SupportsShaderModel_3_0 = false;

	pCaps->m_SupportsCompressedTextures = COMPRESSED_TEXTURES_ON;
	pCaps->m_SupportsCompressedVertices = VERTEX_COMPRESSION_ON;

	pCaps->m_bSupportsAnisotropicFiltering = true;
	pCaps->m_bSupportsMagAnisotropicFiltering = true;

	pCaps->m_bSupportsVertexTextures = true;
	pCaps->m_nMaxAnisotropy = 16;

	pCaps->m_MaxTextureWidth =
		D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

	pCaps->m_MaxTextureHeight =
		D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

	pCaps->m_MaxTextureDepth =
		D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;

	pCaps->m_MaxTextureAspectRatio = 1024;
	pCaps->m_MaxPrimitiveCount = 65536;

	pCaps->m_ZBiasAndSlopeScaledDepthBiasSupported = true;
	pCaps->m_SupportsMipmapping = true;
	pCaps->m_SupportsOverbright = true;
	pCaps->m_SupportsCubeMaps = true;

	pCaps->m_NumPixelShaderConstants = 1024;
	pCaps->m_NumVertexShaderConstants = 1024;

	pCaps->m_TextureMemorySize = desc.DedicatedVideoMemory;

	pCaps->m_MaxNumLights = 4;
	pCaps->m_SupportsHardwareLighting = false;

	pCaps->m_MaxBlendMatrices = 0;
	pCaps->m_MaxBlendMatrixIndices = 0;

	pCaps->m_MaxVertexShaderBlendMatrices = 53;

	pCaps->m_SupportsMipmappedCubemaps = true;
	pCaps->m_SupportsNonPow2Textures = true;

	pCaps->m_nDXSupportLevel = 110;
	pCaps->m_nMaxDXSupportLevel = 110;

	pCaps->m_PreferDynamicTextures = false;
	pCaps->m_HasProjectedBumpEnv = true;

	pCaps->m_MaxUserClipPlanes = 6;

	pCaps->m_HDRType =
		bForceFloatHDR ? HDR_TYPE_FLOAT : HDR_TYPE_INTEGER;

	pCaps->m_SupportsSRGB = true;
	pCaps->m_FakeSRGBWrite = true;
	pCaps->m_CanDoSRGBReadFromRTs = true;

	pCaps->m_bSupportsSpheremapping = true;
	pCaps->m_UseFastClipping = false;

	pCaps->m_pShaderDLL[0] = 0;

	pCaps->m_bNeedsATICentroidHack = false;
	pCaps->m_bColorOnSecondStream = true;
	pCaps->m_bSupportsStreamOffset = true;

	pCaps->m_nMaxViewports = 4;
	pCaps->m_nVertexTextureCount = 16;

	pCaps->m_nMaxVertexTextureDimension =
		D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

	pCaps->m_bSupportsAlphaToCoverage = false;
	pCaps->m_bSupportsShadowDepthTextures = true;

	pCaps->m_bSupportsFetch4 =
		( desc.VendorId == VENDORID_ATI );

	pCaps->m_bSupportsBorderColor = true;
	pCaps->m_ShadowDepthTextureFormat = IMAGE_FORMAT_UNKNOWN;

	pCaps->m_bFogColorSpecifiedInLinearSpace =
		( desc.VendorId == VENDORID_NVIDIA );

	if ( pOutput )
	{
		DXGI_GAMMA_CONTROL_CAPABILITIES gammaCaps;
		ZeroMemory( &gammaCaps, sizeof( gammaCaps ) );

		hr = pOutput->GetGammaControlCapabilities( &gammaCaps );

		if ( SUCCEEDED( hr ) )
		{
			pCaps->m_flMinGammaControlPoint =
				gammaCaps.MinConvertedValue;

			pCaps->m_flMaxGammaControlPoint =
				gammaCaps.MaxConvertedValue;

			pCaps->m_nGammaControlPointCount =
				gammaCaps.NumGammaControlPoints;
		}
		else
		{
			pCaps->m_flMinGammaControlPoint = 0.0f;
			pCaps->m_flMaxGammaControlPoint = 1.0f;
			pCaps->m_nGammaControlPointCount = 0;
		}
	}
	else
	{
		pCaps->m_flMinGammaControlPoint = 0.0f;
		pCaps->m_flMaxGammaControlPoint = 1.0f;
		pCaps->m_nGammaControlPointCount = 0;
	}

	pCaps->m_bCanStretchRectFromTextures = true;

	return true;
}


//-----------------------------------------------------------------------------
// Gets the number of adapters
//-----------------------------------------------------------------------------
int CShaderDeviceMgrDx11::GetAdapterCount() const
{
	return m_Adapters.Count();
}


//-----------------------------------------------------------------------------
// Returns info about each adapter
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::GetAdapterInfo(
	int nAdapter,
	MaterialAdapterInfo_t& info ) const
{
	Assert( nAdapter >= 0 );
	Assert( nAdapter < m_Adapters.Count() );

	const HardwareCaps_t &caps =
		m_Adapters[nAdapter].m_ActualCaps;

	memcpy(
		&info,
		&caps,
		sizeof( MaterialAdapterInfo_t )
	);
}


//-----------------------------------------------------------------------------
// Returns the adapter interface for a particular adapter
//-----------------------------------------------------------------------------
IDXGIAdapter* CShaderDeviceMgrDx11::GetAdapter(
	int nAdapter ) const
{
	Assert( m_pDXGIFactory );
	Assert( nAdapter >= 0 );
	Assert( nAdapter < GetAdapterCount() );

	IDXGIAdapter *pAdapter = NULL;

	HRESULT hr =
		m_pDXGIFactory->EnumAdapters(
			nAdapter,
			&pAdapter
		);

	return FAILED( hr ) ? NULL : pAdapter;
}


//-----------------------------------------------------------------------------
// Returns amount of video memory
//-----------------------------------------------------------------------------
int CShaderDeviceMgrDx11::GetVidMemBytes(
	int nAdapter ) const
{
	LOCK_SHADERAPI();

	IDXGIAdapter *pAdapter =
		GetAdapter( nAdapter );

	if ( !pAdapter )
		return 0;

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = pAdapter->GetDesc( &desc );

	Assert( !FAILED( hr ) );

	pAdapter->Release();

	if ( FAILED( hr ) )
		return 0;

	return static_cast<int>( desc.DedicatedVideoMemory );
}


//-----------------------------------------------------------------------------
// Returns appropriate adapter output
//-----------------------------------------------------------------------------
IDXGIOutput* CShaderDeviceMgrDx11::GetAdapterOutput(
	int nAdapter ) const
{
	LOCK_SHADERAPI();

	IDXGIAdapter *pAdapter =
		GetAdapter( nAdapter );

	if ( !pAdapter )
		return NULL;

	IDXGIOutput *pOutput = NULL;

	for ( UINT i = 0; ; ++i )
	{
		HRESULT hr =
			pAdapter->EnumOutputs(
				i,
				&pOutput
			);

		if ( hr == DXGI_ERROR_NOT_FOUND )
			break;

		if ( FAILED( hr ) )
			continue;

		DXGI_OUTPUT_DESC desc;
		hr = pOutput->GetDesc( &desc );

		if ( FAILED( hr ) )
		{
			pOutput->Release();
			pOutput = NULL;
			continue;
		}

		if ( !desc.AttachedToDesktop )
		{
			pOutput->Release();
			pOutput = NULL;
			continue;
		}

		pAdapter->Release();
		return pOutput;
	}

	pAdapter->Release();
	return NULL;
}


//-----------------------------------------------------------------------------
// Returns the number of modes
//-----------------------------------------------------------------------------
int CShaderDeviceMgrDx11::GetModeCount(
	int nAdapter ) const
{
	LOCK_SHADERAPI();

	Assert( m_pDXGIFactory );
	Assert( nAdapter >= 0 );
	Assert( nAdapter < GetAdapterCount() );

	IDXGIOutput *pOutput =
		GetAdapterOutput( nAdapter );

	if ( !pOutput )
		return 0;

	UINT num = 0;

	DXGI_FORMAT format =
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	UINT flags = 0;

	HRESULT hr =
		pOutput->GetDisplayModeList(
			format,
			flags,
			&num,
			NULL
		);

	pOutput->Release();

	return FAILED( hr ) ? 0 : num;
}


//-----------------------------------------------------------------------------
// Returns mode information
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::GetModeInfo(
	ShaderDisplayMode_t* pInfo,
	int nAdapter,
	int nMode ) const
{
	if ( !pInfo )
		return;

	pInfo->m_nWidth = 0;
	pInfo->m_nHeight = 0;
	pInfo->m_Format = IMAGE_FORMAT_UNKNOWN;
	pInfo->m_nRefreshRateNumerator = 0;
	pInfo->m_nRefreshRateDenominator = 0;

	LOCK_SHADERAPI();

	Assert( m_pDXGIFactory );
	Assert( nAdapter >= 0 );
	Assert( nAdapter < GetAdapterCount() );

	IDXGIOutput *pOutput =
		GetAdapterOutput( nAdapter );

	if ( !pOutput )
		return;

	UINT num = 0;

	DXGI_FORMAT format =
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	UINT flags = DXGI_ENUM_MODES_INTERLACED;

	HRESULT hr =
		pOutput->GetDisplayModeList(
			format,
			flags,
			&num,
			NULL
		);

	if ( FAILED( hr ) || num == 0 )
	{
		pOutput->Release();
		return;
	}

	if ( nMode < 0 || (UINT)nMode >= num )
	{
		pOutput->Release();
		return;
	}

	DXGI_MODE_DESC *pDescs =
		(DXGI_MODE_DESC*)_alloca(
			num * sizeof( DXGI_MODE_DESC )
		);

	hr =
		pOutput->GetDisplayModeList(
			format,
			flags,
			&num,
			pDescs
		);

	if ( FAILED( hr ) )
	{
		pOutput->Release();
		return;
	}

	pInfo->m_nWidth =
		pDescs[nMode].Width;

	pInfo->m_nHeight =
		pDescs[nMode].Height;

	pInfo->m_nRefreshRateNumerator =
		pDescs[nMode].RefreshRate.Numerator;

	pInfo->m_nRefreshRateDenominator =
		pDescs[nMode].RefreshRate.Denominator;

	pOutput->Release();
}


//-----------------------------------------------------------------------------
// Returns current mode
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDx11::GetCurrentModeInfo(
	ShaderDisplayMode_t* pInfo,
	int nAdapter ) const
{
	if ( !pInfo )
		return;

	pInfo->m_nWidth = 0;
	pInfo->m_nHeight = 0;
	pInfo->m_Format = IMAGE_FORMAT_UNKNOWN;
	pInfo->m_nRefreshRateNumerator = 0;
	pInfo->m_nRefreshRateDenominator = 0;

	LOCK_SHADERAPI();

	IDXGIOutput *pOutput =
		GetAdapterOutput( nAdapter );

	if ( !pOutput )
		return;

	DXGI_OUTPUT_DESC outputDesc;
	HRESULT hr =
		pOutput->GetDesc( &outputDesc );

	if ( FAILED( hr ) )
	{
		pOutput->Release();
		return;
	}

	RECT rect = outputDesc.DesktopCoordinates;

	pInfo->m_nWidth =
		rect.right - rect.left;

	pInfo->m_nHeight =
		rect.bottom - rect.top;

	pOutput->Release();
}


//-----------------------------------------------------------------------------
// Initialization / shutdown
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDx11::SetAdapter(
	int nAdapter,
	int nFlags )
{
	(void)nAdapter;
	(void)nFlags;

	return true;
}


//-----------------------------------------------------------------------------
// Sets the mode
//-----------------------------------------------------------------------------
CreateInterfaceFn CShaderDeviceMgrDx11::SetMode(
	void *hWnd,
	int nAdapter,
	const ShaderDeviceInfo_t& mode )
{
	LOCK_SHADERAPI();

	Assert( m_pDXGIFactory );
	Assert( nAdapter >= 0 );
	Assert( nAdapter < GetAdapterCount() );

	int nDXLevel =
		mode.m_nDXLevel != 0
			? mode.m_nDXLevel
			: m_Adapters[nAdapter].m_ActualCaps.m_nDXSupportLevel;

	if ( m_bObeyDxCommandlineOverride )
	{
		nDXLevel =
			CommandLine()->ParmValue(
				"-dxlevel",
				nDXLevel
			);

		m_bObeyDxCommandlineOverride = false;
	}

	if ( nDXLevel >
		m_Adapters[nAdapter].m_ActualCaps.m_nMaxDXSupportLevel )
	{
		nDXLevel =
			m_Adapters[nAdapter].m_ActualCaps.m_nMaxDXSupportLevel;
	}

	nDXLevel =
		GetClosestActualDXLevel( nDXLevel );

	if ( nDXLevel < 110 )
	{
		return NULL;
	}

	if ( g_pShaderAPI )
	{
		g_pShaderAPI->OnDeviceShutdown();
		g_pShaderAPI = NULL;
	}

	if ( g_pShaderDevice )
	{
		g_pShaderDevice->ShutdownDevice();
		g_pShaderDevice = NULL;
	}

	g_pShaderShadow = NULL;

	ShaderDeviceInfo_t adjustedMode = mode;
	adjustedMode.m_nDXLevel = nDXLevel;

	if ( !g_pShaderDeviceDx11->InitDevice(
		hWnd,
		nAdapter,
		adjustedMode ) )
	{
		return NULL;
	}

	if ( !g_pShaderAPIDx11->OnDeviceInit() )
	{
		g_pShaderDeviceDx11->ShutdownDevice();
		return NULL;
	}

	g_pShaderDevice = g_pShaderDeviceDx11;
	g_pShaderAPI = g_pShaderAPIDx11;
	g_pShaderShadow = g_pShaderShadowDx11;

	return ShaderInterfaceFactory;
}


//-----------------------------------------------------------------------------
//
// Device
//
//-----------------------------------------------------------------------------

CShaderDeviceDx11::CShaderDeviceDx11()
{
	m_pDevice = NULL;
	m_pDeviceContext = NULL;
	m_pOutput = NULL;
	m_pSwapChain = NULL;
	m_pRenderTargetView = NULL;
	m_pDepthStencil = NULL;
	m_pDepthStencilView = NULL;

	m_hWnd = NULL;
	m_nAdapter = -1;

	m_nWindowWidth = 0;
	m_nWindowHeight = 0;
}

CShaderDeviceDx11::~CShaderDeviceDx11()
{
	ShutdownDevice();
}


//-----------------------------------------------------------------------------
// Sets the mode
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::InitDevice(
	void *hWnd,
	int nAdapter,
	const ShaderDeviceInfo_t& mode )
{
	if ( m_nAdapter != -1 )
	{
		Warning(
			"CShaderDeviceDx11::InitDevice: "
			"Previous mode has not been shut down!\n"
		);

		return false;
	}

	LOCK_SHADERAPI();

	IDXGIAdapter *pAdapter =
		g_ShaderDeviceMgrDx11.GetAdapter( nAdapter );

	if ( !pAdapter )
		return false;

	m_pOutput =
		g_ShaderDeviceMgrDx11.GetAdapterOutput( nAdapter );

	if ( !m_pOutput )
	{
		pAdapter->Release();
		return false;
	}

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory( &sd, sizeof( sd ) );

	sd.BufferDesc.Width =
		mode.m_DisplayMode.m_nWidth;

	sd.BufferDesc.Height =
		mode.m_DisplayMode.m_nHeight;

	sd.BufferDesc.Format =
		DXGI_FORMAT_R8G8B8A8_UNORM;

	sd.BufferDesc.RefreshRate.Numerator =
		mode.m_DisplayMode.m_nRefreshRateNumerator;

	sd.BufferDesc.RefreshRate.Denominator =
		mode.m_DisplayMode.m_nRefreshRateDenominator;

	sd.BufferUsage =
		DXGI_USAGE_RENDER_TARGET_OUTPUT;

	sd.BufferCount =
		mode.m_nBackBufferCount > 0
			? mode.m_nBackBufferCount
			: 1;

	sd.OutputWindow =
		(HWND)hWnd;

	sd.Windowed =
		mode.m_bWindowed ? TRUE : FALSE;

	sd.Flags =
		mode.m_bWindowed
			? 0
			: DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	sd.SwapEffect =
		sd.BufferCount > 1
			? DXGI_SWAP_EFFECT_SEQUENTIAL
			: DXGI_SWAP_EFFECT_DISCARD;

	sd.SampleDesc.Count =
		mode.m_nAASamples
			? mode.m_nAASamples
			: 1;

	sd.SampleDesc.Quality =
		mode.m_nAAQuality;

	UINT nDeviceFlags = 0;

#ifdef _DEBUG
	nDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	HRESULT hr =
		D3D11CreateDeviceAndSwapChain(
			pAdapter,
			D3D_DRIVER_TYPE_UNKNOWN,
			NULL,
			nDeviceFlags,
			NULL,
			0,
			D3D11_SDK_VERSION,
			&sd,
			&m_pSwapChain,
			&m_pDevice,
			NULL,
			&m_pDeviceContext
		);

	pAdapter->Release();

	if ( FAILED( hr ) )
	{
		ShutdownDevice();
		return false;
	}

	ID3D11Texture2D *pBackBuffer = NULL;

	hr =
		m_pSwapChain->GetBuffer(
			0,
			__uuidof( ID3D11Texture2D ),
			(LPVOID*)&pBackBuffer
		);

	if ( FAILED( hr ) )
	{
		ShutdownDevice();
		return false;
	}

	hr =
		m_pDevice->CreateRenderTargetView(
			pBackBuffer,
			NULL,
			&m_pRenderTargetView
		);

	pBackBuffer->Release();

	if ( FAILED( hr ) )
	{
		ShutdownDevice();
		return false;
	}

	D3D11_TEXTURE2D_DESC depthDesc;
	ZeroMemory(
		&depthDesc,
		sizeof( depthDesc )
	);

	depthDesc.Width =
		mode.m_DisplayMode.m_nWidth;

	depthDesc.Height =
		mode.m_DisplayMode.m_nHeight;

	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;

	depthDesc.Format =
		DXGI_FORMAT_D24_UNORM_S8_UINT;

	depthDesc.SampleDesc =
		sd.SampleDesc;

	depthDesc.Usage =
		D3D11_USAGE_DEFAULT;

	depthDesc.BindFlags =
		D3D11_BIND_DEPTH_STENCIL;

	hr =
		m_pDevice->CreateTexture2D(
			&depthDesc,
			NULL,
			&m_pDepthStencil
		);

	if ( FAILED( hr ) )
	{
		ShutdownDevice();
		return false;
	}

	hr =
		m_pDevice->CreateDepthStencilView(
			m_pDepthStencil,
			NULL,
			&m_pDepthStencilView
		);

	if ( FAILED( hr ) )
	{
		ShutdownDevice();
		return false;
	}

	m_pDeviceContext->OMSetRenderTargets(
		1,
		&m_pRenderTargetView,
		m_pDepthStencilView
	);

	m_hWnd = hWnd;
	m_nAdapter = nAdapter;

	m_ViewHWnd = hWnd;

	GetWindowSize(
		m_nWindowWidth,
		m_nWindowHeight
	);

	// Hardware caps are already supplied by the DX11 device manager.
	// The legacy CHardwareConfig global is not owned by the DX11 backend.

	return true;
}


//-----------------------------------------------------------------------------
// Shuts down the mode
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ShutdownDevice()
{
	if ( m_pDeviceContext )
	{
		m_pDeviceContext->ClearState();
		// Flush before release to finish pending work.
		m_pDeviceContext->Flush();
	}

	if ( m_pDepthStencilView )
	{
		m_pDepthStencilView->Release();
		m_pDepthStencilView = NULL;
	}

	if ( m_pDepthStencil )
	{
		m_pDepthStencil->Release();
		m_pDepthStencil = NULL;
	}

	if ( m_pRenderTargetView )
	{
		m_pRenderTargetView->Release();
		m_pRenderTargetView = NULL;
	}

	if ( m_pSwapChain )
	{
		m_pSwapChain->Release();
		m_pSwapChain = NULL;
	}

	if ( m_pDeviceContext )
	{
		m_pDeviceContext->Release();
		m_pDeviceContext = NULL;
	}

	if ( m_pDevice )
	{
		m_pDevice->Release();
		m_pDevice = NULL;
	}

	if ( m_pOutput )
	{
		m_pOutput->Release();
		m_pOutput = NULL;
	}

	m_hWnd = NULL;

	m_nAdapter = -1;

	m_nWindowWidth = 0;
	m_nWindowHeight = 0;
}


//-----------------------------------------------------------------------------
// Are we using graphics?
//-----------------------------------------------------------------------------
bool CShaderDeviceDx11::IsUsingGraphics() const
{
	return m_nAdapter >= 0;
}


//-----------------------------------------------------------------------------
// Returns adapter
//-----------------------------------------------------------------------------
int CShaderDeviceDx11::GetCurrentAdapter() const
{
	return m_nAdapter;
}


//-----------------------------------------------------------------------------
// Get back buffer information
//-----------------------------------------------------------------------------
ImageFormat CShaderDeviceDx11::GetBackBufferFormat() const
{
	return IMAGE_FORMAT_RGB888;
}

void CShaderDeviceDx11::GetBackBufferDimensions(
	int& width,
	int& height ) const
{
	width = m_nWindowWidth;
	height = m_nWindowHeight;
}


//-----------------------------------------------------------------------------
// Display device name
//-----------------------------------------------------------------------------
char *CShaderDeviceDx11::GetDisplayDeviceName()
{
	static char s_szDisplayDeviceName[256];

	s_szDisplayDeviceName[0] = 0;

	IDXGIAdapter *pAdapter =
		g_ShaderDeviceMgrDx11.GetAdapter( m_nAdapter );

	if ( !pAdapter )
		return s_szDisplayDeviceName;

	DXGI_ADAPTER_DESC desc;
	HRESULT hr =
		pAdapter->GetDesc( &desc );

	pAdapter->Release();

	if ( FAILED( hr ) )
		return s_szDisplayDeviceName;

	Q_UnicodeToUTF8(
		desc.Description,
		s_szDisplayDeviceName,
		sizeof( s_szDisplayDeviceName )
	);

	return s_szDisplayDeviceName;
}


//-----------------------------------------------------------------------------
// Use this to spew information about 3D layer
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::SpewDriverInfo() const
{
	Warning(
		"Dx11 Driver: adapter %d\n",
		m_nAdapter
	);
}


//-----------------------------------------------------------------------------
// Swap buffers
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::Present()
{
	if ( !m_pSwapChain )
		return;

	HRESULT hr =
		m_pSwapChain->Present(
			0,
			0
		);

	if ( FAILED( hr ) )
	{
		Assert( 0 );
	}
}


//-----------------------------------------------------------------------------
// Gamma ramp
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::SetHardwareGammaRamp(
	float fGamma,
	float fGammaTVRangeMin,
	float fGammaTVRangeMax,
	float fGammaTVExponent,
	bool bTVEnabled )
{
	DevMsg(
		"SetHardwareGammaRamp( %f )\n",
		fGamma
	);

	( void )fGammaTVRangeMin;
	( void )fGammaTVRangeMax;
	( void )fGammaTVExponent;
	( void )bTVEnabled;

	Assert( m_pOutput );

	if ( !m_pOutput )
		return;

	float flMin =
		g_pHardwareConfig->Caps().m_flMinGammaControlPoint;

	float flMax =
		g_pHardwareConfig->Caps().m_flMaxGammaControlPoint;

	int nGammaPoints =
		g_pHardwareConfig->Caps().m_nGammaControlPointCount;

	if ( nGammaPoints < 2 )
		return;

	DXGI_GAMMA_CONTROL gammaControl;

	ZeroMemory(
		&gammaControl,
		sizeof( gammaControl )
	);

	gammaControl.Scale.Red =
		gammaControl.Scale.Green =
		gammaControl.Scale.Blue = 1.0f;

	gammaControl.Offset.Red =
		gammaControl.Offset.Green =
		gammaControl.Offset.Blue = 0.0f;

	float flOOCount =
		1.0f / ( nGammaPoints - 1 );

	for ( int i = 0; i < nGammaPoints; i++ )
	{
		float flGamma22 =
			i * flOOCount;

		float flCorrection =
			powf(
				flGamma22,
				fGamma / 2.2f
			);

		flCorrection =
			clamp(
				flCorrection,
				flMin,
				flMax
			);

		gammaControl.GammaCurve[i].Red =
			flCorrection;

		gammaControl.GammaCurve[i].Green =
			flCorrection;

		gammaControl.GammaCurve[i].Blue =
			flCorrection;
	}

	HRESULT hr =
		m_pOutput->SetGammaControl(
			&gammaControl
		);

	if ( FAILED( hr ) )
	{
		Warning(
			"CShaderDeviceDx11::SetHardwareGammaRamp: "
			"Unable to set gamma controls!\n"
		);
	}
}


//-----------------------------------------------------------------------------
// Compiles all manner of shaders
//-----------------------------------------------------------------------------
IShaderBuffer* CShaderDeviceDx11::CompileShader(
	const char *pProgram,
	size_t nBufLen,
	const char *pShaderVersion )
{
	if ( !pProgram || nBufLen == 0 )
		return NULL;

	UINT nCompileFlags =
		D3DCOMPILE_AVOID_FLOW_CONTROL |
		D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;

#ifdef _DEBUG
	nCompileFlags |= D3DCOMPILE_DEBUG;
#endif

	const char *pProfile =
		pShaderVersion;

	if ( pProfile &&
		!Q_stricmp( pProfile, "vs_3_0" ) )
	{
		pProfile = "vs_4_0";
	}
	else if ( pProfile &&
		!Q_stricmp( pProfile, "ps_3_0" ) )
	{
		pProfile = "ps_4_0";
	}
	else if ( pProfile &&
		!Q_stricmp( pProfile, "gs_3_0" ) )
	{
		pProfile = "gs_4_0";
	}

	if ( !pProfile || !pProfile[0] )
	{
		pProfile = "vs_4_0";
	}

	ID3DBlob *pCompiledShader = NULL;
	ID3DBlob *pErrorMessages = NULL;

	HRESULT hr =
		D3DCompile(
			pProgram,
			nBufLen,
			"SourceMaterialShader",
			NULL,
			NULL,
			"main",
			pProfile,
			nCompileFlags,
			0,
			&pCompiledShader,
			&pErrorMessages
		);

	if ( FAILED( hr ) )
	{
		if ( pErrorMessages )
		{
			const char *pErrorMessage =
				(const char*)pErrorMessages->GetBufferPointer();

			Warning(
				"Shader compilation failed:\n%s\n",
				pErrorMessage
			);

			pErrorMessages->Release();
		}

		return NULL;
	}

	CShaderBuffer< ID3DBlob > *pShaderBuffer =
		new CShaderBuffer< ID3DBlob >(
			pCompiledShader
		);

	if ( pErrorMessages )
	{
		pErrorMessages->Release();
	}

	return pShaderBuffer;
}


//-----------------------------------------------------------------------------
// Release input layouts
//-----------------------------------------------------------------------------
void CShaderDeviceDx11::ReleaseInputLayouts(
	VertexShaderIndex_t nIndex )
{
	InputLayoutDict_t &dict =
		m_VertexShaderDict[nIndex].m_InputLayouts;

	unsigned short hCurr =
		dict.FirstInorder();

	while ( hCurr != dict.InvalidIndex() )
	{
		if ( dict[hCurr].m_pInputLayout )
		{
			dict[hCurr].m_pInputLayout->Release();
			dict[hCurr].m_pInputLayout = NULL;
		}

		hCurr =
			dict.NextInorder( hCurr );
	}
}


//-----------------------------------------------------------------------------
// Create, destroy vertex shader
//-----------------------------------------------------------------------------
VertexShaderHandle_t CShaderDeviceDx11::CreateVertexShader(
	IShaderBuffer* pShaderBuffer )
{
	if ( !m_pDevice || !pShaderBuffer )
		return VERTEX_SHADER_HANDLE_INVALID;

	ID3D11VertexShader *pShader = NULL;

	HRESULT hr =
		m_pDevice->CreateVertexShader(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			NULL,
			&pShader
		);

	if ( FAILED( hr ) || !pShader )
		return VERTEX_SHADER_HANDLE_INVALID;

	ID3D11ShaderReflection *pInfo = NULL;

	hr =
		D3DReflect(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			__uuidof( ID3D11ShaderReflection ),
			reinterpret_cast<void**>( &pInfo )
		);

	if ( FAILED( hr ) || !pInfo )
	{
		pShader->Release();
		return VERTEX_SHADER_HANDLE_INVALID;
	}

	VertexShaderIndex_t i =
		m_VertexShaderDict.AddToTail();

	VertexShader_t &dict =
		m_VertexShaderDict[i];

	dict.m_pShader = pShader;
	dict.m_pInfo = pInfo;

	dict.m_nByteCodeLen =
		pShaderBuffer->GetSize();

	dict.m_pByteCode =
		new unsigned char[dict.m_nByteCodeLen];

	memcpy(
		dict.m_pByteCode,
		pShaderBuffer->GetBits(),
		dict.m_nByteCodeLen
	);

	return (VertexShaderHandle_t)i;
}

void CShaderDeviceDx11::DestroyVertexShader(
	VertexShaderHandle_t hShader )
{
	if ( hShader == VERTEX_SHADER_HANDLE_INVALID )
		return;

	if ( g_pShaderAPIDx11 )
	{
		g_pShaderAPIDx11->Unbind( hShader );
	}

	VertexShaderIndex_t i =
		(VertexShaderIndex_t)hShader;

	if ( i < 0 || i >= m_VertexShaderDict.Count() )
		return;

	VertexShader_t &dict =
		m_VertexShaderDict[i];

	if ( dict.m_pShader )
	{
		VerifyEquals(
			dict.m_pShader->Release(),
			0
		);
	}

	if ( dict.m_pInfo )
	{
		VerifyEquals(
			dict.m_pInfo->Release(),
			0
		);
	}

	delete[] dict.m_pByteCode;
	dict.m_pByteCode = NULL;

	ReleaseInputLayouts( i );

	m_VertexShaderDict.Remove( i );
}


//-----------------------------------------------------------------------------
// Create, destroy geometry shader
//-----------------------------------------------------------------------------
GeometryShaderHandle_t CShaderDeviceDx11::CreateGeometryShader(
	IShaderBuffer* pShaderBuffer )
{
	if ( !m_pDevice || !pShaderBuffer )
		return GEOMETRY_SHADER_HANDLE_INVALID;

	ID3D11GeometryShader *pShader = NULL;

	HRESULT hr =
		m_pDevice->CreateGeometryShader(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			NULL,
			&pShader
		);

	if ( FAILED( hr ) || !pShader )
		return GEOMETRY_SHADER_HANDLE_INVALID;

	ID3D11ShaderReflection *pInfo = NULL;

	hr =
		D3DReflect(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			__uuidof( ID3D11ShaderReflection ),
			reinterpret_cast<void**>( &pInfo )
		);

	if ( FAILED( hr ) || !pInfo )
	{
		pShader->Release();
		return GEOMETRY_SHADER_HANDLE_INVALID;
	}

	GeometryShaderIndex_t i =
		m_GeometryShaderDict.AddToTail();

	m_GeometryShaderDict[i].m_pShader =
		pShader;

	m_GeometryShaderDict[i].m_pInfo =
		pInfo;

	return (GeometryShaderHandle_t)i;
}

void CShaderDeviceDx11::DestroyGeometryShader(
	GeometryShaderHandle_t hShader )
{
	if ( hShader == GEOMETRY_SHADER_HANDLE_INVALID )
		return;

	if ( g_pShaderAPIDx11 )
	{
		g_pShaderAPIDx11->Unbind( hShader );
	}

	GeometryShaderIndex_t i =
		(GeometryShaderIndex_t)hShader;

	if ( i < 0 || i >= m_GeometryShaderDict.Count() )
		return;

	if ( m_GeometryShaderDict[i].m_pShader )
	{
		VerifyEquals(
			m_GeometryShaderDict[i].m_pShader->Release(),
			0
		);
	}

	if ( m_GeometryShaderDict[i].m_pInfo )
	{
		VerifyEquals(
			m_GeometryShaderDict[i].m_pInfo->Release(),
			0
		);
	}

	m_GeometryShaderDict.Remove( i );
}


//-----------------------------------------------------------------------------
// Create, destroy pixel shader
//-----------------------------------------------------------------------------
PixelShaderHandle_t CShaderDeviceDx11::CreatePixelShader(
	IShaderBuffer* pShaderBuffer )
{
	if ( !m_pDevice || !pShaderBuffer )
		return PIXEL_SHADER_HANDLE_INVALID;

	ID3D11PixelShader *pShader = NULL;

	HRESULT hr =
		m_pDevice->CreatePixelShader(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			NULL,
			&pShader
		);

	if ( FAILED( hr ) || !pShader )
		return PIXEL_SHADER_HANDLE_INVALID;

	ID3D11ShaderReflection *pInfo = NULL;

	hr =
		D3DReflect(
			pShaderBuffer->GetBits(),
			pShaderBuffer->GetSize(),
			__uuidof( ID3D11ShaderReflection ),
			reinterpret_cast<void**>( &pInfo )
		);

	if ( FAILED( hr ) || !pInfo )
	{
		pShader->Release();
		return PIXEL_SHADER_HANDLE_INVALID;
	}

	PixelShaderIndex_t i =
		m_PixelShaderDict.AddToTail();

	m_PixelShaderDict[i].m_pShader =
		pShader;

	m_PixelShaderDict[i].m_pInfo =
		pInfo;

	return (PixelShaderHandle_t)i;
}

void CShaderDeviceDx11::DestroyPixelShader(
	PixelShaderHandle_t hShader )
{
	if ( hShader == PIXEL_SHADER_HANDLE_INVALID )
		return;

	if ( g_pShaderAPIDx11 )
	{
		g_pShaderAPIDx11->Unbind( hShader );
	}

	PixelShaderIndex_t i =
		(PixelShaderIndex_t)hShader;

	if ( i < 0 || i >= m_PixelShaderDict.Count() )
		return;

	if ( m_PixelShaderDict[i].m_pShader )
	{
		VerifyEquals(
			m_PixelShaderDict[i].m_pShader->Release(),
			0
		);
	}

	if ( m_PixelShaderDict[i].m_pInfo )
	{
		VerifyEquals(
			m_PixelShaderDict[i].m_pInfo->Release(),
			0
		);
	}

	m_PixelShaderDict.Remove( i );
}


//-----------------------------------------------------------------------------
// Finds or creates input layout
//-----------------------------------------------------------------------------
ID3D11InputLayout* CShaderDeviceDx11::GetInputLayout(
	VertexShaderHandle_t hShader,
	VertexFormat_t format )
{
	if ( hShader == VERTEX_SHADER_HANDLE_INVALID )
		return NULL;

	VertexShaderIndex_t i =
		(VertexShaderIndex_t)hShader;

	if ( i < 0 || i >= m_VertexShaderDict.Count() )
		return NULL;

	InputLayout_t insert;
	memset( &insert, 0, sizeof( insert ) );

	insert.m_VertexFormat = format;
	insert.m_pInputLayout = NULL;

	InputLayoutDict_t &dict =
		m_VertexShaderDict[i].m_InputLayouts;

	unsigned short hIndex =
		dict.Find( insert );

	if ( hIndex != dict.InvalidIndex() )
	{
		return dict[hIndex].m_pInputLayout;
	}

	VertexShader_t &shader =
		m_VertexShaderDict[i];

	insert.m_pInputLayout =
		CreateInputLayout(
			format,
			shader.m_pInfo,
			shader.m_pByteCode,
			shader.m_nByteCodeLen
		);

	dict.Insert( insert );

	return insert.m_pInputLayout;
}


//-----------------------------------------------------------------------------
// Creates / destroys Mesh
//-----------------------------------------------------------------------------
IMesh* CShaderDeviceDx11::CreateStaticMesh(
	VertexFormat_t vertexFormat,
	const char *pBudgetGroup,
	IMaterial *pMaterial )
{
	LOCK_SHADERAPI();

	(void)vertexFormat;
	(void)pBudgetGroup;
	(void)pMaterial;

	// Still intentionally deferred until the DX11 mesh implementation is wired.
	return NULL;
}

void CShaderDeviceDx11::DestroyStaticMesh(
	IMesh* pMesh )
{
	LOCK_SHADERAPI();

	(void)pMesh;
}


//-----------------------------------------------------------------------------
// Vertex / index buffers
//-----------------------------------------------------------------------------
IVertexBuffer *CShaderDeviceDx11::CreateVertexBuffer(
	ShaderBufferType_t type,
	VertexFormat_t fmt,
	int nVertexCount,
	const char *pBudgetGroup )
{
	LOCK_SHADERAPI();

	CVertexBufferDx11 *pVertexBuffer =
		new CVertexBufferDx11(
			type,
			fmt,
			nVertexCount,
			pBudgetGroup
		);

	return pVertexBuffer;
}

void CShaderDeviceDx11::DestroyVertexBuffer(
	IVertexBuffer *pVertexBuffer )
{
	LOCK_SHADERAPI();

	if ( !pVertexBuffer )
		return;

	CVertexBufferDx11 *pVertexBufferBase =
		assert_cast<CVertexBufferDx11*>( pVertexBuffer );

	if ( g_pShaderAPIDx11 )
	{
		g_pShaderAPIDx11->UnbindVertexBuffer(
			pVertexBufferBase->GetDx11Buffer()
		);
	}

	delete pVertexBufferBase;
}

IIndexBuffer *CShaderDeviceDx11::CreateIndexBuffer(
	ShaderBufferType_t type,
	MaterialIndexFormat_t fmt,
	int nIndexCount,
	const char *pBudgetGroup )
{
	LOCK_SHADERAPI();

	CIndexBufferDx11 *pIndexBuffer =
		new CIndexBufferDx11(
			type,
			fmt,
			nIndexCount,
			pBudgetGroup
		);

	return pIndexBuffer;
}

void CShaderDeviceDx11::DestroyIndexBuffer(
	IIndexBuffer *pIndexBuffer )
{
	LOCK_SHADERAPI();

	if ( !pIndexBuffer )
		return;

	CIndexBufferDx11 *pIndexBufferBase =
		assert_cast<CIndexBufferDx11*>( pIndexBuffer );

	if ( g_pShaderAPIDx11 )
	{
		g_pShaderAPIDx11->UnbindIndexBuffer(
			pIndexBufferBase->GetDx11Buffer()
		);
	}

	delete pIndexBufferBase;
}

IVertexBuffer *CShaderDeviceDx11::GetDynamicVertexBuffer(
	int nStreamID,
	VertexFormat_t vertexFormat,
	bool bBuffered )
{
	LOCK_SHADERAPI();

	(void)nStreamID;
	(void)vertexFormat;
	(void)bBuffered;

	return NULL;
}

IIndexBuffer *CShaderDeviceDx11::GetDynamicIndexBuffer(
	MaterialIndexFormat_t fmt,
	bool bBuffered )
{
	LOCK_SHADERAPI();

	(void)fmt;
	(void)bBuffered;

	return NULL;
}