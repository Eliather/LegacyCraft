#pragma once

#include "IRenderBackend.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>

#ifndef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT 0x00000400
#endif

#ifndef C4J_CLEAR_DEPTH_FLAG
#define C4J_CLEAR_DEPTH_FLAG  1
#endif
#ifndef C4J_CLEAR_COLOUR_FLAG
#define C4J_CLEAR_COLOUR_FLAG 2
#endif

namespace C4JRenderCommon
{
	/* High-performance discrete GPU exports for backend DLLs */
	struct Matrix4x4
	{
		float m[16];

		static Matrix4x4 Identity()
		{
			Matrix4x4 mat;
			memset(mat.m, 0, sizeof(mat.m));
			mat.m[0] = mat.m[5] = mat.m[10] = mat.m[15] = 1.0f;
			return mat;
		}

		static Matrix4x4 Multiply(const Matrix4x4 &a, const Matrix4x4 &b)
		{
			Matrix4x4 res;
			for (int col = 0; col < 4; ++col)
			{
				for (int row = 0; row < 4; ++row)
				{
					float val = 0.0f;
					for (int k = 0; k < 4; ++k)
					{
						val += a.m[k * 4 + row] * b.m[col * 4 + k];
					}
					res.m[col * 4 + row] = val;
				}
			}
			return res;
		}

		static void TransformVec3(const Matrix4x4 &mat, float x, float y, float z, float &ox, float &oy, float &oz)
		{
			ox = mat.m[0] * x + mat.m[4] * y + mat.m[8]  * z;
			oy = mat.m[1] * x + mat.m[5] * y + mat.m[9]  * z;
			oz = mat.m[2] * x + mat.m[6] * y + mat.m[10] * z;
		}

		static bool Invert(const Matrix4x4 &src, Matrix4x4 &dst)
		{
			float inv[16], det;
			const float *m = src.m;

			inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

			inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
			inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
			inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
			inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

			inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
			inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
			inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
			inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];

			inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7]  + m[9] * m[3] * m[6];
			inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7]  - m[8] * m[3] * m[6];
			inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8] * m[1] * m[7]  + m[8] * m[3] * m[5];
			inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8] * m[1] * m[6]  - m[8] * m[2] * m[5];

			det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
			if (fabsf(det) < 1e-6f)
			{
				dst = Matrix4x4::Identity();
				return false;
			}

			det = 1.0f / det;
			for (int i = 0; i < 16; i++)
				dst.m[i] = inv[i] * det;
			return true;
		}
	};

	struct VSConstantBufferData
	{
		float u_WVP[16];
		float u_MV[16];
		float u_TexMatrix[16];
		float u_TexGen[16];
		float u_LightDir[2][4];
		float u_LightColor[2][4];
		float u_LightAmbient[4];
		float u_GlobalLightUV[2];
		int32_t u_LightingEnable;
		int32_t u_UseTexGen;
	};

	struct PSConstantBufferData
	{
		float u_StateColor[4];
		int32_t u_HasTexture;
		int32_t u_HasLightmap;
		int32_t u_AlphaFunc;
		float u_AlphaCutoff;
		float u_FogColor[4];
		int32_t u_FogMode;
		float u_FogNear;
		float u_FogFar;
		float u_FogDensity;
		int32_t u_UseProjection;
		float u_Pad[3];
	};

	struct CBuffCommand
	{
		int primitiveType;
		int count;
		std::vector<uint8_t> vertexData;
		int vType;
		int psType;
		int boundTextureId;
		float stateColour[4];
		bool blendEnable;
		int  blendSrc;
		int  blendDst;
		bool depthTestEnable;
		bool depthMask;
		bool cullEnable;
		bool cullCW;
		bool alphaTestEnable;
		int  alphaFunc;
		float alphaCutoff;
		Matrix4x4 relativeMatrix;
	};

	struct CBuffEntry
	{
		std::shared_ptr<const std::vector<CBuffCommand>> commands;
		size_t byteSize = 0;
		uint64_t generation = 0;
	};

	struct ThreadRenderState
	{
		std::vector<Matrix4x4> matrixStacks[3];
		int matrixMode = 0;
		bool matrixDirty = false;
		int textureLevels = 1;
		int boundTextureId = 0;
		int boundVertexTextureId = -1;

		float stateColour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		bool blendEnable = true;
		int blendSrc = C4J_BLEND_SRC_ALPHA;
		int blendDst = C4J_BLEND_INV_SRC_ALPHA;
		unsigned int blendFactorRGBA = 0xffffffff;
		bool depthTestEnable = false;
		bool depthMask = true;
		int depthFunc = C4J_CMP_LESS_EQUAL;
		bool cullEnable = false;
		bool cullCW = true;
		bool alphaTestEnable = true;
		int alphaFunc = C4J_CMP_GREATER;
		float alphaCutoff = 0.05f;
		uint8_t colorWriteMask = 0x0F;
		float depthBiasSlope = 0.0f;
		float depthBiasConstant = 0.0f;

		bool stencilEnable = false;
		int stencilFunc = C4J_CMP_ALWAYS;
		uint8_t stencilRef = 0;
		uint8_t stencilReadMask = 0xff;
		uint8_t stencilWriteMask = 0xff;

		bool fogEnable = false;
		int fogMode = 1; /* GL_LINEAR */
		float fogNear = 0.0f;
		float fogFar = 100.0f;
		float fogDensity = 0.01f;
		float fogColor[3] = { 0.0f, 0.0f, 0.0f };

		bool lightingEnable = false;
		bool lightEnabled[2] = { false, false };
		float lightDir[2][4] = { { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f, 0.0f } };
		float lightColor[2][4] = { { 1.0f, 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } };
		float lightAmbient[4] = { 0.4f, 0.4f, 0.4f, 1.0f };
		float vertexTexUV[2] = { 0.9375f, 0.9375f };
		float texGenPlanes[4][4] = {};

		int recordingCBuffIndex = 0;
		Matrix4x4 recordingBaseMatrix = Matrix4x4::Identity();
		Matrix4x4 recordingInvBaseMatrix = Matrix4x4::Identity();
		std::vector<CBuffCommand> recordingCommands;

		void EnsureStackInit(int mode)
		{
			if (mode >= 0 && mode < 3 && matrixStacks[mode].empty())
			{
				matrixStacks[mode].push_back(Matrix4x4::Identity());
			}
		}

		void ResetFrameState()
		{
			boundTextureId = 0;
			boundVertexTextureId = -1;
			stateColour[0] = 1.0f;
			stateColour[1] = 1.0f;
			stateColour[2] = 1.0f;
			stateColour[3] = 1.0f;
			blendEnable = false;
			blendSrc = C4J_BLEND_SRC_ALPHA;
			blendDst = C4J_BLEND_INV_SRC_ALPHA;
			blendFactorRGBA = 0xffffffff;
			depthTestEnable = false;
			depthMask = true;
			depthFunc = C4J_CMP_LESS_EQUAL;
			cullEnable = false;
			cullCW = true;
			alphaTestEnable = true;
			alphaFunc = C4J_CMP_GREATER;
			alphaCutoff = 0.0f;
			colorWriteMask = 0x0F;
			depthBiasSlope = 0.0f;
			depthBiasConstant = 0.0f;
			stencilEnable = false;
			stencilFunc = C4J_CMP_ALWAYS;
			stencilRef = 0;
			stencilReadMask = 0xff;
			stencilWriteMask = 0xff;
			fogEnable = false;
			lightingEnable = false;
			lightEnabled[0] = false;
			lightEnabled[1] = false;
		}
	};

	inline uint32_t ComputeMaxMipLevels(int w, int h)
	{
		uint32_t mips = 1;
		while (w > 1 || h > 1)
		{
			if (w > 1) w >>= 1;
			if (h > 1) h >>= 1;
			++mips;
		}
		return mips;
	}

	inline void ComputeViewportRect(int viewportType, int screenW, int screenH,
		float &outX, float &outY, float &outW, float &outH)
	{
		float w = (float)(screenW > 0 ? screenW : 1280);
		float h = (float)(screenH > 0 ? screenH : 720);
		outX = 0.0f;
		outY = 0.0f;
		outW = w;
		outH = h;

		switch (viewportType)
		{
		case 1: /* VIEWPORT_TYPE_SPLIT_TOP */
			outH = h * 0.5f;
			break;
		case 2: /* VIEWPORT_TYPE_SPLIT_BOTTOM */
			outY = h * 0.5f;
			outH = h * 0.5f;
			break;
		case 3: /* VIEWPORT_TYPE_SPLIT_LEFT */
			outW = w * 0.5f;
			break;
		case 4: /* VIEWPORT_TYPE_SPLIT_RIGHT */
			outX = w * 0.5f;
			outW = w * 0.5f;
			break;
		case 5: /* VIEWPORT_TYPE_QUADRANT_TOP_LEFT */
			outW = w * 0.5f;
			outH = h * 0.5f;
			break;
		case 6: /* VIEWPORT_TYPE_QUADRANT_TOP_RIGHT */
			outX = w * 0.5f;
			outW = w * 0.5f;
			outH = h * 0.5f;
			break;
		case 7: /* VIEWPORT_TYPE_QUADRANT_BOTTOM_LEFT */
			outY = h * 0.5f;
			outW = w * 0.5f;
			outH = h * 0.5f;
			break;
		case 8: /* VIEWPORT_TYPE_QUADRANT_BOTTOM_RIGHT */
			outX = w * 0.5f;
			outY = h * 0.5f;
			outW = w * 0.5f;
			outH = h * 0.5f;
			break;
		default:
			break;
		}
	}

	inline void MatrixTranslate(ThreadRenderState &st, float x, float y, float z)
	{
		st.EnsureStackInit(st.matrixMode);
		Matrix4x4 trans = Matrix4x4::Identity();
		trans.m[12] = x;
		trans.m[13] = y;
		trans.m[14] = z;
		st.matrixStacks[st.matrixMode].back() = Matrix4x4::Multiply(st.matrixStacks[st.matrixMode].back(), trans);
		st.matrixDirty = true;
	}

	inline void MatrixRotate(ThreadRenderState &st, float angle, float x, float y, float z)
	{
		st.EnsureStackInit(st.matrixMode);
		float len = sqrtf(x * x + y * y + z * z);
		if (len < 1e-6f) return;
		x /= len; y /= len; z /= len;

		/* glWrapper::glRotatef already converts degrees to radians before calling MatrixRotate */
		const float rad = angle;
		float c = cosf(rad);
		float s = sinf(rad);
		float t = 1.0f - c;

		Matrix4x4 rot = Matrix4x4::Identity();
		rot.m[0] = t * x * x + c;     rot.m[4] = t * x * y - z * s; rot.m[8]  = t * x * z + y * s;
		rot.m[1] = t * x * y + z * s; rot.m[5] = t * y * y + c;     rot.m[9]  = t * y * z - x * s;
		rot.m[2] = t * x * z - y * s; rot.m[6] = t * y * z + x * s; rot.m[10] = t * z * z + c;

		st.matrixStacks[st.matrixMode].back() = Matrix4x4::Multiply(st.matrixStacks[st.matrixMode].back(), rot);
		st.matrixDirty = true;
	}

	inline void MatrixScale(ThreadRenderState &st, float x, float y, float z)
	{
		st.EnsureStackInit(st.matrixMode);
		Matrix4x4 scl = Matrix4x4::Identity();
		scl.m[0] = x;
		scl.m[5] = y;
		scl.m[10] = z;
		st.matrixStacks[st.matrixMode].back() = Matrix4x4::Multiply(st.matrixStacks[st.matrixMode].back(), scl);
		st.matrixDirty = true;
	}

	inline void MatrixPerspective(ThreadRenderState &st, float fovy, float aspect, float zNear, float zFar)
	{
		st.EnsureStackInit(st.matrixMode);
		const float degToRad = 3.14159265358979323846f / 180.0f;
		float tanHalfFovy = tanf((fovy * degToRad) * 0.5f);
		if (fabsf(tanHalfFovy) < 1e-6f) tanHalfFovy = 1e-6f;
		Matrix4x4 persp;
		memset(persp.m, 0, sizeof(persp.m));
		persp.m[0] = 1.0f / (aspect * tanHalfFovy);
		persp.m[5] = 1.0f / tanHalfFovy;
		persp.m[10] = -(zFar + zNear) / (zFar - zNear);
		persp.m[11] = -1.0f;
		persp.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);

		st.matrixStacks[st.matrixMode].back() = Matrix4x4::Multiply(st.matrixStacks[st.matrixMode].back(), persp);
		st.matrixDirty = true;
	}

	inline void MatrixOrthogonal(ThreadRenderState &st, float left, float right, float bottom, float top, float zNear, float zFar)
	{
		st.EnsureStackInit(st.matrixMode);
		Matrix4x4 ortho;
		memset(ortho.m, 0, sizeof(ortho.m));
		ortho.m[0] = 2.0f / (right - left);
		ortho.m[5] = 2.0f / (top - bottom);
		ortho.m[10] = -2.0f / (zFar - zNear);
		ortho.m[12] = -(right + left) / (right - left);
		ortho.m[13] = -(top + bottom) / (top - bottom);
		ortho.m[14] = -(zFar + zNear) / (zFar - zNear);
		ortho.m[15] = 1.0f;

		st.matrixStacks[st.matrixMode].back() = Matrix4x4::Multiply(st.matrixStacks[st.matrixMode].back(), ortho);
		st.matrixDirty = true;
	}

	inline void FillVSConstants(const ThreadRenderState &st,
		const Matrix4x4 &wvp, const Matrix4x4 &mv,
		int vType, int psType, VSConstantBufferData &out)
	{
		Matrix4x4 texMat = (!st.matrixStacks[2].empty()) ? st.matrixStacks[2].back() : Matrix4x4::Identity();
		memcpy(out.u_WVP, wvp.m, sizeof(wvp.m));
		memcpy(out.u_MV, mv.m, sizeof(mv.m));
		memcpy(out.u_TexMatrix, texMat.m, sizeof(texMat.m));
		memcpy(out.u_TexGen, st.texGenPlanes, sizeof(st.texGenPlanes));
		for (int i = 0; i < 2; ++i)
		{
			memcpy(out.u_LightDir[i], st.lightDir[i], sizeof(float) * 4);
			memcpy(out.u_LightColor[i], st.lightColor[i], sizeof(float) * 4);
			out.u_LightColor[i][3] = st.lightEnabled[i] ? 1.0f : 0.0f;
		}
		memcpy(out.u_LightAmbient, st.lightAmbient, sizeof(float) * 4);
		out.u_GlobalLightUV[0] = st.vertexTexUV[0];
		out.u_GlobalLightUV[1] = st.vertexTexUV[1];
		out.u_LightingEnable = (st.lightingEnable || vType == 2 /* VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_LIT */) ? 1 : 0;
		out.u_UseTexGen = (vType == 3 /* VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN */ || psType == 1 /* PIXEL_SHADER_TYPE_PROJECTION */) ? 1 : 0;
	}

	inline void FillPSConstants(const ThreadRenderState &st,
		const float stateColor[4], bool hasTex, bool hasLight,
		bool alphaTestEnable, int alphaFunc, float alphaCutoff, int psType,
		PSConstantBufferData &out)
	{
		memcpy(out.u_StateColor, stateColor, sizeof(float) * 4);
		out.u_HasTexture = hasTex ? 1 : 0;
		out.u_HasLightmap = hasLight ? 1 : 0;
		out.u_AlphaFunc = alphaTestEnable ? alphaFunc : C4J_CMP_ALWAYS;
		out.u_AlphaCutoff = alphaCutoff;
		out.u_FogColor[0] = st.fogColor[0];
		out.u_FogColor[1] = st.fogColor[1];
		out.u_FogColor[2] = st.fogColor[2];
		out.u_FogColor[3] = st.fogEnable ? 1.0f : 0.0f;
		out.u_FogMode = st.fogMode;
		out.u_FogNear = st.fogNear;
		out.u_FogFar = st.fogFar;
		out.u_FogDensity = st.fogDensity;
		out.u_UseProjection = (psType == 1 /* PIXEL_SHADER_TYPE_PROJECTION */) ? 1 : 0;
		out.u_Pad[0] = out.u_Pad[1] = out.u_Pad[2] = 0.0f;
	}
}
