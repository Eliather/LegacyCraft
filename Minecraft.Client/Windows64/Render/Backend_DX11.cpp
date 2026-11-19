#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

#include "RenderCommon.h"
#include "iggy.h"
#include "gdraw.h"
#include "gdraw_d3d11.h"

using namespace C4JRenderCommon;

#define C4J_D3D11_RING_VB_BYTES (4u * 1024u * 1024u)
#define C4J_DX11_CBUFF_UPLOAD_BUDGET_BYTES (2u * 1024u * 1024u)
#define C4J_DX11_CBUFF_UPLOAD_BUDGET_COUNT 64u

namespace
{
	static HWND                     s_hwnd = NULL;
	static int                      s_width = 1280;
	static int                      s_height = 720;
	static bool                     s_fullscreen = false;
	static bool                     s_vsync = false;
	static bool                     s_flipModel = false;
	static bool                     s_allowTearing = false;
	static char                     s_adapterName[128] = "DirectX 11 Hardware";

	static ID3D11Device            *s_pDevice = NULL;
	static ID3D11DeviceContext     *s_pContext = NULL;
	static IDXGISwapChain          *s_pSwapChain = NULL;
	static IDXGISwapChain1         *s_pSwapChain1 = NULL;
	static ID3D11RenderTargetView  *s_pRTV = NULL;
	static ID3D11Texture2D         *s_pDSBuffer = NULL;
	static ID3D11DepthStencilView  *s_pDSV = NULL;

	static float                    s_clearColour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	static std::atomic<int>         s_nextTextureId(100);
	static int                      s_nextCBuffId = 1;
	static uint64_t                 s_nextCBuffGen = 1;
	static std::atomic<size_t>      s_totalCBuffBytes(0);
	static bool                     s_suspended = false;

	struct DX11TextureData
	{
		ID3D11Texture2D          *pTexture;
		ID3D11ShaderResourceView *pSRV;
		int  width;
		int  height;
		int  mipLevels;
		bool linearFilter;
		bool clampWrap;
	};

	struct DX11BakedVBSet
	{
		ID3D11Buffer *pVB = NULL;
		std::vector<UINT> offsets;
	};

	struct DX11CBuffSlot
	{
		std::shared_ptr<const std::vector<CBuffCommand>> commands;
		size_t byteSize = 0;
		uint64_t generation = 0;
		uint64_t uploadedGen = 0;
		std::shared_ptr<const DX11BakedVBSet> gpuVBs;
	};

	static std::unordered_map<int, DX11TextureData> s_textures;
	static std::unordered_map<int, DX11CBuffSlot>   s_cbuffs;
	static std::vector<std::shared_ptr<const DX11BakedVBSet>> s_pendingReleaseVBSets;
	static std::mutex                               s_cbuffMutex;
	static std::recursive_mutex                     s_d3dMutex;
	static size_t                                   s_frameCBuffUploadBytes = 0;
	static UINT                                     s_frameCBuffUploadCount = 0;

	thread_local static ThreadRenderState s_tls;

	/* Pipeline resources */
	static ID3D11VertexShader *s_pVSStandard = NULL;
	static ID3D11VertexShader *s_pVSCompressed = NULL;
	static ID3D11PixelShader  *s_pPS = NULL;
	static ID3D11InputLayout  *s_pInputLayoutStandard = NULL;
	static ID3D11InputLayout  *s_pInputLayoutCompressed = NULL;
	static ID3D11Buffer       *s_pRingVB = NULL;
	static UINT                s_ringVBOffset = 0;
	static ID3D11Buffer       *s_pQuadIB = NULL;
	static ID3D11Buffer       *s_pFanIB = NULL;
	static ID3D11Buffer       *s_pVSCB = NULL;
	static ID3D11Buffer       *s_pPSCB = NULL;
	static ID3D11SamplerState *s_pSamplers[4] = { NULL, NULL, NULL, NULL };

	static std::unordered_map<uint32_t, ID3D11BlendState*>        s_blendCache;
	static std::unordered_map<uint64_t, ID3D11DepthStencilState*> s_dsCache;
	static std::unordered_map<uint64_t, ID3D11RasterizerState*>   s_rasterCache;

