#include "dx12_backend_context.h"
#include "iggy.h"
#include "gdraw.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace
{
	struct GDrawDX12CB
	{
		float u_World0[4];
		float u_World1[4];
		float u_XOff[4];
		float u_ColorMul[4];
		float u_ColorAdd[4];
		float u_Focal[4];
		float u_Tex0GenS[4];
		float u_Tex0GenT[4];
		float u_FilterData[4];
		float u_BlurTaps[9][4];
		float u_BlurClamp[4];
		int32_t u_Mode;
		int32_t u_HasTex0;
		int32_t u_HasTex1;
		int32_t u_UseAA;
		int32_t u_BlurNumTaps;
		int32_t u_Pad[3];
	};

	struct GDrawDX12Tex
	{
		ID3D12Resource *resource;
		D3D12_RESOURCE_STATES state;
		DXGI_FORMAT format;
		S32 width;
		S32 height;
		bool isWrapped;
		int wrappedMcTexId;
		bool isRT;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu;
		S32 rtvSlot;
		void *uniqueId;
	};

	struct GDrawDX12VB
	{
		ID3D12Resource *vbuf;
		ID3D12Resource *ibuf;
		S32 vlen;
		S32 ilen;
		void *uniqueId;
	};

	struct PendingMakeTex
	{
		GDrawDX12Tex *tex;
		U8 *rgbaData;
		S32 w, h;
		U32 flags;
		bool isAlpha8;
		void *uniqueId;
	};

	struct PendingMakeVB
	{
		U8 *vdata;
		U8 *idata;
		S32 vlen;
		S32 ilen;
		void *uniqueId;
	};

	struct RTStackEntry
	{
		GDrawDX12Tex *tex;
		S32 baseX, baseY, width, height;
		gswf_recti validRect;
		bool cached;
	};

	static const char *s_gdrawDX12HLSL =
		"cbuffer GDrawCB : register(b0)\n"
		"{\n"
		"    float4 u_World0;\n"
		"    float4 u_World1;\n"
		"    float4 u_XOff;\n"
		"    float4 u_ColorMul;\n"
		"    float4 u_ColorAdd;\n"
		"    float4 u_Focal;\n"
		"    float4 u_Tex0GenS;\n"
		"    float4 u_Tex0GenT;\n"
		"    float4 u_FilterData;\n"
		"    float4 u_BlurTaps[9];\n"
		"    float4 u_BlurClamp;\n"
		"    int    u_Mode;\n"
		"    int    u_HasTex0;\n"
		"    int    u_HasTex1;\n"
		"    int    u_UseAA;\n"
		"    int    u_BlurNumTaps;\n"
		"    int3   u_Pad;\n"
		"};\n"
		"Texture2D g_Tex0 : register(t0);\n"
		"Texture2D g_Tex1 : register(t1);\n"
		"SamplerState g_Samp0 : register(s0);\n"
		"struct VS_IN_V2  { float2 pos : POSITION; };\n"
		"struct VS_IN_AA  { float2 pos : POSITION; int4 aa : TEXCOORD0; };\n"
		"struct VS_IN_TC2 { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
		"struct PS_IN\n"
		"{\n"
		"    float4 pos   : SV_POSITION;\n"
		"    float2 uv0   : TEXCOORD0;\n"
		"    float2 uv1   : TEXCOORD1;\n"
		"    float4 factor: COLOR0;\n"
		"};\n"
		"PS_IN VSMainV2(VS_IN_V2 input)\n"
		"{\n"
		"    PS_IN o;\n"
		"    float4 pos4 = float4(input.pos, 0.0f, 1.0f);\n"
		"    float3 wp = float3(dot(u_World0.xyw, pos4.xyw), dot(u_World1.xyw, pos4.xyw), 1.0f);\n"
		"    o.pos = float4(wp.x * u_XOff.x + u_XOff.z, wp.y * u_XOff.y + u_XOff.w, u_World0.z, 1.0f);\n"
		"    o.uv0 = float2(dot(u_Tex0GenS, pos4), dot(u_Tex0GenT, pos4));\n"
		"    o.uv1 = float2(0.0f, 0.0f);\n"
		"    o.factor = float4(1.0f, 1.0f, 1.0f, 1.0f);\n"
		"    return o;\n"
		"}\n"
		"PS_IN VSMainAA(VS_IN_AA input)\n"
		"{\n"
		"    PS_IN o;\n"
		"    float4 pos4 = float4(input.pos, 0.0f, 1.0f);\n"
		"    float3 wp = float3(dot(u_World0.xyw, pos4.xyw), dot(u_World1.xyw, pos4.xyw), 1.0f);\n"
		"    o.pos = float4(wp.x * u_XOff.x + u_XOff.z, wp.y * u_XOff.y + u_XOff.w, u_World0.z, 1.0f);\n"
		"    o.uv0 = float2(dot(u_Tex0GenS, pos4), dot(u_Tex0GenT, pos4));\n"
		"    o.uv1 = float2(0.0f, 0.0f);\n"
		"    float cov = clamp(float(input.aa.x + 128) / 255.0f, 0.0f, 1.0f);\n"
		"    o.factor = float4(cov, 1.0f, 0.0f, 1.0f);\n"
		"    return o;\n"
		"}\n"
		"PS_IN VSMainTC2(VS_IN_TC2 input)\n"
		"{\n"
		"    PS_IN o;\n"
		"    float4 pos4 = float4(input.pos, 0.0f, 1.0f);\n"
		"    float3 wp = float3(dot(u_World0.xyw, pos4.xyw), dot(u_World1.xyw, pos4.xyw), 1.0f);\n"
		"    o.pos = float4(wp.x * u_XOff.x + u_XOff.z, wp.y * u_XOff.y + u_XOff.w, u_World0.z, 1.0f);\n"
		"    o.uv0 = input.uv;\n"
		"    o.uv1 = float2(0.0f, 0.0f);\n"
		"    o.factor = float4(1.0f, 1.0f, 1.0f, 1.0f);\n"
		"    return o;\n"
		"}\n"
		"float4 PSMain(PS_IN input) : SV_TARGET\n"
		"{\n"
		"    float4 z = u_ColorMul;\n"
		"    if (u_HasTex1 != 2)\n"
		"    {\n"
		"        z.rgb *= z.a;\n"
		"    }\n"
		"    if (u_HasTex0 != 0)\n"
		"    {\n"
		"        float2 tc = input.uv0;\n"
		"        if (u_Mode == 3)\n"
		"        {\n"
		"            float r = length(tc);\n"
		"            tc = float2(r, r);\n"
		"        }\n"
		"        else if (u_Mode == 4)\n"
		"        {\n"
		"            float cx = tc.x + u_Focal.x;\n"
		"            float cy = tc.y;\n"
		"            float tx = cx * u_Focal.y;\n"
		"            float rad2 = cx * cx + cy * cy;\n"
		"            float u = sqrt(max(0.0f, rad2 * u_Focal.z + tx * tx)) - tx;\n"
		"            tc = float2(u, u);\n"
		"        }\n"
		"        float4 texCol = g_Tex0.Sample(g_Samp0, tc);\n"
		"        if (u_Mode == 2 || u_Mode == 5)\n"
		"        {\n"
		"            z *= texCol.a;\n"
		"        }\n"
		"        else\n"
		"        {\n"
		"            if (u_HasTex1 == 2 && texCol.a > 1e-5f) texCol.rgb /= texCol.a;\n"
		"            z *= texCol;\n"
		"        }\n"
		"    }\n"
		"    if (u_UseAA != 0)\n"
		"    {\n"
		"        z *= input.factor.x;\n"
		"    }\n"
		"    if (u_HasTex1 == 2)\n"
		"    {\n"
		"        z = z + u_ColorAdd;\n"
		"        z.rgb *= z.a;\n"
		"    }\n"
		"    else if (u_HasTex1 == 1)\n"
		"    {\n"
		"        z.rgb = min(z.rgb + u_ColorAdd.rgb * z.a, z.aaa);\n"
		"    }\n"
		"    if (u_Mode == 5 && z.a < 0.5f)\n"
		"    {\n"
		"        discard;\n"
		"    }\n"
		"    return z;\n"
		"}\n"
		"float4 PSBlur(PS_IN input) : SV_TARGET\n"
		"{\n"
		"    float4 s = float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
		"    [loop]\n"
		"    for (int i = 0; i < u_BlurNumTaps; ++i)\n"
		"    {\n"
		"        float2 tc = clamp(input.uv0 + u_BlurTaps[i].xy, u_BlurClamp.xy, u_BlurClamp.zw);\n"
		"        s += g_Tex0.Sample(g_Samp0, tc) * u_BlurTaps[i].z;\n"
		"    }\n"
		"    return s;\n"
		"}\n"
		"float4 PSColorMatrix(PS_IN input) : SV_TARGET\n"
		"{\n"
		"    float4 c = g_Tex0.Sample(g_Samp0, input.uv0);\n"
		"    if (c.a > 1e-5f) c.rgb /= c.a;\n"
		"    float4 res = c.r * u_BlurTaps[0] + c.g * u_BlurTaps[1] + c.b * u_BlurTaps[2] + c.a * u_BlurTaps[3] + u_BlurTaps[4];\n"
		"    res = saturate(res);\n"
		"    res.rgb *= res.a;\n"
		"    return res;\n"
		"}\n"
		"float4 PSDropShadow(PS_IN input) : SV_TARGET\n"
		"{\n"
		"    float4 fg = g_Tex1.Sample(g_Samp0, input.uv0);\n"
		"    float2 stc = clamp(input.uv0 + u_FilterData.xy, u_BlurClamp.xy, u_BlurClamp.zw);\n"
		"    float sh = saturate(g_Tex0.Sample(g_Samp0, stc).a * u_FilterData.z);\n"
		"    float4 sc = float4(u_ColorMul.rgb * u_ColorMul.a, u_ColorMul.a) * sh;\n"
		"    return fg + sc * (1.0f - fg.a);\n"
		"}\n";

	static DX12BackendGDrawHost *s_host = NULL;
	static GDrawFunctions s_gdrawFuncs = {};

	static ID3D12RootSignature *s_rootSig = NULL;
	static ID3DBlob *s_vsV2 = NULL;
	static ID3DBlob *s_vsAA = NULL;
	static ID3DBlob *s_vsTC2 = NULL;
	static ID3DBlob *s_psMain = NULL;
	static ID3DBlob *s_psBlur = NULL;
	static ID3DBlob *s_psColorMatrix = NULL;
	static ID3DBlob *s_psDropShadow = NULL;
	static std::unordered_map<uint64_t, ID3D12PipelineState*> s_psoCache;

	static ID3D12Resource *s_dummyWhiteTex = NULL;
	static ID3D12DescriptorHeap *s_rtvHeap = NULL;
	static ID3D12DescriptorHeap *s_rtDsvHeap = NULL;
	static ID3D12Resource *s_rtDsvTex = NULL;
	static S32 s_rtDsvW = 0, s_rtDsvH = 0;
	static D3D12_CPU_DESCRIPTOR_HANDLE s_rtDsvCpu = {};
	static std::vector<GDrawDX12Tex*> s_allTextures;
	static std::vector<GDrawDX12VB*>  s_allVBs;
	static std::vector<GDrawDX12Tex*> s_rtPool;
	static bool s_rtSlotUsed[8] = {};

	static S32 s_tileX = 0, s_tileY = 0;
	static S32 s_tx0 = 0, s_ty0 = 0, s_tw = 1280, s_th = 720;
	static S32 s_tx0p = 0, s_ty0p = 0, s_tpw = 1280, s_tph = 720;
	static bool s_tileEnabled = false;
	static bool s_inBlur = false;
	static S32 s_rtSizeW = 1280, s_rtSizeH = 720;
	static S32 s_frametexW = 1280, s_frametexH = 720;
	static S32 s_fw = 1280, s_fh = 720;
	static F32 s_worldToPixel[2] = { 1.0f, 1.0f };
	static F32 s_projection[4] = { 2.0f / 1280.0f, -2.0f / 720.0f, -1.0f, 1.0f };

	static RTStackEntry s_rtStack[8] = {};
	static int s_rtDepth = 0;

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
		if (FAILED(s_host->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initState, NULL, __uuidof(ID3D12Resource), (void**)&res)))
		{
			return NULL;
		}
		return res;
	}

	static void TransitionTex(ID3D12GraphicsCommandList *cmd, GDrawDX12Tex *tex, D3D12_RESOURCE_STATES newState)
	{
		if (!cmd || !tex || !tex->resource || tex->state == newState) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = tex->resource;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = tex->state;
		b.Transition.StateAfter = newState;
		cmd->ResourceBarrier(1, &b);
		tex->state = newState;
	}

	struct TexUploadCtx
	{
		ID3D12Resource *dstTex;
		ID3D12Resource *staging;
		UINT32 w;
		UINT32 h;
		UINT32 rowPitch;
		DXGI_FORMAT fmt;
		D3D12_RESOURCE_STATES finalState;
	};

	static void RecordWholeTexUpload(ID3D12GraphicsCommandList *cmd, void *user)
	{
		TexUploadCtx *ctx = (TexUploadCtx*)user;
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = ctx->dstTex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = ctx->staging;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = 0;
		src.PlacedFootprint.Footprint.Format = ctx->fmt;
		src.PlacedFootprint.Footprint.Width = ctx->w;
		src.PlacedFootprint.Footprint.Height = ctx->h;
		src.PlacedFootprint.Footprint.Depth = 1;
		src.PlacedFootprint.Footprint.RowPitch = ctx->rowPitch;

		cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);

		D3D12_RESOURCE_BARRIER b = {};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = ctx->dstTex;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = ctx->finalState;
		cmd->ResourceBarrier(1, &b);
	}

	static bool UploadNewTextureData(GDrawDX12Tex *tex, const void *srcBytes, int w, int h, DXGI_FORMAT fmt)
	{
		if (!s_host || !s_host->device || !tex || !srcBytes || w <= 0 || h <= 0) return false;
		const UINT srcRow = (UINT)w * 4u;
		const UINT alignedPitch = (srcRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT64 stagingBytes = (UINT64)alignedPitch * (UINT64)h;

		D3D12_HEAP_PROPERTIES hpDef = {};
		hpDef.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = (UINT64)w;
		rd.Height = (UINT)h;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = fmt;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		if (FAILED(s_host->device->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), (void**)&tex->resource)))
		{
			return false;
		}
		tex->format = fmt;
		tex->width = w;
		tex->height = h;
		tex->state = D3D12_RESOURCE_STATE_COPY_DEST;

		ID3D12Resource *staging = CreateCommittedBuffer(stagingBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (!staging) return false;

		uint8_t *mapped = NULL;
		if (SUCCEEDED(staging->Map(0, NULL, (void**)&mapped)) && mapped)
		{
			const uint8_t *src = (const uint8_t*)srcBytes;
			for (int row = 0; row < h; ++row)
			{
				memcpy(mapped + (size_t)row * alignedPitch, src + (size_t)row * srcRow, srcRow);
			}
			staging->Unmap(0, NULL);
		}

		TexUploadCtx ctx = {};
		ctx.dstTex = tex->resource;
		ctx.staging = staging;
		ctx.w = (UINT32)w;
		ctx.h = (UINT32)h;
		ctx.rowPitch = alignedPitch;
		ctx.fmt = fmt;
		ctx.finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

		if (s_host->activeCmd)
		{
			RecordWholeTexUpload(s_host->activeCmd, &ctx);
			tex->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			s_host->DeferredReleaseIUnknown(staging);
		}
		else
		{
			s_host->ExecuteOneShotUpload(RecordWholeTexUpload, &ctx);
			tex->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			staging->Release();
		}
		return true;
	}

	static void ComputeObjectSpaceMatrix(F32 *mat, const gswf_matrix *xform, const F32 *proj, F32 depth, int out_col_major)
	{
		int row = out_col_major ? 1 : 4;
		int col = out_col_major ? 4 : 1;
		F32 xs = proj[0];
		F32 ys = proj[1];

		mat[0*row + 0*col] = xform->m00      * xs;
		mat[0*row + 1*col] = xform->m01      * xs;
		mat[0*row + 2*col] = 0.0f;
		mat[0*row + 3*col] = xform->trans[0] * xs + proj[2];

		mat[1*row + 0*col] = xform->m10      * ys;
		mat[1*row + 1*col] = xform->m11      * ys;
		mat[1*row + 2*col] = 0.0f;
		mat[1*row + 3*col] = xform->trans[1] * ys + proj[3];

		mat[2*row + 0*col] = 0.0f;
		mat[2*row + 1*col] = 0.0f;
		mat[2*row + 2*col] = 0.0f;
		mat[2*row + 3*col] = depth;

		mat[3*row + 0*col] = 0.0f;
		mat[3*row + 1*col] = 0.0f;
		mat[3*row + 2*col] = 0.0f;
		mat[3*row + 3*col] = 1.0f;
	}

	static void SetProjectionRaw(S32 x0, S32 x1, S32 y0, S32 y1)
	{
		s_projection[0] = 2.0f / (float)(x1 - x0);
		s_projection[1] = 2.0f / (float)(y1 - y0);
		s_projection[2] = (float)(x1 + x0) / (float)(x0 - x1);
		s_projection[3] = (float)(y1 + y0) / (float)(y0 - y1);
	}

	static void ApplyViewportAndProj(void)
	{
		if (s_inBlur)
		{
			SetProjectionRaw(0, s_tpw, s_tph, 0);
			if (s_host && s_host->activeCmd)
			{
				D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)s_tpw, (float)s_tph, 0.0f, 1.0f };
				s_host->activeCmd->RSSetViewports(1, &vp);
			}
			return;
		}
		if (s_rtDepth == 0)
		{
			SetProjectionRaw(s_tx0, s_tx0 + s_tw, s_ty0 + s_th, s_ty0);
			if (s_host && s_host->activeCmd)
			{
				D3D12_VIEWPORT vp = { (float)s_tileX, (float)s_tileY, (float)s_tw, (float)s_th, 0.0f, 1.0f };
				s_host->activeCmd->RSSetViewports(1, &vp);
			}
		}
		else
		{
			const RTStackEntry &e = s_rtStack[s_rtDepth - 1];
			if (e.cached)
			{
				SetProjectionRaw(e.baseX, e.baseX + e.width, e.baseY + e.height, e.baseY);
				if (s_host && s_host->activeCmd)
				{
					D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)e.width, (float)e.height, 0.0f, 1.0f };
					s_host->activeCmd->RSSetViewports(1, &vp);
				}
			}
			else
			{
				SetProjectionRaw(s_tx0p, s_tx0p + s_tpw, s_ty0p + s_tph, s_ty0p);
				if (s_host && s_host->activeCmd)
				{
					D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)s_tpw, (float)s_tph, 0.0f, 1.0f };
					s_host->activeCmd->RSSetViewports(1, &vp);
				}
			}
		}
	}

	static D3D12_CPU_DESCRIPTOR_HANDLE GetActiveDSV(void)
	{
		if (s_rtDepth > 0 && s_rtDsvCpu.ptr) return s_rtDsvCpu;
		return s_host ? s_host->activeMainDSV : D3D12_CPU_DESCRIPTOR_HANDLE{0};
	}

	static void BindCurrentRenderTarget(void)
	{
		if (!s_host || !s_host->activeCmd) return;
		if (s_rtDepth == 0)
		{
			s_host->activeCmd->OMSetRenderTargets(1, &s_host->activeMainRTV, FALSE, &s_host->activeMainDSV);
		}
		else
		{
			GDrawDX12Tex *rt = s_rtStack[s_rtDepth - 1].tex;
			TransitionTex(s_host->activeCmd, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetActiveDSV();
			s_host->activeCmd->OMSetRenderTargets(1, &rt->rtvCpu, FALSE, dsv.ptr ? &dsv : NULL);
		}
		ApplyViewportAndProj();
	}

	static void SetupCommonGDrawState(void)
	{
		if (!s_host || !s_host->activeCmd) return;
		BindCurrentRenderTarget();
		ID3D12DescriptorHeap *heaps[2] = { s_host->GetActiveSRVHeap(), s_host->samplerHeap };
		s_host->activeCmd->SetDescriptorHeaps(2, heaps);
		s_host->activeCmd->SetGraphicsRootSignature(s_rootSig);
		LONG sw = (s_rtDepth > 0) ? (LONG)std::max(s_frametexW, s_tpw) : (LONG)s_host->width;
		LONG sh = (s_rtDepth > 0) ? (LONG)std::max(s_frametexH, s_tph) : (LONG)s_host->height;
		D3D12_RECT fullSc = { 0, 0, sw, sh };
		s_host->activeCmd->RSSetScissorRects(1, &fullSc);
	}

	static ID3D12PipelineState *GetOrCreatePSO(int vfmt, int blendMode, bool testId, bool setId, uint8_t stencilTest, uint8_t stencilSet, int psMode = 0)
	{
		uint64_t key = ((uint64_t)(vfmt & 7)) |
			(((uint64_t)(blendMode & 15)) << 3) |
			(((uint64_t)(testId ? 1 : 0)) << 7) |
			(((uint64_t)(setId ? 1 : 0)) << 8) |
			(((uint64_t)stencilTest) << 9) |
			(((uint64_t)stencilSet) << 17) |
			(((uint64_t)(psMode & 7)) << 25);

		auto it = s_psoCache.find(key);
		if (it != s_psoCache.end()) return it->second;

		D3D12_INPUT_ELEMENT_DESC elemsV2[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		D3D12_INPUT_ELEMENT_DESC elemsAA[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,     0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_SINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		D3D12_INPUT_ELEMENT_DESC elemsTC2[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		ID3DBlob *vsBlob = s_vsV2;
		const D3D12_INPUT_ELEMENT_DESC *layout = elemsV2;
		UINT layoutCount = 1;
		if (vfmt == GDRAW_vformat_v2aa)
		{
			vsBlob = s_vsAA;
			layout = elemsAA;
			layoutCount = 2;
		}
		else if (vfmt == GDRAW_vformat_v2tc2)
		{
			vsBlob = s_vsTC2;
			layout = elemsTC2;
			layoutCount = 2;
		}

		ID3DBlob *psBlob = s_psMain;
		if (psMode == 1 && s_psBlur) psBlob = s_psBlur;
		else if (psMode == 2 && s_psColorMatrix) psBlob = s_psColorMatrix;
		else if (psMode == 3 && s_psDropShadow) psBlob = s_psDropShadow;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = s_rootSig;
		pso.VS.pShaderBytecode = vsBlob->GetBufferPointer();
		pso.VS.BytecodeLength = vsBlob->GetBufferSize();
		pso.PS.pShaderBytecode = psBlob->GetBufferPointer();
		pso.PS.BytecodeLength = psBlob->GetBufferSize();
		pso.InputLayout.pInputElementDescs = layout;
		pso.InputLayout.NumElements = layoutCount;

		pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.RasterizerState.FrontCounterClockwise = FALSE;
		pso.RasterizerState.DepthClipEnable = TRUE;

		pso.BlendState.RenderTarget[0].RenderTargetWriteMask = (stencilSet != 0) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;
		pso.BlendState.RenderTarget[0].BlendEnable = (blendMode != GDRAW_BLEND_none) ? TRUE : FALSE;
		pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		if (blendMode == GDRAW_BLEND_add)
		{
			pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
			pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
			pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
		}
		else if (blendMode == GDRAW_BLEND_multiply)
		{
			pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_DEST_COLOR;
			pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_DEST_ALPHA;
			pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		}
		else
		{
			pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
			pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		}

		pso.DepthStencilState.DepthEnable = testId ? TRUE : FALSE;
		pso.DepthStencilState.DepthWriteMask = setId ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pso.DepthStencilState.StencilEnable = ((stencilTest | stencilSet) != 0) ? TRUE : FALSE;
		pso.DepthStencilState.StencilReadMask = stencilTest;
		pso.DepthStencilState.StencilWriteMask = stencilSet;
		pso.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		pso.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		pso.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		pso.DepthStencilState.BackFace = pso.DepthStencilState.FrontFace;

		pso.SampleMask = UINT_MAX;
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		pso.SampleDesc.Count = 1;

		ID3D12PipelineState *created = NULL;
		HRESULT psoHr = s_host->device->CreateGraphicsPipelineState(&pso, __uuidof(ID3D12PipelineState), (void**)&created);
		if (FAILED(psoHr))
		{
			printf("[GDrawDX12] CreateGraphicsPipelineState failed hr=0x%08x vfmt=%d blend=%d\n", (unsigned)psoHr, vfmt, blendMode);
			fflush(stdout);
			return NULL;
		}
		s_psoCache[key] = created;
		return created;
	}

	static float DepthFromId(S32 id)
	{
		return 1.0f - (float)(id + 1) * (1.0f / 65536.0f);
	}

	static void FillDrawConstantBuffer(const GDrawRenderState *r, const GDrawPrimitive *p, GDrawDX12CB &cb)
	{
		memset(&cb, 0, sizeof(cb));
		float d = r ? DepthFromId(r->id) : 0.5f;
		if (r && !r->use_world_space && r->o2w)
		{
			cb.u_World0[0] = r->o2w->m00; cb.u_World0[1] = r->o2w->m01;
			cb.u_World0[2] = d;            cb.u_World0[3] = r->o2w->trans[0];
			cb.u_World1[0] = r->o2w->m10; cb.u_World1[1] = r->o2w->m11;
			cb.u_World1[2] = 0.0f;         cb.u_World1[3] = r->o2w->trans[1];
		}
		else
		{
			cb.u_World0[0] = s_worldToPixel[0]; cb.u_World0[2] = d;
			cb.u_World1[1] = s_worldToPixel[1];
		}

		cb.u_XOff[0] = s_projection[0];
		cb.u_XOff[1] = s_projection[1];
		cb.u_XOff[2] = s_projection[2];
		cb.u_XOff[3] = s_projection[3];

		if (r)
		{
			cb.u_ColorMul[0] = r->color[0];
			cb.u_ColorMul[1] = r->color[1];
			cb.u_ColorMul[2] = r->color[2];
			cb.u_ColorMul[3] = r->color[3];

			if (r->cxf_add)
			{
				cb.u_ColorAdd[0] = (F32)r->cxf_add[0] / 255.0f;
				cb.u_ColorAdd[1] = (F32)r->cxf_add[1] / 255.0f;
				cb.u_ColorAdd[2] = (F32)r->cxf_add[2] / 255.0f;
				cb.u_ColorAdd[3] = (F32)r->cxf_add[3] / 255.0f;
				cb.u_HasTex1 = r->cxf_add[3] ? 2 : 1;
			}
			memcpy(cb.u_Focal, r->focal_point, sizeof(float) * 4);

			if (r->texgen0_enabled && r->s0_texgen && r->t0_texgen)
			{
				memcpy(cb.u_Tex0GenS, r->s0_texgen, sizeof(float) * 4);
				memcpy(cb.u_Tex0GenT, r->t0_texgen, sizeof(float) * 4);
			}

			cb.u_Mode = (int32_t)r->tex0_mode;
			cb.u_HasTex0 = (r->tex[0] != NULL && r->tex0_mode != GDRAW_TEXTURE_none) ? 1 : 0;
			cb.u_UseAA = (p && p->vertex_format == GDRAW_vformat_v2aa) ? 1 : 0;
		}
		else
		{
			cb.u_ColorMul[0] = cb.u_ColorMul[1] = cb.u_ColorMul[2] = cb.u_ColorMul[3] = 1.0f;
		}
	}

	static void BindGDrawTexture0(const GDrawRenderState *r, bool forceLinear = false, bool forceClamp = false)
	{
		ID3D12Resource *res = s_dummyWhiteTex;
		DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
		bool linear = true;
		bool clamp = true;

		if (r && r->tex[0])
		{
			GDrawDX12Tex *gt = (GDrawDX12Tex*)r->tex[0];
			if (gt->isWrapped)
			{
				int w = 0, h = 0;
				ID3D12Resource *mcRes = s_host->GetMCTextureResource(gt->wrappedMcTexId, &w, &h, &linear, &clamp);
				if (mcRes) res = mcRes;
			}
			else if (gt->resource)
			{
				TransitionTex(s_host->activeCmd, gt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				res = gt->resource;
				fmt = gt->format;
			}
			clamp = forceClamp ? true : (r->wrap0 != GDRAW_WRAP_repeat);
			linear = forceLinear ? true : (r->nearest0 == 0);
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = fmt;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
		if (res && s_host->AllocateFrameSRV(res, &srvDesc, &cpu, &gpu))
		{
			s_host->activeCmd->SetGraphicsRootDescriptorTable(1, gpu);
			ID3D12Resource *res1 = s_dummyWhiteTex;
			DXGI_FORMAT fmt1 = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (r && r->tex[1])
			{
				GDrawDX12Tex *gt1 = (GDrawDX12Tex*)r->tex[1];
				if (!gt1->isWrapped && gt1->resource)
				{
					TransitionTex(s_host->activeCmd, gt1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					res1 = gt1->resource;
					fmt1 = gt1->format;
				}
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1 = {};
			srvDesc1.Format = fmt1;
			srvDesc1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc1.Texture2D.MipLevels = 1;
			D3D12_GPU_DESCRIPTOR_HANDLE gpu1 = {};
			if (s_host->AllocateFrameSRV(res1, &srvDesc1, NULL, &gpu1))
			{
				s_host->activeCmd->SetGraphicsRootDescriptorTable(2, gpu1);
			}
		}
		s_host->activeCmd->SetGraphicsRootDescriptorTable(3, s_host->GetSamplerGPUHandle(linear, clamp));
	}

	/* GDrawFunctions callbacks */
	static void RADLINK gdraw_dx12_SetViewSizeAndWorldScale(S32 w, S32 h, F32 scalex, F32 scaley)
	{
		s_fw = (w > 0) ? w : 1;
		s_fh = (h > 0) ? h : 1;
		s_tw = s_fw;
		s_th = s_fh;
		s_tx0 = 0;
		s_ty0 = 0;
		s_worldToPixel[0] = scalex;
		s_worldToPixel[1] = scaley;
		ApplyViewportAndProj();
	}

	static void RADLINK gdraw_dx12_RenderingBegin(void)
	{
		if (s_host) s_host->EnsureFrameRecording();
	}

	static void RADLINK gdraw_dx12_RenderingEnd(void) {}

	static void RADLINK gdraw_dx12_RenderTileBegin(S32 x0, S32 y0, S32 x1, S32 y1, S32 pad, GDrawStats *stats)
	{
		if (x0 == 0 && y0 == 0 && x1 == s_fw && y1 == s_fh)
		{
			pad = 0;
			s_tileEnabled = false;
		}
		else
		{
			s_tileEnabled = true;
		}
		s_tx0 = x0; s_ty0 = y0;
		s_tw = x1 - x0; s_th = y1 - y0;
		s_tpw = s_tw + pad * 2;
		s_tph = s_th + pad * 2;
		s_tx0p = x0 - pad;
		s_ty0p = y0 - pad;
		if (s_tpw > s_frametexW || s_tph > s_frametexH)
		{
			s_frametexW = std::max(s_tpw, s_frametexW);
			s_frametexH = std::max(s_tph, s_frametexH);
		}
		if (!s_host) return;
		s_host->EnsureFrameRecording();
		s_rtDepth = 0;
		SetupCommonGDrawState();
		if (s_host->activeCmd && s_host->activeMainDSV.ptr)
		{
			s_host->activeCmd->ClearDepthStencilView(s_host->activeMainDSV,
				D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);
		}
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_clears;
			stats->num_clears += 1;
		}
	}

	static void RADLINK gdraw_dx12_RenderTileEnd(GDrawStats *)
	{
		s_rtDepth = 0;
		if (s_host) s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_dx12_GetInfo(GDrawInfo *d)
	{
		if (!d) return;
		memset(d, 0, sizeof(*d));
		d->num_stencil_bits = 8;
		d->max_id = 65530;
		d->max_texture_size = 8192;
		d->buffer_format = GDRAW_BFORMAT_vbib;
		d->shared_depth_stencil = 0;
		d->always_mipmap = 0;
		d->conditional_nonpow2 = 0;
		d->has_rendertargets = 1;
	}

	static void RADLINK gdraw_dx12_ClearStencilBits(U32)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetActiveDSV();
		if (s_host && s_host->activeCmd && dsv.ptr)
		{
			s_host->activeCmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);
		}
	}

	static void RADLINK gdraw_dx12_ClearID(void)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetActiveDSV();
		if (s_host && s_host->activeCmd && dsv.ptr)
		{
			s_host->activeCmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
		}
	}

	static void RADLINK gdraw_dx12_SetAntialiasTexture(S32, U8 *) {}

	static void RADLINK gdraw_dx12_DrawIndexedTriangles(GDrawRenderState *r, GDrawPrimitive *p, GDrawVertexBuffer *vbHandle, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || !p || p->num_vertices <= 0) return;
		if (p->vertex_format < 0 || p->vertex_format >= GDRAW_vformat__basic_count) return;

		SetupCommonGDrawState();

		const S32 vfmt = p->vertex_format;
		const UINT vstride = (vfmt == GDRAW_vformat_v2) ? 8u : 16u;
		UINT drawIndices = (p->num_indices > 0) ? (UINT)p->num_indices : 0u;

		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		D3D12_INDEX_BUFFER_VIEW ibv = {};
		if (vbHandle)
		{
			GDrawDX12VB *vb = (GDrawDX12VB*)vbHandle;
			if (!vb->vbuf || !vb->ibuf || drawIndices == 0) return;
			vbv.BufferLocation = vb->vbuf->GetGPUVirtualAddress() + (D3D12_GPU_VIRTUAL_ADDRESS)(uintptr_t)p->vertices;
			vbv.SizeInBytes = (UINT)vb->vlen - (UINT)(uintptr_t)p->vertices;
			vbv.StrideInBytes = vstride;
			ibv.BufferLocation = vb->ibuf->GetGPUVirtualAddress() + (D3D12_GPU_VIRTUAL_ADDRESS)(uintptr_t)p->indices;
			ibv.SizeInBytes = (UINT)vb->ilen - (UINT)(uintptr_t)p->indices;
			ibv.Format = DXGI_FORMAT_R16_UINT;
		}
		else if (p->indices && p->num_indices > 0)
		{
			if (!p->vertices) return;
			const UINT vbytes = (UINT)p->num_vertices * vstride;
			const UINT ibytes = (UINT)p->num_indices * 2u;
			D3D12_GPU_VIRTUAL_ADDRESS vGpu = 0, iGpu = 0;
			void *vp = s_host->AllocFrameUploadBytes(vbytes, 16u, &vGpu);
			void *ip = s_host->AllocFrameUploadBytes(ibytes, 16u, &iGpu);
			if (!vp || !ip) return;
			memcpy(vp, p->vertices, vbytes);
			memcpy(ip, p->indices, ibytes);
			vbv.BufferLocation = vGpu;
			vbv.SizeInBytes = vbytes;
			vbv.StrideInBytes = vstride;
			ibv.BufferLocation = iGpu;
			ibv.SizeInBytes = ibytes;
			ibv.Format = DXGI_FORMAT_R16_UINT;
		}
		else
		{
			/* Dynamic quads without an index buffer (p->indices == NULL, num_indices == 0) */
			if (!p->vertices || (p->num_vertices % 4) != 0) return;
			const UINT numQuads = (UINT)(p->num_vertices / 4);
			drawIndices = numQuads * 6u;
			const UINT vbytes = (UINT)p->num_vertices * vstride;
			const UINT ibytes = drawIndices * 2u;
			D3D12_GPU_VIRTUAL_ADDRESS vGpu = 0, iGpu = 0;
			void *vp = s_host->AllocFrameUploadBytes(vbytes, 16u, &vGpu);
			uint16_t *ip = (uint16_t*)s_host->AllocFrameUploadBytes(ibytes, 16u, &iGpu);
			if (!vp || !ip) return;
			memcpy(vp, p->vertices, vbytes);
			for (UINT q = 0; q < numQuads; ++q)
			{
				const uint16_t base = (uint16_t)(q * 4u);
				ip[q * 6u + 0] = (uint16_t)(base + 0u);
				ip[q * 6u + 1] = (uint16_t)(base + 1u);
				ip[q * 6u + 2] = (uint16_t)(base + 2u);
				ip[q * 6u + 3] = (uint16_t)(base + 0u);
				ip[q * 6u + 4] = (uint16_t)(base + 2u);
				ip[q * 6u + 5] = (uint16_t)(base + 3u);
			}
			vbv.BufferLocation = vGpu;
			vbv.SizeInBytes = vbytes;
			vbv.StrideInBytes = vstride;
			ibv.BufferLocation = iGpu;
			ibv.SizeInBytes = ibytes;
			ibv.Format = DXGI_FORMAT_R16_UINT;
		}

		GDrawDX12CB cb = {};
		FillDrawConstantBuffer(r, p, cb);
		D3D12_GPU_VIRTUAL_ADDRESS cbGpu = 0;
		const UINT cbSize = (UINT)((sizeof(cb) + 255u) & ~255u);
		void *cbPtr = s_host->AllocFrameUploadBytes(cbSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &cbGpu);
		if (!cbPtr) return;
		memcpy(cbPtr, &cb, sizeof(cb));

		const bool testId = (r && r->test_id != 0);
		const bool setId = (r && r->set_id != 0);
		const uint8_t stencilTest = r ? r->stencil_test : 0;
		const uint8_t stencilSet = r ? r->stencil_set : 0;
		const int blendMode = r ? (int)r->blend_mode : GDRAW_BLEND_alpha;
		ID3D12PipelineState *pso = GetOrCreatePSO(vfmt, blendMode, testId, setId, stencilTest, stencilSet);
		if (!pso) return;

		s_host->activeCmd->SetPipelineState(pso);
		s_host->activeCmd->SetGraphicsRootConstantBufferView(0, cbGpu);
		BindGDrawTexture0(r);

		const S32 rtMaxW = (s_rtDepth > 0) ? std::max(s_frametexW, s_tpw) : s_host->width;
		const S32 rtMaxH = (s_rtDepth > 0) ? std::max(s_frametexH, s_tph) : s_host->height;
		if (r && r->scissor)
		{
			S32 x0 = r->scissor_rect.x0, y0 = r->scissor_rect.y0;
			S32 x1 = r->scissor_rect.x1, y1 = r->scissor_rect.y1;
			if (s_rtDepth > 0)
			{
				if (!s_rtStack[s_rtDepth - 1].cached) { x0 -= s_tx0p; x1 -= s_tx0p; y0 -= s_ty0p; y1 -= s_ty0p; }
			}
			else
			{
				if (s_tileEnabled) { x0 -= s_tx0; x1 -= s_tx0; y0 -= s_ty0; y1 -= s_ty0; }
				x0 += s_tileX; x1 += s_tileX; y0 += s_tileY; y1 += s_tileY;
			}
			x0 = std::max<S32>(0, x0); y0 = std::max<S32>(0, y0);
			x1 = std::min<S32>(rtMaxW, x1); y1 = std::min<S32>(rtMaxH, y1);
			if (x1 <= x0 || y1 <= y0) return;
			D3D12_RECT sc = { (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1 };
			s_host->activeCmd->RSSetScissorRects(1, &sc);
		}
		else
		{
			D3D12_RECT sc = { 0, 0, (LONG)rtMaxW, (LONG)rtMaxH };
			s_host->activeCmd->RSSetScissorRects(1, &sc);
		}

		s_host->activeCmd->OMSetStencilRef(255u);
		s_host->activeCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		s_host->activeCmd->IASetVertexBuffers(0, 1, &vbv);
		s_host->activeCmd->IASetIndexBuffer(&ibv);
		s_host->activeCmd->DrawIndexedInstanced(drawIndices, 1, 0, 0, 0);

		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_batches;
			stats->num_batches += 1;
			stats->drawn_indices += (U32)drawIndices;
			stats->drawn_vertices += (U32)p->num_vertices;
		}
		s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_dx12_SetTextureUniqueID(GDrawTexture *t, void *, void *newId)
	{
		GDrawDX12Tex *tex = (GDrawDX12Tex*)t;
		if (tex) tex->uniqueId = newId;
	}

	static rrbool RADLINK gdraw_dx12_MakeTextureBegin(void *owner, S32 width, S32 height, gdraw_texture_format gfmt, gdraw_maketexture_flags flags, GDraw_MakeTexture_ProcessingInfo *info, GDrawStats *)
	{
		if (!info || width <= 0 || height <= 0) return 0;

		PendingMakeTex *pm = (PendingMakeTex*)calloc(1, sizeof(PendingMakeTex));
		if (!pm) return 0;
		pm->tex = new GDrawDX12Tex();
		memset(pm->tex, 0, sizeof(GDrawDX12Tex));
		pm->tex->rtvSlot = -1;
		pm->w = width;
		pm->h = height;
		pm->flags = (U32)flags;
		pm->uniqueId = owner;
		pm->isAlpha8 = (gfmt == GDRAW_TEXTURE_FORMAT_font);
		size_t bpp = pm->isAlpha8 ? 1 : 4;
		pm->rgbaData = (U8*)malloc((size_t)width * (size_t)height * 4u);
		if (!pm->rgbaData)
		{
			delete pm->tex;
			free(pm);
			return 0;
		}

		memset(info, 0, sizeof(*info));
		info->p0 = pm;
		info->texture_data = pm->rgbaData;
		info->num_rows = height;
		info->stride_in_bytes = width * (S32)bpp;
		info->texture_type = GDRAW_TEXTURE_TYPE_rgba;
		return 1;
	}

	static rrbool RADLINK gdraw_dx12_MakeTextureMore(GDraw_MakeTexture_ProcessingInfo *) { return 0; }

	static GDrawTexture *RADLINK gdraw_dx12_MakeTextureEnd(GDraw_MakeTexture_ProcessingInfo *info, GDrawStats *stats)
	{
		if (!info || !info->p0) return NULL;
		PendingMakeTex *pm = (PendingMakeTex*)info->p0;
		GDrawDX12Tex *tex = pm->tex;
		tex->uniqueId = pm->uniqueId;

		if (pm->isAlpha8)
		{
			const size_t n = (size_t)pm->w * (size_t)pm->h;
			const U8 *aSrc = pm->rgbaData;
			for (size_t idx = n; idx-- > 0;)
			{
				U8 a = aSrc[idx];
				pm->rgbaData[idx * 4 + 0] = 255;
				pm->rgbaData[idx * 4 + 1] = 255;
				pm->rgbaData[idx * 4 + 2] = 255;
				pm->rgbaData[idx * 4 + 3] = a;
			}
		}

		bool ok = UploadNewTextureData(tex, pm->rgbaData, pm->w, pm->h, DXGI_FORMAT_R8G8B8A8_UNORM);
		free(pm->rgbaData);
		free(pm);

		if (!ok)
		{
			if (tex->resource) tex->resource->Release();
			delete tex;
			return NULL;
		}

		s_allTextures.push_back(tex);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_alloc_tex;
			stats->alloc_tex += 1;
			stats->alloc_tex_bytes += (U32)(tex->width * tex->height * 4);
		}
		return (GDrawTexture*)tex;
	}

	static rrbool RADLINK gdraw_dx12_UpdateTextureBegin(GDrawTexture *t, void *, GDrawStats *) { return t != NULL ? 1 : 0; }

	static void RADLINK gdraw_dx12_UpdateTextureRect(GDrawTexture *t, void *, S32 x, S32 y, S32 stride, S32 w, S32 h, U8 *samples, gdraw_texture_format format)
	{
		GDrawDX12Tex *tex = (GDrawDX12Tex*)t;
		if (!tex || !tex->resource || !samples || w <= 0 || h <= 0 || !s_host || !s_host->activeCmd) return;

		std::vector<uint8_t> rgba((size_t)w * (size_t)h * 4u);
		for (S32 row = 0; row < h; ++row)
		{
			const U8 *srcRow = samples + (size_t)row * (size_t)stride;
			uint8_t *dstRow = rgba.data() + (size_t)row * (size_t)w * 4u;
			if (format == GDRAW_TEXTURE_FORMAT_font)
			{
				for (S32 col = 0; col < w; ++col)
				{
					dstRow[col * 4 + 0] = 255;
					dstRow[col * 4 + 1] = 255;
					dstRow[col * 4 + 2] = 255;
					dstRow[col * 4 + 3] = srcRow[col];
				}
			}
			else
			{
				memcpy(dstRow, srcRow, (size_t)w * 4u);
			}
		}

		const UINT srcRow = (UINT)w * 4u;
		const UINT alignedPitch = (srcRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		ID3D12Resource *tmp = CreateCommittedBuffer((UINT64)alignedPitch * (UINT64)h, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (!tmp) return;
		void *mp = NULL;
		if (SUCCEEDED(tmp->Map(0, NULL, &mp)) && mp)
		{
			for (int row = 0; row < h; ++row)
				memcpy((uint8_t*)mp + (size_t)row * alignedPitch, rgba.data() + (size_t)row * srcRow, srcRow);
			tmp->Unmap(0, NULL);
		}
		TransitionTex(s_host->activeCmd, tex, D3D12_RESOURCE_STATE_COPY_DEST);
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = tex->resource;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = tmp;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = 0;
		src.PlacedFootprint.Footprint.Format = tex->format;
		src.PlacedFootprint.Footprint.Width = (UINT)w;
		src.PlacedFootprint.Footprint.Height = (UINT)h;
		src.PlacedFootprint.Footprint.Depth = 1;
		src.PlacedFootprint.Footprint.RowPitch = alignedPitch;

		s_host->activeCmd->CopyTextureRegion(&dst, (UINT)x, (UINT)y, 0, &src, NULL);
		TransitionTex(s_host->activeCmd, tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		s_host->DeferredReleaseIUnknown(tmp);
	}

	static void RADLINK gdraw_dx12_UpdateTextureEnd(GDrawTexture *, void *, GDrawStats *) {}

	static void RADLINK gdraw_dx12_FreeTexture(GDrawTexture *t, void *, GDrawStats *)
	{
		GDrawDX12Tex *tex = (GDrawDX12Tex*)t;
		if (!tex) return;
		if (tex->isRT)
		{
			if (tex->rtvSlot >= 0 && tex->rtvSlot < 8)
				s_rtSlotUsed[tex->rtvSlot] = false;
			return;
		}
		auto it = std::find(s_allTextures.begin(), s_allTextures.end(), tex);
		if (it != s_allTextures.end()) s_allTextures.erase(it);
		if (!tex->isWrapped && tex->resource && s_host && s_host->DeferredReleaseIUnknown)
		{
			s_host->DeferredReleaseIUnknown(tex->resource);
		}
		delete tex;
	}

	static rrbool RADLINK gdraw_dx12_TryToLockTexture(GDrawTexture *t, void *, GDrawStats *) { return t != NULL ? 1 : 0; }

	static rrbool RADLINK gdraw_dx12_MakeVertexBufferBegin(void *owner, gdraw_vformat, S32 vbuf_size, S32 ibuf_size, GDraw_MakeVertexBuffer_ProcessingInfo *info, GDrawStats *)
	{
		if (!info || vbuf_size <= 0 || ibuf_size <= 0) return 0;
		PendingMakeVB *pv = (PendingMakeVB*)calloc(1, sizeof(PendingMakeVB));
		if (!pv) return 0;
		pv->vdata = (U8*)malloc((size_t)vbuf_size);
		pv->idata = (U8*)malloc((size_t)ibuf_size);
		pv->vlen = vbuf_size;
		pv->ilen = ibuf_size;
		pv->uniqueId = owner;
		if (!pv->vdata || !pv->idata)
		{
			free(pv->vdata); free(pv->idata); free(pv);
			return 0;
		}
		memset(info, 0, sizeof(*info));
		info->p0 = pv;
		info->vertex_data = pv->vdata;
		info->index_data = pv->idata;
		info->vertex_data_length = vbuf_size;
		info->index_data_length = ibuf_size;
		return 1;
	}

	static rrbool RADLINK gdraw_dx12_MakeVertexBufferMore(GDraw_MakeVertexBuffer_ProcessingInfo *) { return 0; }

	static GDrawVertexBuffer *RADLINK gdraw_dx12_MakeVertexBufferEnd(GDraw_MakeVertexBuffer_ProcessingInfo *info, GDrawStats *stats)
	{
		if (!info || !info->p0) return NULL;
		PendingMakeVB *pv = (PendingMakeVB*)info->p0;
		GDrawDX12VB *vb = new GDrawDX12VB();
		vb->vbuf = CreateCommittedBuffer((UINT64)pv->vlen, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		vb->ibuf = CreateCommittedBuffer((UINT64)pv->ilen, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
		vb->vlen = pv->vlen;
		vb->ilen = pv->ilen;
		vb->uniqueId = pv->uniqueId;

		if (!vb->vbuf || !vb->ibuf)
		{
			if (vb->vbuf) vb->vbuf->Release();
			if (vb->ibuf) vb->ibuf->Release();
			delete vb;
			free(pv->vdata); free(pv->idata); free(pv);
			return NULL;
		}

		void *mp = NULL;
		if (SUCCEEDED(vb->vbuf->Map(0, NULL, &mp)) && mp) { memcpy(mp, pv->vdata, (size_t)pv->vlen); vb->vbuf->Unmap(0, NULL); }
		mp = NULL;
		if (SUCCEEDED(vb->ibuf->Map(0, NULL, &mp)) && mp) { memcpy(mp, pv->idata, (size_t)pv->ilen); vb->ibuf->Unmap(0, NULL); }

		free(pv->vdata); free(pv->idata); free(pv);
		s_allVBs.push_back(vb);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_alloc_tex;
			stats->alloc_tex += 1;
			stats->alloc_tex_bytes += (U32)(vb->vlen + vb->ilen);
		}
		return (GDrawVertexBuffer*)vb;
	}

	static rrbool RADLINK gdraw_dx12_TryToLockVertexBuffer(GDrawVertexBuffer *vb, void *, GDrawStats *) { return vb != NULL ? 1 : 0; }

	static void RADLINK gdraw_dx12_FreeVertexBuffer(GDrawVertexBuffer *buf, void *, GDrawStats *)
	{
		GDrawDX12VB *vb = (GDrawDX12VB*)buf;
		if (!vb) return;
		auto it = std::find(s_allVBs.begin(), s_allVBs.end(), vb);
		if (it != s_allVBs.end()) s_allVBs.erase(it);
		if (s_host && s_host->DeferredReleaseIUnknown)
		{
			if (vb->vbuf) s_host->DeferredReleaseIUnknown(vb->vbuf);
			if (vb->ibuf) s_host->DeferredReleaseIUnknown(vb->ibuf);
		}
		delete vb;
	}

	static void RADLINK gdraw_dx12_UnlockHandles(GDrawStats *) {}

	static bool EnsureRTHeapsAndDSV(S32 w, S32 h)
	{
		if (!s_host || !s_host->device) return false;
		if (!s_rtvHeap)
		{
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.NumDescriptors = 8;
			hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			if (FAILED(s_host->device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&s_rtvHeap)))
				return false;
		}
		if (!s_rtDsvHeap)
		{
			D3D12_DESCRIPTOR_HEAP_DESC hd = {};
			hd.NumDescriptors = 1;
			hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			if (FAILED(s_host->device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&s_rtDsvHeap)))
				return false;
			s_rtDsvCpu = s_rtDsvHeap->GetCPUDescriptorHandleForHeapStart();
		}
		if (s_rtDsvTex && (s_rtDsvW != w || s_rtDsvH != h))
		{
			s_host->DeferredReleaseIUnknown(s_rtDsvTex);
			s_rtDsvTex = NULL;
			s_rtDsvW = 0;
			s_rtDsvH = 0;
		}
		if (!s_rtDsvTex)
		{
			D3D12_HEAP_PROPERTIES hp = {};
			hp.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width = (UINT64)w;
			rd.Height = (UINT)h;
			rd.DepthOrArraySize = 1;
			rd.MipLevels = 1;
			rd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			rd.SampleDesc.Count = 1;
			rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			D3D12_CLEAR_VALUE cv = {};
			cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			cv.DepthStencil.Depth = 1.0f;
			cv.DepthStencil.Stencil = 0;
			if (FAILED(s_host->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, __uuidof(ID3D12Resource), (void**)&s_rtDsvTex)))
				return false;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvd = {};
			dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			s_host->device->CreateDepthStencilView(s_rtDsvTex, &dsvd, s_rtDsvCpu);
			s_rtDsvW = w;
			s_rtDsvH = h;
		}
		return true;
	}

	static GDrawDX12Tex *AcquirePooledRT(S32 w, S32 h)
	{
		int slot = -1;
		for (int i = 0; i < 8; ++i)
		{
			if (!s_rtSlotUsed[i]) { slot = i; break; }
		}
		if (slot < 0) return NULL;
		if ((int)s_rtPool.size() <= slot) s_rtPool.resize((size_t)(slot + 1), NULL);
		GDrawDX12Tex *rt = s_rtPool[slot];
		if (rt && (rt->width != w || rt->height != h))
		{
			if (rt->resource) s_host->DeferredReleaseIUnknown(rt->resource);
			delete rt;
			rt = NULL;
			s_rtPool[slot] = NULL;
		}
		if (!rt)
		{
			D3D12_HEAP_PROPERTIES hp = {};
			hp.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC rd = {};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width = (UINT64)w;
			rd.Height = (UINT)h;
			rd.DepthOrArraySize = 1;
			rd.MipLevels = 1;
			rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			rd.SampleDesc.Count = 1;
			rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			D3D12_CLEAR_VALUE cv = {};
			cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			ID3D12Resource *res = NULL;
			if (FAILED(s_host->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, __uuidof(ID3D12Resource), (void**)&res)))
				return NULL;
			UINT rtvInc = s_host->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = s_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtvCpu.ptr += (SIZE_T)slot * rtvInc;
			s_host->device->CreateRenderTargetView(res, NULL, rtvCpu);
			rt = new GDrawDX12Tex();
			memset(rt, 0, sizeof(*rt));
			rt->resource = res;
			rt->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
			rt->format = DXGI_FORMAT_R8G8B8A8_UNORM;
			rt->width = w;
			rt->height = h;
			rt->isRT = true;
			rt->rtvCpu = rtvCpu;
			rt->rtvSlot = slot;
			s_rtPool[slot] = rt;
		}
		s_rtSlotUsed[slot] = true;
		return rt;
	}

	static rrbool RADLINK gdraw_dx12_TextureDrawBufferBegin(gswf_recti *reg, gdraw_texture_format, U32, void *owner, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || s_rtDepth >= 8) return 0;
		S32 needW = std::max(s_frametexW, s_tpw);
		S32 needH = std::max(s_frametexH, s_tph);
		if (owner && reg)
		{
			needW = std::max(needW, reg->x1 - reg->x0);
			needH = std::max(needH, reg->y1 - reg->y0);
		}
		if (needW <= 0 || needH <= 0) return 0;
		if (needW > s_frametexW || needH > s_frametexH)
		{
			s_frametexW = std::max(s_frametexW, needW);
			s_frametexH = std::max(s_frametexH, needH);
		}
		if (!EnsureRTHeapsAndDSV(s_frametexW, s_frametexH)) return 0;
		GDrawDX12Tex *rt = AcquirePooledRT(s_frametexW, s_frametexH);
		if (!rt) return 0;

		RTStackEntry &e = s_rtStack[s_rtDepth++];
		e.tex = rt;
		e.cached = (owner != NULL);
		if (owner && reg)
		{
			e.baseX = reg->x0;
			e.baseY = reg->y0;
			e.width = reg->x1 - reg->x0;
			e.height = reg->y1 - reg->y0;
			e.validRect = { 0, 0, e.width, e.height };
		}
		else
		{
			e.baseX = s_tx0p;
			e.baseY = s_ty0p;
			e.width = s_tpw;
			e.height = s_tph;
			if (reg)
			{
				e.validRect.x0 = std::max<S32>(0, reg->x0 - s_tx0p);
				e.validRect.y0 = std::max<S32>(0, reg->y0 - s_ty0p);
				e.validRect.x1 = std::min<S32>(s_tpw, reg->x1 - s_tx0p);
				e.validRect.y1 = std::min<S32>(s_tph, reg->y1 - s_ty0p);
			}
			else
			{
				e.validRect = { 0, 0, s_tpw, s_tph };
			}
		}

		BindCurrentRenderTarget();
		const float clearCol[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		s_host->activeCmd->ClearRenderTargetView(rt->rtvCpu, clearCol, 0, NULL);
		if (s_rtDepth == 1 && s_rtDsvCpu.ptr)
		{
			s_host->activeCmd->ClearDepthStencilView(s_rtDsvCpu, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);
		}
		if (stats)
		{
			stats->nonzero_flags |= (GDRAW_STATS_rendtarg | GDRAW_STATS_clears);
			stats->rendertarget_changes += 1;
			stats->num_clears += 1;
		}
		return 1;
	}

	static GDrawTexture *RADLINK gdraw_dx12_TextureDrawBufferEnd(GDrawStats *stats)
	{
		if (s_rtDepth <= 0) return NULL;
		GDrawDX12Tex *rt = s_rtStack[s_rtDepth - 1].tex;
		TransitionTex(s_host->activeCmd, rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		--s_rtDepth;
		BindCurrentRenderTarget();
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_rendtarg;
			stats->rendertarget_changes += 1;
		}
		return (GDrawTexture*)rt;
	}

	static void DrawFilterScreenQuad(const GDrawRenderState *r, const gswf_recti &s, const F32 tc[4],
		int psMode, int blendMode, const float (*blurTaps)[4], int numTaps, const float *blurClamp,
		const float *filterData, bool forceLinear, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || s.x1 <= s.x0 || s.y1 <= s.y0) return;

		struct QuadV { float x, y, u, v; };
		const QuadV verts[4] = {
			{ (float)s.x0, (float)s.y0, tc[0], tc[1] },
			{ (float)s.x1, (float)s.y0, tc[2], tc[1] },
			{ (float)s.x1, (float)s.y1, tc[2], tc[3] },
			{ (float)s.x0, (float)s.y1, tc[0], tc[3] }
		};
		const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

		D3D12_GPU_VIRTUAL_ADDRESS vGpu = 0, iGpu = 0;
		void *vp = s_host->AllocFrameUploadBytes(sizeof(verts), 16u, &vGpu);
		void *ip = s_host->AllocFrameUploadBytes(sizeof(idx), 16u, &iGpu);
		if (!vp || !ip) return;
		memcpy(vp, verts, sizeof(verts));
		memcpy(ip, idx, sizeof(idx));

		GDrawDX12CB cb = {};
		cb.u_World0[0] = 1.0f; cb.u_World0[2] = 0.0f;
		cb.u_World1[1] = 1.0f;
		memcpy(cb.u_XOff, s_projection, sizeof(float) * 4);
		cb.u_HasTex0 = 1;
		cb.u_Mode = GDRAW_TEXTURE_normal;
		if (r)
		{
			cb.u_ColorMul[0] = r->color[0]; cb.u_ColorMul[1] = r->color[1];
			cb.u_ColorMul[2] = r->color[2]; cb.u_ColorMul[3] = r->color[3];
			if (r->cxf_add && psMode == 0)
			{
				cb.u_ColorAdd[0] = (F32)r->cxf_add[0] / 255.0f;
				cb.u_ColorAdd[1] = (F32)r->cxf_add[1] / 255.0f;
				cb.u_ColorAdd[2] = (F32)r->cxf_add[2] / 255.0f;
				cb.u_ColorAdd[3] = (F32)r->cxf_add[3] / 255.0f;
				cb.u_HasTex1 = r->cxf_add[3] ? 2 : 1;
			}
		}
		else
		{
			cb.u_ColorMul[0] = cb.u_ColorMul[1] = cb.u_ColorMul[2] = cb.u_ColorMul[3] = 1.0f;
		}
		if (psMode == 1 && blurTaps)
		{
			cb.u_BlurNumTaps = std::min(numTaps, 9);
			memcpy(cb.u_BlurTaps, blurTaps, (size_t)cb.u_BlurNumTaps * sizeof(float) * 4);
		}
		else if (psMode == 2 && r && r->shader_data)
		{
			for (int i = 0; i < 5; ++i)
				memcpy(cb.u_BlurTaps[i], &r->shader_data[i * 4], sizeof(float) * 4);
		}
		else if (psMode == 3 && r && r->shader_data)
		{
			cb.u_ColorMul[0] = r->shader_data[0]; cb.u_ColorMul[1] = r->shader_data[1];
			cb.u_ColorMul[2] = r->shader_data[2]; cb.u_ColorMul[3] = r->shader_data[3];
		}
		if (blurClamp) memcpy(cb.u_BlurClamp, blurClamp, sizeof(float) * 4);
		else { cb.u_BlurClamp[0] = 0.0f; cb.u_BlurClamp[1] = 0.0f; cb.u_BlurClamp[2] = 1.0f; cb.u_BlurClamp[3] = 1.0f; }
		if (filterData) memcpy(cb.u_FilterData, filterData, sizeof(float) * 4);

		D3D12_GPU_VIRTUAL_ADDRESS cbGpu = 0;
		const UINT cbSize = (UINT)((sizeof(cb) + 255u) & ~255u);
		void *cbPtr = s_host->AllocFrameUploadBytes(cbSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &cbGpu);
		if (!cbPtr) return;
		memcpy(cbPtr, &cb, sizeof(cb));

		ID3D12PipelineState *pso = GetOrCreatePSO(GDRAW_vformat_v2tc2, blendMode, false, false, 0, 0, psMode);
		if (!pso) return;

		D3D12_VIEWPORT vpMain = { 0.0f, 0.0f, (float)s_frametexW, (float)s_frametexH, 0.0f, 1.0f };
		if (s_rtDepth == 0 && !s_inBlur)
		{
			vpMain = { (float)s_tileX, (float)s_tileY, (float)s_tw, (float)s_th, 0.0f, 1.0f };
		}
		else if (s_inBlur)
		{
			vpMain = { 0.0f, 0.0f, (float)s_tpw, (float)s_tph, 0.0f, 1.0f };
		}
		s_host->activeCmd->RSSetViewports(1, &vpMain);
		const S32 scW = (s_rtDepth > 0 || s_inBlur) ? s_frametexW : s_host->width;
		const S32 scH = (s_rtDepth > 0 || s_inBlur) ? s_frametexH : s_host->height;
		D3D12_RECT sc = { 0, 0, (LONG)scW, (LONG)scH };
		s_host->activeCmd->RSSetScissorRects(1, &sc);
		s_host->activeCmd->SetPipelineState(pso);
		s_host->activeCmd->SetGraphicsRootConstantBufferView(0, cbGpu);
		BindGDrawTexture0(r, forceLinear, true);

		D3D12_VERTEX_BUFFER_VIEW vbv = { vGpu, (UINT)sizeof(verts), (UINT)sizeof(QuadV) };
		D3D12_INDEX_BUFFER_VIEW ibv = { iGpu, (UINT)sizeof(idx), DXGI_FORMAT_R16_UINT };
		s_host->activeCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		s_host->activeCmd->IASetVertexBuffers(0, 1, &vbv);
		s_host->activeCmd->IASetIndexBuffer(&ibv);
		s_host->activeCmd->DrawIndexedInstanced(6u, 1u, 0u, 0, 0u);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_batches;
			stats->num_batches += 1;
			stats->drawn_indices += 6;
			stats->drawn_vertices += 4;
		}
	}

	static void ExpandRectClamped(gswf_recti &dst, const gswf_recti &src, S32 ex, S32 ey, S32 maxW, S32 maxH)
	{
		dst.x0 = std::max<S32>(0, src.x0 - ex);
		dst.y0 = std::max<S32>(0, src.y0 - ey);
		dst.x1 = std::min<S32>(maxW, src.x1 + ex);
		dst.y1 = std::min<S32>(maxH, src.y1 + ey);
	}

	static void RunBlurAxis(int axis, GDrawRenderState *r, F32 blurWidth, F32 texel,
		gswf_recti &drawBounds, gswf_recti &sampleBounds, GDrawTexture *protect, GDrawStats *stats)
	{
		if (blurWidth <= 1.0f || !r->tex[0]) return;
		S32 w = ((S32)ceilf((blurWidth - 1.0f) * 0.5f)) * 2 + 1;
		F32 data[9][4] = {};
		S32 taps = ((w - 1) >> 1) + 1;
		S32 expand = 0;
		const int offAxis = 1 - axis;
		if (taps <= 9)
		{
			F32 edgeWeight = 1.0f - (F32)(w - blurWidth) * 0.5f;
			F32 invWeight = 1.0f / blurWidth;
			for (S32 i = 0; i < taps; ++i)
			{
				data[i][axis] = (-taps + 1.5f + i * 2) * texel;
				data[i][offAxis] = 0.0f;
				data[i][2] = 2.0f * invWeight;
				data[i][3] = 0.0f;
			}
			data[taps - 1][axis] = (taps - 1) * texel;
			data[taps - 1][2] = edgeWeight * invWeight;
			data[0][axis] = (-taps + 1.0f + 1.0f / (edgeWeight + 1.0f)) * texel;
			data[0][2] = (edgeWeight + 1.0f) * invWeight;
			expand = taps - 1;
		}
		else
		{
			taps = 9;
			F32 halfSpan = (blurWidth - 1.0f) * 0.5f;
			for (S32 i = 0; i < 9; ++i)
			{
				F32 offset = -halfSpan + (2.0f * halfSpan) * (i / 8.0f);
				data[i][axis] = offset * texel;
				data[i][offAxis] = 0.0f;
				data[i][2] = 1.0f / 9.0f;
			}
			expand = (S32)ceilf(halfSpan);
		}
		ExpandRectClamped(drawBounds, drawBounds, axis ? 0 : expand, axis ? expand : 0, s_tpw, s_tph);
		const F32 scaleS = 1.0f / (F32)s_frametexW;
		const F32 scaleT = 1.0f / (F32)s_frametexH;
		const F32 tc[4] = {
			scaleS * drawBounds.x0,
			scaleT * drawBounds.y0,
			scaleS * drawBounds.x1,
			scaleT * drawBounds.y1
		};
		const float clampv[4] = {
			scaleS * (sampleBounds.x0 + 0.5f),
			scaleT * (sampleBounds.y0 + 0.5f),
			scaleS * (sampleBounds.x1 - 0.5f),
			scaleT * (sampleBounds.y1 - 0.5f)
		};
		gswf_recti region = { s_tx0p + drawBounds.x0, s_ty0p + drawBounds.y0, s_tx0p + drawBounds.x1, s_ty0p + drawBounds.y1 };
		if (!gdraw_dx12_TextureDrawBufferBegin(&region, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats)) return;
		DrawFilterScreenQuad(r, drawBounds, tc, 1, GDRAW_BLEND_none, data, taps, clampv, NULL, true, stats);
		GDrawTexture *outTex = gdraw_dx12_TextureDrawBufferEnd(stats);
		if (r->tex[0] && r->tex[0] != protect && r->tex[0] != outTex)
			gdraw_dx12_FreeTexture(r->tex[0], NULL, stats);
		r->tex[0] = outTex;
		ExpandRectClamped(sampleBounds, drawBounds, 1, 1, s_tpw, s_tph);
	}

	static void RADLINK gdraw_dx12_FilterQuad(GDrawRenderState *r, S32 x0, S32 y0, S32 x1, S32 y1, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || !r) return;
		SetupCommonGDrawState();

		gswf_recti s;
		s.x0 = std::max<S32>(x0, s_tx0p);
		s.y0 = std::max<S32>(y0, s_ty0p);
		s.x1 = std::min<S32>(x1, s_tx0p + s_tpw);
		s.y1 = std::min<S32>(y1, s_ty0p + s_tph);
		if (s.x1 <= s.x0 || s.y1 <= s.y0) return;

		F32 tc[4] = {
			(s.x0 - s_tx0p) / (F32)s_frametexW,
			(s.y0 - s_ty0p) / (F32)s_frametexH,
			(s.x1 - s_tx0p) / (F32)s_frametexW,
			(s.y1 - s_ty0p) / (F32)s_frametexH
		};

		if (r->blend_mode == GDRAW_BLEND_filter)
		{
			if (r->filter == GDRAW_FILTER_blur)
			{
				gswf_recti db = { s.x0 - s_tx0p, s.y0 - s_ty0p, s.x1 - s_tx0p, s.y1 - s_ty0p };
				gswf_recti sb = db;
				ExpandRectClamped(sb, sb, 1, 1, s_tpw, s_tph);
				GDrawTexture *protect = r->tex[0];
				const F32 dx = 1.0f / (F32)s_frametexW;
				const F32 dy = 1.0f / (F32)s_frametexH;
				s_inBlur = true;
				for (S32 p = 0; p < r->blur_passes; ++p)
				{
					RunBlurAxis(0, r, r->blur_x, dx, db, sb, protect, stats);
					RunBlurAxis(1, r, r->blur_y, dy, db, sb, protect, stats);
				}
				s_inBlur = false;
				BindCurrentRenderTarget();
			}
			else if (r->filter == GDRAW_FILTER_colormatrix)
			{
				GDrawTexture *oldTex = r->tex[0];
				if (gdraw_dx12_TextureDrawBufferBegin(&s, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats))
				{
					DrawFilterScreenQuad(r, s, tc, 2, GDRAW_BLEND_none, NULL, 0, NULL, NULL, true, stats);
					GDrawTexture *outTex = gdraw_dx12_TextureDrawBufferEnd(stats);
					if (oldTex && oldTex != outTex) gdraw_dx12_FreeTexture(oldTex, NULL, stats);
					r->tex[0] = outTex;
				}
			}
			else if (r->filter == GDRAW_FILTER_dropshadow || r->filter == GDRAW_FILTER_bevel)
			{
				GDrawTexture *oldTex0 = r->tex[0];
				GDrawTexture *oldTex1 = r->tex[1];
				if (gdraw_dx12_TextureDrawBufferBegin(&s, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats))
				{
					const float fdata[4] = {
						r->shader_data ? (-r->shader_data[4] / (F32)s_frametexW) : 0.0f,
						r->shader_data ? (-r->shader_data[5] / (F32)s_frametexH) : 0.0f,
						r->shader_data ? r->shader_data[6] : 1.0f,
						0.0f
					};
					DrawFilterScreenQuad(r, s, tc, 3, GDRAW_BLEND_none, NULL, 0, NULL, fdata, true, stats);
					GDrawTexture *outTex = gdraw_dx12_TextureDrawBufferEnd(stats);
					if (oldTex0 && oldTex0 != outTex) gdraw_dx12_FreeTexture(oldTex0, NULL, stats);
					if (oldTex1 && oldTex1 != oldTex0 && oldTex1 != outTex) gdraw_dx12_FreeTexture(oldTex1, NULL, stats);
					r->tex[0] = outTex;
				}
			}
		}
		else
		{
			DrawFilterScreenQuad(r, s, tc, 0, (int)r->blend_mode, NULL, 0, NULL, NULL, (r->nearest0 == 0), stats);
		}
		s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_dx12_DescribeTexture(GDrawTexture *t, GDraw_Texture_Description *desc)
	{
		GDrawDX12Tex *tex = (GDrawDX12Tex*)t;
		if (!desc || !tex) return;
		desc->width = tex->width;
		desc->height = tex->height;
		desc->size_in_bytes = (U32)(tex->width * tex->height * 4);
	}

	static void RADLINK gdraw_dx12_DescribeVertexBuffer(GDrawVertexBuffer *b, GDraw_VertexBuffer_Description *desc)
	{
		GDrawDX12VB *vb = (GDrawDX12VB*)b;
		if (!desc || !vb) return;
		desc->size_in_bytes = vb->vlen + vb->ilen;
	}

	static rrbool CompileHLSL(const char *entry, const char *target, ID3DBlob **outBlob)
	{
		ID3DBlob *err = NULL;
		HRESULT hr = D3DCompile(s_gdrawDX12HLSL, strlen(s_gdrawDX12HLSL), "GDrawDX12", NULL, NULL,
			entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, outBlob, &err);
		if (err) err->Release();
		return SUCCEEDED(hr);
	}
}

GDrawFunctions *gdraw_DX12_CreateContext(DX12BackendGDrawHost *host, int w, int h)
{
	if (!host || !host->device) return NULL;
	s_host = host;
	s_rtSizeW = (w > 0) ? w : 1280;
	s_rtSizeH = (h > 0) ? h : 720;
	s_frametexW = s_rtSizeW;
	s_frametexH = s_rtSizeH;
	s_fw = s_rtSizeW;
	s_fh = s_rtSizeH;
	s_tw = s_fw;
	s_th = s_fh;
	s_tpw = s_fw;
	s_tph = s_fh;

	if (!CompileHLSL("VSMainV2", "vs_5_0", &s_vsV2) ||
		!CompileHLSL("VSMainAA", "vs_5_0", &s_vsAA) ||
		!CompileHLSL("VSMainTC2", "vs_5_0", &s_vsTC2) ||
		!CompileHLSL("PSMain", "ps_5_0", &s_psMain) ||
		!CompileHLSL("PSBlur", "ps_5_0", &s_psBlur) ||
		!CompileHLSL("PSColorMatrix", "ps_5_0", &s_psColorMatrix) ||
		!CompileHLSL("PSDropShadow", "ps_5_0", &s_psDropShadow))
	{
		return NULL;
	}

	D3D12_DESCRIPTOR_RANGE rTex0 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
	D3D12_DESCRIPTOR_RANGE rTex1 = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
	D3D12_DESCRIPTOR_RANGE rSamp = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };

	D3D12_ROOT_PARAMETER params[4] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &rTex0;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges = &rTex1;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[3].DescriptorTable.NumDescriptorRanges = 1;
	params[3].DescriptorTable.pDescriptorRanges = &rSamp;
	params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 4;
	rsDesc.pParameters = params;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob *sigBlob = NULL;
	ID3DBlob *errBlob = NULL;
	if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob)))
	{
		if (errBlob) errBlob->Release();
		return NULL;
	}
	if (errBlob) errBlob->Release();
	if (FAILED(s_host->device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		__uuidof(ID3D12RootSignature), (void**)&s_rootSig)))
	{
		sigBlob->Release();
		return NULL;
	}
	sigBlob->Release();

	GDrawDX12Tex white = {};
	const uint32_t whitePx = 0xFFFFFFFFu;
	UploadNewTextureData(&white, &whitePx, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	s_dummyWhiteTex = white.resource;

	memset(&s_gdrawFuncs, 0, sizeof(s_gdrawFuncs));
	s_gdrawFuncs.SetViewSizeAndWorldScale = gdraw_dx12_SetViewSizeAndWorldScale;
	s_gdrawFuncs.RenderingBegin = gdraw_dx12_RenderingBegin;
	s_gdrawFuncs.RenderingEnd = gdraw_dx12_RenderingEnd;
	s_gdrawFuncs.RenderTileBegin = gdraw_dx12_RenderTileBegin;
	s_gdrawFuncs.RenderTileEnd = gdraw_dx12_RenderTileEnd;
	s_gdrawFuncs.GetInfo = gdraw_dx12_GetInfo;
	s_gdrawFuncs.DrawIndexedTriangles = gdraw_dx12_DrawIndexedTriangles;
	s_gdrawFuncs.SetAntialiasTexture = gdraw_dx12_SetAntialiasTexture;
	s_gdrawFuncs.ClearStencilBits = gdraw_dx12_ClearStencilBits;
	s_gdrawFuncs.ClearID = gdraw_dx12_ClearID;
	s_gdrawFuncs.FilterQuad = gdraw_dx12_FilterQuad;
	s_gdrawFuncs.MakeTextureBegin = gdraw_dx12_MakeTextureBegin;
	s_gdrawFuncs.MakeTextureMore = NULL;
	s_gdrawFuncs.MakeTextureEnd = gdraw_dx12_MakeTextureEnd;
	s_gdrawFuncs.UpdateTextureBegin = gdraw_dx12_UpdateTextureBegin;
	s_gdrawFuncs.UpdateTextureRect = gdraw_dx12_UpdateTextureRect;
	s_gdrawFuncs.UpdateTextureEnd = gdraw_dx12_UpdateTextureEnd;
	s_gdrawFuncs.FreeTexture = gdraw_dx12_FreeTexture;
	s_gdrawFuncs.TryToLockTexture = gdraw_dx12_TryToLockTexture;
	s_gdrawFuncs.MakeVertexBufferBegin = gdraw_dx12_MakeVertexBufferBegin;
	s_gdrawFuncs.MakeVertexBufferMore = gdraw_dx12_MakeVertexBufferMore;
	s_gdrawFuncs.MakeVertexBufferEnd = gdraw_dx12_MakeVertexBufferEnd;
	s_gdrawFuncs.TryToLockVertexBuffer = gdraw_dx12_TryToLockVertexBuffer;
	s_gdrawFuncs.FreeVertexBuffer = gdraw_dx12_FreeVertexBuffer;
	s_gdrawFuncs.TextureDrawBufferBegin = gdraw_dx12_TextureDrawBufferBegin;
	s_gdrawFuncs.TextureDrawBufferEnd = gdraw_dx12_TextureDrawBufferEnd;
	s_gdrawFuncs.UnlockHandles = gdraw_dx12_UnlockHandles;
	s_gdrawFuncs.SetTextureUniqueID = gdraw_dx12_SetTextureUniqueID;
	s_gdrawFuncs.DescribeTexture = gdraw_dx12_DescribeTexture;
	s_gdrawFuncs.DescribeVertexBuffer = gdraw_dx12_DescribeVertexBuffer;

	return &s_gdrawFuncs;
}

