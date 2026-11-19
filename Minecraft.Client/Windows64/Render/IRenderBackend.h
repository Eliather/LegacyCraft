#pragma once

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C4J_RENDER_BACKEND_ABI_VERSION 1

typedef enum C4JRendererBackendType
{
	C4J_BACKEND_DX11   = 0,
	C4J_BACKEND_OPENGL = 1,
	C4J_BACKEND_VULKAN = 2,
	C4J_BACKEND_DX12   = 3
} C4JRendererBackendType;

typedef struct C4JClearRect
{
	long left;
	long top;
	long right;
	long bottom;
} C4JClearRect;

typedef struct C4JBackendInfo
{
	int backendType;
	int vsyncEnabled;
	int screenWidth;
	int screenHeight;
	char adapterName[128];
	char backendName[32];
} C4JBackendInfo;

/* Numeric blend/compare enum values shared by 4J_Render.h and all backends */
typedef enum C4JBlendFactor
{
	C4J_BLEND_ZERO            = 1,
	C4J_BLEND_ONE             = 2,
	C4J_BLEND_SRC_COLOR       = 3,
	C4J_BLEND_INV_SRC_COLOR   = 4,
	C4J_BLEND_SRC_ALPHA       = 5,
	C4J_BLEND_INV_SRC_ALPHA   = 6,
	C4J_BLEND_DEST_ALPHA      = 7,
	C4J_BLEND_INV_DEST_ALPHA  = 8,
	C4J_BLEND_DEST_COLOR      = 9,
	C4J_BLEND_INV_DEST_COLOR  = 10,
	C4J_BLEND_SRC_ALPHA_SAT   = 11,
	C4J_BLEND_FACTOR_ALPHA    = 14,
	C4J_BLEND_INV_FACTOR_ALPHA= 15
} C4JBlendFactor;

typedef enum C4JCompareFunc
{
	C4J_CMP_NEVER         = 1,
	C4J_CMP_LESS          = 2,
	C4J_CMP_EQUAL         = 3,
	C4J_CMP_LESS_EQUAL    = 4,
	C4J_CMP_GREATER       = 5,
	C4J_CMP_NOT_EQUAL     = 6,
	C4J_CMP_GREATER_EQUAL = 7,
	C4J_CMP_ALWAYS        = 8
} C4JCompareFunc;

typedef int (*C4JCaptureAllocFn)(int width, int height, uint8_t **outPixels, void *user);

