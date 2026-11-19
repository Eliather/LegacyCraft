#include "RenderCommon.h"
#include "dx12_backend_context.h"
#include <stdio.h>
#include <unordered_map>
#include <atomic>

using namespace C4JRenderCommon;

extern "C" __declspec(dllexport) const C4JRenderBackendAPI *C4JBackend_GetAPI(void);

#define C4J_DX12_SWAP_BUFFERS 3
#define C4J_DX12_UPLOAD_RING_BYTES (48u * 1024u * 1024u)
#define C4J_DX12_RING_RESERVE_BYTES (8u * 1024u * 1024u)
#define C4J_DX12_VB_SLAB_BYTES (8u * 1024u * 1024u)
#define C4J_DX12_CBUFF_UPLOAD_BUDGET_BYTES (6u * 1024u * 1024u)
#define C4J_DX12_CBUFF_UPLOAD_BUDGET_COUNT 96u
#define C4J_DX12_MAX_QUADS 65536u
#define C4J_DX12_MAX_FAN_TRIS 4096u

namespace
{
	struct DX12TextureSlot
	{
		ID3D12Resource *resource = NULL;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		int width = 0;
		int height = 0;
		int mipLevels = 1;
		bool linearFilter = false;
		bool clampWrap = false;
	};

	struct DX12VBFreeBlock
	{
		UINT64 offset = 0;
		UINT64 size = 0;
	};

	struct DX12VBSlab
	{
		ID3D12Resource *res = NULL;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
		UINT64 size = 0;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
		std::vector<DX12VBFreeBlock> freeBlocks;
	};

	struct DX12VBSlice
	{
		int slabIdx = -1;
		UINT64 offset = 0;
		UINT size = 0;
		bool valid() const { return slabIdx >= 0 && size > 0; }
	};

	struct DX12BakedVB
	{
		DX12VBSlice slice;
		uint64_t generation = 0;
		bool dirty = true;
	};

	struct DX12CBuffSlot
	{
		std::shared_ptr<const std::vector<CBuffCommand>> commands;
		std::vector<DX12BakedVB> gpuVBs;
		size_t byteSize = 0;
		uint64_t generation = 0;
	};

	struct DeferredReleaseItem
	{
		uint64_t fenceValue;
		IUnknown *obj;
	};

	struct DeferredSliceFreeItem
	{
		uint64_t fenceValue;
		DX12VBSlice slice;
	};

	/* Global DX12 core */
	static std::recursive_mutex s_dx12Mutex;
	static std::mutex s_cbuffMutex;

	static HWND s_hwnd = NULL;
	static int s_width = 1280;
	static int s_height = 720;
	static bool s_fullscreen = false;
	static bool s_vsync = false;
	static bool s_suspended = false;
	static bool s_allowTearing = false;
	static char s_adapterName[128] = "DirectX 12 Adapter";

	static IDXGIFactory4 *s_factory = NULL;
	static ID3D12Device *s_device = NULL;
	static ID3D12CommandQueue *s_queue = NULL;
	static IDXGISwapChain3 *s_swapChain = NULL;
	static ID3D12Resource *s_backbuffers[C4J_DX12_SWAP_BUFFERS] = {};
	static D3D12_CPU_DESCRIPTOR_HANDLE s_backbufferRTVs[C4J_DX12_SWAP_BUFFERS] = {};
	static UINT s_currentBackbuffer = 0;

	static ID3D12Resource *s_depthBuffer = NULL;
	static D3D12_CPU_DESCRIPTOR_HANDLE s_depthDSV = {};

	static ID3D12DescriptorHeap *s_rtvHeap = NULL;
	static UINT s_rtvDescSize = 0;
	static ID3D12DescriptorHeap *s_dsvHeap = NULL;
	static UINT s_dsvDescSize = 0;
	static ID3D12DescriptorHeap *s_samplerHeap = NULL;
	static UINT s_samplerDescSize = 0;

	/* Synchronization & frames-in-flight */
	static ID3D12Fence *s_fence = NULL;
	static HANDLE s_fenceEvent = NULL;
	static uint64_t s_nextFenceValue = 1;

	static ID3D12CommandAllocator *s_cmdAllocs[C4J_DX12_FRAMES_IN_FLIGHT] = {};
	static ID3D12GraphicsCommandList *s_cmdList = NULL;
	static uint64_t s_frameFenceValues[C4J_DX12_FRAMES_IN_FLIGHT] = {};
	static uint32_t s_frameIdx = 0;
	static uint64_t s_frameSeq = 1;
	static bool s_frameRecording = false;
	static bool s_backbufferInRTState = false;

	/* Per-frame upload ring & SRV heap */
	static ID3D12Resource *s_uploadRings[C4J_DX12_FRAMES_IN_FLIGHT] = {};
	static uint8_t *s_uploadMapped[C4J_DX12_FRAMES_IN_FLIGHT] = {};
	static uint32_t s_uploadOffset = 0;

	static ID3D12DescriptorHeap *s_srvHeaps[C4J_DX12_FRAMES_IN_FLIGHT] = {};
	static UINT s_srvDescSize = 0;
	static uint32_t s_srvCursor = 0;

	/* One-shot upload helper (for texture uploads before/outside frame recording) */
	static ID3D12CommandAllocator *s_uploadAlloc = NULL;
	static ID3D12GraphicsCommandList *s_uploadCmd = NULL;

	/* Static index buffers & dummy texture */
	static ID3D12Resource *s_quadIB = NULL;
	static ID3D12Resource *s_fanIB = NULL;
	static ID3D12Resource *s_dummyWhiteTex = NULL;

	/* World pipeline & PSO cache */
	static ID3D12RootSignature *s_worldRootSig = NULL;
	static ID3DBlob *s_vsStd = NULL;
	static ID3DBlob *s_vsComp = NULL;
	static ID3DBlob *s_psWorld = NULL;
	static std::unordered_map<uint64_t, ID3D12PipelineState*> s_worldPSOCache;
	static ID3D12PipelineState *s_cachedWorldPSO = NULL;
	static uint64_t s_cachedWorldPSOKey = ~0ULL;
	static bool s_cachedWorldPSOKeyValid = false;
	static ID3D12RootSignature *s_cachedBoundRootSig = NULL;
	static ID3D12Resource *s_cachedTex0Res = NULL;
	static ID3D12Resource *s_cachedTex1Res = NULL;
	static D3D12_GPU_DESCRIPTOR_HANDLE s_cachedTex0Gpu = {};
	static bool s_cachedTexLinear = false;
	static bool s_cachedTexClamp = false;
	static int s_cachedTexLookupId = -1;
	static ID3D12Resource *s_cachedTexLookupRes = NULL;
	static bool s_cachedTexLookupLinear = false;
	static bool s_cachedTexLookupClamp = false;
	static bool s_cachedTexLookupHas = false;
	static int s_cachedLightLookupId = -1;
	static ID3D12Resource *s_cachedLightLookupRes = NULL;
	static bool s_cachedLightLookupHas = false;
	static VSConstantBufferData s_lastVSConsts = {};
	static D3D12_GPU_VIRTUAL_ADDRESS s_lastVSGpuVA = 0;
	static bool s_lastVSConstsValid = false;
	static D3D12_GPU_VIRTUAL_ADDRESS s_cachedBoundVSGpu = 0;
	static PSConstantBufferData s_lastPSConsts = {};
	static D3D12_GPU_VIRTUAL_ADDRESS s_lastPSGpuVA = 0;
	static bool s_lastPSConstsValid = false;
	static D3D12_GPU_VIRTUAL_ADDRESS s_cachedBoundPSGpu = 0;
	static bool s_cachedBlendFactorValid = false;
	static unsigned int s_cachedBlendFactorRGBA = 0;
	static bool s_cachedBlendFactorUsesAlpha = false;
	static bool s_cachedStencilRefValid = false;
	static uint8_t s_cachedStencilRef = 0;
	static int s_cachedIBMode = 0; /* 0 = none, 1 = quad, 2 = fan */
	static D3D_PRIMITIVE_TOPOLOGY s_cachedTopo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	static D3D12_GPU_VIRTUAL_ADDRESS s_cachedVBLoc = 0;
	static UINT s_cachedVBSize = 0;
	static UINT s_cachedVBStride = 0;

	struct PendingDX12TexUpdate
	{
		int texId;
		ID3D12Resource *resource;
		int xoffset, yoffset, width, height, level;
		std::vector<uint8_t> pixels;
	};
	static std::vector<PendingDX12TexUpdate> s_pendingTexUpdates;

	/* Viewport, Clear & State */
	static int s_currentViewportType = 0;
	static D3D12_VIEWPORT s_activeViewport = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
	static D3D12_RECT s_activeScissor = { 0, 0, 1280, 720 };
	static float s_clearColour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	static std::unordered_map<int, DX12TextureSlot> s_textures;
	static int s_nextTextureId = 1;
	static std::unordered_map<int, DX12CBuffSlot> s_cbuffs;
	static int s_nextCBuffId = 1;
	static std::atomic<size_t> s_totalCBuffBytes(0);
	static std::vector<DeferredReleaseItem> s_deferredReleases;
	static std::vector<DX12VBSlab> s_vbSlabs;
	static std::vector<DeferredSliceFreeItem> s_deferredFreeSlices;
	static size_t s_frameCBuffUploadBytes = 0;
	static UINT s_frameCBuffUploadCount = 0;

	static thread_local ThreadRenderState s_tls;
	static DX12BackendGDrawHost s_gdrawHost = {};

	/* Exact same HLSL shaders as DX11, split across b0 (VSParams) and b1 (PSParams) */
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
		"cbuffer PSParams : register(b1)\n"
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

	/* Helpers */
	static void WaitForFenceValue(uint64_t val)
	{
		if (!s_fence || val == 0) return;
		if (s_fence->GetCompletedValue() < val)
		{
			s_fence->SetEventOnCompletion(val, s_fenceEvent);
			WaitForSingleObject(s_fenceEvent, INFINITE);
		}
	}

	static void WaitForGPUIdle(void)
	{
		if (!s_queue || !s_fence) return;
		uint64_t val = s_nextFenceValue++;
		if (SUCCEEDED(s_queue->Signal(s_fence, val)))
		{
			WaitForFenceValue(val);
		}
	}