void gdraw_DX12_DestroyContext(void)
{
	for (auto &kv : s_psoCache) if (kv.second) kv.second->Release();
	s_psoCache.clear();
	for (GDrawDX12Tex *t : s_allTextures)
	{
		if (t && t->resource && !t->isWrapped) t->resource->Release();
		delete t;
	}
	s_allTextures.clear();
	for (GDrawDX12Tex *rt : s_rtPool)
	{
		if (rt && rt->resource) rt->resource->Release();
		delete rt;
	}
	s_rtPool.clear();
	memset(s_rtSlotUsed, 0, sizeof(s_rtSlotUsed));
	for (GDrawDX12VB *vb : s_allVBs)
	{
		if (vb) { if (vb->vbuf) vb->vbuf->Release(); if (vb->ibuf) vb->ibuf->Release(); delete vb; }
	}
	s_allVBs.clear();
	if (s_dummyWhiteTex) { s_dummyWhiteTex->Release(); s_dummyWhiteTex = NULL; }
	if (s_rtDsvTex) { s_rtDsvTex->Release(); s_rtDsvTex = NULL; }
	s_rtDsvW = 0; s_rtDsvH = 0;
	if (s_rtvHeap) { s_rtvHeap->Release(); s_rtvHeap = NULL; }
	if (s_rtDsvHeap) { s_rtDsvHeap->Release(); s_rtDsvHeap = NULL; }
	if (s_rootSig) { s_rootSig->Release(); s_rootSig = NULL; }
	if (s_vsV2) { s_vsV2->Release(); s_vsV2 = NULL; }
	if (s_vsAA) { s_vsAA->Release(); s_vsAA = NULL; }
	if (s_vsTC2) { s_vsTC2->Release(); s_vsTC2 = NULL; }
	if (s_psMain) { s_psMain->Release(); s_psMain = NULL; }
	if (s_psBlur) { s_psBlur->Release(); s_psBlur = NULL; }
	if (s_psColorMatrix) { s_psColorMatrix->Release(); s_psColorMatrix = NULL; }
	if (s_psDropShadow) { s_psDropShadow->Release(); s_psDropShadow = NULL; }
	s_host = NULL;
}