typedef struct C4JRenderBackendAPI
{
	uint32_t abiSize;
	uint32_t abiVersion;
	int      backendType;
	const char *backendName;

	/* Window & Swapchain Lifecycle */
	int  (*InitialiseWindow)(HWND hwnd, int width, int height, int fullscreen, int vsync);
	int  (*ResizeWindow)(int width, int height, int fullscreen);
	void (*Shutdown)(void);
	void (*SetVsync)(int enabled);
	int  (*GetVsync)(void);
	void (*GetBackendInfo)(C4JBackendInfo *outInfo);

	/* Frame & Core */
	void (*Tick)(void);
	void (*UpdateGamma)(unsigned short usGamma);
	void (*StartFrame)(void);
	void (*DoScreenGrabOnNextPresent)(void);
	void (*Present)(void);
	void (*Clear)(int flags, const C4JClearRect *pRect);
	void (*SetClearColour)(const float colourRGBA[4]);
	int  (*IsWidescreen)(void);
	int  (*IsHiDef)(void);
	void (*BeginConditionalSurvey)(int identifier);
	void (*EndConditionalSurvey)(void);
	void (*BeginConditionalRendering)(int identifier);
	void (*EndConditionalRendering)(void);

	/* Matrix stack */
	void (*MatrixMode)(int type);
	void (*MatrixSetIdentity)(void);
	void (*MatrixTranslate)(float x, float y, float z);
	void (*MatrixRotate)(float angle, float x, float y, float z);
	void (*MatrixScale)(float x, float y, float z);
	void (*MatrixPerspective)(float fovy, float aspect, float zNear, float zFar);
	void (*MatrixOrthogonal)(float left, float right, float bottom, float top, float zNear, float zFar);
	void (*MatrixPop)(void);
	void (*MatrixPush)(void);
	void (*MatrixMult)(float *mat);
	const float *(*MatrixGet)(int type);
	void (*Set_matrixDirty)(void);

	/* Drawing */
	void (*DrawVertices)(int primitiveType, int count, void *dataIn, int vType, int psType);
	void (*DrawVertexBuffer)(int primitiveType, int count, void *bufferHandle, int vType, int psType);

	/* Command buffers */
	void (*CBuffLockStaticCreations)(void);
	int  (*CBuffCreate)(int count);
	void (*CBuffDelete)(int first, int count);
	void (*CBuffStart)(int index, int full);
	void (*CBuffClear)(int index);
	int  (*CBuffSize)(int index);
	void (*CBuffEnd)(void);
	int  (*CBuffCall)(int index, int full);
	void (*CBuffTick)(void);
	void (*CBuffDeferredModeStart)(void);
	void (*CBuffDeferredModeEnd)(void);

	/* Textures */
	int  (*TextureCreate)(void);
	void (*TextureFree)(int idx);
	void (*TextureBind)(int idx);
	void (*TextureBindVertex)(int idx);
	void (*TextureSetTextureLevels)(int levels);
	int  (*TextureGetTextureLevels)(void);
	void (*TextureData)(int width, int height, void *data, int level, int format);
	void (*TextureDataUpdate)(int xoffset, int yoffset, int width, int height, void *data, int level);
	void (*TextureSetParam)(int param, int value);
	void (*TextureDynamicUpdateStart)(void);
	void (*TextureDynamicUpdateEnd)(void);
	void *(*TextureGetNativeHandle)(int idx);

	/* State control */
	void (*StateSetColour)(float r, float g, float b, float a);
	void (*StateSetDepthMask)(int enable);
	void (*StateSetBlendEnable)(int enable);
	void (*StateSetBlendFunc)(int src, int dst);
	void (*StateSetBlendFactor)(unsigned int colour);
	void (*StateSetAlphaFunc)(int func, float param);
	void (*StateSetDepthFunc)(int func);
	void (*StateSetFaceCull)(int enable);
	void (*StateSetFaceCullCW)(int enable);
	void (*StateSetLineWidth)(float width);
	void (*StateSetWriteEnable)(int red, int green, int blue, int alpha);
	void (*StateSetDepthTestEnable)(int enable);
	void (*StateSetAlphaTestEnable)(int enable);
	void (*StateSetDepthSlopeAndBias)(float slope, float bias);
	void (*StateSetFogEnable)(int enable);
	void (*StateSetFogMode)(int mode);
	void (*StateSetFogNearDistance)(float dist);
	void (*StateSetFogFarDistance)(float dist);
	void (*StateSetFogDensity)(float density);
	void (*StateSetFogColour)(float red, float green, float blue);
	void (*StateSetLightingEnable)(int enable);
	void (*StateSetVertexTextureUV)(float u, float v);
	void (*StateSetLightColour)(int light, float red, float green, float blue);
	void (*StateSetLightAmbientColour)(float red, float green, float blue);
	void (*StateSetLightDirection)(int light, float x, float y, float z);
	void (*StateSetLightEnable)(int light, int enable);
	void (*StateSetViewport)(int viewportType);
	void (*StateSetEnableViewportClipPlanes)(int enable);
	void (*StateSetTexGenCol)(int col, float x, float y, float z, float w, int eyeSpace);
	void (*StateSetStencil)(int function, uint8_t stencil_ref, uint8_t stencil_func_mask, uint8_t stencil_write_mask);
	void (*StateSetForceLOD)(int LOD);

	/* Lifecycle & Events */
	void (*BeginEvent)(const wchar_t *eventName);
	void (*EndEvent)(void);
	void (*Suspend)(void);
	int  (*Suspended)(void);
	void (*Resume)(void);

	/* Iggy / GDraw Bridge */
	void *(*GDraw_CreateContext)(int w, int h);
	void  (*GDraw_DestroyContext)(void);
	void  (*GDraw_SetRendertargetSize)(int w, int h);
	void  (*GDraw_SetTileOrigin)(int xPos, int yPos);
	void  (*GDraw_NoMoreGDrawThisFrame)(void);
	void  (*GDraw_BeginCustomDraw_4J)(void *region, float *mat16);
	void  (*GDraw_CalculateCustomDraw_4J)(void *region, float *mat16);
	void  (*GDraw_EndCustomDraw)(void *region);
	void  (*GDraw_SetViewport_4J)(void);
	void *(*GDraw_WrappedTextureCreate)(int textureId);
	void  (*GDraw_WrappedTextureDestroy)(void *handle);

	/* Backbuffer capture (top-down RGBA8, alpha=255) */
	int  (*CaptureBackBufferRGBA)(C4JCaptureAllocFn allocFn, void *user, int *outWidth, int *outHeight);
} C4JRenderBackendAPI;

typedef const C4JRenderBackendAPI *(*PFN_C4JBackend_GetAPI)(void);

#ifdef __cplusplus
}
#endif