	static ID3D12Resource *CreateCommittedBuffer(UINT64 size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initState)
	{
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = heapType;
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width = (size > 0) ? size : 256;
		rd.Height = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource *res = NULL;
		if (FAILED(s_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, NULL, __uuidof(ID3D12Resource), (void**)&res)))
		{
			return NULL;
		}
		return res;
	}

	static UINT64 AlignVBSliceSize(UINT sizeBytes)
	{
		UINT64 aligned = ((UINT64)sizeBytes + 255ull) & ~255ull;
		return (aligned > 0) ? aligned : 256ull;
	}

	static void FreeVBSliceToSlab(const DX12VBSlice &slice)
	{
		if (!slice.valid() || slice.slabIdx < 0 || slice.slabIdx >= (int)s_vbSlabs.size()) return;
		DX12VBSlab &slab = s_vbSlabs[slice.slabIdx];
		UINT64 alignedSz = AlignVBSliceSize(slice.size);
		DX12VBFreeBlock nb = { slice.offset, alignedSz };

		auto pos = std::lower_bound(slab.freeBlocks.begin(), slab.freeBlocks.end(), nb.offset,
			[](const DX12VBFreeBlock &b, UINT64 off) { return b.offset < off; });
		pos = slab.freeBlocks.insert(pos, nb);

		if (pos + 1 != slab.freeBlocks.end())
		{
			auto next = pos + 1;
			if (pos->offset + pos->size == next->offset)
			{
				pos->size += next->size;
				slab.freeBlocks.erase(next);
			}
		}
		if (pos != slab.freeBlocks.begin())
		{
			auto prev = pos - 1;
			if (prev->offset + prev->size == pos->offset)
			{
				prev->size += pos->size;
				slab.freeBlocks.erase(pos);
			}
		}
	}

	static bool AllocVBSlice(UINT sizeBytes, DX12VBSlice &outSlice)
	{
		if (!s_device || sizeBytes == 0) return false;
		const UINT64 alignedSz = AlignVBSliceSize(sizeBytes);

		for (int si = 0; si < (int)s_vbSlabs.size(); ++si)
		{
			DX12VBSlab &slab = s_vbSlabs[si];
			for (size_t bi = 0; bi < slab.freeBlocks.size(); ++bi)
			{
				if (slab.freeBlocks[bi].size >= alignedSz)
				{
					outSlice.slabIdx = si;
					outSlice.offset = slab.freeBlocks[bi].offset;
					outSlice.size = sizeBytes;
					if (slab.freeBlocks[bi].size == alignedSz)
					{
						slab.freeBlocks.erase(slab.freeBlocks.begin() + (ptrdiff_t)bi);
					}
					else
					{
						slab.freeBlocks[bi].offset += alignedSz;
						slab.freeBlocks[bi].size -= alignedSz;
					}
					return true;
				}
			}
		}

		const UINT64 slabBytes = std::max<UINT64>(C4J_DX12_VB_SLAB_BYTES, alignedSz);
		ID3D12Resource *res = CreateCommittedBuffer(slabBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
		if (!res) return false;

		DX12VBSlab slab;
		slab.res = res;
		slab.gpuVA = res->GetGPUVirtualAddress();
		slab.size = slabBytes;
		slab.state = D3D12_RESOURCE_STATE_COPY_DEST;
		if (slabBytes > alignedSz)
		{
			slab.freeBlocks.push_back({ alignedSz, slabBytes - alignedSz });
		}
		int newIdx = (int)s_vbSlabs.size();
		s_vbSlabs.push_back(std::move(slab));
		outSlice.slabIdx = newIdx;
		outSlice.offset = 0;
		outSlice.size = sizeBytes;
		return true;
	}

	static void TransitionVBSlab(ID3D12GraphicsCommandList *cmd, int slabIdx, D3D12_RESOURCE_STATES target)
	{
		if (!cmd || slabIdx < 0 || slabIdx >= (int)s_vbSlabs.size()) return;
		DX12VBSlab &slab = s_vbSlabs[slabIdx];
		if (!slab.res || slab.state == target) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = slab.res;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = slab.state;
		b.Transition.StateAfter = target;
		cmd->ResourceBarrier(1, &b);
		slab.state = target;
	}

	static void QueueDeferredSliceFreeLocked(const DX12VBSlice &slice)
	{
		if (slice.valid())
		{
			s_deferredFreeSlices.push_back({ s_nextFenceValue + 1, slice });
		}
	}

	static void DeferredReleaseIUnknownFn(IUnknown *obj)
	{
		if (!obj) return;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		s_deferredReleases.push_back({ s_nextFenceValue + 1, obj });
	}

	static void DrainDeferredReleases(bool forceAll)
	{
		std::vector<DeferredReleaseItem> toRelease;
		std::vector<DeferredSliceFreeItem> toFreeSlices;
		uint64_t completed = s_fence ? s_fence->GetCompletedValue() : UINT64_MAX;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			if (!s_deferredReleases.empty())
			{
				std::vector<DeferredReleaseItem> keep;
				for (const auto &item : s_deferredReleases)
				{
					if (forceAll || item.fenceValue <= completed) toRelease.push_back(item);
					else keep.push_back(item);
				}
				s_deferredReleases.swap(keep);
			}
			if (!s_deferredFreeSlices.empty())
			{
				std::vector<DeferredSliceFreeItem> keepSlices;
				for (const auto &item : s_deferredFreeSlices)
				{
					if (forceAll || item.fenceValue <= completed) toFreeSlices.push_back(item);
					else keepSlices.push_back(item);
				}
				s_deferredFreeSlices.swap(keepSlices);
			}
		}
		for (auto &item : toRelease)
		{
			if (item.obj) item.obj->Release();
		}
		for (const auto &item : toFreeSlices)
		{
			FreeVBSliceToSlab(item.slice);
		}
	}

	static void ExecuteOneShotUploadFn(void (*recordFn)(ID3D12GraphicsCommandList *cmd, void *user), void *user)
	{
		if (!s_device || !s_queue || !s_uploadAlloc || !s_uploadCmd || !recordFn) return;
		s_uploadAlloc->Reset();
		s_uploadCmd->Reset(s_uploadAlloc, NULL);
		recordFn(s_uploadCmd, user);
		s_uploadCmd->Close();
		ID3D12CommandList *lists[1] = { s_uploadCmd };
		s_queue->ExecuteCommandLists(1, lists);
		WaitForGPUIdle();
	}

	static void InvalidateWorldPipelineCacheFn(void)
	{
		s_cachedWorldPSO = NULL;
		s_cachedWorldPSOKey = ~0ULL;
		s_cachedWorldPSOKeyValid = false;
		s_cachedBoundRootSig = NULL;
		s_cachedTex0Res = NULL;
		s_cachedTex1Res = NULL;
		s_cachedTexLookupId = -1;
		s_cachedLightLookupId = -1;
		s_lastVSConstsValid = false;
		s_lastPSConstsValid = false;
		s_cachedBoundVSGpu = 0;
		s_cachedBoundPSGpu = 0;
		s_cachedBlendFactorValid = false;
		s_cachedStencilRefValid = false;
		s_cachedIBMode = 0;
		s_cachedTopo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		s_cachedVBLoc = 0;
		s_cachedVBSize = 0;
		s_cachedVBStride = 0;
	}

	static void FlushPendingDX12TexUpdates(void);

	static void EnsureFrameRecordingFn(void)
	{
		if (s_frameRecording || !s_device || !s_cmdList || !s_swapChain) return;

		WaitForFenceValue(s_frameFenceValues[s_frameIdx]);
		DrainDeferredReleases(false);

		s_currentBackbuffer = s_swapChain->GetCurrentBackBufferIndex();
		s_cmdAllocs[s_frameIdx]->Reset();
		s_cmdList->Reset(s_cmdAllocs[s_frameIdx], NULL);
		s_uploadOffset = 0;
		s_srvCursor = 0;
		s_frameCBuffUploadBytes = 0;
		s_frameCBuffUploadCount = 0;
		InvalidateWorldPipelineCacheFn();

		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = s_backbuffers[s_currentBackbuffer];
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		s_cmdList->ResourceBarrier(1, &b);

		s_backbufferInRTState = true;
		s_frameRecording = true;

		s_gdrawHost.activeCmd = s_cmdList;
		s_gdrawHost.activeMainRTV = s_backbufferRTVs[s_currentBackbuffer];
		s_gdrawHost.activeMainDSV = s_depthDSV;
		s_gdrawHost.frameIndex = s_frameIdx;
		s_gdrawHost.frameSeq = s_frameSeq;

		FlushPendingDX12TexUpdates();
	}

	static void *AllocFrameUploadBytesFn(uint32_t size, uint32_t align, D3D12_GPU_VIRTUAL_ADDRESS *outGpuVA)
	{
		if (size == 0) size = 16;
		if (align < 16) align = 16;
		uint32_t aligned = (s_uploadOffset + (align - 1u)) & ~(align - 1u);
		if (aligned + size > C4J_DX12_UPLOAD_RING_BYTES)
		{
			return NULL;
		}
		void *ptr = s_uploadMapped[s_frameIdx] + aligned;
		if (outGpuVA)
		{
			*outGpuVA = s_uploadRings[s_frameIdx]->GetGPUVirtualAddress() + aligned;
		}
		s_uploadOffset = aligned + size;
		return ptr;
	}

	static bool AllocateFrameSRVFn(ID3D12Resource *res, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE *outCpu, D3D12_GPU_DESCRIPTOR_HANDLE *outGpu)
	{
		if (!s_srvHeaps[s_frameIdx] || !res || s_srvCursor >= C4J_DX12_FRAME_SRV_CAPACITY) return false;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = s_srvHeaps[s_frameIdx]->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = s_srvHeaps[s_frameIdx]->GetGPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = { cpuStart.ptr + (SIZE_T)s_srvCursor * s_srvDescSize };
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = { gpuStart.ptr + (UINT64)s_srvCursor * s_srvDescSize };
		s_device->CreateShaderResourceView(res, desc, cpu);
		++s_srvCursor;
		if (outCpu) *outCpu = cpu;
		if (outGpu) *outGpu = gpu;
		return true;
	}

	static ID3D12DescriptorHeap *GetActiveSRVHeapFn(void) { return s_srvHeaps[s_frameIdx]; }

	static D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGPUHandleFn(bool linearFilter, bool clampWrap)
	{
		int idx = (linearFilter ? 2 : 0) + (clampWrap ? 1 : 0);
		D3D12_GPU_DESCRIPTOR_HANDLE base = s_samplerHeap->GetGPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE out = { base.ptr + (UINT64)idx * s_samplerDescSize };
		return out;
	}

	static ID3D12Resource *GetMCTextureResourceFn(int textureId, int *outW, int *outH, bool *outLinear, bool *outClamp)
	{
		auto it = s_textures.find(textureId);
		if (it == s_textures.end()) return NULL;
		if (outW) *outW = it->second.width;
		if (outH) *outH = it->second.height;
		if (outLinear) *outLinear = it->second.linearFilter;
		if (outClamp) *outClamp = it->second.clampWrap;
		return it->second.resource;
	}

	static void ApplyMCViewportAndTargetFn(void)
	{
		if (!s_frameRecording || !s_cmdList) return;
		s_cmdList->OMSetRenderTargets(1, &s_backbufferRTVs[s_currentBackbuffer], FALSE, &s_depthDSV);
		s_cmdList->RSSetViewports(1, &s_activeViewport);
		s_cmdList->RSSetScissorRects(1, &s_activeScissor);
	}

	static void TransitionMCTexture(ID3D12GraphicsCommandList *cmd, DX12TextureSlot &slot, D3D12_RESOURCE_STATES target)
	{
		if (!cmd || !slot.resource || slot.state == target) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = slot.resource;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = slot.state;
		b.Transition.StateAfter = target;
		cmd->ResourceBarrier(1, &b);
		slot.state = target;
	}

	static IDXGIAdapter1 *SelectBestDiscreteAdapter(IDXGIFactory4 *pFactory)
	{
		if (!pFactory) return NULL;
		IDXGIFactory6 *pFactory6 = NULL;
		if (SUCCEEDED(pFactory->QueryInterface(__uuidof(IDXGIFactory6), (void**)&pFactory6)) && pFactory6)
		{
			IDXGIAdapter1 *pHighPerf = NULL;
			if (SUCCEEDED(pFactory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&pHighPerf)) && pHighPerf)
			{
				DXGI_ADAPTER_DESC1 desc = {};
				if (SUCCEEDED(pHighPerf->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				{
					pFactory6->Release();
					return pHighPerf;
				}
				pHighPerf->Release();
			}
			pFactory6->Release();
		}

		IDXGIAdapter1 *best = NULL;
		SIZE_T bestScore = 0;
		for (UINT i = 0;; ++i)
		{
			IDXGIAdapter1 *cand = NULL;
			if (pFactory->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(cand->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
			{
				SIZE_T score = desc.DedicatedVideoMemory + ((desc.VendorId == 0x10DE || desc.VendorId == 0x1002) ? (SIZE_T)1 << 40 : 0);
				if (!best || score > bestScore)
				{
					if (best) best->Release();
					best = cand;
					bestScore = score;
					continue;
				}
			}
			cand->Release();
		}
		return best;
	}

	static bool CreateSwapchainResources(int width, int height)
	{
		for (UINT i = 0; i < C4J_DX12_SWAP_BUFFERS; ++i)
		{
			if (FAILED(s_swapChain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&s_backbuffers[i])) || !s_backbuffers[i])
			{
				return false;
			}
			D3D12_CPU_DESCRIPTOR_HANDLE base = s_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			s_backbufferRTVs[i].ptr = base.ptr + (SIZE_T)i * s_rtvDescSize;
			s_device->CreateRenderTargetView(s_backbuffers[i], NULL, s_backbufferRTVs[i]);
		}

		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC dd = {};
		dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		dd.Width = (UINT64)width;
		dd.Height = (UINT)height;
		dd.DepthOrArraySize = 1;
		dd.MipLevels = 1;
		dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dd.SampleDesc.Count = 1;
		dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE cv = {};
		cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		cv.DepthStencil.Depth = 1.0f;
		cv.DepthStencil.Stencil = 0;

		if (FAILED(s_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, __uuidof(ID3D12Resource), (void**)&s_depthBuffer)))
		{
			return false;
		}
		s_depthDSV = s_dsvHeap->GetCPUDescriptorHandleForHeapStart();
		s_device->CreateDepthStencilView(s_depthBuffer, NULL, s_depthDSV);

		float vx, vy, vw, vh;
		ComputeViewportRect(s_currentViewportType, width, height, vx, vy, vw, vh);
		s_activeViewport = { vx, vy, vw, vh, 0.0f, 1.0f };
		s_activeScissor = { 0, 0, (LONG)width, (LONG)height };
		return true;
	}

	static void ReleaseSwapchainResources(void)
	{
		for (UINT i = 0; i < C4J_DX12_SWAP_BUFFERS; ++i)
		{
			if (s_backbuffers[i]) { s_backbuffers[i]->Release(); s_backbuffers[i] = NULL; }
		}
		if (s_depthBuffer) { s_depthBuffer->Release(); s_depthBuffer = NULL; }
	}

	static D3D12_BLEND MapBlend(int factor)
	{
		switch (factor)
		{
		case C4J_BLEND_ZERO:             return D3D12_BLEND_ZERO;
		case C4J_BLEND_ONE:              return D3D12_BLEND_ONE;
		case C4J_BLEND_SRC_COLOR:        return D3D12_BLEND_SRC_COLOR;
		case C4J_BLEND_INV_SRC_COLOR:    return D3D12_BLEND_INV_SRC_COLOR;
		case C4J_BLEND_SRC_ALPHA:        return D3D12_BLEND_SRC_ALPHA;
		case C4J_BLEND_INV_SRC_ALPHA:    return D3D12_BLEND_INV_SRC_ALPHA;
		case C4J_BLEND_DEST_ALPHA:       return D3D12_BLEND_DEST_ALPHA;
		case C4J_BLEND_INV_DEST_ALPHA:   return D3D12_BLEND_INV_DEST_ALPHA;
		case C4J_BLEND_DEST_COLOR:       return D3D12_BLEND_DEST_COLOR;
		case C4J_BLEND_INV_DEST_COLOR:   return D3D12_BLEND_INV_DEST_COLOR;
		case C4J_BLEND_SRC_ALPHA_SAT:    return D3D12_BLEND_SRC_ALPHA_SAT;
		case C4J_BLEND_FACTOR_ALPHA:     return D3D12_BLEND_BLEND_FACTOR;
		case C4J_BLEND_INV_FACTOR_ALPHA: return D3D12_BLEND_INV_BLEND_FACTOR;
		default:                         return D3D12_BLEND_SRC_ALPHA;
		}
	}

	static D3D12_BLEND SanitizeAlphaBlend(D3D12_BLEND b)
	{
		switch (b)
		{
		case D3D12_BLEND_SRC_COLOR:      return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR:  return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR:     return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
		default:                         return b;
		}
	}

	static D3D12_COMPARISON_FUNC MapCmp(int func)
	{
		if (func >= 1 && func <= 8) return (D3D12_COMPARISON_FUNC)func;
		return D3D12_COMPARISON_FUNC_LESS_EQUAL;
	}

	static ID3D12PipelineState *GetOrCreateWorldPSO(
		int vType, int primType,
		bool blendEnable, int blendSrc, int blendDst, uint8_t writeMask,
		bool depthTestEnable, bool depthMask, int depthFunc,
		bool stencilEnable, int stencilFunc,
		bool cullEnable, bool cullCW, int depthBiasBucket)
	{
		const bool isLine = (primType == 4 || primType == 5);
		const int isComp = (vType == 1) ? 1 : 0;

		uint64_t key = ((uint64_t)isComp) |
			((uint64_t)(isLine ? 1 : 0) << 1) |
			((uint64_t)(blendEnable ? 1 : 0) << 2) |
			((uint64_t)(blendSrc & 0x1F) << 3) |
			((uint64_t)(blendDst & 0x1F) << 8) |
			((uint64_t)(writeMask & 0xF) << 13) |
			((uint64_t)(depthTestEnable ? 1 : 0) << 17) |
			((uint64_t)(depthMask ? 1 : 0) << 18) |
			((uint64_t)(depthFunc & 0xF) << 19) |
			((uint64_t)(stencilEnable ? 1 : 0) << 23) |
			((uint64_t)(stencilFunc & 0xF) << 24) |
			((uint64_t)(cullEnable ? 1 : 0) << 28) |
			((uint64_t)(cullCW ? 1 : 0) << 29) |
			((uint64_t)(depthBiasBucket & 0x3) << 30);

		if (s_cachedWorldPSOKeyValid && key == s_cachedWorldPSOKey && s_cachedWorldPSO)
		{
			return s_cachedWorldPSO;
		}

		auto it = s_worldPSOCache.find(key);
		if (it != s_worldPSOCache.end())
		{
			s_cachedWorldPSOKey = key;
			s_cachedWorldPSOKeyValid = true;
			return it->second;
		}

		D3D12_INPUT_ELEMENT_DESC stdLayout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32_UINT,        0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32_UINT,        0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32_UINT,        0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		D3D12_INPUT_ELEMENT_DESC compLayout[] = {
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_SINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_SINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = s_worldRootSig;
		pso.VS.pShaderBytecode = (isComp ? s_vsComp : s_vsStd)->GetBufferPointer();
		pso.VS.BytecodeLength = (isComp ? s_vsComp : s_vsStd)->GetBufferSize();
		pso.PS.pShaderBytecode = s_psWorld->GetBufferPointer();
		pso.PS.BytecodeLength = s_psWorld->GetBufferSize();
		pso.InputLayout.pInputElementDescs = isComp ? compLayout : stdLayout;
		pso.InputLayout.NumElements = isComp ? 2 : 5;

		pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pso.RasterizerState.FrontCounterClockwise = TRUE;
		pso.RasterizerState.CullMode = !cullEnable ? D3D12_CULL_MODE_NONE : (cullCW ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_FRONT);
		pso.RasterizerState.DepthClipEnable = TRUE;
		if (depthBiasBucket != 0)
		{
			/* glWrapper::glPolygonOffset divides OpenGL factor/units by 65536.0f; restore scale for D3D12 SlopeScaledDepthBias and D24 DepthBias. */
			pso.RasterizerState.SlopeScaledDepthBias = s_tls.depthBiasSlope * 65536.0f;
			pso.RasterizerState.DepthBias = (INT)lroundf(s_tls.depthBiasConstant * 100000.0f);
		}

		D3D12_BLEND sb = MapBlend(blendSrc);
		D3D12_BLEND db = MapBlend(blendDst);
		pso.BlendState.RenderTarget[0].BlendEnable = blendEnable ? TRUE : FALSE;
		pso.BlendState.RenderTarget[0].SrcBlend = sb;
		pso.BlendState.RenderTarget[0].DestBlend = db;
		pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		pso.BlendState.RenderTarget[0].SrcBlendAlpha = SanitizeAlphaBlend(sb);
		pso.BlendState.RenderTarget[0].DestBlendAlpha = SanitizeAlphaBlend(db);
		pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		pso.BlendState.RenderTarget[0].RenderTargetWriteMask = writeMask;

		pso.DepthStencilState.DepthEnable = depthTestEnable ? TRUE : FALSE;
		pso.DepthStencilState.DepthWriteMask = depthMask ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		pso.DepthStencilState.DepthFunc = MapCmp(depthFunc);
		pso.DepthStencilState.StencilEnable = stencilEnable ? TRUE : FALSE;
		pso.DepthStencilState.StencilReadMask = s_tls.stencilReadMask;
		pso.DepthStencilState.StencilWriteMask = s_tls.stencilWriteMask;
		pso.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.FrontFace.StencilFunc = MapCmp(stencilFunc);
		pso.DepthStencilState.BackFace = pso.DepthStencilState.FrontFace;

		pso.SampleMask = UINT_MAX;
		pso.PrimitiveTopologyType = isLine ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		pso.SampleDesc.Count = 1;

		ID3D12PipelineState *created = NULL;
		if (FAILED(s_device->CreateGraphicsPipelineState(&pso, __uuidof(ID3D12PipelineState), (void**)&created)))
		{
			return NULL;
		}
		s_worldPSOCache[key] = created;
		return created;
	}

	static bool CreateWorldPipelineResources(void)
	{
		ID3DBlob *err = NULL;
		if (FAILED(D3DCompile(s_hlslVS, strlen(s_hlslVS), "WorldVS", NULL, NULL, "VSMainStandard", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &s_vsStd, &err)))
		{
			if (err) err->Release();
			return false;
		}
		if (err) { err->Release(); err = NULL; }

		if (FAILED(D3DCompile(s_hlslVS, strlen(s_hlslVS), "WorldVS", NULL, NULL, "VSMainCompressed", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &s_vsComp, &err)))
		{
			if (err) err->Release();
			return false;
		}
		if (err) { err->Release(); err = NULL; }

		if (FAILED(D3DCompile(s_hlslPS, strlen(s_hlslPS), "WorldPS", NULL, NULL, "PSMain", "ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &s_psWorld, &err)))
		{
			if (err) err->Release();
			return false;
		}
		if (err) { err->Release(); err = NULL; }

		D3D12_DESCRIPTOR_RANGE rSRVs = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
		D3D12_DESCRIPTOR_RANGE rSamp0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
		D3D12_DESCRIPTOR_RANGE rSamp1 = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };

		D3D12_ROOT_PARAMETER params[5] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[2].DescriptorTable.NumDescriptorRanges = 1;
		params[2].DescriptorTable.pDescriptorRanges = &rSRVs;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[3].DescriptorTable.NumDescriptorRanges = 1;
		params[3].DescriptorTable.pDescriptorRanges = &rSamp0;
		params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[4].DescriptorTable.NumDescriptorRanges = 1;
		params[4].DescriptorTable.pDescriptorRanges = &rSamp1;
		params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
		rsDesc.NumParameters = 5;
		rsDesc.pParameters = params;
		rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ID3DBlob *sigBlob = NULL;
		if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &err)))
		{
			if (err) err->Release();
			return false;
		}
		if (err) err->Release();
		if (FAILED(s_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
			__uuidof(ID3D12RootSignature), (void**)&s_worldRootSig)))
		{
			sigBlob->Release();
			return false;
		}
		sigBlob->Release();

		/* Static Quad & Fan IBs in upload-friendly heap or default heap */
		std::vector<uint32_t> quadIdx(C4J_DX12_MAX_QUADS * 6u);
		for (uint32_t q = 0; q < C4J_DX12_MAX_QUADS; ++q)
		{
			uint32_t v = q * 4u;
			quadIdx[q * 6u + 0u] = v + 0u;
			quadIdx[q * 6u + 1u] = v + 1u;
			quadIdx[q * 6u + 2u] = v + 2u;
			quadIdx[q * 6u + 3u] = v + 0u;
			quadIdx[q * 6u + 4u] = v + 2u;
			quadIdx[q * 6u + 5u] = v + 3u;
		}
		s_quadIB = CreateCommittedBuffer(quadIdx.size() * sizeof(uint32_t), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (!s_quadIB) return false;
		void *mp = NULL;
		if (SUCCEEDED(s_quadIB->Map(0, NULL, &mp)) && mp) { memcpy(mp, quadIdx.data(), quadIdx.size() * sizeof(uint32_t)); s_quadIB->Unmap(0, NULL); }

		std::vector<uint32_t> fanIdx(C4J_DX12_MAX_FAN_TRIS * 3u);
		for (uint32_t t = 0; t < C4J_DX12_MAX_FAN_TRIS; ++t)
		{
			fanIdx[t * 3u + 0u] = 0u;
			fanIdx[t * 3u + 1u] = t + 1u;
			fanIdx[t * 3u + 2u] = t + 2u;
		}
		s_fanIB = CreateCommittedBuffer(fanIdx.size() * sizeof(uint32_t), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (!s_fanIB) return false;
		mp = NULL;
		if (SUCCEEDED(s_fanIB->Map(0, NULL, &mp)) && mp) { memcpy(mp, fanIdx.data(), fanIdx.size() * sizeof(uint32_t)); s_fanIB->Unmap(0, NULL); }

		/* 1x1 white dummy texture */
		D3D12_HEAP_PROPERTIES hpDef = {};
		hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC trd = {};
		trd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		trd.Width = 1;
		trd.Height = 1;
		trd.DepthOrArraySize = 1;
		trd.MipLevels = 1;
		trd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		trd.SampleDesc.Count = 1;
		if (FAILED(s_device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &trd,
			D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), (void**)&s_dummyWhiteTex)))
		{
			return false;
		}
		const UINT pitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
		ID3D12Resource *st = CreateCommittedBuffer(pitch, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (st)
		{
			void *smp = NULL;
			if (SUCCEEDED(st->Map(0, NULL, &smp)) && smp)
			{
				uint32_t wpx = 0xFFFFFFFFu;
				memcpy(smp, &wpx, 4);
				st->Unmap(0, NULL);
			}
			struct InitWhiteCtx { ID3D12Resource *dst; ID3D12Resource *src; } wctx = { s_dummyWhiteTex, st };
			ExecuteOneShotUploadFn([](ID3D12GraphicsCommandList *cmd, void *u) {
				InitWhiteCtx *c = (InitWhiteCtx*)u;
				D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = c->dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = c->src; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				s.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				s.PlacedFootprint.Footprint.Width = 1; s.PlacedFootprint.Footprint.Height = 1; s.PlacedFootprint.Footprint.Depth = 1;
				s.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
				cmd->CopyTextureRegion(&d, 0, 0, 0, &s, NULL);
				D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource = c->dst; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
				b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				cmd->ResourceBarrier(1, &b);
			}, &wctx);
			st->Release();
		}
		return true;
	}

	static void ExecuteDrawInternal(
		int primType, int count,
		ID3D12Resource *staticVB, UINT64 staticVBOffset, UINT staticVBBytes,
		const void *dynamicBytes, UINT dynamicByteCount,
		int vType, int psType,
		const Matrix4x4 &wvp, const Matrix4x4 &mv,
		int texId, const float stateColor[4],
		bool blendEnable, int blendSrc, int blendDst,
		bool depthTestEnable, bool depthMask,
		bool cullEnable, bool cullCW,
		bool alphaTestEnable, int alphaFunc, float alphaCutoff)
	{
		if (count <= 0) return;
		EnsureFrameRecordingFn();
		if (!s_cmdList) return;

		const bool isComp = (vType == 1);
		const UINT stride = isComp ? 16u : 32u;

		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		if (staticVB)
		{
			vbv.BufferLocation = staticVB->GetGPUVirtualAddress() + staticVBOffset;
			vbv.SizeInBytes = staticVBBytes;
			vbv.StrideInBytes = stride;
		}
		else
		{
			if (!dynamicBytes || dynamicByteCount == 0) return;
			D3D12_GPU_VIRTUAL_ADDRESS vbGpu = 0;
			void *dst = AllocFrameUploadBytesFn(dynamicByteCount, 16u, &vbGpu);
			if (!dst) return;
			memcpy(dst, dynamicBytes, dynamicByteCount);
			vbv.BufferLocation = vbGpu;
			vbv.SizeInBytes = dynamicByteCount;
			vbv.StrideInBytes = stride;
		}

		ID3D12Resource *tex0Res = s_dummyWhiteTex;
		bool texLinear = false;
		bool texClamp = false;
		bool hasTex = false;
		if (texId > 0)
		{
			if (s_cachedTexLookupId == texId)
			{
				tex0Res = s_cachedTexLookupRes;
				texLinear = s_cachedTexLookupLinear;
				texClamp = s_cachedTexLookupClamp;
				hasTex = s_cachedTexLookupHas;
			}
			else
			{
				auto it = s_textures.find(texId);
				if (it != s_textures.end() && it->second.resource)
				{
					TransitionMCTexture(s_cmdList, it->second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					tex0Res = it->second.resource;
					texLinear = it->second.linearFilter;
					texClamp = it->second.clampWrap;
					hasTex = true;
				}
				s_cachedTexLookupId = texId;
				s_cachedTexLookupRes = tex0Res;
				s_cachedTexLookupLinear = texLinear;
				s_cachedTexLookupClamp = texClamp;
				s_cachedTexLookupHas = hasTex;
			}
		}

		ID3D12Resource *tex1Res = s_dummyWhiteTex;
		bool hasLight = false;
		if (s_tls.boundVertexTextureId > 0)
		{
			if (s_cachedLightLookupId == s_tls.boundVertexTextureId)
			{
				tex1Res = s_cachedLightLookupRes;
				hasLight = s_cachedLightLookupHas;
			}
			else
			{
				auto itL = s_textures.find(s_tls.boundVertexTextureId);
				if (itL != s_textures.end() && itL->second.resource)
				{
					TransitionMCTexture(s_cmdList, itL->second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					tex1Res = itL->second.resource;
					hasLight = true;
				}
				s_cachedLightLookupId = s_tls.boundVertexTextureId;
				s_cachedLightLookupRes = tex1Res;
				s_cachedLightLookupHas = hasLight;
			}
		}

		const bool rootSigChanged = (s_cachedBoundRootSig != s_worldRootSig);
		const bool needDescUpdate = rootSigChanged ||
			(tex0Res != s_cachedTex0Res) || (tex1Res != s_cachedTex1Res);
		const bool needSamplerUpdate = rootSigChanged ||
			(texLinear != s_cachedTexLinear) || (texClamp != s_cachedTexClamp);

		D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = s_cachedTex0Gpu;
		if (needDescUpdate)
		{
			if (s_srvCursor + 2 > C4J_DX12_FRAME_SRV_CAPACITY) return;
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MipLevels = (UINT)-1;

			D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = {};
			AllocateFrameSRVFn(tex0Res, &srvDesc, &cpu0, &gpu0);
			AllocateFrameSRVFn(tex1Res, &srvDesc, NULL, NULL);
			s_cachedTex0Res = tex0Res;
			s_cachedTex1Res = tex1Res;
			s_cachedTex0Gpu = gpu0;
		}

		VSConstantBufferData vsConsts = {};
		FillVSConstants(s_tls, wvp, mv, vType, psType, vsConsts);
		PSConstantBufferData psConsts = {};
		FillPSConstants(s_tls, stateColor, hasTex, hasLight, alphaTestEnable, alphaFunc, alphaCutoff, psType, psConsts);

		D3D12_GPU_VIRTUAL_ADDRESS vsGpu = s_lastVSGpuVA;
		if (!s_lastVSConstsValid || memcmp(&vsConsts, &s_lastVSConsts, sizeof(vsConsts)) != 0)
		{
			const UINT vsAligned = (sizeof(vsConsts) + 255u) & ~255u;
			void *vsPtr = AllocFrameUploadBytesFn(vsAligned, 256u, &vsGpu);
			if (!vsPtr) return;
			memcpy(vsPtr, &vsConsts, sizeof(vsConsts));
			s_lastVSConsts = vsConsts;
			s_lastVSGpuVA = vsGpu;
			s_lastVSConstsValid = true;
		}

		D3D12_GPU_VIRTUAL_ADDRESS psGpu = s_lastPSGpuVA;
		if (!s_lastPSConstsValid || memcmp(&psConsts, &s_lastPSConsts, sizeof(psConsts)) != 0)
		{
			const UINT psAligned = (sizeof(psConsts) + 255u) & ~255u;
			void *psPtr = AllocFrameUploadBytesFn(psAligned, 256u, &psGpu);
			if (!psPtr) return;
			memcpy(psPtr, &psConsts, sizeof(psConsts));
			s_lastPSConsts = psConsts;
			s_lastPSGpuVA = psGpu;
			s_lastPSConstsValid = true;
		}

		int biasBucket = (fabsf(s_tls.depthBiasSlope) > 1e-6f || fabsf(s_tls.depthBiasConstant) > 1e-6f) ? 1 : 0;
		ID3D12PipelineState *pso = GetOrCreateWorldPSO(
			vType, primType, blendEnable, blendSrc, blendDst, s_tls.colorWriteMask,
			depthTestEnable, depthMask, s_tls.depthFunc,
			s_tls.stencilEnable, s_tls.stencilFunc,
			cullEnable, cullCW, biasBucket);
		if (!pso) return;

		if (rootSigChanged)
		{
			ID3D12DescriptorHeap *heaps[2] = { s_srvHeaps[s_frameIdx], s_samplerHeap };
			s_cmdList->SetDescriptorHeaps(2, heaps);
			s_cmdList->SetGraphicsRootSignature(s_worldRootSig);
			s_cmdList->OMSetRenderTargets(1, &s_backbufferRTVs[s_currentBackbuffer], FALSE, &s_depthDSV);
			s_cmdList->RSSetViewports(1, &s_activeViewport);
			s_cmdList->RSSetScissorRects(1, &s_activeScissor);
			s_cachedBoundRootSig = s_worldRootSig;
			s_cmdList->SetGraphicsRootDescriptorTable(4, GetSamplerGPUHandleFn(true, true));
			s_cachedBoundVSGpu = 0;
			s_cachedBoundPSGpu = 0;
		}
		if (s_cachedWorldPSO != pso)
		{
			s_cmdList->SetPipelineState(pso);
			s_cachedWorldPSO = pso;
		}

		const bool blendFactorUsesAlpha = (blendSrc == C4J_BLEND_FACTOR_ALPHA || blendDst == C4J_BLEND_INV_FACTOR_ALPHA ||
			blendSrc == C4J_BLEND_INV_FACTOR_ALPHA || blendDst == C4J_BLEND_FACTOR_ALPHA);
		if (!s_cachedBlendFactorValid || s_cachedBlendFactorRGBA != s_tls.blendFactorRGBA ||
			s_cachedBlendFactorUsesAlpha != blendFactorUsesAlpha)
		{
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
			s_cmdList->OMSetBlendFactor(blendFactor);
			s_cachedBlendFactorValid = true;
			s_cachedBlendFactorRGBA = s_tls.blendFactorRGBA;
			s_cachedBlendFactorUsesAlpha = blendFactorUsesAlpha;
		}

		if (!s_cachedStencilRefValid || s_cachedStencilRef != s_tls.stencilRef)
		{
			s_cmdList->OMSetStencilRef(s_tls.stencilRef);
			s_cachedStencilRefValid = true;
			s_cachedStencilRef = s_tls.stencilRef;
		}

		if (s_cachedBoundVSGpu != vsGpu)
		{
			s_cmdList->SetGraphicsRootConstantBufferView(0, vsGpu);
			s_cachedBoundVSGpu = vsGpu;
		}
		if (s_cachedBoundPSGpu != psGpu)
		{
			s_cmdList->SetGraphicsRootConstantBufferView(1, psGpu);
			s_cachedBoundPSGpu = psGpu;
		}
		if (needDescUpdate)
		{
			s_cmdList->SetGraphicsRootDescriptorTable(2, gpu0);
		}
		if (needSamplerUpdate)
		{
			s_cmdList->SetGraphicsRootDescriptorTable(3, GetSamplerGPUHandleFn(texLinear, texClamp));
			s_cachedTexLinear = texLinear;
			s_cachedTexClamp = texClamp;
		}
		if (s_cachedVBLoc != vbv.BufferLocation || s_cachedVBSize != vbv.SizeInBytes || s_cachedVBStride != vbv.StrideInBytes)
		{
			s_cmdList->IASetVertexBuffers(0, 1, &vbv);
			s_cachedVBLoc = vbv.BufferLocation;
			s_cachedVBSize = vbv.SizeInBytes;
			s_cachedVBStride = vbv.StrideInBytes;
		}

		const bool isQuad = (primType == 3 || primType == 7);
		if (isQuad && s_quadIB && count >= 4 && (count % 4) == 0)
		{
			if (s_cachedIBMode != 1)
			{
				D3D12_INDEX_BUFFER_VIEW ibv = { s_quadIB->GetGPUVirtualAddress(), (UINT)(C4J_DX12_MAX_QUADS * 6u * 4u), DXGI_FORMAT_R32_UINT };
				s_cmdList->IASetIndexBuffer(&ibv);
				s_cachedIBMode = 1;
			}
			if (s_cachedTopo != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
			{
				s_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				s_cachedTopo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			}
			UINT quadsRemaining = (UINT)(count / 4);
			UINT vertStart = 0;
			while (quadsRemaining > 0)
			{
				UINT batch = (quadsRemaining > C4J_DX12_MAX_QUADS) ? C4J_DX12_MAX_QUADS : quadsRemaining;
				s_cmdList->DrawIndexedInstanced(batch * 6u, 1, 0, (INT)vertStart, 0);
				quadsRemaining -= batch;
				vertStart += batch * 4u;
			}
		}
		else if (primType == 2 && s_fanIB && count >= 3)
		{
			UINT tris = (UINT)(count - 2);
			if (tris > C4J_DX12_MAX_FAN_TRIS) tris = C4J_DX12_MAX_FAN_TRIS;
			if (s_cachedIBMode != 2)
			{
				D3D12_INDEX_BUFFER_VIEW ibv = { s_fanIB->GetGPUVirtualAddress(), (UINT)(C4J_DX12_MAX_FAN_TRIS * 3u * 4u), DXGI_FORMAT_R32_UINT };
				s_cmdList->IASetIndexBuffer(&ibv);
				s_cachedIBMode = 2;
			}
			if (s_cachedTopo != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)
			{
				s_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				s_cachedTopo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			}
			s_cmdList->DrawIndexedInstanced(tris * 3u, 1, 0, 0, 0);
		}
		else
		{
			D3D_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			if (primType == 1) topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			else if (primType == 4) topo = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			else if (primType == 5) topo = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			if (s_cachedTopo != topo)
			{
				s_cmdList->IASetPrimitiveTopology(topo);
				s_cachedTopo = topo;
			}
			s_cmdList->DrawInstanced((UINT)count, 1, 0, 0);
		}
	}

	/* ABI Functions */
	static int DX12_InitialiseWindow(HWND hwnd, int width, int height, int fullscreen, int vsync)
	{
		s_hwnd = hwnd;
		s_width = (width > 0) ? width : 1280;
		s_height = (height > 0) ? height : 720;
		s_fullscreen = (fullscreen != 0);
		s_vsync = (vsync != 0);

		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&s_factory)) || !s_factory)
		{
			return 0;
		}

		IDXGIAdapter1 *adapter = SelectBestDiscreteAdapter(s_factory);
		if (adapter)
		{
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(adapter->GetDesc1(&desc)))
			{
				WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, s_adapterName, sizeof(s_adapterName), NULL, NULL);
			}
		}

		HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&s_device);
		if (adapter) adapter->Release();
		if (FAILED(hr) || !s_device)
		{
			return 0;
		}

		IDXGIFactory5 *f5 = NULL;
		s_allowTearing = false;
		if (SUCCEEDED(s_factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)) && f5)
		{
			BOOL allow = FALSE;
			if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
			{
				s_allowTearing = (allow != FALSE);
			}
			f5->Release();
		}

		D3D12_COMMAND_QUEUE_DESC qd = {};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(s_device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&s_queue)))
		{
			return 0;
		}

		DXGI_SWAP_CHAIN_DESC1 sc1 = {};
		sc1.Width = (UINT)s_width;
		sc1.Height = (UINT)s_height;
		sc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sc1.SampleDesc.Count = 1;
		sc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sc1.BufferCount = C4J_DX12_SWAP_BUFFERS;
		sc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sc1.Flags = s_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		IDXGISwapChain1 *scBase = NULL;
		hr = s_factory->CreateSwapChainForHwnd(s_queue, s_hwnd, &sc1, NULL, NULL, &scBase);
		s_factory->MakeWindowAssociation(s_hwnd, DXGI_MWA_NO_ALT_ENTER);
		if (FAILED(hr) || !scBase) return 0;
		scBase->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&s_swapChain);
		scBase->Release();
		if (!s_swapChain) return 0;

		/* Descriptor Heaps */
		D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
		if (FAILED(s_device->CreateDescriptorHeap(&rtvDesc, __uuidof(ID3D12DescriptorHeap), (void**)&s_rtvHeap))) return 0;
		s_rtvDescSize = s_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
		if (FAILED(s_device->CreateDescriptorHeap(&dsvDesc, __uuidof(ID3D12DescriptorHeap), (void**)&s_dsvHeap))) return 0;
		s_dsvDescSize = s_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		D3D12_DESCRIPTOR_HEAP_DESC smpDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
		if (FAILED(s_device->CreateDescriptorHeap(&smpDesc, __uuidof(ID3D12DescriptorHeap), (void**)&s_samplerHeap))) return 0;
		s_samplerDescSize = s_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		D3D12_CPU_DESCRIPTOR_HANDLE smpBase = s_samplerHeap->GetCPUDescriptorHandleForHeapStart();
		for (int i = 0; i < 4; ++i)
		{
			bool linear = (i >= 2);
			bool clamp = ((i & 1) != 0);
			D3D12_SAMPLER_DESC sd = {};
			sd.Filter = linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
			sd.AddressU = clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			sd.AddressV = clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sd.MaxLOD = D3D12_FLOAT32_MAX;
			sd.MaxAnisotropy = 1;
			sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
			D3D12_CPU_DESCRIPTOR_HANDLE dst = { smpBase.ptr + (SIZE_T)i * s_samplerDescSize };
			s_device->CreateSampler(&sd, dst);
		}

		s_srvDescSize = s_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (uint32_t f = 0; f < C4J_DX12_FRAMES_IN_FLIGHT; ++f)
		{
			if (FAILED(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&s_cmdAllocs[f])))
				return 0;

			s_uploadRings[f] = CreateCommittedBuffer(C4J_DX12_UPLOAD_RING_BYTES, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
			if (!s_uploadRings[f]) return 0;
			void *mp = NULL;
			if (FAILED(s_uploadRings[f]->Map(0, NULL, &mp)) || !mp) return 0;
			s_uploadMapped[f] = (uint8_t*)mp;

			D3D12_DESCRIPTOR_HEAP_DESC srvHD = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, C4J_DX12_FRAME_SRV_CAPACITY, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
			if (FAILED(s_device->CreateDescriptorHeap(&srvHD, __uuidof(ID3D12DescriptorHeap), (void**)&s_srvHeaps[f]))) return 0;
		}

		if (FAILED(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_cmdAllocs[0], NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&s_cmdList)))
			return 0;
		s_cmdList->Close();

		if (FAILED(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&s_uploadAlloc)) ||
			FAILED(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_uploadAlloc, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&s_uploadCmd)))
			return 0;
		s_uploadCmd->Close();

		if (FAILED(s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&s_fence))) return 0;
		s_fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!s_fenceEvent) return 0;

		if (!CreateSwapchainResources(s_width, s_height) || !CreateWorldPipelineResources()) return 0;

		s_gdrawHost.device = s_device;
		s_gdrawHost.queue = s_queue;
		s_gdrawHost.rtvHeap = s_rtvHeap;
		s_gdrawHost.rtvDescSize = s_rtvDescSize;
		s_gdrawHost.dsvHeap = s_dsvHeap;
		s_gdrawHost.dsvDescSize = s_dsvDescSize;
		s_gdrawHost.samplerHeap = s_samplerHeap;
		s_gdrawHost.samplerDescSize = s_samplerDescSize;
		s_gdrawHost.width = s_width;
		s_gdrawHost.height = s_height;
		s_gdrawHost.EnsureFrameRecording = EnsureFrameRecordingFn;
		s_gdrawHost.ExecuteOneShotUpload = ExecuteOneShotUploadFn;
		s_gdrawHost.AllocateFrameSRV = AllocateFrameSRVFn;
		s_gdrawHost.AllocFrameUploadBytes = AllocFrameUploadBytesFn;
		s_gdrawHost.DeferredReleaseIUnknown = DeferredReleaseIUnknownFn;
		s_gdrawHost.GetActiveSRVHeap = GetActiveSRVHeapFn;
		s_gdrawHost.GetSamplerGPUHandle = GetSamplerGPUHandleFn;
		s_gdrawHost.GetMCTextureResource = GetMCTextureResourceFn;
		s_gdrawHost.ApplyMCViewportAndTarget = ApplyMCViewportAndTargetFn;
		s_gdrawHost.InvalidateWorldPipelineCache = InvalidateWorldPipelineCacheFn;

		printf("[RenderDX12] Initialized DirectX 12 on adapter: %s\n", s_adapterName);
		fflush(stdout);
		return 1;
	}

	static int DX12_ResizeWindow(int width, int height, int fullscreen)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (!s_swapChain || width <= 0 || height <= 0) return 0;

		if (s_frameRecording && s_cmdList)
		{
			if (s_backbufferInRTState)
			{
				D3D12_RESOURCE_BARRIER b = {};
				b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource = s_backbuffers[s_currentBackbuffer];
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				s_cmdList->ResourceBarrier(1, &b);
				s_backbufferInRTState = false;
			}
			s_cmdList->Close();
			s_frameRecording = false;
		}
		WaitForGPUIdle();
		ReleaseSwapchainResources();

		s_width = width;
		s_height = height;
		s_fullscreen = (fullscreen != 0);
		UINT flags = s_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
		if (FAILED(s_swapChain->ResizeBuffers(C4J_DX12_SWAP_BUFFERS, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, flags)))
		{
			return 0;
		}
		if (!CreateSwapchainResources(width, height)) return 0;
		s_gdrawHost.width = width;
		s_gdrawHost.height = height;
		return 1;
	}

	static void DX12_Shutdown(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		WaitForGPUIdle();
		DrainDeferredReleases(true);

		gdraw_DX12_DestroyContext();

		{
			std::lock_guard<std::mutex> cbLock(s_cbuffMutex);
			for (auto &kv : s_cbuffs)
			{
				for (auto &vb : kv.second.gpuVBs)
				{
					if (vb.slice.valid()) QueueDeferredSliceFreeLocked(vb.slice);
				}
			}
			s_cbuffs.clear();
			s_totalCBuffBytes.store(0, std::memory_order_relaxed);
		}
		DrainDeferredReleases(true);
		for (auto &slab : s_vbSlabs)
		{
			if (slab.res) slab.res->Release();
		}
		s_vbSlabs.clear();
		s_pendingTexUpdates.clear();
		for (auto &kv : s_textures) if (kv.second.resource) kv.second.resource->Release();
		s_textures.clear();

		for (auto &kv : s_worldPSOCache) if (kv.second) kv.second->Release();
		s_worldPSOCache.clear();

		if (s_dummyWhiteTex) { s_dummyWhiteTex->Release(); s_dummyWhiteTex = NULL; }
		if (s_quadIB) { s_quadIB->Release(); s_quadIB = NULL; }
		if (s_fanIB) { s_fanIB->Release(); s_fanIB = NULL; }
		if (s_worldRootSig) { s_worldRootSig->Release(); s_worldRootSig = NULL; }
		if (s_vsStd) { s_vsStd->Release(); s_vsStd = NULL; }
		if (s_vsComp) { s_vsComp->Release(); s_vsComp = NULL; }
		if (s_psWorld) { s_psWorld->Release(); s_psWorld = NULL; }

		ReleaseSwapchainResources();
		for (uint32_t f = 0; f < C4J_DX12_FRAMES_IN_FLIGHT; ++f)
		{
			if (s_uploadRings[f]) { s_uploadRings[f]->Unmap(0, NULL); s_uploadRings[f]->Release(); s_uploadRings[f] = NULL; s_uploadMapped[f] = NULL; }
			if (s_srvHeaps[f]) { s_srvHeaps[f]->Release(); s_srvHeaps[f] = NULL; }
			if (s_cmdAllocs[f]) { s_cmdAllocs[f]->Release(); s_cmdAllocs[f] = NULL; }
		}
		if (s_cmdList) { s_cmdList->Release(); s_cmdList = NULL; }
		if (s_uploadCmd) { s_uploadCmd->Release(); s_uploadCmd = NULL; }
		if (s_uploadAlloc) { s_uploadAlloc->Release(); s_uploadAlloc = NULL; }
		if (s_fence) { s_fence->Release(); s_fence = NULL; }
		if (s_fenceEvent) { CloseHandle(s_fenceEvent); s_fenceEvent = NULL; }
		if (s_samplerHeap) { s_samplerHeap->Release(); s_samplerHeap = NULL; }
		if (s_dsvHeap) { s_dsvHeap->Release(); s_dsvHeap = NULL; }
		if (s_rtvHeap) { s_rtvHeap->Release(); s_rtvHeap = NULL; }
		if (s_swapChain) { s_swapChain->Release(); s_swapChain = NULL; }
		if (s_queue) { s_queue->Release(); s_queue = NULL; }
		if (s_device) { s_device->Release(); s_device = NULL; }
		if (s_factory) { s_factory->Release(); s_factory = NULL; }
	}

	static void DX12_SetVsync(int enabled) { s_vsync = (enabled != 0); }
	static int  DX12_GetVsync(void) { return s_vsync ? 1 : 0; }
	static void DX12_GetBackendInfo(C4JBackendInfo *outInfo)
	{
		if (!outInfo) return;
		outInfo->backendType = C4J_BACKEND_DX12;
		outInfo->vsyncEnabled = s_vsync ? 1 : 0;
		outInfo->screenWidth = s_width;
		outInfo->screenHeight = s_height;
		snprintf(outInfo->adapterName, sizeof(outInfo->adapterName), "%s", s_adapterName);
		outInfo->adapterName[sizeof(outInfo->adapterName) - 1] = 0;
		snprintf(outInfo->backendName, sizeof(outInfo->backendName), "DirectX 12");
		outInfo->backendName[sizeof(outInfo->backendName) - 1] = 0;
	}

	static void DX12_Tick(void) {}
	static void DX12_UpdateGamma(unsigned short) {}
	static bool RecordSubRectTexUploadFromRing(ID3D12GraphicsCommandList *cmd, DX12TextureSlot &slot, int xoffset, int yoffset, int width, int height, int level, const void *data)
	{
		if (!cmd || !slot.resource || !data || width <= 0 || height <= 0 || !s_uploadRings[s_frameIdx]) return false;
		const UINT srcRow = (UINT)width * 4u;
		const UINT alignedPitch = (srcRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT totalBytes = alignedPitch * (UINT)height;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
		void *mp = AllocFrameUploadBytesFn(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &gpuVA);
		if (!mp) return false;
		const UINT64 ringOff = (UINT64)(gpuVA - s_uploadRings[s_frameIdx]->GetGPUVirtualAddress());
		const uint8_t *src = (const uint8_t*)data;
		for (int row = 0; row < height; ++row)
			memcpy((uint8_t*)mp + (size_t)row * alignedPitch, src + (size_t)row * srcRow, srcRow);

		TransitionMCTexture(cmd, slot, D3D12_RESOURCE_STATE_COPY_DEST);
		D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = slot.resource; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = (UINT)level;
		D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = s_uploadRings[s_frameIdx]; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		s.PlacedFootprint.Offset = ringOff;
		s.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.PlacedFootprint.Footprint.Width = (UINT)width; s.PlacedFootprint.Footprint.Height = (UINT)height;
		s.PlacedFootprint.Footprint.Depth = 1; s.PlacedFootprint.Footprint.RowPitch = alignedPitch;
		cmd->CopyTextureRegion(&d, (UINT)xoffset, (UINT)yoffset, 0, &s, NULL);
		TransitionMCTexture(cmd, slot, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		return true;
	}

	static void RecordSubRectTexUpload(ID3D12GraphicsCommandList *cmd, DX12TextureSlot &slot, int xoffset, int yoffset, int width, int height, int level, const void *data)
	{
		if (RecordSubRectTexUploadFromRing(cmd, slot, xoffset, yoffset, width, height, level, data))
		{
			return;
		}
		if (!cmd || !slot.resource || !data || width <= 0 || height <= 0) return;
		const UINT srcRow = (UINT)width * 4u;
		const UINT alignedPitch = (srcRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		ID3D12Resource *st = CreateCommittedBuffer((UINT64)alignedPitch * (UINT64)height, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (!st) return;
		void *mp = NULL;
		if (SUCCEEDED(st->Map(0, NULL, &mp)) && mp)
		{
			const uint8_t *src = (const uint8_t*)data;
			for (int row = 0; row < height; ++row)
				memcpy((uint8_t*)mp + (size_t)row * alignedPitch, src + (size_t)row * srcRow, srcRow);
			st->Unmap(0, NULL);
		}
		TransitionMCTexture(cmd, slot, D3D12_RESOURCE_STATE_COPY_DEST);
		D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = slot.resource; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = (UINT)level;
		D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = st; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		s.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.PlacedFootprint.Footprint.Width = (UINT)width; s.PlacedFootprint.Footprint.Height = (UINT)height;
		s.PlacedFootprint.Footprint.Depth = 1; s.PlacedFootprint.Footprint.RowPitch = alignedPitch;
		cmd->CopyTextureRegion(&d, (UINT)xoffset, (UINT)yoffset, 0, &s, NULL);
		TransitionMCTexture(cmd, slot, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		DeferredReleaseIUnknownFn(st);
	}

	static void FlushPendingDX12TexUpdates(void)
	{
		if (s_pendingTexUpdates.empty() || !s_frameRecording || !s_cmdList) return;
		for (size_t i = 0; i < s_pendingTexUpdates.size(); ++i)
		{
			const PendingDX12TexUpdate &u = s_pendingTexUpdates[i];
			auto it = s_textures.find(u.texId);
			if (it == s_textures.end() || it->second.resource != u.resource) continue;
			RecordSubRectTexUpload(s_cmdList, it->second, u.xoffset, u.yoffset, u.width, u.height, u.level, u.pixels.data());
		}
		s_pendingTexUpdates.clear();
	}

	static void DX12_StartFrame(void)
	{
		s_tls.ResetFrameState();
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		EnsureFrameRecordingFn();
		FlushPendingDX12TexUpdates();
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_DoScreenGrabOnNextPresent(void) {}
	static void DX12_Present(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (!s_swapChain) return;
		if (!s_frameRecording) EnsureFrameRecordingFn();

		if (s_backbufferInRTState)
		{
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = s_backbuffers[s_currentBackbuffer];
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			s_cmdList->ResourceBarrier(1, &b);
			s_backbufferInRTState = false;
		}
		s_cmdList->Close();
		ID3D12CommandList *lists[1] = { s_cmdList };
		s_queue->ExecuteCommandLists(1, lists);
		s_frameRecording = false;
		s_gdrawHost.activeCmd = NULL;

		UINT syncInterval = s_vsync ? 1u : 0u;
		UINT flags = (!s_vsync && s_allowTearing && !s_fullscreen) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
		s_swapChain->Present(syncInterval, flags);

		uint64_t sigVal = s_nextFenceValue++;
		s_queue->Signal(s_fence, sigVal);
		s_frameFenceValues[s_frameIdx] = sigVal;
		s_frameIdx = (s_frameIdx + 1) % C4J_DX12_FRAMES_IN_FLIGHT;
		++s_frameSeq;
	}

	static void DX12_Clear(int flags, const C4JClearRect *pRect)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		EnsureFrameRecordingFn();
		if (!s_cmdList) return;

		D3D12_RECT rect = { 0, 0, (LONG)s_width, (LONG)s_height };
		UINT numRects = 0;
		if (pRect)
		{
			rect.left = std::max<LONG>(0, pRect->left);
			rect.top = std::max<LONG>(0, pRect->top);
			rect.right = std::min<LONG>(s_width, pRect->right);
			rect.bottom = std::min<LONG>(s_height, pRect->bottom);
			if (rect.right <= rect.left || rect.bottom <= rect.top) return;
			numRects = 1;
		}
		if (flags & C4J_CLEAR_COLOUR_FLAG)
		{
			s_cmdList->ClearRenderTargetView(s_backbufferRTVs[s_currentBackbuffer], s_clearColour, numRects, numRects ? &rect : NULL);
		}
		D3D12_CLEAR_FLAGS d3dFlags = (D3D12_CLEAR_FLAGS)0;
		if (flags & C4J_CLEAR_DEPTH_FLAG)  d3dFlags |= D3D12_CLEAR_FLAG_DEPTH;
		if (flags & GL_STENCIL_BUFFER_BIT) d3dFlags |= D3D12_CLEAR_FLAG_STENCIL;
		if (d3dFlags != 0)
		{
			s_cmdList->ClearDepthStencilView(s_depthDSV, d3dFlags, 1.0f, 0, numRects, numRects ? &rect : NULL);
		}
	}

	static void DX12_SetClearColour(const float rgba[4]) { if (rgba) memcpy(s_clearColour, rgba, 16); }
	static int  DX12_IsWidescreen(void) { return 1; }
	static int  DX12_IsHiDef(void) { return 1; }
	static void DX12_NoOpInt(int) {}
	static void DX12_NoOpVoid(void) {}

	/* Matrices */
	static void DX12_MatrixMode(int type) { if (type >= 0 && type < 3) { s_tls.matrixMode = type; s_tls.EnsureStackInit(type); } }
	static void DX12_MatrixSetIdentity(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity(); s_tls.matrixDirty = true; }
	static void DX12_MatrixTranslate(float x, float y, float z) { MatrixTranslate(s_tls, x, y, z); }
	static void DX12_MatrixRotate(float a, float x, float y, float z) { MatrixRotate(s_tls, a, x, y, z); }
	static void DX12_MatrixScale(float x, float y, float z) { MatrixScale(s_tls, x, y, z); }
	static void DX12_MatrixPerspective(float fovy, float aspect, float zn, float zf) { MatrixPerspective(s_tls, fovy, aspect, zn, zf); }
	static void DX12_MatrixOrthogonal(float l, float r, float b, float t, float zn, float zf) { MatrixOrthogonal(s_tls, l, r, b, t, zn, zf); }
	static void DX12_MatrixPop(void) { if (s_tls.matrixStacks[s_tls.matrixMode].size() > 1) { s_tls.matrixStacks[s_tls.matrixMode].pop_back(); s_tls.matrixDirty = true; } }
	static void DX12_MatrixPush(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].push_back(s_tls.matrixStacks[s_tls.matrixMode].back()); }
	static void DX12_MatrixMult(float *mat)
	{
		if (!mat) return;
		s_tls.EnsureStackInit(s_tls.matrixMode);
		Matrix4x4 m;
		memcpy(m.m, mat, sizeof(m.m));
		s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Multiply(s_tls.matrixStacks[s_tls.matrixMode].back(), m);
		s_tls.matrixDirty = true;
	}
	static const float *DX12_MatrixGet(int type)
	{
		static const float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		if (type >= 0 && type < 3 && !s_tls.matrixStacks[type].empty()) return s_tls.matrixStacks[type].back().m;
		return id;
	}
	static void DX12_SetMatrixDirty(void) { s_tls.matrixDirty = true; }

	static void DX12_DrawVertices(int primType, int count, void *dataIn, int vType, int psType)
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

		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, NULL, 0, 0, dataIn, totalBytes, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	static void DX12_DrawVertexBuffer(int primType, int count, void *bufferHandle, int vType, int psType)
	{
		if (!bufferHandle || count <= 0) return;
		const UINT stride = (vType == 1) ? 16u : 32u;
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, (ID3D12Resource*)bufferHandle, 0, (UINT)count * stride, NULL, 0, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	/* Command buffers with lazy DEFAULT-VRAM slab VB pooling & budgeted uploads */
	static int DX12_CBuffCreate(int count)
	{
		if (count <= 0) return 0;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		int first = s_nextCBuffId;
		s_nextCBuffId += count;
		return first;
	}
	static void DX12_CBuffDelete(int first, int count)
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
					for (auto &vb : it->second.gpuVBs) QueueDeferredSliceFreeLocked(vb.slice);
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
				for (auto &vb : it->second.gpuVBs) QueueDeferredSliceFreeLocked(vb.slice);
				s_cbuffs.erase(it);
			}
		}
	}
	static void DX12_CBuffStart(int index, int)
	{
		s_tls.recordingCBuffIndex = index;
		s_tls.recordingCommands.clear();
		s_tls.EnsureStackInit(0);
		s_tls.recordingBaseMatrix = s_tls.matrixStacks[0].back();
		Matrix4x4::Invert(s_tls.recordingBaseMatrix, s_tls.recordingInvBaseMatrix);
	}
	static void DX12_CBuffClear(int index)
	{
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		if (it != s_cbuffs.end())
		{
			s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
			for (auto &vb : it->second.gpuVBs) QueueDeferredSliceFreeLocked(vb.slice);
			it->second.gpuVBs.clear();
			it->second.commands.reset();
			it->second.byteSize = 0;
			it->second.generation++;
		}
	}
	static int DX12_CBuffSize(int index)
	{
		if (index == -1)
		{
			return (int)s_totalCBuffBytes.load(std::memory_order_relaxed);
		}
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		return (it != s_cbuffs.end()) ? (int)it->second.byteSize : 0;
	}
	static void DX12_CBuffEnd(void)
	{
		int index = s_tls.recordingCBuffIndex;
		s_tls.recordingCBuffIndex = 0;
		if (index == 0) return;

		auto cmds = std::make_shared<std::vector<CBuffCommand>>(std::move(s_tls.recordingCommands));
		s_tls.recordingCommands.clear();
		size_t bytes = 0;
		for (const auto &c : *cmds) bytes += c.vertexData.size();

		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		DX12CBuffSlot &slot = s_cbuffs[index];
		s_totalCBuffBytes.fetch_sub(slot.byteSize, std::memory_order_relaxed);
		for (auto &vb : slot.gpuVBs) QueueDeferredSliceFreeLocked(vb.slice);
		slot.gpuVBs.clear();
		slot.commands = std::move(cmds);
		slot.byteSize = bytes;
		s_totalCBuffBytes.fetch_add(bytes, std::memory_order_relaxed);
		slot.generation++;
	}
	static int DX12_CBuffCall(int index, int)
	{
		std::shared_ptr<const std::vector<CBuffCommand>> cmds;
		uint64_t gen = 0;
		thread_local static std::vector<DX12VBSlice> tls_slices;
		thread_local static std::vector<uint8_t> tls_needsBake;
		bool needsUpload = false;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(index);
			if (it == s_cbuffs.end() || !it->second.commands || it->second.commands->empty()) return 0;
			cmds = it->second.commands;
			gen = it->second.generation;

			const size_t n = cmds->size();
			tls_slices.resize(n);
			tls_needsBake.assign(n, 0);
			const bool vecMatches = (it->second.gpuVBs.size() == n);
			for (size_t i = 0; i < n; ++i)
			{
				if ((*cmds)[i].vertexData.empty())
				{
					tls_slices[i] = DX12VBSlice();
					continue;
				}
				if (!vecMatches || it->second.gpuVBs[i].dirty || !it->second.gpuVBs[i].slice.valid() ||
					it->second.gpuVBs[i].generation != gen)
				{
					tls_slices[i] = DX12VBSlice();
					tls_needsBake[i] = 1;
					needsUpload = true;
				}
				else
				{
					tls_slices[i] = it->second.gpuVBs[i].slice;
				}
			}
		}

		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		EnsureFrameRecordingFn();

		if (needsUpload && s_frameRecording && s_cmdList && s_uploadRings[s_frameIdx])
		{
			thread_local static std::vector<std::pair<size_t, DX12VBSlice>> tls_newlyBaked;
			tls_newlyBaked.clear();
			const size_t n = cmds->size();
			for (size_t i = 0; i < n; ++i)
			{
				if (!tls_needsBake[i]) continue;
				const auto &c = (*cmds)[i];
				const UINT sz = (UINT)c.vertexData.size();
				if (sz == 0) continue;

				const bool withinBudget = (s_frameCBuffUploadCount == 0) ||
					(s_frameCBuffUploadCount < C4J_DX12_CBUFF_UPLOAD_BUDGET_COUNT &&
					 s_frameCBuffUploadBytes + sz <= C4J_DX12_CBUFF_UPLOAD_BUDGET_BYTES);
				const bool ringHasReserve = ((UINT64)s_uploadOffset + (UINT64)sz + C4J_DX12_RING_RESERVE_BYTES <= C4J_DX12_UPLOAD_RING_BYTES);
				if (!withinBudget || !ringHasReserve)
				{
					break;
				}

				D3D12_GPU_VIRTUAL_ADDRESS srcGpu = 0;
				void *srcMp = AllocFrameUploadBytesFn(sz, 16u, &srcGpu);
				if (!srcMp) break;

				DX12VBSlice newSlice;
				if (!AllocVBSlice(sz, newSlice)) break;

				memcpy(srcMp, c.vertexData.data(), sz);
				TransitionVBSlab(s_cmdList, newSlice.slabIdx, D3D12_RESOURCE_STATE_COPY_DEST);
				const UINT64 ringOff = (UINT64)(srcGpu - s_uploadRings[s_frameIdx]->GetGPUVirtualAddress());
				s_cmdList->CopyBufferRegion(s_vbSlabs[newSlice.slabIdx].res, newSlice.offset, s_uploadRings[s_frameIdx], ringOff, sz);

				++s_frameCBuffUploadCount;
				s_frameCBuffUploadBytes += sz;
				tls_newlyBaked.push_back({ i, newSlice });
				tls_slices[i] = newSlice;
			}

			if (!tls_newlyBaked.empty())
			{
				std::lock_guard<std::mutex> cbLock(s_cbuffMutex);
				auto it = s_cbuffs.find(index);
				if (it != s_cbuffs.end() && it->second.generation == gen)
				{
					if (it->second.gpuVBs.size() != n)
					{
						for (auto &vb : it->second.gpuVBs) QueueDeferredSliceFreeLocked(vb.slice);
						it->second.gpuVBs.assign(n, DX12BakedVB());
					}
					for (const auto &baked : tls_newlyBaked)
					{
						DX12BakedVB &slotVB = it->second.gpuVBs[baked.first];
						QueueDeferredSliceFreeLocked(slotVB.slice);
						slotVB.slice = baked.second;
						slotVB.generation = gen;
						slotVB.dirty = false;
					}
				}
				else
				{
					for (const auto &baked : tls_newlyBaked)
					{
						QueueDeferredSliceFreeLocked(baked.second);
					}
					tls_slices.assign(n, DX12VBSlice());
				}
			}
		}

		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 baseMV = s_tls.matrixStacks[0].back();
		Matrix4x4 proj = s_tls.matrixStacks[1].back();

		for (size_t i = 0; i < cmds->size(); ++i)
		{
			const auto &c = (*cmds)[i];
			Matrix4x4 mv = Matrix4x4::Multiply(baseMV, c.relativeMatrix);
			Matrix4x4 wvp = Matrix4x4::Multiply(proj, mv);
			const int texToBind = (s_tls.boundTextureId != 0) ? s_tls.boundTextureId : c.boundTextureId;
			float combined[4] = {
				s_tls.stateColour[0] * c.stateColour[0],
				s_tls.stateColour[1] * c.stateColour[1],
				s_tls.stateColour[2] * c.stateColour[2],
				s_tls.stateColour[3] * c.stateColour[3]
			};
			ID3D12Resource *vbRes = NULL;
			UINT64 vbOffset = 0;
			UINT vbBytes = 0;
			if (tls_slices[i].valid() && tls_slices[i].slabIdx < (int)s_vbSlabs.size())
			{
				TransitionVBSlab(s_cmdList, tls_slices[i].slabIdx, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
				vbRes = s_vbSlabs[tls_slices[i].slabIdx].res;
				vbOffset = tls_slices[i].offset;
				vbBytes = tls_slices[i].size;
			}
			ExecuteDrawInternal(c.primitiveType, c.count, vbRes, vbOffset, vbBytes,
				vbRes ? NULL : c.vertexData.data(), (UINT)c.vertexData.size(),
				c.vType, c.psType, wvp, mv, texToBind, combined,
				s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
				s_tls.depthTestEnable, s_tls.depthMask,
				s_tls.cullEnable, s_tls.cullCW,
				s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
		}
		return 1;
	}

	/* Textures */
	static int DX12_TextureCreate(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		int id = s_nextTextureId++;
		s_textures[id] = DX12TextureSlot();
		return id;
	}
	static void DX12_TextureFree(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (s_cachedTexLookupId == idx) s_cachedTexLookupId = -1;
		if (s_cachedLightLookupId == idx) s_cachedLightLookupId = -1;
		auto it = s_textures.find(idx);
		if (it != s_textures.end())
		{
			if (it->second.resource)
			{
				if (s_cachedTex0Res == it->second.resource) s_cachedTex0Res = NULL;
				if (s_cachedTex1Res == it->second.resource) s_cachedTex1Res = NULL;
				DeferredReleaseIUnknownFn(it->second.resource);
			}
			s_textures.erase(it);
		}
	}
	static void DX12_TextureBind(int idx)
	{
		if (s_tls.boundTextureId != idx)
		{
			s_tls.boundTextureId = idx;
			s_tls.textureLevels = 1;
		}
	}
	static void DX12_TextureBindVertex(int idx) { s_tls.boundVertexTextureId = idx; }
	static void DX12_TextureSetTextureLevels(int levels) { s_tls.textureLevels = (levels > 0) ? levels : 1; }
	static int  DX12_TextureGetTextureLevels(void)
	{
		if (s_tls.boundTextureId > 0)
		{
			std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end() && it->second.resource != NULL) return it->second.mipLevels;
		}
		return s_tls.textureLevels;
	}

	static void DX12_TextureData(int width, int height, void *data, int level, int)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (!s_device || s_tls.boundTextureId <= 0 || width <= 0 || height <= 0) return;

		if (s_cachedTexLookupId == s_tls.boundTextureId) s_cachedTexLookupId = -1;
		if (s_cachedLightLookupId == s_tls.boundTextureId) s_cachedLightLookupId = -1;

		auto &slot = s_textures[s_tls.boundTextureId];
		if (level == 0 || !slot.resource || slot.width != width || slot.height != height)
		{
			if (level == 0)
			{
				if (slot.resource)
				{
					if (s_cachedTex0Res == slot.resource) s_cachedTex0Res = NULL;
					if (s_cachedTex1Res == slot.resource) s_cachedTex1Res = NULL;
					DeferredReleaseIUnknownFn(slot.resource);
					slot.resource = NULL;
				}
				slot.width = width;
				slot.height = height;
				slot.mipLevels = (s_tls.textureLevels > 1) ? std::min<int>(s_tls.textureLevels, (int)ComputeMaxMipLevels(width, height)) : 1;

				D3D12_HEAP_PROPERTIES hpDef = {};
				hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
				D3D12_RESOURCE_DESC rd = {};
				rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				rd.Width = (UINT64)width;
				rd.Height = (UINT)height;
				rd.DepthOrArraySize = 1;
				rd.MipLevels = (UINT16)slot.mipLevels;
				rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				rd.SampleDesc.Count = 1;

				if (FAILED(s_device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, __uuidof(ID3D12Resource), (void**)&slot.resource)))
				{
					s_tls.textureLevels = 1;
					return;
				}
				slot.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				s_tls.textureLevels = slot.mipLevels;
			}
		}
		if (!slot.resource || !data || level < 0 || level >= slot.mipLevels) return;

		int lw = std::max(1, width >> level);
		int lh = std::max(1, height >> level);
		if (level > 0) { lw = width; lh = height; }

		if (s_frameRecording && s_cmdList)
		{
			RecordSubRectTexUpload(s_cmdList, slot, 0, 0, lw, lh, level, data);
		}
		else
		{
			PendingDX12TexUpdate u;
			u.texId = s_tls.boundTextureId;
			u.resource = slot.resource;
			u.xoffset = 0;
			u.yoffset = 0;
			u.width = lw;
			u.height = lh;
			u.level = level;
			const size_t bytes = (size_t)lw * (size_t)lh * 4u;
			u.pixels.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
			s_pendingTexUpdates.push_back(std::move(u));
		}
	}

	static void DX12_TextureDataUpdate(int xoffset, int yoffset, int width, int height, void *data, int level)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (!s_device || s_tls.boundTextureId <= 0 || !data || width <= 0 || height <= 0) return;
		auto it = s_textures.find(s_tls.boundTextureId);
		if (it == s_textures.end() || !it->second.resource) return;

		if (s_cachedTexLookupId == s_tls.boundTextureId) s_cachedTexLookupId = -1;
		if (s_cachedLightLookupId == s_tls.boundTextureId) s_cachedLightLookupId = -1;

		if (s_frameRecording && s_cmdList)
		{
			RecordSubRectTexUpload(s_cmdList, it->second, xoffset, yoffset, width, height, level, data);
			return;
		}

		PendingDX12TexUpdate u;
		u.texId = s_tls.boundTextureId;
		u.resource = it->second.resource;
		u.xoffset = xoffset;
		u.yoffset = yoffset;
		u.width = width;
		u.height = height;
		u.level = level;
		const size_t bytes = (size_t)width * (size_t)height * 4u;
		u.pixels.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
		s_pendingTexUpdates.push_back(std::move(u));
	}

	static void DX12_TextureSetParam(int param, int value)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (s_tls.boundTextureId <= 0) return;
		auto &slot = s_textures[s_tls.boundTextureId];
		if (param == 1 || param == 2) slot.linearFilter = (value != 0);
		else if (param == 3 || param == 4) slot.clampWrap = (value == 0);
		if (s_cachedTexLookupId == s_tls.boundTextureId) s_cachedTexLookupId = -1;
	}
	static void *DX12_TextureGetNativeHandle(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		auto it = s_textures.find(idx);
		return (it != s_textures.end()) ? (void*)it->second.resource : NULL;
	}

	/* State setters */
	static void DX12_StateSetColour(float r, float g, float b, float a) { s_tls.stateColour[0]=r; s_tls.stateColour[1]=g; s_tls.stateColour[2]=b; s_tls.stateColour[3]=a; }
	static void DX12_StateSetDepthMask(int en) { s_tls.depthMask = (en != 0); }
	static void DX12_StateSetBlendEnable(int en) { s_tls.blendEnable = (en != 0); }
	static void DX12_StateSetBlendFunc(int src, int dst) { s_tls.blendSrc = src; s_tls.blendDst = dst; }
	static void DX12_StateSetBlendFactor(unsigned int col) { s_tls.blendFactorRGBA = col; }
	static void DX12_StateSetAlphaFunc(int func, float param) { s_tls.alphaFunc = func; s_tls.alphaCutoff = param; }
	static void DX12_StateSetDepthFunc(int func) { s_tls.depthFunc = func; }
	static void DX12_StateSetFaceCull(int en) { s_tls.cullEnable = (en != 0); }
	static void DX12_StateSetFaceCullCW(int en) { s_tls.cullCW = (en != 0); }
	static void DX12_StateSetLineWidth(float) {}
	static void DX12_StateSetWriteEnable(int r, int g, int b, int a) { s_tls.colorWriteMask = (uint8_t)((r?1:0)|(g?2:0)|(b?4:0)|(a?8:0)); }
	static void DX12_StateSetDepthTestEnable(int en) { s_tls.depthTestEnable = (en != 0); }
	static void DX12_StateSetAlphaTestEnable(int en) { s_tls.alphaTestEnable = (en != 0); }
	static void DX12_StateSetDepthSlopeAndBias(float slope, float bias) { s_tls.depthBiasSlope = slope; s_tls.depthBiasConstant = bias; }
	static void DX12_StateSetFogEnable(int en) { s_tls.fogEnable = (en != 0); }
	static void DX12_StateSetFogMode(int mode) { s_tls.fogMode = mode; }
	static void DX12_StateSetFogNearDistance(float d) { s_tls.fogNear = d; }
	static void DX12_StateSetFogFarDistance(float d) { s_tls.fogFar = d; }
	static void DX12_StateSetFogDensity(float d) { s_tls.fogDensity = d; }
	static void DX12_StateSetFogColour(float r, float g, float b) { s_tls.fogColor[0]=r; s_tls.fogColor[1]=g; s_tls.fogColor[2]=b; }
	static void DX12_StateSetLightingEnable(int en) { s_tls.lightingEnable = (en != 0); }
	static void DX12_StateSetVertexTextureUV(float u, float v) { s_tls.vertexTexUV[0]=u; s_tls.vertexTexUV[1]=v; }
	static void DX12_StateSetLightColour(int l, float r, float g, float b) { if (l>=0 && l<2) { s_tls.lightColor[l][0]=r; s_tls.lightColor[l][1]=g; s_tls.lightColor[l][2]=b; } }
	static void DX12_StateSetLightAmbientColour(float r, float g, float b) { s_tls.lightAmbient[0]=r; s_tls.lightAmbient[1]=g; s_tls.lightAmbient[2]=b; s_tls.lightAmbient[3]=1.0f; }
	static void DX12_StateSetLightDirection(int l, float x, float y, float z)
	{
		if (l >= 0 && l < 2)
		{
			s_tls.EnsureStackInit(0);
			float tx, ty, tz;
			Matrix4x4::TransformVec3(s_tls.matrixStacks[0].back(), x, y, z, tx, ty, tz);
			s_tls.lightDir[l][0]=tx; s_tls.lightDir[l][1]=ty; s_tls.lightDir[l][2]=tz; s_tls.lightDir[l][3]=0.0f;
		}
	}
	static void DX12_StateSetLightEnable(int l, int en) { if (l>=0 && l<2) s_tls.lightEnabled[l] = (en != 0); }
	static void DX12_StateSetViewport(int viewportType)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		s_currentViewportType = viewportType;
		float vx, vy, vw, vh;
		ComputeViewportRect(viewportType, s_width, s_height, vx, vy, vw, vh);
		s_activeViewport = { vx, vy, vw, vh, 0.0f, 1.0f };
		s_activeScissor = { 0, 0, (LONG)s_width, (LONG)s_height };
		if (s_frameRecording && s_cmdList)
		{
			s_cmdList->RSSetViewports(1, &s_activeViewport);
			s_cmdList->RSSetScissorRects(1, &s_activeScissor);
		}
	}
	static void DX12_StateSetTexGenCol(int col, float x, float y, float z, float w, int)
	{
		if (col >= 0 && col < 4) { s_tls.texGenPlanes[col][0]=x; s_tls.texGenPlanes[col][1]=y; s_tls.texGenPlanes[col][2]=z; s_tls.texGenPlanes[col][3]=w; }
	}
	static void DX12_StateSetStencil(int func, uint8_t ref, uint8_t fmask, uint8_t wmask)
	{
		if (func == -1) { s_tls.stencilEnable = false; s_tls.stencilFunc = C4J_CMP_ALWAYS; }
		else { s_tls.stencilEnable = true; s_tls.stencilFunc = func; s_tls.stencilRef = ref; s_tls.stencilReadMask = fmask; s_tls.stencilWriteMask = wmask; }
	}
	static void DX12_BeginEvent(const wchar_t *) {}
	static void DX12_EndEvent(void) {}
	static void DX12_Suspend(void) { s_suspended = true; }
	static int  DX12_Suspended(void) { return s_suspended ? 1 : 0; }
	static void DX12_Resume(void) { s_suspended = false; }

	/* GDraw Bridge */
	static void *DX12_GDrawCreateContext(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		void *fn = (void*)gdraw_DX12_CreateContext(&s_gdrawHost, w, h);
		InvalidateWorldPipelineCacheFn();
		return fn;
	}
	static void DX12_GDrawDestroyContext(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_DestroyContext();
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_GDrawSetRendertargetSize(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_SetRendertargetSize(w, h);
	}
	static void DX12_GDrawSetTileOrigin(int x, int y)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_SetTileOrigin(x, y);
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_GDrawNoMoreGDrawThisFrame(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_NoMoreGDrawThisFrame();
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_GDrawBeginCustomDraw4J(void *region, float *mat16)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_BeginCustomDraw_4J(region, mat16);
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_GDrawCalculateCustomDraw4J(void *region, float *mat16)
	{
		gdraw_DX12_CalculateCustomDraw_4J(region, mat16);
	}
	static void DX12_GDrawEndCustomDraw(void *region)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_EndCustomDraw(region);
		InvalidateWorldPipelineCacheFn();
	}
	static void DX12_GDrawSetViewport4J(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_SetViewport_4J();
	}
	static void *DX12_GDrawWrappedTextureCreate(int textureId)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		return gdraw_DX12_WrappedTextureCreate(textureId);
	}
	static void DX12_GDrawWrappedTextureDestroy(void *handle)
	{
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		gdraw_DX12_WrappedTextureDestroy(handle);
	}

	static int DX12_CaptureBackBufferRGBA(C4JCaptureAllocFn allocFn, void *user, int *outWidth, int *outHeight)
	{
		if (outWidth) *outWidth = 0;
		if (outHeight) *outHeight = 0;
		std::lock_guard<std::recursive_mutex> lock(s_dx12Mutex);
		if (!s_device || !s_queue || !s_swapChain || !s_cmdList || s_suspended || s_width <= 0 || s_height <= 0 || !allocFn)
		{
			return 0;
		}
		if (!s_frameRecording)
		{
			EnsureFrameRecordingFn();
		}
		ID3D12Resource *backbuffer = s_backbuffers[s_currentBackbuffer];
		if (!s_frameRecording || !backbuffer) return 0;

		D3D12_RESOURCE_DESC desc = backbuffer->GetDesc();
		if (desc.Width == 0 || desc.Height == 0 || desc.SampleDesc.Count > 1) return 0;

		const bool isBgra =
			(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
		const bool isRgba =
			(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
		if (!isBgra && !isRgba) return 0;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		UINT64 totalBytes = 0;
		s_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);
		if (totalBytes == 0) return 0;

		ID3D12Resource *readbackBuf = CreateCommittedBuffer(totalBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
		if (!readbackBuf) return 0;

		if (s_backbufferInRTState)
		{
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = backbuffer;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			s_cmdList->ResourceBarrier(1, &b);
		}
		else
		{
			D3D12_RESOURCE_BARRIER b = {};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = backbuffer;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			s_cmdList->ResourceBarrier(1, &b);
		}

		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = backbuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLoc.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = readbackBuf;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dstLoc.PlacedFootprint = footprint;

		s_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, NULL);

		D3D12_RESOURCE_BARRIER bBack = {};
		bBack.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		bBack.Transition.pResource = backbuffer;
		bBack.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		bBack.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		bBack.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		s_cmdList->ResourceBarrier(1, &bBack);
		s_backbufferInRTState = true;

		s_cmdList->Close();
		ID3D12CommandList *lists[1] = { s_cmdList };
		s_queue->ExecuteCommandLists(1, lists);
		WaitForGPUIdle();

		int ok = 0;
		void *mapped = NULL;
		D3D12_RANGE readRange = { 0, (SIZE_T)totalBytes };
		if (SUCCEEDED(readbackBuf->Map(0, &readRange, &mapped)) && mapped)
		{
			const UINT w = (UINT)desc.Width;
			const UINT h = desc.Height;
			uint8_t *dstPixels = NULL;
			if (allocFn((int)w, (int)h, &dstPixels, user) && dstPixels)
			{
				const uint8_t *base = (const uint8_t *)mapped + footprint.Offset;
				const size_t dstRowBytes = (size_t)w * 4u;
				for (UINT y = 0; y < h; ++y)
				{
					const uint8_t *srcRow = base + ((size_t)footprint.Footprint.RowPitch * y);
					uint8_t *dstRow = dstPixels + (dstRowBytes * y);
					for (UINT x = 0; x < w; ++x)
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
				if (outWidth) *outWidth = (int)w;
				if (outHeight) *outHeight = (int)h;
				ok = 1;
			}
			D3D12_RANGE emptyWrite = { 0, 0 };
			readbackBuf->Unmap(0, &emptyWrite);
		}

		readbackBuf->Release();

		/* Re-open s_cmdList cleanly with backbuffer in RENDER_TARGET and RTV/DSV/viewport bound */
		s_cmdAllocs[s_frameIdx]->Reset();
		s_cmdList->Reset(s_cmdAllocs[s_frameIdx], NULL);
		s_uploadOffset = 0;
		s_srvCursor = 0;
		ApplyMCViewportAndTargetFn();
		InvalidateWorldPipelineCacheFn();
		return ok;
	}

	static const C4JRenderBackendAPI s_dx12API =
	{
		sizeof(C4JRenderBackendAPI),
		C4J_RENDER_BACKEND_ABI_VERSION,
		C4J_BACKEND_DX12,
		"DirectX 12",

		DX12_InitialiseWindow,
		DX12_ResizeWindow,
		DX12_Shutdown,
		DX12_SetVsync,
		DX12_GetVsync,
		DX12_GetBackendInfo,

		DX12_Tick,
		DX12_UpdateGamma,
		DX12_StartFrame,
		DX12_DoScreenGrabOnNextPresent,
		DX12_Present,
		DX12_Clear,
		DX12_SetClearColour,
		DX12_IsWidescreen,
		DX12_IsHiDef,
		DX12_NoOpInt,
		DX12_NoOpVoid,
		DX12_NoOpInt,
		DX12_NoOpVoid,

		DX12_MatrixMode,
		DX12_MatrixSetIdentity,
		DX12_MatrixTranslate,
		DX12_MatrixRotate,
		DX12_MatrixScale,
		DX12_MatrixPerspective,
		DX12_MatrixOrthogonal,
		DX12_MatrixPop,
		DX12_MatrixPush,
		DX12_MatrixMult,
		DX12_MatrixGet,
		DX12_SetMatrixDirty,

		DX12_DrawVertices,
		DX12_DrawVertexBuffer,

		DX12_NoOpVoid,
		DX12_CBuffCreate,
		DX12_CBuffDelete,
		DX12_CBuffStart,
		DX12_CBuffClear,
		DX12_CBuffSize,
		DX12_CBuffEnd,
		DX12_CBuffCall,
		DX12_NoOpVoid,
		DX12_NoOpVoid,
		DX12_NoOpVoid,

		DX12_TextureCreate,
		DX12_TextureFree,
		DX12_TextureBind,
		DX12_TextureBindVertex,
		DX12_TextureSetTextureLevels,
		DX12_TextureGetTextureLevels,
		DX12_TextureData,
		DX12_TextureDataUpdate,
		DX12_TextureSetParam,
		DX12_NoOpVoid,
		DX12_NoOpVoid,
		DX12_TextureGetNativeHandle,

		DX12_StateSetColour,
		DX12_StateSetDepthMask,
		DX12_StateSetBlendEnable,
		DX12_StateSetBlendFunc,
		DX12_StateSetBlendFactor,
		DX12_StateSetAlphaFunc,
		DX12_StateSetDepthFunc,
		DX12_StateSetFaceCull,
		DX12_StateSetFaceCullCW,
		DX12_StateSetLineWidth,
		DX12_StateSetWriteEnable,
		DX12_StateSetDepthTestEnable,
		DX12_StateSetAlphaTestEnable,
		DX12_StateSetDepthSlopeAndBias,
		DX12_StateSetFogEnable,
		DX12_StateSetFogMode,
		DX12_StateSetFogNearDistance,
		DX12_StateSetFogFarDistance,
		DX12_StateSetFogDensity,
		DX12_StateSetFogColour,
		DX12_StateSetLightingEnable,
		DX12_StateSetVertexTextureUV,
		DX12_StateSetLightColour,
		DX12_StateSetLightAmbientColour,
		DX12_StateSetLightDirection,
		DX12_StateSetLightEnable,
		DX12_StateSetViewport,
		DX12_NoOpInt,
		DX12_StateSetTexGenCol,
		DX12_StateSetStencil,
		DX12_NoOpInt,

		DX12_BeginEvent,
		DX12_EndEvent,
		DX12_Suspend,
		DX12_Suspended,
		DX12_Resume,

		DX12_GDrawCreateContext,
		DX12_GDrawDestroyContext,
		DX12_GDrawSetRendertargetSize,
		DX12_GDrawSetTileOrigin,
		DX12_GDrawNoMoreGDrawThisFrame,
		DX12_GDrawBeginCustomDraw4J,
		DX12_GDrawCalculateCustomDraw4J,
		DX12_GDrawEndCustomDraw,
		DX12_GDrawSetViewport4J,
		DX12_GDrawWrappedTextureCreate,
		DX12_GDrawWrappedTextureDestroy,
		DX12_CaptureBackBufferRGBA
	};
}

extern "C" __declspec(dllexport) const C4JRenderBackendAPI *C4JBackend_GetAPI(void)
{
	return &s_dx12API;
}