void gdraw_DX12_SetRendertargetSize(int w, int h)
{
	s_rtSizeW = (w > 0) ? w : 1280;
	s_rtSizeH = (h > 0) ? h : 720;
	s_frametexW = std::max(s_frametexW, s_rtSizeW);
	s_frametexH = std::max(s_frametexH, s_rtSizeH);
	s_fw = s_rtSizeW;
	s_fh = s_rtSizeH;
	s_tw = s_fw;
	s_th = s_fh;
	s_tpw = s_fw;
	s_tph = s_fh;
	ApplyViewportAndProj();
}

void gdraw_DX12_SetTileOrigin(int xPos, int yPos)
{
	s_tileX = xPos;
	s_tileY = yPos;
	s_rtDepth = 0;
	if (s_host)
	{
		s_host->EnsureFrameRecording();
		SetupCommonGDrawState();
	}
}

void gdraw_DX12_NoMoreGDrawThisFrame(void)
{
	s_rtDepth = 0;
	if (s_host && s_host->ApplyMCViewportAndTarget)
	{
		s_host->ApplyMCViewportAndTarget();
	}
}

void gdraw_DX12_BeginCustomDraw_4J(void *regionPtr, float *mat16)
{
	IggyCustomDrawCallbackRegion *region = (IggyCustomDrawCallbackRegion*)regionPtr;
	if (mat16 && region)
	{
		ComputeObjectSpaceMatrix(mat16, region->o2w, s_projection, 0.0f, 0);
	}
	if (s_host)
	{
		if (s_host->InvalidateWorldPipelineCache) s_host->InvalidateWorldPipelineCache();
		if (s_host->ApplyMCViewportAndTarget) s_host->ApplyMCViewportAndTarget();
	}
}