	/* State shadow cache (eliminates redundant D3D calls) */
	static bool                    s_cacheValid = false;
	static bool                    s_cachedCompressed = false;
	static ID3D11Buffer           *s_cachedVB = NULL;
	static UINT                    s_cachedVBStride = 0;
	static UINT                    s_cachedVBOffset = 0;
	static ID3D11Buffer           *s_cachedIB = NULL;
	static D3D11_PRIMITIVE_TOPOLOGY s_cachedTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	static int                     s_cachedTexId = -1;
	static ID3D11ShaderResourceView *s_cachedTexLookupSRV = NULL;
	static bool                    s_cachedTexLinear = false;
	static bool                    s_cachedTexClamp = false;
	static int                     s_cachedLightTexId = -1;
	static ID3D11ShaderResourceView *s_cachedLightLookupSRV = NULL;
	static ID3D11ShaderResourceView *s_cachedSRV0 = (ID3D11ShaderResourceView*)(uintptr_t)1;
	static ID3D11ShaderResourceView *s_cachedSRV1 = (ID3D11ShaderResourceView*)(uintptr_t)1;
	static int                     s_cachedSamplerIdx = -1;
	static uint32_t                s_cachedBlendKey = 0xFFFFFFFFu;
	static ID3D11BlendState       *s_cachedBlend = NULL;
	static unsigned int            s_cachedBlendFactor = 0;
	static bool                    s_cachedBlendFactorUsesAlpha = false;
	static uint64_t                s_cachedDSKey = ~0ULL;
	static ID3D11DepthStencilState *s_cachedDS = NULL;
	static uint8_t                 s_cachedStencilRef = 0;
	static uint64_t                s_cachedRasterKey = ~0ULL;
	static ID3D11RasterizerState  *s_cachedRS = NULL;
	static VSConstantBufferData    s_lastVSConsts = {};
	static PSConstantBufferData    s_lastPSConsts = {};
	static bool                    s_lastConstsValid = false;
	static int                     s_currentViewportType = 0;
	static D3D11_VIEWPORT          s_activeViewport = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };

	static const char *s_hlslVS =
		"cbuffer VSParams : register(b0)\n"
		"{\n"
		"    matrix u_WVP;\n"
		"    matrix u_MV;\n"
		"    matrix u_TexMatrix;\n"
		"    float4 u_TexGen[4];\n"
		"    float4 u_LightDir[2];\n"
		"    float4 u_LightColor[2];\n"
		"    float4 u_LightAmbient;\n"
		"    float2 u_GlobalLightUV;\n"
		"    int    u_LightingEnable;\n"
		"    int    u_UseTexGen;\n"
		"};\n"
		"struct VS_STD_IN\n"
		"{\n"
		"    float3 pos   : POSITION;\n"
		"    float2 uv    : TEXCOORD0;\n"
		"    uint   col   : COLOR0;\n"
		"    uint   norm  : NORMAL0;\n"
		"    uint   extra : TEXCOORD1;\n"
		"};\n"
		"struct VS_COMP_IN\n"
		"{\n"
		"    int4 a : TEXCOORD0;\n"
		"    int4 b : TEXCOORD1;\n"
		"};\n"
		"struct PS_IN\n"
		"{\n"
		"    float4 pos     : SV_POSITION;\n"
		"    float4 uv      : TEXCOORD0;\n"
		"    float2 lightUV : TEXCOORD1;\n"
		"    float  fogDist : TEXCOORD2;\n"
		"    float4 col     : COLOR0;\n"
		"};\n"
		"float3 ApplyLighting(float3 viewNormal)\n"
		"{\n"
		"    float3 acc = u_LightAmbient.rgb;\n"
		"    [unroll]\n"
		"    for (int i = 0; i < 2; ++i)\n"
		"    {\n"
		"        if (u_LightColor[i].w > 0.5f)\n"
		"        {\n"
		"            float ndotl = max(0.0f, dot(viewNormal, normalize(u_LightDir[i].xyz)));\n"
		"            acc += u_LightColor[i].rgb * ndotl;\n"
		"        }\n"
		"    }\n"
		"    return saturate(acc);\n"
		"}\n"
		"PS_IN VSMainStandard(VS_STD_IN input)\n"
		"{\n"
		"    PS_IN output;\n"
		"    float4 pos4 = float4(input.pos, 1.0f);\n"
		"    output.pos = mul(u_WVP, pos4);\n"
		"    output.pos.z = output.pos.z * 0.5f + output.pos.w * 0.5f;\n"
		"    float4 viewPos = mul(u_MV, pos4);\n"
		"    output.fogDist = length(viewPos.xyz);\n"
		"    if (u_UseTexGen != 0)\n"
		"    {\n"
		"        float4 g = float4(\n"
		"            dot(pos4, u_TexGen[0]),\n"
		"            dot(pos4, u_TexGen[1]),\n"
		"            dot(pos4, u_TexGen[2]),\n"
		"            dot(viewPos, u_TexGen[3]));\n"
		"        output.uv = mul(u_TexMatrix, g);\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"        float2 uv = input.uv;\n"
		"        if (uv.x > 1.0f && uv.x <= 2.0f) uv.x -= 1.0f;\n"
		"        float4 tuv = mul(u_TexMatrix, float4(uv, 0.0f, 1.0f));\n"
		"        output.uv = float4(tuv.xy, 0.0f, 1.0f);\n"
		"    }\n"
		"    if (input.extra == 0xfe00fe00u)\n"
		"    {\n"
		"        output.lightUV = u_GlobalLightUV + (0.5f / 16.0f);\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"        float lu = float(input.extra & 0xFFFFu);\n"
		"        float lv = float((input.extra >> 16) & 0xFFFFu);\n"
		"        output.lightUV = (float2(lu, lv) + 8.0f) / 256.0f;\n"
		"    }\n"
		"    float a = float(input.col & 0xFFu) / 255.0f;\n"
		"    float b = float((input.col >> 8) & 0xFFu) / 255.0f;\n"
		"    float g = float((input.col >> 16) & 0xFFu) / 255.0f;\n"
		"    float r = float((input.col >> 24) & 0xFFu) / 255.0f;\n"
		"    float4 outCol = float4(r, g, b, a);\n"
		"    if (u_LightingEnable != 0)\n"
		"    {\n"
		"        int nx = (int)(input.norm << 24) >> 24;\n"
		"        int ny = (int)(input.norm << 16) >> 24;\n"
		"        int nz = (int)(input.norm << 8) >> 24;\n"
		"        float3 nrm = float3(float(nx), float(ny), float(nz)) / 127.0f;\n"
		"        float nlen = length(nrm);\n"
		"        if (nlen > 1e-4f)\n"
		"        {\n"
		"            float3 viewNorm = mul((float3x3)u_MV, nrm / nlen);\n"
		"            float vnLen = length(viewNorm);\n"
		"            if (vnLen > 1e-4f) viewNorm /= vnLen;\n"
		"            outCol.rgb *= ApplyLighting(viewNorm);\n"
		"        }\n"
		"    }\n"
		"    output.col = outCol;\n"
		"    return output;\n"
		"}\n"
		"PS_IN VSMainCompressed(VS_COMP_IN input)\n"
		"{\n"
		"    PS_IN output;\n"
		"    float4 pos4 = float4(float3(input.a.xyz) / 1024.0f, 1.0f);\n"
		"    output.pos = mul(u_WVP, pos4);\n"
		"    output.pos.z = output.pos.z * 0.5f + output.pos.w * 0.5f;\n"
		"    float4 viewPos = mul(u_MV, pos4);\n"
		"    output.fogDist = length(viewPos.xyz);\n"
		"    float2 uv = float2(input.b.xy) / 8192.0f;\n"
		"    if (uv.x > 1.0f && uv.x <= 2.0f) uv.x -= 1.0f;\n"
		"    float4 tuv = mul(u_TexMatrix, float4(uv, 0.0f, 1.0f));\n"
		"    output.uv = float4(tuv.xy, 0.0f, 1.0f);\n"
		"    output.lightUV = (float2(input.b.zw) + 8.0f) / 256.0f;\n"
		"    uint col16 = ((uint)input.a.w & 0xFFFFu) ^ 0x8000u;\n"
		"    float r = float((col16 >> 11) & 31u) / 31.0f;\n"
		"    float g = float((col16 >> 5) & 63u) / 63.0f;\n"
		"    float b = float(col16 & 31u) / 31.0f;\n"
		"    output.col = float4(r, g, b, 1.0f);\n"
		"    return output;\n"
		"}\n";

	static const char *s_hlslPS =
		"Texture2D g_Texture  : register(t0);\n"
		"Texture2D g_Lightmap : register(t1);\n"
		"SamplerState g_Sampler      : register(s0);\n"
		"SamplerState g_LightSampler : register(s1);\n"
		"cbuffer PSParams : register(b0)\n"
		"{\n"
		"    float4 u_StateColor;\n"
		"    int    u_HasTexture;\n"
		"    int    u_HasLightmap;\n"
		"    int    u_AlphaFunc;\n"
		"    float  u_AlphaCutoff;\n"
		"    float4 u_FogColor;\n"
		"    int    u_FogMode;\n"
		"    float  u_FogNear;\n"
		"    float  u_FogFar;\n"
		"    float  u_FogDensity;\n"
		"    int    u_UseProjection;\n"
		"    float3 u_Pad;\n"
		"};\n"
		"struct PS_IN\n"
		"{\n"
		"    float4 pos     : SV_POSITION;\n"
		"    float4 uv      : TEXCOORD0;\n"
		"    float2 lightUV : TEXCOORD1;\n"
		"    float  fogDist : TEXCOORD2;\n"
		"    float4 col     : COLOR0;\n"
		"};\n"
		"bool PassAlphaTest(float a)\n"
		"{\n"
		"    if (u_AlphaFunc == 5) return a >  u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 7) return a >= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 2) return a <  u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 4) return a <= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 3) return abs(a - u_AlphaCutoff) < 0.001f;\n"
		"    if (u_AlphaFunc == 6) return abs(a - u_AlphaCutoff) >= 0.001f;\n"
		"    if (u_AlphaFunc == 1) return false;\n"
		"    return true;\n"
		"}\n"
		"float4 PSMain(PS_IN input) : SV_TARGET\n"
		"{\n"
		"    float4 finalCol = input.col * u_StateColor;\n"
		"    if (u_HasTexture != 0)\n"
		"    {\n"
		"        float2 texUV = (u_UseProjection != 0 && abs(input.uv.w) > 1e-5f) ? (input.uv.xy / input.uv.w) : input.uv.xy;\n"
		"        float4 texCol = g_Texture.Sample(g_Sampler, texUV);\n"
		"        finalCol *= texCol;\n"
		"    }\n"
		"    if (u_HasLightmap != 0)\n"
		"    {\n"
		"        float4 lmCol = g_Lightmap.Sample(g_LightSampler, input.lightUV);\n"
		"        finalCol.rgb *= lmCol.rgb;\n"
		"    }\n"
		"    if (u_AlphaFunc != 8 && !PassAlphaTest(finalCol.a))\n"
		"    {\n"
		"        discard;\n"
		"    }\n"
		"    if (u_FogColor.w > 0.5f)\n"
		"    {\n"
		"        float fogFactor = 1.0f;\n"
		"        if (u_FogMode == 2)\n"
		"        {\n"
		"            fogFactor = exp(-u_FogDensity * input.fogDist);\n"
		"        }\n"
		"        else\n"
		"        {\n"
		"            float denom = max(u_FogFar - u_FogNear, 0.0001f);\n"
		"            fogFactor = (u_FogFar - input.fogDist) / denom;\n"
		"        }\n"
		"        fogFactor = saturate(fogFactor);\n"
		"        finalCol.rgb = lerp(u_FogColor.rgb, finalCol.rgb, fogFactor);\n"
		"    }\n"
		"    return finalCol;\n"
		"}\n";

	static void InvalidateStateCache()
	{
		s_cacheValid = false;
		s_cachedVB = NULL;
		s_cachedVBStride = 0;
		s_cachedVBOffset = 0;
		s_cachedIB = NULL;
		s_cachedTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		s_cachedTexId = -1;
		s_cachedTexLookupSRV = NULL;
		s_cachedTexLinear = false;
		s_cachedTexClamp = false;
		s_cachedLightTexId = -1;
		s_cachedLightLookupSRV = NULL;
		s_cachedSRV0 = (ID3D11ShaderResourceView*)(uintptr_t)1;
		s_cachedSRV1 = (ID3D11ShaderResourceView*)(uintptr_t)1;
		s_cachedSamplerIdx = -1;
		s_cachedBlendKey = 0xFFFFFFFFu;
		s_cachedBlend = NULL;
		s_cachedDSKey = ~0ULL;
		s_cachedDS = NULL;
		s_cachedRasterKey = ~0ULL;
		s_cachedRS = NULL;
		s_lastConstsValid = false;
	}

	static void DrainDeferredReleases()
	{
		std::vector<std::shared_ptr<const DX11BakedVBSet>> toRelease;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			if (s_pendingReleaseVBSets.empty()) return;
			std::vector<std::shared_ptr<const DX11BakedVBSet>> keep;
			for (auto &set : s_pendingReleaseVBSets)
			{
				if (set && set.use_count() == 1)
				{
					toRelease.push_back(std::move(set));
				}
				else if (set)
				{
					keep.push_back(std::move(set));
				}
			}
			s_pendingReleaseVBSets.swap(keep);
		}
		for (auto &set : toRelease)
		{
			if (set && set->pVB)
			{
				set->pVB->Release();
			}
		}
	}

	static D3D11_BLEND SanitizeAlphaBlend(D3D11_BLEND b)
	{
		switch (b)
		{
		case D3D11_BLEND_SRC_COLOR:      return D3D11_BLEND_SRC_ALPHA;
		case D3D11_BLEND_INV_SRC_COLOR:  return D3D11_BLEND_INV_SRC_ALPHA;
		case D3D11_BLEND_DEST_COLOR:     return D3D11_BLEND_DEST_ALPHA;
		case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
		default:                         return b;
		}
	}

	static D3D11_COMPARISON_FUNC SanitizeComparison(int func)
	{
		if (func >= D3D11_COMPARISON_NEVER && func <= D3D11_COMPARISON_ALWAYS)
		{
			return (D3D11_COMPARISON_FUNC)func;
		}
		return D3D11_COMPARISON_LESS_EQUAL;
	}

	static inline uint32_t MakeBlendKey(bool enable, int src, int dst, uint8_t writeMask)
	{
		return ((uint32_t)(enable ? 1 : 0)) |
			(((uint32_t)(src & 0x3f)) << 1) |
			(((uint32_t)(dst & 0x3f)) << 7) |
			(((uint32_t)(writeMask & 0x0f)) << 13);
	}

	static ID3D11BlendState *GetBlendState(uint32_t key, bool enable, int src, int dst, uint8_t writeMask)
	{
		auto it = s_blendCache.find(key);
		if (it != s_blendCache.end()) return it->second;

		D3D11_BLEND_DESC desc = {};
		desc.RenderTarget[0].BlendEnable = enable ? TRUE : FALSE;
		D3D11_BLEND srcB = (src >= 1 && src <= 19) ? (D3D11_BLEND)src : D3D11_BLEND_SRC_ALPHA;
		D3D11_BLEND dstB = (dst >= 1 && dst <= 19) ? (D3D11_BLEND)dst : D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].SrcBlend = srcB;
		desc.RenderTarget[0].DestBlend = dstB;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = SanitizeAlphaBlend(srcB);
		desc.RenderTarget[0].DestBlendAlpha = SanitizeAlphaBlend(dstB);
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = writeMask;

		ID3D11BlendState *pState = NULL;
		if (SUCCEEDED(s_pDevice->CreateBlendState(&desc, &pState)))
		{
			s_blendCache[key] = pState;
		}
		return pState;
	}

	static inline uint64_t MakeDepthStencilKey(bool depthEnable, bool depthMask, int depthFunc,
		bool stencilEnable, int stencilFunc, uint8_t readMask, uint8_t writeMask)
	{
		return ((uint64_t)(depthEnable ? 1 : 0)) |
			(((uint64_t)(depthMask ? 1 : 0)) << 1) |
			(((uint64_t)(depthFunc & 0x0f)) << 2) |
			(((uint64_t)(stencilEnable ? 1 : 0)) << 6) |
			(((uint64_t)(stencilFunc & 0x0f)) << 7) |
			(((uint64_t)readMask) << 11) |
			(((uint64_t)writeMask) << 19);
	}

	static ID3D11DepthStencilState *GetDepthStencilState(uint64_t key, bool depthEnable, bool depthMask, int depthFunc,
		bool stencilEnable, int stencilFunc, uint8_t readMask, uint8_t writeMask)
	{
		auto it = s_dsCache.find(key);
		if (it != s_dsCache.end()) return it->second;

		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = depthEnable ? TRUE : FALSE;
		desc.DepthWriteMask = depthMask ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = SanitizeComparison(depthFunc);
		desc.StencilEnable = stencilEnable ? TRUE : FALSE;
		desc.StencilReadMask = readMask;
		desc.StencilWriteMask = writeMask;
		D3D11_COMPARISON_FUNC sFunc = SanitizeComparison(stencilFunc);
		desc.FrontFace.StencilFunc = sFunc;
		desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.BackFace = desc.FrontFace;

		ID3D11DepthStencilState *pState = NULL;
		if (SUCCEEDED(s_pDevice->CreateDepthStencilState(&desc, &pState)))
		{
			s_dsCache[key] = pState;
		}
		return pState;
	}

	static inline uint64_t MakeRasterizerKey(bool cullEnable, bool cullCW, float slopeBias, float constBias)
	{
		int32_t slopeKey = (int32_t)(slopeBias * 65536.0f);
		int32_t constKey = (int32_t)(constBias * 65536.0f);
		return ((uint64_t)(cullEnable ? 1 : 0)) |
			(((uint64_t)(cullCW ? 1 : 0)) << 1) |
			(((uint64_t)((uint16_t)slopeKey)) << 2) |
			(((uint64_t)((uint16_t)constKey)) << 18);
	}

	static ID3D11RasterizerState *GetRasterizerState(uint64_t key, bool cullEnable, bool cullCW, float slopeBias, float constBias)
	{
		auto it = s_rasterCache.find(key);
		if (it != s_rasterCache.end()) return it->second;

		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = !cullEnable ? D3D11_CULL_NONE : (cullCW ? D3D11_CULL_BACK : D3D11_CULL_FRONT);
		desc.FrontCounterClockwise = TRUE;
		/* glWrapper::glPolygonOffset divides OpenGL factor/units by 65536.0f.
		   Restore the original OpenGL factor/units scale for D3D11 SlopeScaledDepthBias and D24 DepthBias. */
		desc.DepthBias = (INT)lroundf(constBias * 65536.0f);
		desc.SlopeScaledDepthBias = slopeBias * 65536.0f;
		desc.DepthClipEnable = TRUE;

		ID3D11RasterizerState *pState = NULL;
		if (SUCCEEDED(s_pDevice->CreateRasterizerState(&desc, &pState)))
		{
			s_rasterCache[key] = pState;
		}
		return pState;
	}

	static IDXGIAdapter1 *SelectBestDiscreteAdapter(IDXGIFactory1 *pFactory)
	{
		IDXGIAdapter1 *bestAdapter = NULL;
		SIZE_T bestScore = 0;

		IDXGIFactory6 *pFactory6 = NULL;
		if (SUCCEEDED(pFactory->QueryInterface(__uuidof(IDXGIFactory6), (void**)&pFactory6)) && pFactory6)
		{
			IDXGIAdapter1 *cand = NULL;
			if (SUCCEEDED(pFactory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&cand)) && cand)
			{
				DXGI_ADAPTER_DESC1 desc = {};
				if (SUCCEEDED(cand->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				{
					pFactory6->Release();
					return cand;
				}
				cand->Release();
			}
			pFactory6->Release();
		}

		for (UINT i = 0; ; ++i)
		{
			IDXGIAdapter1 *cand = NULL;
			if (pFactory->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(cand->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
			{
				SIZE_T score = desc.DedicatedVideoMemory;
				if (desc.VendorId == 0x10DE || desc.VendorId == 0x1002)
				{
					score += (SIZE_T)(512ULL * 1024ULL * 1024ULL);
				}
				if (!bestAdapter || score > bestScore)
				{
					if (bestAdapter) bestAdapter->Release();
					bestAdapter = cand;
					bestScore = score;
					continue;
				}
			}
			cand->Release();
		}
		return bestAdapter;
	}

	static bool CreateBackbufferTargets(int width, int height)
	{
		if (!s_pDevice || !s_pSwapChain) return false;
		ID3D11Texture2D *pBackBuffer = NULL;
		HRESULT hr = s_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
		if (FAILED(hr) || !pBackBuffer) return false;

		hr = s_pDevice->CreateRenderTargetView(pBackBuffer, NULL, &s_pRTV);
		pBackBuffer->Release();
		if (FAILED(hr) || !s_pRTV) return false;

		D3D11_TEXTURE2D_DESC descDepth = {};
		descDepth.Width = (UINT)width;
		descDepth.Height = (UINT)height;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDepth.SampleDesc.Count = 1;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		hr = s_pDevice->CreateTexture2D(&descDepth, NULL, &s_pDSBuffer);
		if (FAILED(hr) || !s_pDSBuffer) return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = descDepth.Format;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		hr = s_pDevice->CreateDepthStencilView(s_pDSBuffer, &dsvDesc, &s_pDSV);
		if (FAILED(hr) || !s_pDSV) return false;

		s_pContext->OMSetRenderTargets(1, &s_pRTV, s_pDSV);
		float vx, vy, vw, vh;
		ComputeViewportRect(s_currentViewportType, width, height, vx, vy, vw, vh);
		s_activeViewport = { vx, vy, vw, vh, 0.0f, 1.0f };
		s_pContext->RSSetViewports(1, &s_activeViewport);
		return true;
	}

	static void ReleaseBackbufferTargets()
	{
		if (s_pContext) s_pContext->OMSetRenderTargets(0, NULL, NULL);
		if (s_pRTV) { s_pRTV->Release(); s_pRTV = NULL; }
		if (s_pDSV) { s_pDSV->Release(); s_pDSV = NULL; }
		if (s_pDSBuffer) { s_pDSBuffer->Release(); s_pDSBuffer = NULL; }
	}

	static bool CreatePipelineObjects()
	{
		ID3DBlob *pVSStdBlob = NULL;
		ID3DBlob *pVSCompBlob = NULL;
		ID3DBlob *pPSBlob = NULL;
		ID3DBlob *pErrBlob = NULL;

		HRESULT hr = D3DCompile(s_hlslVS, strlen(s_hlslVS), "VSStd", NULL, NULL, "VSMainStandard", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVSStdBlob, &pErrBlob);
		if (FAILED(hr)) { if (pErrBlob) pErrBlob->Release(); return false; }
		if (pErrBlob) { pErrBlob->Release(); pErrBlob = NULL; }

		hr = D3DCompile(s_hlslVS, strlen(s_hlslVS), "VSComp", NULL, NULL, "VSMainCompressed", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pVSCompBlob, &pErrBlob);
		if (FAILED(hr)) { if (pErrBlob) pErrBlob->Release(); pVSStdBlob->Release(); return false; }
		if (pErrBlob) { pErrBlob->Release(); pErrBlob = NULL; }

		hr = D3DCompile(s_hlslPS, strlen(s_hlslPS), "PSMain", NULL, NULL, "PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pPSBlob, &pErrBlob);
		if (FAILED(hr)) { if (pErrBlob) pErrBlob->Release(); pVSStdBlob->Release(); pVSCompBlob->Release(); return false; }
		if (pErrBlob) { pErrBlob->Release(); pErrBlob = NULL; }

		s_pDevice->CreateVertexShader(pVSStdBlob->GetBufferPointer(), pVSStdBlob->GetBufferSize(), NULL, &s_pVSStandard);
		s_pDevice->CreateVertexShader(pVSCompBlob->GetBufferPointer(), pVSCompBlob->GetBufferSize(), NULL, &s_pVSCompressed);
		s_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &s_pPS);

		D3D11_INPUT_ELEMENT_DESC stdLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32_UINT,        0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32_UINT,        0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32_UINT,        0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		s_pDevice->CreateInputLayout(stdLayout, 5, pVSStdBlob->GetBufferPointer(), pVSStdBlob->GetBufferSize(), &s_pInputLayoutStandard);

		D3D11_INPUT_ELEMENT_DESC compLayout[] =
		{
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_SINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_SINT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		s_pDevice->CreateInputLayout(compLayout, 2, pVSCompBlob->GetBufferPointer(), pVSCompBlob->GetBufferSize(), &s_pInputLayoutCompressed);

		pVSStdBlob->Release();
		pVSCompBlob->Release();
		pPSBlob->Release();

		D3D11_BUFFER_DESC ringDesc = {};
		ringDesc.ByteWidth = C4J_D3D11_RING_VB_BYTES;
		ringDesc.Usage = D3D11_USAGE_DYNAMIC;
		ringDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		ringDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_pDevice->CreateBuffer(&ringDesc, NULL, &s_pRingVB);
		s_ringVBOffset = 0;

		D3D11_BUFFER_DESC vsCbDesc = {};
		vsCbDesc.ByteWidth = (UINT)((sizeof(VSConstantBufferData) + 15u) & ~15u);
		vsCbDesc.Usage = D3D11_USAGE_DYNAMIC;
		vsCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		vsCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_pDevice->CreateBuffer(&vsCbDesc, NULL, &s_pVSCB);

		D3D11_BUFFER_DESC psCbDesc = {};
		psCbDesc.ByteWidth = (UINT)((sizeof(PSConstantBufferData) + 15u) & ~15u);
		psCbDesc.Usage = D3D11_USAGE_DYNAMIC;
		psCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		psCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		s_pDevice->CreateBuffer(&psCbDesc, NULL, &s_pPSCB);

		{
			const UINT maxQuads = 65536;
			std::vector<uint32_t> indices(maxQuads * 6);
			for (UINT q = 0; q < maxQuads; ++q)
			{
				UINT base = q * 4;
				indices[q * 6 + 0] = base + 0;
				indices[q * 6 + 1] = base + 1;
				indices[q * 6 + 2] = base + 2;
				indices[q * 6 + 3] = base + 2;
				indices[q * 6 + 4] = base + 3;
				indices[q * 6 + 5] = base + 0;
			}
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
			ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA initData = { indices.data(), 0, 0 };
			s_pDevice->CreateBuffer(&ibDesc, &initData, &s_pQuadIB);
		}

		{
			const UINT maxFanTris = 4096;
			std::vector<uint32_t> indices(maxFanTris * 3);
			for (UINT t = 0; t < maxFanTris; ++t)
			{
				indices[t * 3 + 0] = 0;
				indices[t * 3 + 1] = t + 1;
				indices[t * 3 + 2] = t + 2;
			}
			D3D11_BUFFER_DESC ibDesc = {};
			ibDesc.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
			ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
			ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA initData = { indices.data(), 0, 0 };
			s_pDevice->CreateBuffer(&ibDesc, &initData, &s_pFanIB);
		}

		for (int i = 0; i < 4; ++i)
		{
			D3D11_SAMPLER_DESC sdesc = {};
			sdesc.Filter = (i >= 2) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
			sdesc.AddressU = (i & 1) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
			sdesc.AddressV = sdesc.AddressU;
			sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sdesc.MaxLOD = D3D11_FLOAT32_MAX;
			s_pDevice->CreateSamplerState(&sdesc, &s_pSamplers[i]);
		}

		InvalidateStateCache();
		return (s_pVSStandard && s_pVSCompressed && s_pPS && s_pInputLayoutStandard && s_pInputLayoutCompressed && s_pRingVB);
	}

	static void ExecuteDrawInternal(
		int primType, int count,
		ID3D11Buffer *pVB, UINT vbOffsetBytes,
		const void *dynamicBytes, UINT dynamicByteCount,
		int vType, int psType,
		const Matrix4x4 &wvp, const Matrix4x4 &mv,
		int texId, const float stateColor[4],
		bool blendEnable, int blendSrc, int blendDst,
		bool depthTestEnable, bool depthMask,
		bool cullEnable, bool cullCW,
		bool alphaTestEnable, int alphaFunc, float alphaCutoff)
	{
		if (!s_pContext || count <= 0) return;

		/* Upload dynamic vertices into 4MB ring VB if static VB not supplied */
		if (!pVB)
		{
			if (!dynamicBytes || dynamicByteCount == 0 || dynamicByteCount > C4J_D3D11_RING_VB_BYTES) return;
			D3D11_MAP mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
			UINT alignedOffset = (s_ringVBOffset + 15u) & ~15u;
			if (alignedOffset + dynamicByteCount > C4J_D3D11_RING_VB_BYTES)
			{
				alignedOffset = 0;
				mapType = D3D11_MAP_WRITE_DISCARD;
			}
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(s_pContext->Map(s_pRingVB, 0, mapType, 0, &mapped)))
			{
				memcpy((uint8_t*)mapped.pData + alignedOffset, dynamicBytes, dynamicByteCount);
				s_pContext->Unmap(s_pRingVB, 0);
				if (mapType == D3D11_MAP_WRITE_DISCARD && s_cachedVB == s_pRingVB) s_cachedVB = NULL;
				pVB = s_pRingVB;
				vbOffsetBytes = alignedOffset;
				s_ringVBOffset = alignedOffset + dynamicByteCount;
			}
			else
			{
				return;
			}
		}

		ID3D11ShaderResourceView *pSRV = NULL;
		bool texLinear = false;
		bool texClamp = false;
		if (texId > 0)
		{
			if (s_cachedTexId == texId)
			{
				pSRV = s_cachedTexLookupSRV;
				texLinear = s_cachedTexLinear;
				texClamp = s_cachedTexClamp;
			}
			else
			{
				auto it = s_textures.find(texId);
				if (it != s_textures.end())
				{
					pSRV = it->second.pSRV;
					texLinear = it->second.linearFilter;
					texClamp = it->second.clampWrap;
				}
				s_cachedTexId = texId;
				s_cachedTexLookupSRV = pSRV;
				s_cachedTexLinear = texLinear;
				s_cachedTexClamp = texClamp;
			}
		}

		ID3D11ShaderResourceView *pLightSRV = NULL;
		if (s_tls.boundVertexTextureId > 0)
		{
			if (s_cachedLightTexId == s_tls.boundVertexTextureId)
			{
				pLightSRV = s_cachedLightLookupSRV;
			}
			else
			{
				auto itL = s_textures.find(s_tls.boundVertexTextureId);
				if (itL != s_textures.end()) pLightSRV = itL->second.pSRV;
				s_cachedLightTexId = s_tls.boundVertexTextureId;
				s_cachedLightLookupSRV = pLightSRV;
			}
		}

		VSConstantBufferData vsConsts = {};
		FillVSConstants(s_tls, wvp, mv, vType, psType, vsConsts);

		PSConstantBufferData psConsts = {};
		FillPSConstants(s_tls, stateColor, pSRV != NULL, pLightSRV != NULL,
			alphaTestEnable, alphaFunc, alphaCutoff, psType, psConsts);

		if (!s_lastConstsValid || memcmp(&vsConsts, &s_lastVSConsts, sizeof(vsConsts)) != 0)
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(s_pContext->Map(s_pVSCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &vsConsts, sizeof(vsConsts));
				s_pContext->Unmap(s_pVSCB, 0);
				s_lastVSConsts = vsConsts;
			}
		}

		if (!s_lastConstsValid || memcmp(&psConsts, &s_lastPSConsts, sizeof(psConsts)) != 0)
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(s_pContext->Map(s_pPSCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &psConsts, sizeof(psConsts));
				s_pContext->Unmap(s_pPSCB, 0);
				s_lastPSConsts = psConsts;
			}
		}
		s_lastConstsValid = true;

		const bool isCompressed = (vType == 1);
		UINT stride = isCompressed ? 16u : 32u;

		if (!s_cacheValid)
		{
			s_pContext->IASetInputLayout(isCompressed ? s_pInputLayoutCompressed : s_pInputLayoutStandard);
			s_pContext->VSSetShader(isCompressed ? s_pVSCompressed : s_pVSStandard, NULL, 0);
			s_pContext->PSSetShader(s_pPS, NULL, 0);
			s_pContext->VSSetConstantBuffers(0, 1, &s_pVSCB);
			s_pContext->PSSetConstantBuffers(0, 1, &s_pPSCB);
			s_cachedCompressed = isCompressed;
		}
		else if (s_cachedCompressed != isCompressed)
		{
			s_pContext->IASetInputLayout(isCompressed ? s_pInputLayoutCompressed : s_pInputLayoutStandard);
			s_pContext->VSSetShader(isCompressed ? s_pVSCompressed : s_pVSStandard, NULL, 0);
			s_cachedCompressed = isCompressed;
		}

		if (!s_cacheValid || s_cachedVB != pVB || s_cachedVBStride != stride || s_cachedVBOffset != vbOffsetBytes)
		{
			s_pContext->IASetVertexBuffers(0, 1, &pVB, &stride, &vbOffsetBytes);
			s_cachedVB = pVB;
			s_cachedVBStride = stride;
			s_cachedVBOffset = vbOffsetBytes;
		}

		int samplerIdx = (texLinear ? 2 : 0) + (texClamp ? 1 : 0);
		if (!s_cacheValid || s_cachedSamplerIdx != samplerIdx)
		{
			ID3D11SamplerState *samplers[2] = { s_pSamplers[samplerIdx], s_pSamplers[3] };
			s_pContext->PSSetSamplers(0, 2, samplers);
			s_cachedSamplerIdx = samplerIdx;
		}

		if (!s_cacheValid || s_cachedSRV0 != pSRV || s_cachedSRV1 != pLightSRV)
		{
			ID3D11ShaderResourceView *srvs[2] = { pSRV, pLightSRV };
			s_pContext->PSSetShaderResources(0, 2, srvs);
			s_cachedSRV0 = pSRV;
			s_cachedSRV1 = pLightSRV;
		}

		const uint32_t blendKey = MakeBlendKey(blendEnable, blendSrc, blendDst, s_tls.colorWriteMask);
		const bool blendFactorUsesAlpha = (blendSrc == C4J_BLEND_FACTOR_ALPHA || blendDst == C4J_BLEND_INV_FACTOR_ALPHA ||
			blendSrc == C4J_BLEND_INV_FACTOR_ALPHA || blendDst == C4J_BLEND_FACTOR_ALPHA);
		if (!s_cacheValid || s_cachedBlendKey != blendKey || s_cachedBlendFactor != s_tls.blendFactorRGBA ||
			s_cachedBlendFactorUsesAlpha != blendFactorUsesAlpha)
		{
			ID3D11BlendState *pBlend = (!s_cacheValid || s_cachedBlendKey != blendKey)
				? GetBlendState(blendKey, blendEnable, blendSrc, blendDst, s_tls.colorWriteMask)
				: s_cachedBlend;
			float blendFactor[4] = {
				(float)((s_tls.blendFactorRGBA >> 16) & 0xFF) / 255.0f,
				(float)((s_tls.blendFactorRGBA >> 8)  & 0xFF) / 255.0f,
				(float)( s_tls.blendFactorRGBA        & 0xFF) / 255.0f,
				(float)((s_tls.blendFactorRGBA >> 24) & 0xFF) / 255.0f
			};
			if (blendFactorUsesAlpha)
			{
				blendFactor[0] = blendFactor[1] = blendFactor[2] = blendFactor[3];
			}
			s_pContext->OMSetBlendState(pBlend, blendFactor, 0xffffffff);
			s_cachedBlendKey = blendKey;
			s_cachedBlend = pBlend;
			s_cachedBlendFactor = s_tls.blendFactorRGBA;
			s_cachedBlendFactorUsesAlpha = blendFactorUsesAlpha;
		}

		const uint64_t dsKey = MakeDepthStencilKey(depthTestEnable, depthMask, s_tls.depthFunc,
			s_tls.stencilEnable, s_tls.stencilFunc, s_tls.stencilReadMask, s_tls.stencilWriteMask);
		if (!s_cacheValid || s_cachedDSKey != dsKey || s_cachedStencilRef != s_tls.stencilRef)
		{
			ID3D11DepthStencilState *pDS = (!s_cacheValid || s_cachedDSKey != dsKey)
				? GetDepthStencilState(dsKey, depthTestEnable, depthMask, s_tls.depthFunc,
					s_tls.stencilEnable, s_tls.stencilFunc, s_tls.stencilReadMask, s_tls.stencilWriteMask)
				: s_cachedDS;
			s_pContext->OMSetDepthStencilState(pDS, s_tls.stencilRef);
			s_cachedDSKey = dsKey;
			s_cachedDS = pDS;
			s_cachedStencilRef = s_tls.stencilRef;
		}

		const uint64_t rasterKey = MakeRasterizerKey(cullEnable, cullCW, s_tls.depthBiasSlope, s_tls.depthBiasConstant);
		if (!s_cacheValid || s_cachedRasterKey != rasterKey)
		{
			ID3D11RasterizerState *pRS = GetRasterizerState(rasterKey, cullEnable, cullCW, s_tls.depthBiasSlope, s_tls.depthBiasConstant);
			s_pContext->RSSetState(pRS);
			s_cachedRasterKey = rasterKey;
			s_cachedRS = pRS;
		}
		s_cacheValid = true;

		const bool isQuad = (primType == 3 /* QUAD_LIST */ || primType == 7);
		if (isQuad && s_pQuadIB && count >= 4 && (count % 4) == 0)
		{
			if (s_cachedIB != s_pQuadIB) { s_pContext->IASetIndexBuffer(s_pQuadIB, DXGI_FORMAT_R32_UINT, 0); s_cachedIB = s_pQuadIB; }
			if (s_cachedTopo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) { s_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); s_cachedTopo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; }
			UINT quadsRemaining = (UINT)(count / 4);
			UINT vertStart = 0;
			while (quadsRemaining > 0)
			{
				UINT batch = (quadsRemaining > 65536u) ? 65536u : quadsRemaining;
				s_pContext->DrawIndexed(batch * 6u, 0, (INT)vertStart);
				quadsRemaining -= batch;
				vertStart += batch * 4u;
			}
		}
		else if (primType == 2 /* TRIANGLE_FAN */)
		{
			if (count >= 3 && s_pFanIB)
			{
				UINT tris = (UINT)(count - 2);
				if (tris > 4096u) tris = 4096u;
				if (s_cachedIB != s_pFanIB) { s_pContext->IASetIndexBuffer(s_pFanIB, DXGI_FORMAT_R32_UINT, 0); s_cachedIB = s_pFanIB; }
				if (s_cachedTopo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) { s_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); s_cachedTopo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; }
				s_pContext->DrawIndexed(tris * 3u, 0, 0);
			}
		}
		else
		{
			D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			if (primType == 1) topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			else if (primType == 4) topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
			else if (primType == 5) topo = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
			if (s_cachedTopo != topo) { s_pContext->IASetPrimitiveTopology(topo); s_cachedTopo = topo; }
			s_pContext->Draw((UINT)count, 0);
		}
	}

	/* ABI implementation functions */
	static int DX11_InitialiseWindow(HWND hwnd, int width, int height, int fullscreen, int vsync)
	{
		s_hwnd = hwnd;
		s_width = (width > 0) ? width : 1280;
		s_height = (height > 0) ? height : 720;
		s_fullscreen = (fullscreen != 0);
		s_vsync = (vsync != 0);

		IDXGIFactory1 *pFactory = NULL;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)) || !pFactory)
		{
			return 0;
		}

		IDXGIAdapter1 *pAdapter = SelectBestDiscreteAdapter(pFactory);
		if (pAdapter)
		{
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(pAdapter->GetDesc1(&desc)))
			{
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, s_adapterName, sizeof(s_adapterName), NULL, NULL);
			}
		}

		D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
		D3D_FEATURE_LEVEL achievedFL = D3D_FEATURE_LEVEL_11_0;
		HRESULT hr = D3D11CreateDevice(
			pAdapter,
			pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
			NULL, 0,
			featureLevels, 3, D3D11_SDK_VERSION,
			&s_pDevice, &achievedFL, &s_pContext);
		if (FAILED(hr))
		{
			hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
				featureLevels + 1, 2, D3D11_SDK_VERSION,
				&s_pDevice, &achievedFL, &s_pContext);
		}
		if (pAdapter) pAdapter->Release();

		if (FAILED(hr) || !s_pDevice || !s_pContext)
		{
			pFactory->Release();
			return 0;
		}

		IDXGIFactory2 *pFactory2 = NULL;
		IDXGIFactory5 *pFactory5 = NULL;
		s_allowTearing = false;
		if (SUCCEEDED(pFactory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&pFactory5)) && pFactory5)
		{
			BOOL allow = FALSE;
			if (SUCCEEDED(pFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
			{
				s_allowTearing = (allow != FALSE);
			}
			pFactory5->Release();
		}

		s_flipModel = false;
		if (SUCCEEDED(pFactory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&pFactory2)) && pFactory2)
		{
			DXGI_SWAP_CHAIN_DESC1 sc1 = {};
			sc1.Width = (UINT)s_width;
			sc1.Height = (UINT)s_height;
			sc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sc1.SampleDesc.Count = 1;
			sc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sc1.BufferCount = 2;
			sc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			sc1.Flags = s_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

			hr = pFactory2->CreateSwapChainForHwnd(s_pDevice, s_hwnd, &sc1, NULL, NULL, &s_pSwapChain1);
			if (SUCCEEDED(hr) && s_pSwapChain1)
			{
				s_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), (void**)&s_pSwapChain);
				s_flipModel = true;
			}
			pFactory2->Release();
		}

		if (!s_pSwapChain)
		{
			DXGI_SWAP_CHAIN_DESC sd = {};
			sd.BufferCount = 1;
			sd.BufferDesc.Width = (UINT)s_width;
			sd.BufferDesc.Height = (UINT)s_height;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = s_hwnd;
			sd.SampleDesc.Count = 1;
			sd.Windowed = TRUE;
			sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			hr = pFactory->CreateSwapChain(s_pDevice, &sd, &s_pSwapChain);
			s_allowTearing = false;
		}
		pFactory->MakeWindowAssociation(s_hwnd, DXGI_MWA_NO_ALT_ENTER);
		pFactory->Release();

		if (!s_pSwapChain || !CreateBackbufferTargets(s_width, s_height) || !CreatePipelineObjects())
		{
			return 0;
		}
		return 1;
	}

	static int DX11_ResizeWindow(int width, int height, int fullscreen)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (!s_pSwapChain || width <= 0 || height <= 0) return 0;

		s_width = width;
		s_height = height;
		s_fullscreen = (fullscreen != 0);

		ReleaseBackbufferTargets();
		UINT bufCount = s_flipModel ? 2u : 1u;
		UINT flags = (s_flipModel && s_allowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
		HRESULT hr = s_pSwapChain->ResizeBuffers(bufCount, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, flags);
		if (FAILED(hr)) return 0;
		if (!CreateBackbufferTargets(width, height)) return 0;
		InvalidateStateCache();
		return 1;
	}

	static void DX11_Shutdown(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		{
			std::lock_guard<std::mutex> cbLock(s_cbuffMutex);
			for (auto &kv : s_cbuffs)
			{
				if (kv.second.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(kv.second.gpuVBs));
			}
			s_cbuffs.clear();
		}
		DrainDeferredReleases();
		s_totalCBuffBytes.store(0, std::memory_order_relaxed);
		for (auto &kv : s_textures)
		{
			if (kv.second.pSRV) kv.second.pSRV->Release();
			if (kv.second.pTexture) kv.second.pTexture->Release();
		}
		s_textures.clear();
		for (auto &kv : s_blendCache) if (kv.second) kv.second->Release();
		s_blendCache.clear();
		for (auto &kv : s_dsCache) if (kv.second) kv.second->Release();
		s_dsCache.clear();
		for (auto &kv : s_rasterCache) if (kv.second) kv.second->Release();
		s_rasterCache.clear();
		for (int i = 0; i < 4; ++i) { if (s_pSamplers[i]) { s_pSamplers[i]->Release(); s_pSamplers[i] = NULL; } }
		if (s_pRingVB) { s_pRingVB->Release(); s_pRingVB = NULL; }
		if (s_pQuadIB) { s_pQuadIB->Release(); s_pQuadIB = NULL; }
		if (s_pFanIB)  { s_pFanIB->Release();  s_pFanIB = NULL; }
		if (s_pVSCB)   { s_pVSCB->Release();   s_pVSCB = NULL; }
		if (s_pPSCB)   { s_pPSCB->Release();   s_pPSCB = NULL; }
		if (s_pInputLayoutStandard)   { s_pInputLayoutStandard->Release(); s_pInputLayoutStandard = NULL; }
		if (s_pInputLayoutCompressed) { s_pInputLayoutCompressed->Release(); s_pInputLayoutCompressed = NULL; }
		if (s_pVSStandard)   { s_pVSStandard->Release(); s_pVSStandard = NULL; }
		if (s_pVSCompressed) { s_pVSCompressed->Release(); s_pVSCompressed = NULL; }
		if (s_pPS)           { s_pPS->Release(); s_pPS = NULL; }
		ReleaseBackbufferTargets();
		if (s_pSwapChain1) { s_pSwapChain1->Release(); s_pSwapChain1 = NULL; }
		if (s_pSwapChain)  { s_pSwapChain->Release();  s_pSwapChain = NULL; }
		if (s_pContext)    { s_pContext->ClearState(); s_pContext->Release(); s_pContext = NULL; }
		if (s_pDevice)     { s_pDevice->Release();     s_pDevice = NULL; }
	}

	static void DX11_SetVsync(int enabled) { s_vsync = (enabled != 0); }
	static int  DX11_GetVsync(void) { return s_vsync ? 1 : 0; }
	static void DX11_GetBackendInfo(C4JBackendInfo *out)
	{
		if (!out) return;
		memset(out, 0, sizeof(*out));
		out->backendType = C4J_BACKEND_DX11;
		out->vsyncEnabled = s_vsync ? 1 : 0;
		out->screenWidth = s_width;
		out->screenHeight = s_height;
		strncpy_s(out->adapterName, sizeof(out->adapterName), s_adapterName, _TRUNCATE);
		strncpy_s(out->backendName, sizeof(out->backendName), "DirectX 11", _TRUNCATE);
	}

	static void DX11_Tick(void) {}
	static void DX11_UpdateGamma(unsigned short) {}
	static void DX11_StartFrame(void)
	{
		s_tls.ResetFrameState();
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		DrainDeferredReleases();
		if (s_pContext && s_pRTV && s_pDSV)
		{
			s_pContext->OMSetRenderTargets(1, &s_pRTV, s_pDSV);
			s_pContext->RSSetViewports(1, &s_activeViewport);
		}
		InvalidateStateCache();
	}
	static void DX11_DoScreenGrabOnNextPresent(void) {}
	static void DX11_Present(void)
	{
		s_frameCBuffUploadBytes = 0;
		s_frameCBuffUploadCount = 0;
		if (!s_pSwapChain) return;
		UINT syncInterval = s_vsync ? 1u : 0u;
		UINT flags = (!s_vsync && s_flipModel && s_allowTearing && !s_fullscreen) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
		s_pSwapChain->Present(syncInterval, flags);
		if (s_flipModel && s_pContext && s_pRTV && s_pDSV)
		{
			s_pContext->OMSetRenderTargets(1, &s_pRTV, s_pDSV);
		}
	}
	static void DX11_Clear(int flags, const C4JClearRect *pRect)
	{
		(void)pRect;
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (!s_pContext) return;
		if ((flags & 2 /* CLEAR_COLOUR_FLAG */) && s_pRTV)
		{
			s_pContext->ClearRenderTargetView(s_pRTV, s_clearColour);
		}
		if (s_pDSV)
		{
			UINT d3dFlags = 0;
			if (flags & 1 /* CLEAR_DEPTH_FLAG */) d3dFlags |= D3D11_CLEAR_DEPTH;
			if (flags & GL_STENCIL_BUFFER_BIT) d3dFlags |= D3D11_CLEAR_STENCIL;
			if (d3dFlags != 0) s_pContext->ClearDepthStencilView(s_pDSV, d3dFlags, 1.0f, 0);
		}
	}
	static void DX11_SetClearColour(const float rgba[4]) { if (rgba) memcpy(s_clearColour, rgba, 16); }
	static int  DX11_IsWidescreen(void) { return 1; }
	static int  DX11_IsHiDef(void) { return 1; }
	static void DX11_NoOpInt(int) {}
	static void DX11_NoOpVoid(void) {}

	/* Matrix stack */
	static void DX11_MatrixMode(int type) { if (type >= 0 && type < 3) { s_tls.matrixMode = type; s_tls.EnsureStackInit(type); } }
	static void DX11_MatrixSetIdentity(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity(); s_tls.matrixDirty = true; }
	static void DX11_MatrixTranslate(float x, float y, float z) { MatrixTranslate(s_tls, x, y, z); }
	static void DX11_MatrixRotate(float a, float x, float y, float z) { MatrixRotate(s_tls, a, x, y, z); }
	static void DX11_MatrixScale(float x, float y, float z) { MatrixScale(s_tls, x, y, z); }
	static void DX11_MatrixPerspective(float fovy, float aspect, float zn, float zf) { MatrixPerspective(s_tls, fovy, aspect, zn, zf); }
	static void DX11_MatrixOrthogonal(float l, float r, float b, float t, float zn, float zf) { MatrixOrthogonal(s_tls, l, r, b, t, zn, zf); }
	static void DX11_MatrixPop(void) { if (s_tls.matrixStacks[s_tls.matrixMode].size() > 1) { s_tls.matrixStacks[s_tls.matrixMode].pop_back(); s_tls.matrixDirty = true; } }
	static void DX11_MatrixPush(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].push_back(s_tls.matrixStacks[s_tls.matrixMode].back()); }
	static void DX11_MatrixMult(float *mat)
	{
		if (!mat) return;
		s_tls.EnsureStackInit(s_tls.matrixMode);
		Matrix4x4 m;
		memcpy(m.m, mat, sizeof(m.m));
		s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Multiply(s_tls.matrixStacks[s_tls.matrixMode].back(), m);
		s_tls.matrixDirty = true;
	}
	static const float *DX11_MatrixGet(int type)
	{
		static const float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		if (type >= 0 && type < 3 && !s_tls.matrixStacks[type].empty()) return s_tls.matrixStacks[type].back().m;
		return id;
	}
	static void DX11_SetMatrixDirty(void) { s_tls.matrixDirty = true; }

	/* DrawVertices */
	static void DX11_DrawVertices(int primType, int count, void *dataIn, int vType, int psType)
	{
		if (!dataIn || count <= 0) return;
		const UINT stride = (vType == 1) ? 16u : 32u;
		const UINT totalBytes = (UINT)count * stride;

		if (s_tls.recordingCBuffIndex != 0)
		{
			s_tls.EnsureStackInit(0);
			CBuffCommand cmd;
			cmd.primitiveType = primType;
			cmd.count = count;
			cmd.vertexData.assign((const uint8_t*)dataIn, (const uint8_t*)dataIn + totalBytes);
			cmd.vType = vType;
			cmd.psType = psType;
			cmd.boundTextureId = s_tls.boundTextureId;
			memcpy(cmd.stateColour, s_tls.stateColour, sizeof(cmd.stateColour));
			cmd.blendEnable = s_tls.blendEnable;
			cmd.blendSrc = s_tls.blendSrc;
			cmd.blendDst = s_tls.blendDst;
			cmd.depthTestEnable = s_tls.depthTestEnable;
			cmd.depthMask = s_tls.depthMask;
			cmd.cullEnable = s_tls.cullEnable;
			cmd.cullCW = s_tls.cullCW;
			cmd.alphaTestEnable = s_tls.alphaTestEnable;
			cmd.alphaFunc = s_tls.alphaFunc;
			cmd.alphaCutoff = s_tls.alphaCutoff;
			cmd.relativeMatrix = Matrix4x4::Multiply(s_tls.recordingInvBaseMatrix, s_tls.matrixStacks[0].back());
			s_tls.recordingCommands.push_back(std::move(cmd));
			return;
		}

		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, NULL, 0, dataIn, totalBytes, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	static void DX11_DrawVertexBuffer(int primType, int count, void *bufferHandle, int vType, int psType)
	{
		if (!bufferHandle || count <= 0) return;
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, (ID3D11Buffer*)bufferHandle, 0, NULL, 0, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	/* Command buffers with lazy static GPU VB baking */
	static int DX11_CBuffCreate(int count)
	{
		if (count <= 0) return 0;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		int first = s_nextCBuffId;
		s_nextCBuffId += count;
		return first;
	}
	static void DX11_CBuffDelete(int first, int count)
	{
		if (count <= 0) return;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		if (s_cbuffs.empty()) return;
		if ((size_t)count > s_cbuffs.size())
		{
			const int end = first + count;
			for (auto it = s_cbuffs.begin(); it != s_cbuffs.end(); )
			{
				if (it->first >= first && it->first < end)
				{
					s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
					if (it->second.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(it->second.gpuVBs));
					it = s_cbuffs.erase(it);
				}
				else
				{
					++it;
				}
			}
			return;
		}
		for (int i = 0; i < count; ++i)
		{
			auto it = s_cbuffs.find(first + i);
			if (it != s_cbuffs.end())
			{
				s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
				if (it->second.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(it->second.gpuVBs));
				s_cbuffs.erase(it);
			}
		}
	}
	static void DX11_CBuffStart(int index, int)
	{
		s_tls.recordingCBuffIndex = index;
		s_tls.recordingCommands.clear();
		s_tls.EnsureStackInit(0);
		s_tls.recordingBaseMatrix = s_tls.matrixStacks[0].back();
		Matrix4x4::Invert(s_tls.recordingBaseMatrix, s_tls.recordingInvBaseMatrix);
	}
	static void DX11_CBuffClear(int index)
	{
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		if (it != s_cbuffs.end())
		{
			s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
			if (it->second.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(it->second.gpuVBs));
			it->second.commands.reset();
			it->second.byteSize = 0;
			it->second.generation = ++s_nextCBuffGen;
			it->second.uploadedGen = 0;
		}
	}
	static int DX11_CBuffSize(int index)
	{
		if (index == -1)
		{
			return (int)s_totalCBuffBytes.load(std::memory_order_relaxed);
		}
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		return (it != s_cbuffs.end()) ? (int)it->second.byteSize : 0;
	}
	static void DX11_CBuffEnd(void)
	{
		if (s_tls.recordingCBuffIndex != 0)
		{
			size_t bytes = 0;
			for (const auto &c : s_tls.recordingCommands) bytes += c.vertexData.size();
			auto cmds = std::make_shared<std::vector<CBuffCommand>>(std::move(s_tls.recordingCommands));
			s_tls.recordingCommands.clear();

			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			DX11CBuffSlot &slot = s_cbuffs[s_tls.recordingCBuffIndex];
			s_totalCBuffBytes.fetch_sub(slot.byteSize, std::memory_order_relaxed);
			if (slot.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(slot.gpuVBs));
			slot.commands = std::move(cmds);
			slot.byteSize = bytes;
			s_totalCBuffBytes.fetch_add(bytes, std::memory_order_relaxed);
			slot.generation = ++s_nextCBuffGen;
			slot.uploadedGen = 0;
			s_tls.recordingCBuffIndex = 0;
		}
	}
	static int DX11_CBuffCall(int index, int)
	{
		std::shared_ptr<const std::vector<CBuffCommand>> cmds;
		std::shared_ptr<const DX11BakedVBSet> vbSet;
		uint64_t gen = 0;
		bool needsUpload = false;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(index);
			if (it == s_cbuffs.end() || !it->second.commands || it->second.commands->empty()) return 0;
			cmds = it->second.commands;
			gen = it->second.generation;
			if (it->second.uploadedGen != gen || !it->second.gpuVBs)
			{
				needsUpload = true;
			}
			else
			{
				vbSet = it->second.gpuVBs;
			}
		}

		std::lock_guard<std::recursive_mutex> d3dLock(s_d3dMutex);
		if (!s_pDevice || !s_pContext) return 0;

		if (needsUpload)
		{
			size_t totalAlignedBytes = 0;
			size_t nonEmptyCount = 0;
			size_t singleNonEmptyIdx = 0;
			bool mustStatic = false;
			for (size_t i = 0; i < cmds->size(); ++i)
			{
				const size_t sz = (*cmds)[i].vertexData.size();
				if (sz == 0) continue;
				if (sz > C4J_D3D11_RING_VB_BYTES) mustStatic = true;
				totalAlignedBytes = (totalAlignedBytes + 31u) & ~(size_t)31u;
				totalAlignedBytes += sz;
				singleNonEmptyIdx = i;
				++nonEmptyCount;
			}

			const bool withinBudget = (s_frameCBuffUploadCount < C4J_DX11_CBUFF_UPLOAD_BUDGET_COUNT) &&
				(s_frameCBuffUploadBytes < C4J_DX11_CBUFF_UPLOAD_BUDGET_BYTES);
			if (nonEmptyCount > 0 && totalAlignedBytes > 0 && (withinBudget || mustStatic))
			{
				std::shared_ptr<DX11BakedVBSet> newSet = std::make_shared<DX11BakedVBSet>();
				newSet->offsets.assign(cmds->size(), 0u);

				const void *srcData = NULL;
				UINT createBytes = 0;
				if (nonEmptyCount == 1)
				{
					const auto &onlyCmd = (*cmds)[singleNonEmptyIdx];
					srcData = onlyCmd.vertexData.data();
					createBytes = (UINT)onlyCmd.vertexData.size();
				}
				else
				{
					thread_local static std::vector<uint8_t> tls_packScratch;
					tls_packScratch.resize(totalAlignedBytes);
					size_t cursor = 0;
					for (size_t i = 0; i < cmds->size(); ++i)
					{
						const auto &cmd = (*cmds)[i];
						if (cmd.vertexData.empty()) continue;
						cursor = (cursor + 31u) & ~(size_t)31u;
						newSet->offsets[i] = (UINT)cursor;
						memcpy(tls_packScratch.data() + cursor, cmd.vertexData.data(), cmd.vertexData.size());
						cursor += cmd.vertexData.size();
					}
					srcData = tls_packScratch.data();
					createBytes = (UINT)cursor;
				}

				D3D11_BUFFER_DESC desc = {};
				desc.ByteWidth = createBytes;
				desc.Usage = D3D11_USAGE_IMMUTABLE;
				desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				D3D11_SUBRESOURCE_DATA init = { srcData, 0, 0 };
				if (SUCCEEDED(s_pDevice->CreateBuffer(&desc, &init, &newSet->pVB)) && newSet->pVB)
				{
					++s_frameCBuffUploadCount;
					s_frameCBuffUploadBytes += createBytes;

					std::lock_guard<std::mutex> lock(s_cbuffMutex);
					auto it = s_cbuffs.find(index);
					if (it != s_cbuffs.end() && it->second.generation == gen)
					{
						if (it->second.gpuVBs) s_pendingReleaseVBSets.push_back(std::move(it->second.gpuVBs));
						it->second.gpuVBs = newSet;
						it->second.uploadedGen = gen;
						vbSet = std::move(newSet);
					}
					else
					{
						s_pendingReleaseVBSets.push_back(std::move(newSet));
					}
				}
			}
		}

		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 curModel = s_tls.matrixStacks[0].back();
		Matrix4x4 curProj = s_tls.matrixStacks[1].back();

		for (size_t i = 0; i < cmds->size(); ++i)
		{
			const auto &cmd = (*cmds)[i];
			if (cmd.count <= 0 || cmd.vertexData.empty()) continue;

			Matrix4x4 effModel = Matrix4x4::Multiply(curModel, cmd.relativeMatrix);
			Matrix4x4 wvp = Matrix4x4::Multiply(curProj, effModel);
			int texId = (s_tls.boundTextureId != 0) ? s_tls.boundTextureId : cmd.boundTextureId;
			float combined[4] = {
				s_tls.stateColour[0] * cmd.stateColour[0],
				s_tls.stateColour[1] * cmd.stateColour[1],
				s_tls.stateColour[2] * cmd.stateColour[2],
				s_tls.stateColour[3] * cmd.stateColour[3]
			};
			ID3D11Buffer *staticVB = (vbSet && vbSet->pVB) ? vbSet->pVB : NULL;
			UINT staticOffset = (staticVB && i < vbSet->offsets.size()) ? vbSet->offsets[i] : 0u;
			ExecuteDrawInternal(cmd.primitiveType, cmd.count, staticVB, staticOffset,
				staticVB ? NULL : cmd.vertexData.data(), (UINT)cmd.vertexData.size(),
				cmd.vType, cmd.psType, wvp, effModel, texId, combined,
				s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
				s_tls.depthTestEnable, s_tls.depthMask,
				s_tls.cullEnable, s_tls.cullCW,
				s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
		}
		return 1;
	}

	/* Textures */
	static int DX11_TextureCreate(void) { return s_nextTextureId.fetch_add(1); }
	static void DX11_TextureFree(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (s_cachedTexId == idx) s_cachedTexId = -1;
		if (s_cachedLightTexId == idx) s_cachedLightTexId = -1;
		auto it = s_textures.find(idx);
		if (it != s_textures.end())
		{
			if (it->second.pSRV) it->second.pSRV->Release();
			if (it->second.pTexture) it->second.pTexture->Release();
			s_textures.erase(it);
		}
	}
	static void DX11_TextureBind(int idx)
	{
		if (s_tls.boundTextureId != idx)
		{
			s_tls.boundTextureId = idx;
			s_tls.textureLevels = 1;
		}
	}
	static void DX11_TextureBindVertex(int idx) { s_tls.boundVertexTextureId = idx; }
	static void DX11_TextureSetLevels(int levels) { s_tls.textureLevels = levels; }
	static int  DX11_TextureGetLevels(void)
	{
		if (s_tls.boundTextureId > 0)
		{
			std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end()) return it->second.mipLevels;
		}
		return s_tls.textureLevels;
	}
	static void DX11_TextureData(int width, int height, void *data, int level, int)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (!s_pDevice || !s_pContext || s_tls.boundTextureId <= 0 || width <= 0 || height <= 0 || !data) return;

		if (level == 0)
		{
			if (s_cachedTexId == s_tls.boundTextureId) s_cachedTexId = -1;
			if (s_cachedLightTexId == s_tls.boundTextureId) s_cachedLightTexId = -1;
			bool prevLinear = false;
			bool prevClamp = false;
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end())
			{
				prevLinear = it->second.linearFilter;
				prevClamp = it->second.clampWrap;
				if (it->second.pSRV) it->second.pSRV->Release();
				if (it->second.pTexture) it->second.pTexture->Release();
				s_textures.erase(it);
			}
			UINT maxMips = ComputeMaxMipLevels(width, height);
			UINT mips = (s_tls.textureLevels > 1) ? (UINT)s_tls.textureLevels : 1u;
			if (mips > maxMips) mips = maxMips;

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = (UINT)width;
			desc.Height = (UINT)height;
			desc.MipLevels = mips;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			ID3D11Texture2D *pTex = NULL;
			if (SUCCEEDED(s_pDevice->CreateTexture2D(&desc, NULL, &pTex)) && pTex)
			{
				s_pContext->UpdateSubresource(pTex, 0, NULL, data, (UINT)(width * 4), 0);
				ID3D11ShaderResourceView *pSRV = NULL;
				if (SUCCEEDED(s_pDevice->CreateShaderResourceView(pTex, NULL, &pSRV)) && pSRV)
				{
					DX11TextureData td = { pTex, pSRV, width, height, (int)mips, prevLinear, prevClamp };
					s_textures[s_tls.boundTextureId] = td;
					s_tls.textureLevels = (int)mips;
				}
				else
				{
					pTex->Release();
					if (prevLinear || prevClamp)
					{
						DX11TextureData td = { NULL, NULL, 0, 0, 1, prevLinear, prevClamp };
						s_textures[s_tls.boundTextureId] = td;
					}
					s_tls.textureLevels = 1;
				}
			}
			else
			{
				if (prevLinear || prevClamp)
				{
					DX11TextureData td = { NULL, NULL, 0, 0, 1, prevLinear, prevClamp };
					s_textures[s_tls.boundTextureId] = td;
				}
				s_tls.textureLevels = 1;
			}
		}
		else
		{
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end() && it->second.pTexture && level >= 0 && level < it->second.mipLevels)
			{
				s_pContext->UpdateSubresource(it->second.pTexture, (UINT)level, NULL, data, (UINT)(width * 4), 0);
			}
		}
	}
	static void DX11_TextureDataUpdate(int xoff, int yoff, int width, int height, void *data, int level)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (!s_pContext || s_tls.boundTextureId <= 0 || !data || width <= 0 || height <= 0) return;
		auto it = s_textures.find(s_tls.boundTextureId);
		if (it != s_textures.end() && it->second.pTexture && level >= 0 && level < it->second.mipLevels)
		{
			D3D11_BOX box = { (UINT)xoff, (UINT)yoff, 0, (UINT)(xoff + width), (UINT)(yoff + height), 1 };
			s_pContext->UpdateSubresource(it->second.pTexture, (UINT)level, &box, data, (UINT)(width * 4), 0);
		}
	}
	static void DX11_TextureSetParam(int param, int value)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (s_tls.boundTextureId <= 0) return;
		DX11TextureData &td = s_textures[s_tls.boundTextureId];
		if (param == 1 /* MIN_FILTER */ || param == 2 /* MAG_FILTER */) td.linearFilter = (value != 0);
		else if (param == 3 /* WRAP_S */ || param == 4 /* WRAP_T */) td.clampWrap = (value == 0);
		if (s_cachedTexId == s_tls.boundTextureId) s_cachedTexId = -1;
	}
	static void *DX11_TextureGetNativeHandle(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		auto it = s_textures.find(idx);
		return (it != s_textures.end()) ? (void*)it->second.pSRV : NULL;
	}

	/* State setters */
	static void DX11_StateSetColour(float r, float g, float b, float a) { s_tls.stateColour[0]=r; s_tls.stateColour[1]=g; s_tls.stateColour[2]=b; s_tls.stateColour[3]=a; }
	static void DX11_StateSetDepthMask(int e) { s_tls.depthMask = (e != 0); }
	static void DX11_StateSetBlendEnable(int e) { s_tls.blendEnable = (e != 0); }
	static void DX11_StateSetBlendFunc(int s, int d) { s_tls.blendSrc = s; s_tls.blendDst = d; }
	static void DX11_StateSetBlendFactor(unsigned int c) { s_tls.blendFactorRGBA = c; }
	static void DX11_StateSetAlphaFunc(int f, float p) { s_tls.alphaFunc = f; s_tls.alphaCutoff = p; }
	static void DX11_StateSetDepthFunc(int f) { s_tls.depthFunc = f; }
	static void DX11_StateSetFaceCull(int e) { s_tls.cullEnable = (e != 0); }
	static void DX11_StateSetFaceCullCW(int e) { s_tls.cullCW = (e != 0); }
	static void DX11_StateSetLineWidth(float) {}
	static void DX11_StateSetWriteEnable(int r, int g, int b, int a)
	{
		uint8_t m = 0;
		if (r) m |= D3D11_COLOR_WRITE_ENABLE_RED;
		if (g) m |= D3D11_COLOR_WRITE_ENABLE_GREEN;
		if (b) m |= D3D11_COLOR_WRITE_ENABLE_BLUE;
		if (a) m |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
		s_tls.colorWriteMask = m;
	}
	static void DX11_StateSetDepthTestEnable(int e) { s_tls.depthTestEnable = (e != 0); }
	static void DX11_StateSetAlphaTestEnable(int e) { s_tls.alphaTestEnable = (e != 0); }
	static void DX11_StateSetDepthSlopeAndBias(float slope, float bias) { s_tls.depthBiasSlope = slope; s_tls.depthBiasConstant = bias; }
	static void DX11_StateSetFogEnable(int e) { s_tls.fogEnable = (e != 0); }
	static void DX11_StateSetFogMode(int m) { s_tls.fogMode = m; }
	static void DX11_StateSetFogNear(float d) { s_tls.fogNear = d; }
	static void DX11_StateSetFogFar(float d) { s_tls.fogFar = d; }
	static void DX11_StateSetFogDensity(float d) { s_tls.fogDensity = d; }
	static void DX11_StateSetFogColour(float r, float g, float b) { s_tls.fogColor[0]=r; s_tls.fogColor[1]=g; s_tls.fogColor[2]=b; }
	static void DX11_StateSetLightingEnable(int e) { s_tls.lightingEnable = (e != 0); }
	static void DX11_StateSetVertexTextureUV(float u, float v) { s_tls.vertexTexUV[0]=u; s_tls.vertexTexUV[1]=v; }
	static void DX11_StateSetLightColour(int l, float r, float g, float b) { if (l>=0 && l<2) { s_tls.lightColor[l][0]=r; s_tls.lightColor[l][1]=g; s_tls.lightColor[l][2]=b; } }
	static void DX11_StateSetLightAmbientColour(float r, float g, float b) { s_tls.lightAmbient[0]=r; s_tls.lightAmbient[1]=g; s_tls.lightAmbient[2]=b; s_tls.lightAmbient[3]=1.0f; }
	static void DX11_StateSetLightDirection(int l, float x, float y, float z)
	{
		if (l < 0 || l >= 2) return;
		s_tls.EnsureStackInit(0);
		float tx, ty, tz;
		Matrix4x4::TransformVec3(s_tls.matrixStacks[0].back(), x, y, z, tx, ty, tz);
		s_tls.lightDir[l][0]=tx; s_tls.lightDir[l][1]=ty; s_tls.lightDir[l][2]=tz; s_tls.lightDir[l][3]=0.0f;
	}
	static void DX11_StateSetLightEnable(int l, int e) { if (l>=0 && l<2) s_tls.lightEnabled[l] = (e != 0); }
	static void DX11_StateSetViewport(int viewportType)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		s_currentViewportType = viewportType;
		if (!s_pContext) return;
		float vx, vy, vw, vh;
		ComputeViewportRect(viewportType, s_width, s_height, vx, vy, vw, vh);
		s_activeViewport = { vx, vy, vw, vh, 0.0f, 1.0f };
		s_pContext->RSSetViewports(1, &s_activeViewport);
	}
	static void DX11_StateSetTexGenCol(int col, float x, float y, float z, float w, int eyeSpace)
	{
		(void)eyeSpace;
		if (col >= 0 && col < 4) { s_tls.texGenPlanes[col][0]=x; s_tls.texGenPlanes[col][1]=y; s_tls.texGenPlanes[col][2]=z; s_tls.texGenPlanes[col][3]=w; }
	}
	static void DX11_StateSetStencil(int func, uint8_t ref, uint8_t fmask, uint8_t wmask)
	{
		s_tls.stencilEnable = (func != C4J_CMP_ALWAYS);
		s_tls.stencilFunc = func;
		s_tls.stencilRef = ref;
		s_tls.stencilReadMask = fmask;
		s_tls.stencilWriteMask = wmask;
	}
	static void DX11_BeginEvent(const wchar_t*) {}
	static void DX11_Suspend(void) { s_suspended = true; }
	static int  DX11_Suspended(void) { return s_suspended ? 1 : 0; }
	static void DX11_Resume(void) { s_suspended = false; }

	/* Iggy / GDraw bridge */
	static void *DX11_GDrawCreateContext(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		GDrawFunctions *funcs = gdraw_D3D11_CreateContext(s_pDevice, s_pContext, w, h);
		if (funcs)
		{
			gdraw_D3D11_SetResourceLimits(GDRAW_D3D11_RESOURCE_vertexbuffer, 5000, 64 * 1024 * 1024);
			gdraw_D3D11_SetResourceLimits(GDRAW_D3D11_RESOURCE_texture, 5000, 128 * 1024 * 1024);
			gdraw_D3D11_SetResourceLimits(GDRAW_D3D11_RESOURCE_rendertarget, 10, 32 * 1024 * 1024);
		}
		InvalidateStateCache();
		return (void*)funcs;
	}
	static void DX11_GDrawDestroyContext(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_DestroyContext();
		InvalidateStateCache();
	}
	static void DX11_GDrawSetRendertargetSize(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_SetRendertargetSize(w, h);
		InvalidateStateCache();
	}
	static void DX11_GDrawSetTileOrigin(int xPos, int yPos)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_SetTileOrigin(s_pRTV, s_pDSV, NULL, xPos, yPos);
		InvalidateStateCache();
	}
	static void DX11_GDrawNoMoreGDrawThisFrame(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_NoMoreGDrawThisFrame();
		InvalidateStateCache();
	}
	static void DX11_GDrawBeginCustomDraw4J(void *region, float *mat16)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_BeginCustomDraw_4J((IggyCustomDrawCallbackRegion*)region, mat16);
		InvalidateStateCache();
	}
	static void DX11_GDrawCalculateCustomDraw4J(void *region, float *mat16)
	{
		gdraw_D3D11_CalculateCustomDraw_4J((IggyCustomDrawCallbackRegion*)region, mat16);
	}
	static void DX11_GDrawEndCustomDraw(void *region)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_EndCustomDraw((IggyCustomDrawCallbackRegion*)region);
		InvalidateStateCache();
	}
	static void DX11_GDrawSetViewport4J(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		gdraw_D3D11_setViewport_4J();
		InvalidateStateCache();
	}
	static void *DX11_GDrawWrappedTextureCreate(int textureId)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		ID3D11ShaderResourceView *srv = (ID3D11ShaderResourceView*)DX11_TextureGetNativeHandle(textureId);
		if (!srv) return NULL;
		return (void*)gdraw_D3D11_WrappedTextureCreate(srv);
	}
	static void DX11_GDrawWrappedTextureDestroy(void *handle)
	{
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (handle) gdraw_D3D11_WrappedTextureDestroy((GDrawTexture*)handle);
	}

	static int DX11_CaptureBackBufferRGBA(C4JCaptureAllocFn allocFn, void *user, int *outWidth, int *outHeight)
	{
		if (outWidth) *outWidth = 0;
		if (outHeight) *outHeight = 0;
		std::lock_guard<std::recursive_mutex> lock(s_d3dMutex);
		if (!s_pSwapChain || !s_pDevice || !s_pContext || s_suspended || !allocFn) return 0;

		ID3D11Texture2D *backBuffer = NULL;
		HRESULT hr = s_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr) || !backBuffer) return 0;

		D3D11_TEXTURE2D_DESC desc = {};
		backBuffer->GetDesc(&desc);
		if (desc.Width == 0 || desc.Height == 0)
		{
			backBuffer->Release();
			return 0;
		}

		const bool isBgra =
			(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
		const bool isRgba =
			(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
		if (!isBgra && !isRgba)
		{
			backBuffer->Release();
			return 0;
		}

		ID3D11Texture2D *copySource = backBuffer;
		ID3D11Texture2D *resolvedTexture = NULL;
		if (desc.SampleDesc.Count > 1)
		{
			D3D11_TEXTURE2D_DESC resolveDesc = desc;
			resolveDesc.BindFlags = 0;
			resolveDesc.CPUAccessFlags = 0;
			resolveDesc.MiscFlags = 0;
			resolveDesc.Usage = D3D11_USAGE_DEFAULT;
			resolveDesc.SampleDesc.Count = 1;
			resolveDesc.SampleDesc.Quality = 0;
			hr = s_pDevice->CreateTexture2D(&resolveDesc, NULL, &resolvedTexture);
			if (FAILED(hr) || !resolvedTexture)
			{
				backBuffer->Release();
				return 0;
			}
			s_pContext->ResolveSubresource(resolvedTexture, 0, backBuffer, 0, desc.Format);
			copySource = resolvedTexture;
		}

		D3D11_TEXTURE2D_DESC stagingDesc = desc;
		stagingDesc.BindFlags = 0;
		stagingDesc.MiscFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.ArraySize = 1;
		stagingDesc.MipLevels = 1;
		stagingDesc.SampleDesc.Count = 1;
		stagingDesc.SampleDesc.Quality = 0;

		ID3D11Texture2D *stagingTexture = NULL;
		hr = s_pDevice->CreateTexture2D(&stagingDesc, NULL, &stagingTexture);
		if (FAILED(hr) || !stagingTexture)
		{
			if (resolvedTexture) resolvedTexture->Release();
			backBuffer->Release();
			return 0;
		}

		s_pContext->CopyResource(stagingTexture, copySource);
		s_pContext->Flush();

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		hr = s_pContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr) || !mapped.pData)
		{
			stagingTexture->Release();
			if (resolvedTexture) resolvedTexture->Release();
			backBuffer->Release();
			return 0;
		}

		int ok = 0;
		uint8_t *dstPixels = NULL;
		if (allocFn((int)desc.Width, (int)desc.Height, &dstPixels, user) && dstPixels)
		{
			const size_t rowBytes = (size_t)desc.Width * 4u;
			for (UINT y = 0; y < desc.Height; ++y)
			{
				const uint8_t *srcRow = (const uint8_t *)mapped.pData + ((size_t)mapped.RowPitch * y);
				uint8_t *dstRow = dstPixels + (rowBytes * y);
				for (UINT x = 0; x < desc.Width; ++x)
				{
					const uint8_t c0 = srcRow[(x * 4u) + 0u];
					const uint8_t c1 = srcRow[(x * 4u) + 1u];
					const uint8_t c2 = srcRow[(x * 4u) + 2u];
					if (isBgra)
					{
						dstRow[(x * 4u) + 0u] = c2;
						dstRow[(x * 4u) + 1u] = c1;
						dstRow[(x * 4u) + 2u] = c0;
					}
					else
					{
						dstRow[(x * 4u) + 0u] = c0;
						dstRow[(x * 4u) + 1u] = c1;
						dstRow[(x * 4u) + 2u] = c2;
					}
					dstRow[(x * 4u) + 3u] = 0xFFu;
				}
			}
			if (outWidth) *outWidth = (int)desc.Width;
			if (outHeight) *outHeight = (int)desc.Height;
			ok = 1;
		}

		s_pContext->Unmap(stagingTexture, 0);
		stagingTexture->Release();
		if (resolvedTexture) resolvedTexture->Release();
		backBuffer->Release();
		return ok;
	}

	static const C4JRenderBackendAPI s_dx11API =
	{
		sizeof(C4JRenderBackendAPI),
		C4J_RENDER_BACKEND_ABI_VERSION,
		C4J_BACKEND_DX11,
		"DirectX 11",
		DX11_InitialiseWindow,
		DX11_ResizeWindow,
		DX11_Shutdown,
		DX11_SetVsync,
		DX11_GetVsync,
		DX11_GetBackendInfo,
		DX11_Tick,
		DX11_UpdateGamma,
		DX11_StartFrame,
		DX11_DoScreenGrabOnNextPresent,
		DX11_Present,
		DX11_Clear,
		DX11_SetClearColour,
		DX11_IsWidescreen,
		DX11_IsHiDef,
		DX11_NoOpInt,
		DX11_NoOpVoid,
		DX11_NoOpInt,
		DX11_NoOpVoid,
		DX11_MatrixMode,
		DX11_MatrixSetIdentity,
		DX11_MatrixTranslate,
		DX11_MatrixRotate,
		DX11_MatrixScale,
		DX11_MatrixPerspective,
		DX11_MatrixOrthogonal,
		DX11_MatrixPop,
		DX11_MatrixPush,
		DX11_MatrixMult,
		DX11_MatrixGet,
		DX11_SetMatrixDirty,
		DX11_DrawVertices,
		DX11_DrawVertexBuffer,
		DX11_NoOpVoid,
		DX11_CBuffCreate,
		DX11_CBuffDelete,
		DX11_CBuffStart,
		DX11_CBuffClear,
		DX11_CBuffSize,
		DX11_CBuffEnd,
		DX11_CBuffCall,
		DX11_NoOpVoid,
		DX11_NoOpVoid,
		DX11_NoOpVoid,
		DX11_TextureCreate,
		DX11_TextureFree,
		DX11_TextureBind,
		DX11_TextureBindVertex,
		DX11_TextureSetLevels,
		DX11_TextureGetLevels,
		DX11_TextureData,
		DX11_TextureDataUpdate,
		DX11_TextureSetParam,
		DX11_NoOpVoid,
		DX11_NoOpVoid,
		DX11_TextureGetNativeHandle,
		DX11_StateSetColour,
		DX11_StateSetDepthMask,
		DX11_StateSetBlendEnable,
		DX11_StateSetBlendFunc,
		DX11_StateSetBlendFactor,
		DX11_StateSetAlphaFunc,
		DX11_StateSetDepthFunc,
		DX11_StateSetFaceCull,
		DX11_StateSetFaceCullCW,
		DX11_StateSetLineWidth,
		DX11_StateSetWriteEnable,
		DX11_StateSetDepthTestEnable,
		DX11_StateSetAlphaTestEnable,
		DX11_StateSetDepthSlopeAndBias,
		DX11_StateSetFogEnable,
		DX11_StateSetFogMode,
		DX11_StateSetFogNear,
		DX11_StateSetFogFar,
		DX11_StateSetFogDensity,
		DX11_StateSetFogColour,
		DX11_StateSetLightingEnable,
		DX11_StateSetVertexTextureUV,
		DX11_StateSetLightColour,
		DX11_StateSetLightAmbientColour,
		DX11_StateSetLightDirection,
		DX11_StateSetLightEnable,
		DX11_StateSetViewport,
		DX11_NoOpInt,
		DX11_StateSetTexGenCol,
		DX11_StateSetStencil,
		DX11_NoOpInt,
		DX11_BeginEvent,
		DX11_NoOpVoid,
		DX11_Suspend,
		DX11_Suspended,
		DX11_Resume,
		DX11_GDrawCreateContext,
		DX11_GDrawDestroyContext,
		DX11_GDrawSetRendertargetSize,
		DX11_GDrawSetTileOrigin,
		DX11_GDrawNoMoreGDrawThisFrame,
		DX11_GDrawBeginCustomDraw4J,
		DX11_GDrawCalculateCustomDraw4J,
		DX11_GDrawEndCustomDraw,
		DX11_GDrawSetViewport4J,
		DX11_GDrawWrappedTextureCreate,
		DX11_GDrawWrappedTextureDestroy,
		DX11_CaptureBackBufferRGBA
	};
}

extern "C" __declspec(dllexport) const C4JRenderBackendAPI *C4JBackend_GetAPI(void)
{
	return &s_dx11API;
}