void gdraw_DX12_CalculateCustomDraw_4J(void *regionPtr, float *mat16)
{
	IggyCustomDrawCallbackRegion *region = (IggyCustomDrawCallbackRegion*)regionPtr;
	if (mat16 && region)
	{
		ComputeObjectSpaceMatrix(mat16, region->o2w, s_projection, 0.0f, 0);
	}
}

void gdraw_DX12_EndCustomDraw(void *)
{
	if (s_host)
	{
		SetupCommonGDrawState();
	}
}

void gdraw_DX12_SetViewport_4J(void)
{
	ApplyViewportAndProj();
}

void *gdraw_DX12_WrappedTextureCreate(int textureId)
{
	GDrawDX12Tex *tex = new GDrawDX12Tex();
	memset(tex, 0, sizeof(*tex));
	tex->isWrapped = true;
	tex->wrappedMcTexId = textureId;
	tex->rtvSlot = -1;
	if (s_host && s_host->GetMCTextureResource)
	{
		s_host->GetMCTextureResource(textureId, &tex->width, &tex->height, NULL, NULL);
	}
	s_allTextures.push_back(tex);
	return (void*)tex;
}

void gdraw_DX12_WrappedTextureDestroy(void *handle)
{
	gdraw_dx12_FreeTexture((GDrawTexture*)handle, NULL, NULL);
}
