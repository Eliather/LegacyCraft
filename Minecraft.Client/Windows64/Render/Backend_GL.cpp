#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>

#include "RenderCommon.h"
#include "iggy.h"
#include "gdraw.h"
#include "gdraw_wgl.h"

using namespace C4JRenderCommon;

#define C4J_GL_RING_VB_BYTES (8u * 1024u * 1024u)
#define C4J_GL_FRAME_UPLOAD_BUDGET_BYTES (8u * 1024u * 1024u)
#define C4J_GL_FRAME_UPLOAD_BUDGET_COUNT 256

#ifndef WGL_DRAW_TO_WINDOW_ARB
#define WGL_DRAW_TO_WINDOW_ARB             0x2001
#define WGL_SUPPORT_OPENGL_ARB             0x2010
#define WGL_DOUBLE_BUFFER_ARB              0x2011
#define WGL_PIXEL_TYPE_ARB                 0x2013
#define WGL_COLOR_BITS_ARB                 0x2014
#define WGL_ALPHA_BITS_ARB                 0x201B
#define WGL_DEPTH_BITS_ARB                 0x2022
#define WGL_STENCIL_BITS_ARB               0x2023
#define WGL_TYPE_RGBA_ARB                  0x202B
#define WGL_ACCELERATION_ARB               0x2003
#define WGL_FULL_ACCELERATION_ARB          0x2027
#endif

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB             0x2092
#define WGL_CONTEXT_FLAGS_ARB                     0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB              0x9126
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext, const int *attribList);
typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);

namespace
{
	static HWND  s_hwnd = NULL;
	static HDC   s_hdc = NULL;
	static HGLRC s_hglrc = NULL;
	static int   s_width = 1280;
	static int   s_height = 720;
	static bool  s_fullscreen = false;
	static bool  s_vsync = false;
	static bool  s_suspended = false;
	static char  s_adapterName[128] = "OpenGL Hardware";

	static float            s_clearColour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	static std::atomic<int> s_nextTextureId(100);
	static int              s_nextCBuffId = 1;
	static uint64_t         s_nextCBuffGen = 1;
	static std::atomic<size_t> s_totalCBuffBytes(0);

	/* Extension pointers loaded cleanly per context */
	static PFNWGLCREATECONTEXTATTRIBSARBPROC pfnWglCreateContextAttribsARB = NULL;
	static PFNWGLCHOOSEPIXELFORMATARBPROC    pfnWglChoosePixelFormatARB = NULL;
	static PFNWGLSWAPINTERVALEXTPROC         pfnWglSwapIntervalEXT = NULL;

	static PFNGLGENBUFFERSPROC               pglGenBuffers = NULL;
	static PFNGLDELETEBUFFERSPROC            pglDeleteBuffers = NULL;
	static PFNGLBINDBUFFERPROC               pglBindBuffer = NULL;
	static PFNGLBUFFERDATAPROC               pglBufferData = NULL;
	static PFNGLBUFFERSUBDATAPROC            pglBufferSubData = NULL;
	static PFNGLGENVERTEXARRAYSPROC          pglGenVertexArrays = NULL;
	static PFNGLDELETEVERTEXARRAYSPROC       pglDeleteVertexArrays = NULL;
	static PFNGLBINDVERTEXARRAYPROC          pglBindVertexArray = NULL;
	static PFNGLCREATESHADERPROC             pglCreateShader = NULL;
	static PFNGLDELETESHADERPROC             pglDeleteShader = NULL;
	static PFNGLSHADERSOURCEPROC             pglShaderSource = NULL;
	static PFNGLCOMPILESHADERPROC            pglCompileShader = NULL;
	static PFNGLGETSHADERIVPROC              pglGetShaderiv = NULL;
	static PFNGLGETSHADERINFOLOGPROC         pglGetShaderInfoLog = NULL;
	static PFNGLCREATEPROGRAMPROC            pglCreateProgram = NULL;
	static PFNGLDELETEPROGRAMPROC            pglDeleteProgram = NULL;
	static PFNGLATTACHSHADERPROC             pglAttachShader = NULL;
	static PFNGLBINDATTRIBLOCATIONPROC       pglBindAttribLocation = NULL;
	static PFNGLLINKPROGRAMPROC              pglLinkProgram = NULL;
	static PFNGLGETPROGRAMIVPROC             pglGetProgramiv = NULL;
	static PFNGLGETPROGRAMINFOLOGPROC        pglGetProgramInfoLog = NULL;
	static PFNGLUSEPROGRAMPROC               pglUseProgram = NULL;
	static PFNGLGETUNIFORMLOCATIONPROC       pglGetUniformLocation = NULL;
	static PFNGLUNIFORM1IPROC                pglUniform1i = NULL;
	static PFNGLUNIFORM1FPROC                pglUniform1f = NULL;
	static PFNGLUNIFORM2FVPROC               pglUniform2fv = NULL;
	static PFNGLUNIFORM3FVPROC               pglUniform3fv = NULL;
	static PFNGLUNIFORM4FVPROC               pglUniform4fv = NULL;
	static PFNGLUNIFORMMATRIX4FVPROC         pglUniformMatrix4fv = NULL;
	static PFNGLVERTEXATTRIBPOINTERPROC      pglVertexAttribPointer = NULL;
	static PFNGLVERTEXATTRIBIPOINTERPROC     pglVertexAttribIPointer = NULL;
	static PFNGLENABLEVERTEXATTRIBARRAYPROC  pglEnableVertexAttribArray = NULL;
	static PFNGLDISABLEVERTEXATTRIBARRAYPROC pglDisableVertexAttribArray = NULL;
	static PFNGLACTIVETEXTUREPROC            pglActiveTexture = NULL;
	static PFNGLBLENDFUNCSEPARATEPROC        pglBlendFuncSeparate = NULL;
	static PFNGLBLENDCOLORPROC               pglBlendColor = NULL;
	static PFNGLBINDFRAMEBUFFERPROC          pglBindFramebuffer = NULL;

	struct GLTextureData
	{
		GLuint texId;
		int width;
		int height;
		int mipLevels;
		bool linearFilter;
		bool clampWrap;
		int appliedLinear = -1;
		int appliedClamp = -1;
	};

	struct GLCBuffGPU
	{
		std::vector<GLuint> vbos;
		std::vector<GLuint> vaos;
	};

	struct GLCBuffSlot
	{
		std::shared_ptr<const std::vector<CBuffCommand>> commands;
		size_t byteSize = 0;
		uint64_t generation = 0;
		uint64_t uploadedGen = 0;
		std::shared_ptr<const GLCBuffGPU> gpu;
	};

	static std::unordered_map<int, GLTextureData> s_textures;
	static std::unordered_map<int, GLCBuffSlot>   s_cbuffs;
	static std::vector<GLuint>                    s_pendingDeleteVBOs;
	static std::vector<GLuint>                    s_pendingDeleteVAOs;
	static std::mutex                             s_cbuffMutex;
	static std::recursive_mutex                   s_glMutex;

	thread_local static ThreadRenderState s_tls;

	struct ProgramUniforms
	{
		GLuint prog = 0;
		GLint u_WVP = -1;
		GLint u_MV = -1;
		GLint u_TexMatrix = -1;
		GLint u_TexGen = -1;
		GLint u_LightDir = -1;
		GLint u_LightColor = -1;
		GLint u_LightAmbient = -1;
		GLint u_GlobalLightUV = -1;
		GLint u_LightingEnable = -1;
		GLint u_UseTexGen = -1;
		GLint u_StateColor = -1;
		GLint u_HasTexture = -1;
		GLint u_HasLightmap = -1;
		GLint u_AlphaFunc = -1;
		GLint u_AlphaCutoff = -1;
		GLint u_FogColor = -1;
		GLint u_FogMode = -1;
		GLint u_FogNear = -1;
		GLint u_FogFar = -1;
		GLint u_FogDensity = -1;
		GLint u_UseProjection = -1;
		GLint u_Texture = -1;
		GLint u_Lightmap = -1;
		GLint u_SunDir = -1;
		GLint u_CameraPos = -1;
		GLint u_SkyColor = -1;
		GLint u_ShaderFogColor = -1;
		GLint u_WorldMeta = -1;
		GLint u_WaterTint = -1;
		GLint u_TorchTint = -1;
		VSConstantBufferData lastVS = {};
		PSConstantBufferData lastPS = {};
		C4JShaderFrameParams lastShader = {};
		bool lastShaderValid = false;
		bool lastValid = false;
	};

	static ProgramUniforms s_progVanillaStd;
	static ProgramUniforms s_progVanillaComp;
	static ProgramUniforms s_progOpaqueStd;
	static ProgramUniforms s_progOpaqueComp;
	static ProgramUniforms s_progStd;
	static ProgramUniforms s_progComp;
	static ProgramUniforms s_customProgStd;
	static ProgramUniforms s_customProgComp;
	static C4JShaderFrameParams s_shaderFrameParams = {};
	static int                  s_shaderMode = C4J_SHADER_MODE_OFF;
	static GLuint s_worldVAO = 0;
	static GLuint s_ringVBO = 0;
	static GLsizeiptr s_ringVBOOffset = 0;
	static GLuint s_quadIBO = 0;
	static GLuint s_fanIBO = 0;
	static int    s_currentViewportType = 0;
	static size_t s_frameUploadedBytes = 0;
	static int    s_frameUploadedCount = 0;

	/* Cached GL state shadow */
	static bool   s_cacheValid = false;
	static GLuint s_cachedProg = 0;
	static GLuint s_cachedVAO = 0;
	static GLuint s_cachedVBO = 0;
	static int    s_cachedVType = -1;
	static GLuint s_cachedIBO = 0;
	static GLuint s_cachedTex0 = 0xFFFFFFFFu;
	static GLuint s_cachedTex1 = 0xFFFFFFFFu;
	static bool   s_cachedBlend = false;
	static int    s_cachedBlendSrc = -1;
	static int    s_cachedBlendDst = -1;
	static unsigned int s_cachedBlendFactor = 0xDEADBEEFu;
	static bool   s_cachedDepthTest = false;
	static bool   s_cachedDepthMask = true;
	static int    s_cachedDepthFunc = -1;
	static bool   s_cachedCull = false;
	static bool   s_cachedCullCW = true;
	static uint8_t s_cachedColorMask = 0xFF;
	static float  s_cachedSlopeBias = 999.0f;
	static float  s_cachedConstBias = 999.0f;
	static bool   s_cachedStencil = false;
	static int    s_cachedStencilFunc = -1;
	static uint8_t s_cachedStencilRef = 0;
	static uint8_t s_cachedStencilRead = 0xFF;
	static uint8_t s_cachedStencilWrite = 0xFF;
	static bool   s_cachedScissor = true;

	static void InvalidateStateCache()
	{
		s_cacheValid = false;
		s_progVanillaStd.lastValid = false;
		s_progVanillaComp.lastValid = false;
		s_progOpaqueStd.lastValid = false;
		s_progOpaqueComp.lastValid = false;
		s_progStd.lastValid = false;
		s_progComp.lastValid = false;
		s_customProgStd.lastValid = false;
		s_customProgComp.lastValid = false;
		s_cachedProg = 0;
		s_cachedVAO = 0;
		s_cachedVBO = 0;
		s_cachedVType = -1;
		s_cachedIBO = 0;
		s_cachedTex0 = 0xFFFFFFFFu;
		s_cachedTex1 = 0xFFFFFFFFu;
		s_cachedBlendSrc = -1;
		s_cachedBlendDst = -1;
		s_cachedBlendFactor = 0xDEADBEEFu;
		s_cachedDepthFunc = -1;
		s_cachedColorMask = 0xFF;
		s_cachedSlopeBias = 999.0f;
		s_cachedConstBias = 999.0f;
		s_cachedStencilFunc = -1;
		s_cachedScissor = true;
	}

	static void *GetAnyGLProcAddress(const char *name)
	{
		void *p = (void*)wglGetProcAddress(name);
		if (p == NULL || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1)
		{
			static HMODULE s_ogl = GetModuleHandleA("opengl32.dll");
			if (s_ogl) p = (void*)GetProcAddress(s_ogl, name);
		}
		return p;
	}

	static void LoadGLFunctions()
	{
		pglGenBuffers               = (PFNGLGENBUFFERSPROC)GetAnyGLProcAddress("glGenBuffers");
		pglDeleteBuffers            = (PFNGLDELETEBUFFERSPROC)GetAnyGLProcAddress("glDeleteBuffers");
		pglBindBuffer               = (PFNGLBINDBUFFERPROC)GetAnyGLProcAddress("glBindBuffer");
		pglBufferData               = (PFNGLBUFFERDATAPROC)GetAnyGLProcAddress("glBufferData");
		pglBufferSubData            = (PFNGLBUFFERSUBDATAPROC)GetAnyGLProcAddress("glBufferSubData");
		pglGenVertexArrays          = (PFNGLGENVERTEXARRAYSPROC)GetAnyGLProcAddress("glGenVertexArrays");
		pglDeleteVertexArrays       = (PFNGLDELETEVERTEXARRAYSPROC)GetAnyGLProcAddress("glDeleteVertexArrays");
		pglBindVertexArray          = (PFNGLBINDVERTEXARRAYPROC)GetAnyGLProcAddress("glBindVertexArray");
		pglCreateShader             = (PFNGLCREATESHADERPROC)GetAnyGLProcAddress("glCreateShader");
		pglDeleteShader             = (PFNGLDELETESHADERPROC)GetAnyGLProcAddress("glDeleteShader");
		pglShaderSource             = (PFNGLSHADERSOURCEPROC)GetAnyGLProcAddress("glShaderSource");
		pglCompileShader            = (PFNGLCOMPILESHADERPROC)GetAnyGLProcAddress("glCompileShader");
		pglGetShaderiv              = (PFNGLGETSHADERIVPROC)GetAnyGLProcAddress("glGetShaderiv");
		pglGetShaderInfoLog         = (PFNGLGETSHADERINFOLOGPROC)GetAnyGLProcAddress("glGetShaderInfoLog");
		pglCreateProgram            = (PFNGLCREATEPROGRAMPROC)GetAnyGLProcAddress("glCreateProgram");
		pglDeleteProgram            = (PFNGLDELETEPROGRAMPROC)GetAnyGLProcAddress("glDeleteProgram");
		pglAttachShader             = (PFNGLATTACHSHADERPROC)GetAnyGLProcAddress("glAttachShader");
		pglBindAttribLocation       = (PFNGLBINDATTRIBLOCATIONPROC)GetAnyGLProcAddress("glBindAttribLocation");
		pglLinkProgram              = (PFNGLLINKPROGRAMPROC)GetAnyGLProcAddress("glLinkProgram");
		pglGetProgramiv             = (PFNGLGETPROGRAMIVPROC)GetAnyGLProcAddress("glGetProgramiv");
		pglGetProgramInfoLog        = (PFNGLGETPROGRAMINFOLOGPROC)GetAnyGLProcAddress("glGetProgramInfoLog");
		pglUseProgram               = (PFNGLUSEPROGRAMPROC)GetAnyGLProcAddress("glUseProgram");
		pglGetUniformLocation       = (PFNGLGETUNIFORMLOCATIONPROC)GetAnyGLProcAddress("glGetUniformLocation");
		pglUniform1i                = (PFNGLUNIFORM1IPROC)GetAnyGLProcAddress("glUniform1i");
		pglUniform1f                = (PFNGLUNIFORM1FPROC)GetAnyGLProcAddress("glUniform1f");
		pglUniform2fv               = (PFNGLUNIFORM2FVPROC)GetAnyGLProcAddress("glUniform2fv");
		pglUniform3fv               = (PFNGLUNIFORM3FVPROC)GetAnyGLProcAddress("glUniform3fv");
		pglUniform4fv               = (PFNGLUNIFORM4FVPROC)GetAnyGLProcAddress("glUniform4fv");
		pglUniformMatrix4fv         = (PFNGLUNIFORMMATRIX4FVPROC)GetAnyGLProcAddress("glUniformMatrix4fv");
		pglVertexAttribPointer      = (PFNGLVERTEXATTRIBPOINTERPROC)GetAnyGLProcAddress("glVertexAttribPointer");
		pglVertexAttribIPointer     = (PFNGLVERTEXATTRIBIPOINTERPROC)GetAnyGLProcAddress("glVertexAttribIPointer");
		pglEnableVertexAttribArray  = (PFNGLENABLEVERTEXATTRIBARRAYPROC)GetAnyGLProcAddress("glEnableVertexAttribArray");
		pglDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)GetAnyGLProcAddress("glDisableVertexAttribArray");
		pglActiveTexture            = (PFNGLACTIVETEXTUREPROC)GetAnyGLProcAddress("glActiveTexture");
		pglBlendFuncSeparate        = (PFNGLBLENDFUNCSEPARATEPROC)GetAnyGLProcAddress("glBlendFuncSeparate");
		pglBlendColor               = (PFNGLBLENDCOLORPROC)GetAnyGLProcAddress("glBlendColor");
		pglBindFramebuffer          = (PFNGLBINDFRAMEBUFFERPROC)GetAnyGLProcAddress("glBindFramebuffer");
		pfnWglSwapIntervalEXT       = (PFNWGLSWAPINTERVALEXTPROC)GetAnyGLProcAddress("wglSwapIntervalEXT");
	}

	static const char *s_glslVSStd =
		"#version 130\n"
		"uniform mat4 u_WVP;\n"
		"uniform mat4 u_MV;\n"
		"uniform mat4 u_TexMatrix;\n"
		"uniform vec4 u_TexGen[4];\n"
		"uniform vec4 u_LightDir[2];\n"
		"uniform vec4 u_LightColor[2];\n"
		"uniform vec4 u_LightAmbient;\n"
		"uniform vec2 u_GlobalLightUV;\n"
		"uniform int  u_LightingEnable;\n"
		"uniform int  u_UseTexGen;\n"
		"in vec3 a_Pos;\n"
		"in vec2 a_UV;\n"
		"in uint a_Col;\n"
		"in uint a_Norm;\n"
		"in uint a_Extra;\n"
		"out vec4 v_UV;\n"
		"out vec2 v_LightUV;\n"
		"out float v_FogDist;\n"
		"out vec3 v_ViewPos;\n"
		"out vec3 v_WorldPos;\n"
		"out vec4 v_Col;\n"
		"vec3 UnpackSignedNormal(uint n) {\n"
		"    int nx = int(n & 0xFFu); if (nx >= 128) nx -= 256;\n"
		"    int ny = int((n >> 8u) & 0xFFu); if (ny >= 128) ny -= 256;\n"
		"    int nz = int((n >> 16u) & 0xFFu); if (nz >= 128) nz -= 256;\n"
		"    return vec3(float(nx), float(ny), float(nz)) / 127.0;\n"
		"}\n"
		"void main() {\n"
		"    vec4 pos4 = vec4(a_Pos, 1.0);\n"
		"    gl_Position = u_WVP * pos4;\n"
		"    vec4 viewPos = u_MV * pos4;\n"
		"    v_ViewPos = viewPos.xyz;\n"
		"    v_WorldPos = pos4.xyz;\n"
		"    v_FogDist = length(viewPos.xyz);\n"
		"    vec4 baseCol = vec4(\n"
		"        float((a_Col >> 24u) & 0xFFu) / 255.0,\n"
		"        float((a_Col >> 16u) & 0xFFu) / 255.0,\n"
		"        float((a_Col >> 8u)  & 0xFFu) / 255.0,\n"
		"        float(a_Col & 0xFFu) / 255.0);\n"
		"    if (u_LightingEnable != 0) {\n"
		"        vec3 norm3 = UnpackSignedNormal(a_Norm);\n"
		"        vec3 viewN = normalize((u_MV * vec4(norm3, 0.0)).xyz);\n"
		"        vec3 accum = u_LightAmbient.rgb;\n"
		"        for (int i = 0; i < 2; ++i) {\n"
		"            if (u_LightColor[i].a > 0.5) {\n"
		"                float ndotl = max(dot(viewN, normalize(u_LightDir[i].xyz)), 0.0);\n"
		"                accum += u_LightColor[i].rgb * ndotl;\n"
		"            }\n"
		"        }\n"
		"        baseCol.rgb *= min(accum, vec3(1.0));\n"
		"    }\n"
		"    v_Col = baseCol;\n"
		"    if (u_UseTexGen != 0) {\n"
		"        vec4 g = vec4(dot(pos4, u_TexGen[0]), dot(pos4, u_TexGen[1]), dot(pos4, u_TexGen[2]), dot(viewPos, u_TexGen[3]));\n"
		"        v_UV = u_TexMatrix * g;\n"
		"    } else {\n"
		"        vec2 uv = a_UV;\n"
		"        if (uv.x > 1.0 && uv.x <= 2.0) uv.x -= 1.0;\n"
		"        vec4 tuv = u_TexMatrix * vec4(uv, 0.0, 1.0);\n"
		"        v_UV = vec4(tuv.xy, 0.0, 1.0);\n"
		"    }\n"
		"    if (a_Extra == 0xFE00FE00u) {\n"
		"        v_LightUV = u_GlobalLightUV + vec2(0.5 / 16.0);\n"
		"    } else {\n"
		"        int lx = int(a_Extra & 0xFFFFu); if (lx >= 32768) lx -= 65536;\n"
		"        int ly = int((a_Extra >> 16u) & 0xFFFFu); if (ly >= 32768) ly -= 65536;\n"
		"        v_LightUV = (vec2(float(lx), float(ly)) + vec2(8.0)) / 256.0;\n"
		"    }\n"
		"}\n";

	static const char *s_glslVSComp =
		"#version 130\n"
		"uniform mat4 u_WVP;\n"
		"uniform mat4 u_MV;\n"
		"uniform mat4 u_TexMatrix;\n"
		"in ivec4 a_CompA;\n"
		"in ivec4 a_CompB;\n"
		"out vec4 v_UV;\n"
		"out vec2 v_LightUV;\n"
		"out float v_FogDist;\n"
		"out vec3 v_ViewPos;\n"
		"out vec3 v_WorldPos;\n"
		"out vec4 v_Col;\n"
		"void main() {\n"
		"    vec4 pos4 = vec4(vec3(a_CompA.xyz) / 1024.0, 1.0);\n"
		"    gl_Position = u_WVP * pos4;\n"
		"    vec4 viewPos = u_MV * pos4;\n"
		"    v_ViewPos = viewPos.xyz;\n"
		"    v_WorldPos = pos4.xyz;\n"
		"    v_FogDist = length(viewPos.xyz);\n"
		"    vec2 uv = vec2(a_CompB.xy) / 8192.0;\n"
		"    if (uv.x > 1.0 && uv.x <= 2.0) uv.x -= 1.0;\n"
		"    vec4 tuv = u_TexMatrix * vec4(uv, 0.0, 1.0);\n"
		"    v_UV = vec4(tuv.xy, 0.0, 1.0);\n"
		"    v_LightUV = (vec2(a_CompB.zw) + vec2(8.0)) / 256.0;\n"
		"    uint col16 = (uint(a_CompA.w) & 0xFFFFu) ^ 0x8000u;\n"
		"    float r = float((col16 >> 11u) & 31u) / 31.0;\n"
		"    float g = float((col16 >> 5u) & 63u) / 63.0;\n"
		"    float b = float(col16 & 31u) / 31.0;\n"
		"    v_Col = vec4(r, g, b, 1.0);\n"
		"}\n";

	static const char *s_glslFSVanilla =
		"#version 150\n"
		"uniform sampler2D u_Texture;\n"
		"uniform sampler2D u_Lightmap;\n"
		"uniform vec4 u_StateColor;\n"
		"uniform int  u_HasTexture;\n"
		"uniform int  u_HasLightmap;\n"
		"uniform int  u_AlphaFunc;\n"
		"uniform float u_AlphaCutoff;\n"
		"uniform vec4 u_FogColor;\n"
		"uniform int  u_FogMode;\n"
		"uniform float u_FogNear;\n"
		"uniform float u_FogFar;\n"
		"uniform float u_FogDensity;\n"
		"uniform int  u_UseProjection;\n"
		"in vec4 v_UV;\n"
		"in vec2 v_LightUV;\n"
		"in float v_FogDist;\n"
		"in vec3 v_ViewPos;\n"
		"in vec3 v_WorldPos;\n"
		"in vec4 v_Col;\n"
		"out vec4 o_Color;\n"
		"bool PassAlphaTest(float a) {\n"
		"    if (u_AlphaFunc == 5) return a > u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 7) return a >= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 2) return a < u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 4) return a <= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 3) return abs(a - u_AlphaCutoff) < 0.001;\n"
		"    if (u_AlphaFunc == 6) return abs(a - u_AlphaCutoff) >= 0.001;\n"
		"    if (u_AlphaFunc == 1) return false;\n"
		"    return true;\n"
		"}\n"
		"void main() {\n"
		"    vec4 finalCol = v_Col * u_StateColor;\n"
		"    if (u_HasTexture != 0) {\n"
		"        vec2 texUV = (u_UseProjection != 0 && abs(v_UV.w) > 1e-5) ? (v_UV.xy / v_UV.w) : v_UV.xy;\n"
		"        finalCol *= texture(u_Texture, texUV);\n"
		"    }\n"
		"    if (u_HasLightmap != 0) {\n"
		"        finalCol.rgb *= texture(u_Lightmap, v_LightUV).rgb;\n"
		"    }\n"
		"    if (u_AlphaFunc != 8 && !PassAlphaTest(finalCol.a)) discard;\n"
		"    if (u_FogColor.w > 0.5) {\n"
		"        float fogFactor = (u_FogMode == 2)\n"
		"            ? exp(-u_FogDensity * v_FogDist)\n"
		"            : (u_FogFar - v_FogDist) / max(u_FogFar - u_FogNear, 0.0001);\n"
		"        finalCol.rgb = mix(u_FogColor.rgb, finalCol.rgb, clamp(fogFactor, 0.0, 1.0));\n"
		"    }\n"
		"    o_Color = finalCol;\n"
		"}\n";

	static const char *s_glslFS =
		"#version 130\n"
		"uniform sampler2D u_Texture;\n"
		"uniform sampler2D u_Lightmap;\n"
		"uniform vec4 u_StateColor;\n"
		"uniform int  u_HasTexture;\n"
		"uniform int  u_HasLightmap;\n"
		"uniform int  u_AlphaFunc;\n"
		"uniform float u_AlphaCutoff;\n"
		"uniform vec4 u_FogColor;\n"
		"uniform int  u_FogMode;\n"
		"uniform float u_FogNear;\n"
		"uniform float u_FogFar;\n"
		"uniform float u_FogDensity;\n"
		"uniform int  u_UseProjection;\n"
		"uniform vec4 u_SunDir;\n"
		"uniform vec4 u_CameraPos;\n"
		"uniform vec4 u_SkyColor;\n"
		"uniform vec4 u_ShaderFogColor;\n"
		"uniform vec4 u_WorldMeta;\n"
		"uniform vec4 u_WaterTint;\n"
		"uniform vec4 u_TorchTint;\n"
		"in vec4 v_UV;\n"
		"in vec2 v_LightUV;\n"
		"in float v_FogDist;\n"
		"in vec3 v_ViewPos;\n"
		"in vec3 v_WorldPos;\n"
		"in vec4 v_Col;\n"
		"out vec4 o_Color;\n"
		"bool PassAlphaTest(float a) {\n"
		"    if (u_AlphaFunc == 5) return a > u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 7) return a >= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 2) return a < u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 4) return a <= u_AlphaCutoff;\n"
		"    if (u_AlphaFunc == 3) return abs(a - u_AlphaCutoff) < 0.001;\n"
		"    if (u_AlphaFunc == 6) return abs(a - u_AlphaCutoff) >= 0.001;\n"
		"    if (u_AlphaFunc == 1) return false;\n"
		"    return true;\n"
		"}\n"
		"vec3 ToneMapFast(vec3 c) {\n"
		"    float lum = dot(c, vec3(0.299, 0.587, 0.114));\n"
		"    return clamp(c * (1.07 / (1.0 + 0.16 * lum)), 0.0, 1.0);\n"
		"}\n"
		"void main() {\n"
		"    vec4 finalCol = v_Col * u_StateColor;\n"
		"    if (u_HasTexture != 0) {\n"
		"        vec2 texUV = (u_UseProjection != 0 && abs(v_UV.w) > 1e-5) ? (v_UV.xy / v_UV.w) : v_UV.xy;\n"
		"        finalCol *= texture(u_Texture, texUV);\n"
		"    }\n"
		"    if (u_HasLightmap != 0) {\n"
		"        finalCol.rgb *= texture(u_Lightmap, v_LightUV).rgb;\n"
		"    }\n"
		"    if (u_AlphaFunc != 8 && !PassAlphaTest(finalCol.a)) {\n"
		"        discard;\n"
		"    }\n"
		"    if (u_HasLightmap != 0) {\n"
		"        float lmBlock = clamp((v_LightUV.x - 0.03125) * 1.07, 0.0, 1.0);\n"
		"        float lmSky   = clamp((v_LightUV.y - 0.03125) * 1.07, 0.0, 1.0);\n"
		"        float dayFac  = clamp(u_SunDir.w, 0.0, 1.0);\n"
		"        float rainFac = clamp(u_SkyColor.w, 0.0, 1.0);\n"
		"        vec3 surfN = cross(dFdx(v_WorldPos), dFdy(v_WorldPos));\n"
		"        surfN *= inversesqrt(max(dot(surfN, surfN), 1e-10));\n"
		"        vec3 lightDirW = u_SunDir.xyz;\n"
		"        float ndl = abs(dot(surfN, lightDirW)) * 0.65 + 0.35;\n"
		"        float sunBounce = (0.88 + 0.22 * ndl) * (1.0 - 0.40 * rainFac);\n"
		"        vec3 sunTint = mix(vec3(0.76, 0.84, 1.04), vec3(1.06, 1.01, 0.94), dayFac);\n"
		"        vec3 skyAmb = mix(vec3(1.0), sunTint * sunBounce, lmSky * lmSky);\n"
		"        finalCol.rgb *= skyAmb;\n"
		"        float torchPow = (lmBlock * lmBlock) * u_TorchTint.w;\n"
		"        finalCol.rgb += finalCol.rgb * u_TorchTint.rgb * torchPow;\n"
		"#ifndef C4J_OPAQUE_ONLY\n"
		"        if (finalCol.a > 0.15 && finalCol.a < 0.92 && u_HasTexture != 0) {\n"
		"            vec3 viewN = cross(dFdx(v_ViewPos), dFdy(v_ViewPos));\n"
		"            viewN *= inversesqrt(max(dot(viewN, viewN), 1e-10));\n"
		"            if (viewN.z < 0.0) viewN = -viewN;\n"
		"            vec3 viewDir = normalize(-v_ViewPos);\n"
		"            float om = 1.0 - clamp(dot(viewN, viewDir), 0.0, 1.0);\n"
		"            float fresnel = om * om * om;\n"
		"            float timeSec = u_CameraPos.w;\n"
		"            float wave = 0.5 + 0.5 * sin(timeSec * 2.4 + v_WorldPos.x * 1.9 + v_WorldPos.z * 1.5);\n"
		"            vec3 reflCol = mix(u_WaterTint.rgb, u_SkyColor.rgb, 0.65);\n"
		"            finalCol.rgb = mix(finalCol.rgb, reflCol, fresnel * 0.48 * lmSky);\n"
		"            float s = clamp(dot(reflect(-vec3(0.2, 0.85, 0.48), viewN), viewDir), 0.0, 1.0);\n"
		"            float s2 = s * s; float s4 = s2 * s2; float s8 = s4 * s4; float s16 = s8 * s8;\n"
		"            finalCol.rgb += sunTint * (s16 * (0.4 + 0.6 * wave) * 0.45 * lmSky * (1.0 - rainFac));\n"
		"        }\n"
		"#endif\n"
		"        finalCol.rgb = ToneMapFast(finalCol.rgb);\n"
		"    }\n"
		"    if (u_FogColor.a > 0.5) {\n"
		"        float fogFactor = 1.0;\n"
		"        if (u_FogMode == 2) {\n"
		"            fogFactor = exp(-u_FogDensity * v_FogDist);\n"
		"        } else {\n"
		"            float denom = max(u_FogFar - u_FogNear, 0.0001);\n"
		"            fogFactor = (u_FogFar - v_FogDist) / denom;\n"
		"        }\n"
		"        fogFactor = clamp(fogFactor, 0.0, 1.0);\n"
		"        finalCol.rgb = mix(u_FogColor.rgb, finalCol.rgb, fogFactor);\n"
		"    }\n"
		"    o_Color = finalCol;\n"
		"}\n";

	static GLuint CompileShader(GLenum type, const char *src)
	{
		GLuint sh = pglCreateShader(type);
		pglShaderSource(sh, 1, &src, NULL);
		pglCompileShader(sh);
		GLint ok = 0;
		pglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char logBuf[1024] = {};
			pglGetShaderInfoLog(sh, sizeof(logBuf), NULL, logBuf);
			printf("[RenderGL] Shader compile error: %s\n", logBuf);
			pglDeleteShader(sh);
			return 0;
		}
		return sh;
	}

	static bool BuildProgram(const char *vsSrc, const char *fsSrc, bool compressed, ProgramUniforms &out)
	{
		GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
		GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
		if (!vs || !fs)
		{
			if (vs) pglDeleteShader(vs);
			if (fs) pglDeleteShader(fs);
			return false;
		}
		GLuint prog = pglCreateProgram();
		pglAttachShader(prog, vs);
		pglAttachShader(prog, fs);
		if (!compressed)
		{
			pglBindAttribLocation(prog, 0, "a_Pos");
			pglBindAttribLocation(prog, 1, "a_UV");
			pglBindAttribLocation(prog, 2, "a_Col");
			pglBindAttribLocation(prog, 3, "a_Norm");
			pglBindAttribLocation(prog, 4, "a_Extra");
		}
		else
		{
			pglBindAttribLocation(prog, 0, "a_CompA");
			pglBindAttribLocation(prog, 1, "a_CompB");
		}
		pglLinkProgram(prog);
		pglDeleteShader(vs);
		pglDeleteShader(fs);

		GLint ok = 0;
		pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char logBuf[1024] = {};
			pglGetProgramInfoLog(prog, sizeof(logBuf), NULL, logBuf);
			printf("[RenderGL] Program link error: %s\n", logBuf);
			pglDeleteProgram(prog);
			return false;
		}

		out.prog = prog;
		out.u_WVP            = pglGetUniformLocation(prog, "u_WVP");
		out.u_MV             = pglGetUniformLocation(prog, "u_MV");
		out.u_TexMatrix      = pglGetUniformLocation(prog, "u_TexMatrix");
		out.u_TexGen         = pglGetUniformLocation(prog, "u_TexGen");
		out.u_LightDir       = pglGetUniformLocation(prog, "u_LightDir");
		out.u_LightColor     = pglGetUniformLocation(prog, "u_LightColor");
		out.u_LightAmbient   = pglGetUniformLocation(prog, "u_LightAmbient");
		out.u_GlobalLightUV  = pglGetUniformLocation(prog, "u_GlobalLightUV");
		out.u_LightingEnable = pglGetUniformLocation(prog, "u_LightingEnable");
		out.u_UseTexGen      = pglGetUniformLocation(prog, "u_UseTexGen");
		out.u_StateColor     = pglGetUniformLocation(prog, "u_StateColor");
		out.u_HasTexture     = pglGetUniformLocation(prog, "u_HasTexture");
		out.u_HasLightmap    = pglGetUniformLocation(prog, "u_HasLightmap");
		out.u_AlphaFunc      = pglGetUniformLocation(prog, "u_AlphaFunc");
		out.u_AlphaCutoff    = pglGetUniformLocation(prog, "u_AlphaCutoff");
		out.u_FogColor       = pglGetUniformLocation(prog, "u_FogColor");
		out.u_FogMode        = pglGetUniformLocation(prog, "u_FogMode");
		out.u_FogNear        = pglGetUniformLocation(prog, "u_FogNear");
		out.u_FogFar         = pglGetUniformLocation(prog, "u_FogFar");
		out.u_FogDensity     = pglGetUniformLocation(prog, "u_FogDensity");
		out.u_UseProjection  = pglGetUniformLocation(prog, "u_UseProjection");
		out.u_Texture        = pglGetUniformLocation(prog, "u_Texture");
		out.u_Lightmap       = pglGetUniformLocation(prog, "u_Lightmap");
		out.u_SunDir         = pglGetUniformLocation(prog, "u_SunDir");
		out.u_CameraPos      = pglGetUniformLocation(prog, "u_CameraPos");
		out.u_SkyColor       = pglGetUniformLocation(prog, "u_SkyColor");
		out.u_ShaderFogColor = pglGetUniformLocation(prog, "u_ShaderFogColor");
		out.u_WorldMeta      = pglGetUniformLocation(prog, "u_WorldMeta");
		out.u_WaterTint      = pglGetUniformLocation(prog, "u_WaterTint");
		out.u_TorchTint      = pglGetUniformLocation(prog, "u_TorchTint");
		out.lastValid        = false;
		out.lastShaderValid  = false;

		pglUseProgram(prog);
		if (out.u_Texture >= 0) pglUniform1i(out.u_Texture, 0);
		if (out.u_Lightmap >= 0) pglUniform1i(out.u_Lightmap, 1);
		pglUseProgram(0);
		return true;
	}

	static GLenum ToGLBlend(int b)
	{
		switch (b)
		{
		case C4J_BLEND_ZERO:             return GL_ZERO;
		case C4J_BLEND_ONE:              return GL_ONE;
		case C4J_BLEND_SRC_COLOR:        return GL_SRC_COLOR;
		case C4J_BLEND_INV_SRC_COLOR:    return GL_ONE_MINUS_SRC_COLOR;
		case C4J_BLEND_SRC_ALPHA:        return GL_SRC_ALPHA;
		case C4J_BLEND_INV_SRC_ALPHA:    return GL_ONE_MINUS_SRC_ALPHA;
		case C4J_BLEND_DEST_ALPHA:       return GL_DST_ALPHA;
		case C4J_BLEND_INV_DEST_ALPHA:   return GL_ONE_MINUS_DST_ALPHA;
		case C4J_BLEND_DEST_COLOR:       return GL_DST_COLOR;
		case C4J_BLEND_INV_DEST_COLOR:   return GL_ONE_MINUS_DST_COLOR;
		case C4J_BLEND_SRC_ALPHA_SAT:    return GL_SRC_ALPHA_SATURATE;
		case C4J_BLEND_FACTOR_ALPHA:     return GL_CONSTANT_ALPHA;
		case C4J_BLEND_INV_FACTOR_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;
		default:                         return GL_SRC_ALPHA;
		}
	}

	static GLenum ToGLCompare(int c)
	{
		switch (c)
		{
		case C4J_CMP_NEVER:         return GL_NEVER;
		case C4J_CMP_LESS:          return GL_LESS;
		case C4J_CMP_EQUAL:         return GL_EQUAL;
		case C4J_CMP_LESS_EQUAL:    return GL_LEQUAL;
		case C4J_CMP_GREATER:       return GL_GREATER;
		case C4J_CMP_NOT_EQUAL:     return GL_NOTEQUAL;
		case C4J_CMP_GREATER_EQUAL: return GL_GEQUAL;
		case C4J_CMP_ALWAYS:        return GL_ALWAYS;
		default:                    return GL_LEQUAL;
		}
	}

	static void DrainDeferredDeletes()
	{
		std::vector<GLuint> toFreeVBOs;
		std::vector<GLuint> toFreeVAOs;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			toFreeVBOs.swap(s_pendingDeleteVBOs);
			toFreeVAOs.swap(s_pendingDeleteVAOs);
		}
		if (!toFreeVAOs.empty() && pglDeleteVertexArrays)
		{
			pglDeleteVertexArrays((GLsizei)toFreeVAOs.size(), toFreeVAOs.data());
		}
		if (!toFreeVBOs.empty() && pglDeleteBuffers)
		{
			pglDeleteBuffers((GLsizei)toFreeVBOs.size(), toFreeVBOs.data());
		}
	}

	static void ApplyViewport(int viewportType)
	{
		float vx, vy, vw, vh;
		ComputeViewportRect(viewportType, s_width, s_height, vx, vy, vw, vh);
		int glX = (int)vx;
		int glY = s_height - (int)(vy + vh);
		int glW = (int)vw;
		int glH = (int)vh;
		if (glW < 1) glW = 1;
		if (glH < 1) glH = 1;
		glViewport(glX, glY, glW, glH);
	}

	static void SetupVAOAttribs(int vType, const uint8_t *basePtr, bool updateEnables)
	{
		if (vType == 1)
		{
			if (updateEnables)
			{
				pglEnableVertexAttribArray(0);
				pglEnableVertexAttribArray(1);
				pglDisableVertexAttribArray(2);
				pglDisableVertexAttribArray(3);
				pglDisableVertexAttribArray(4);
			}
			pglVertexAttribIPointer(0, 4, GL_SHORT, 16, basePtr + 0);
			pglVertexAttribIPointer(1, 4, GL_SHORT, 16, basePtr + 8);
		}
		else
		{
			if (updateEnables)
			{
				pglEnableVertexAttribArray(0);
				pglEnableVertexAttribArray(1);
				pglEnableVertexAttribArray(2);
				pglEnableVertexAttribArray(3);
				pglEnableVertexAttribArray(4);
			}
			pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, basePtr + 0);
			pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, basePtr + 12);
			pglVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, 32, basePtr + 20);
			pglVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, 32, basePtr + 24);
			pglVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, 32, basePtr + 28);
		}
	}

	static void BindVertexAttribs(int vType, GLuint vbo)
	{
		if (s_cacheValid && s_cachedVBO == vbo && s_cachedVType == vType) return;
		pglBindBuffer(GL_ARRAY_BUFFER, vbo);
		SetupVAOAttribs(vType, (const uint8_t*)0, !s_cacheValid || s_cachedVType != vType);
		s_cachedVBO = vbo;
		s_cachedVType = vType;
	}

	static void ExecuteDrawInternal(
		int primType, int count,
		GLuint staticVAO, GLuint staticVBO,
		const void *dynamicBytes, size_t dynamicByteCount,
		int vType, int psType,
		const Matrix4x4 &wvp, const Matrix4x4 &mv,
		int texId, const float stateColor[4],
		bool blendEnable, int blendSrc, int blendDst,
		bool depthTestEnable, bool depthMask,
		bool cullEnable, bool cullCW,
		bool alphaTestEnable, int alphaFunc, float alphaCutoff)
	{
		if (!s_hglrc || count <= 0) return;

		if (staticVAO != 0)
		{
			if (!s_cacheValid || s_cachedVAO != staticVAO)
			{
				pglBindVertexArray(staticVAO);
				s_cachedVAO = staticVAO;
			}
		}
		else
		{
			if (!s_cacheValid || s_cachedVAO != s_worldVAO)
			{
				pglBindVertexArray(s_worldVAO);
				s_cachedVAO = s_worldVAO;
			}
			if (staticVBO == 0)
			{
				if (!dynamicBytes || dynamicByteCount == 0 || dynamicByteCount > C4J_GL_RING_VB_BYTES) return;
				if (!s_cacheValid || s_cachedVBO != s_ringVBO)
				{
					pglBindBuffer(GL_ARRAY_BUFFER, s_ringVBO);
					s_cachedVBO = s_ringVBO;
				}
				GLsizeiptr aligned = (s_ringVBOOffset + 15) & ~((GLsizeiptr)15);
				if (aligned + (GLsizeiptr)dynamicByteCount > (GLsizeiptr)C4J_GL_RING_VB_BYTES)
				{
					pglBufferData(GL_ARRAY_BUFFER, C4J_GL_RING_VB_BYTES, NULL, GL_STREAM_DRAW);
					aligned = 0;
				}
				pglBufferSubData(GL_ARRAY_BUFFER, aligned, (GLsizeiptr)dynamicByteCount, dynamicBytes);
				s_ringVBOOffset = aligned + (GLsizeiptr)dynamicByteCount;
				SetupVAOAttribs(vType, (const uint8_t*)(uintptr_t)aligned, !s_cacheValid || s_cachedVType != vType);
				s_cachedVType = vType;
			}
			else
			{
				BindVertexAttribs(vType, staticVBO);
			}
		}

		GLuint tex0 = 0;
		bool texLinear = false;
		bool texClamp = false;
		GLTextureData *td0 = NULL;
		if (texId > 0)
		{
			auto it = s_textures.find(texId);
			if (it != s_textures.end())
			{
				td0 = &it->second;
				tex0 = td0->texId;
				texLinear = td0->linearFilter;
				texClamp = td0->clampWrap;
			}
		}

		GLuint tex1 = 0;
		if (s_tls.boundVertexTextureId > 0)
		{
			auto itL = s_textures.find(s_tls.boundVertexTextureId);
			if (itL != s_textures.end()) tex1 = itL->second.texId;
		}

		const bool isCompressed = (vType == 1);
		const bool inWorldPass = (s_shaderFrameParams.worldMeta[3] > 0.5f);
		const bool useVanilla = (!inWorldPass || s_shaderMode == C4J_SHADER_MODE_OFF || tex1 == 0);
		const bool useCustom = (!useVanilla && s_shaderMode == C4J_SHADER_MODE_CUSTOM) &&
			s_customProgStd.prog != 0 && s_customProgComp.prog != 0;
		ProgramUniforms &pu = useVanilla
			? (isCompressed ? s_progVanillaComp : s_progVanillaStd)
			: (useCustom
				? (isCompressed ? s_customProgComp : s_customProgStd)
				: (blendEnable
					? (isCompressed ? s_progComp : s_progStd)
					: (isCompressed ? s_progOpaqueComp : s_progOpaqueStd)));

		VSConstantBufferData vsConsts = {};
		FillVSConstants(s_tls, wvp, mv, vType, psType, vsConsts);

		PSConstantBufferData psConsts = {};
		FillPSConstants(s_tls, stateColor, tex0 != 0, tex1 != 0,
			alphaTestEnable, alphaFunc, alphaCutoff, psType, psConsts);

		if (!s_cacheValid || s_cachedProg != pu.prog)
		{
			pglUseProgram(pu.prog);
			s_cachedProg = pu.prog;
		}

		if (!pu.lastValid)
		{
			if (pu.u_WVP >= 0)            pglUniformMatrix4fv(pu.u_WVP, 1, GL_FALSE, vsConsts.u_WVP);
			if (pu.u_MV >= 0)             pglUniformMatrix4fv(pu.u_MV, 1, GL_FALSE, vsConsts.u_MV);
			if (pu.u_TexMatrix >= 0)      pglUniformMatrix4fv(pu.u_TexMatrix, 1, GL_FALSE, vsConsts.u_TexMatrix);
			if (pu.u_TexGen >= 0)         pglUniform4fv(pu.u_TexGen, 4, vsConsts.u_TexGen);
			if (pu.u_LightDir >= 0)       pglUniform4fv(pu.u_LightDir, 2, &vsConsts.u_LightDir[0][0]);
			if (pu.u_LightColor >= 0)     pglUniform4fv(pu.u_LightColor, 2, &vsConsts.u_LightColor[0][0]);
			if (pu.u_LightAmbient >= 0)   pglUniform4fv(pu.u_LightAmbient, 1, vsConsts.u_LightAmbient);
			if (pu.u_GlobalLightUV >= 0)  pglUniform2fv(pu.u_GlobalLightUV, 1, vsConsts.u_GlobalLightUV);
			if (pu.u_LightingEnable >= 0) pglUniform1i(pu.u_LightingEnable, vsConsts.u_LightingEnable);
			if (pu.u_UseTexGen >= 0)      pglUniform1i(pu.u_UseTexGen, vsConsts.u_UseTexGen);
			pu.lastVS = vsConsts;

			if (pu.u_StateColor >= 0)    pglUniform4fv(pu.u_StateColor, 1, psConsts.u_StateColor);
			if (pu.u_HasTexture >= 0)    pglUniform1i(pu.u_HasTexture, psConsts.u_HasTexture);
			if (pu.u_HasLightmap >= 0)   pglUniform1i(pu.u_HasLightmap, psConsts.u_HasLightmap);
			if (pu.u_AlphaFunc >= 0)     pglUniform1i(pu.u_AlphaFunc, psConsts.u_AlphaFunc);
			if (pu.u_AlphaCutoff >= 0)   pglUniform1f(pu.u_AlphaCutoff, psConsts.u_AlphaCutoff);
			if (pu.u_FogColor >= 0)      pglUniform4fv(pu.u_FogColor, 1, psConsts.u_FogColor);
			if (pu.u_FogMode >= 0)       pglUniform1i(pu.u_FogMode, psConsts.u_FogMode);
			if (pu.u_FogNear >= 0)       pglUniform1f(pu.u_FogNear, psConsts.u_FogNear);
			if (pu.u_FogFar >= 0)        pglUniform1f(pu.u_FogFar, psConsts.u_FogFar);
			if (pu.u_FogDensity >= 0)    pglUniform1f(pu.u_FogDensity, psConsts.u_FogDensity);
			if (pu.u_UseProjection >= 0) pglUniform1i(pu.u_UseProjection, psConsts.u_UseProjection);
			pu.lastPS = psConsts;
			pu.lastValid = true;
		}
		else
		{
			if (pu.u_WVP >= 0 && memcmp(pu.lastVS.u_WVP, vsConsts.u_WVP, sizeof(vsConsts.u_WVP)) != 0)
			{
				pglUniformMatrix4fv(pu.u_WVP, 1, GL_FALSE, vsConsts.u_WVP);
				memcpy(pu.lastVS.u_WVP, vsConsts.u_WVP, sizeof(vsConsts.u_WVP));
			}
			if (pu.u_MV >= 0 && memcmp(pu.lastVS.u_MV, vsConsts.u_MV, sizeof(vsConsts.u_MV)) != 0)
			{
				pglUniformMatrix4fv(pu.u_MV, 1, GL_FALSE, vsConsts.u_MV);
				memcpy(pu.lastVS.u_MV, vsConsts.u_MV, sizeof(vsConsts.u_MV));
			}
			const size_t vsTailOff = offsetof(VSConstantBufferData, u_TexMatrix);
			if (memcmp((const uint8_t*)&pu.lastVS + vsTailOff, (const uint8_t*)&vsConsts + vsTailOff, sizeof(VSConstantBufferData) - vsTailOff) != 0)
			{
				if (pu.u_TexMatrix >= 0 && memcmp(pu.lastVS.u_TexMatrix, vsConsts.u_TexMatrix, sizeof(vsConsts.u_TexMatrix)) != 0)
					pglUniformMatrix4fv(pu.u_TexMatrix, 1, GL_FALSE, vsConsts.u_TexMatrix);
				if (pu.u_TexGen >= 0 && memcmp(pu.lastVS.u_TexGen, vsConsts.u_TexGen, sizeof(vsConsts.u_TexGen)) != 0)
					pglUniform4fv(pu.u_TexGen, 4, vsConsts.u_TexGen);
				if (pu.u_LightDir >= 0 && memcmp(pu.lastVS.u_LightDir, vsConsts.u_LightDir, sizeof(vsConsts.u_LightDir)) != 0)
					pglUniform4fv(pu.u_LightDir, 2, &vsConsts.u_LightDir[0][0]);
				if (pu.u_LightColor >= 0 && memcmp(pu.lastVS.u_LightColor, vsConsts.u_LightColor, sizeof(vsConsts.u_LightColor)) != 0)
					pglUniform4fv(pu.u_LightColor, 2, &vsConsts.u_LightColor[0][0]);
				if (pu.u_LightAmbient >= 0 && memcmp(pu.lastVS.u_LightAmbient, vsConsts.u_LightAmbient, sizeof(vsConsts.u_LightAmbient)) != 0)
					pglUniform4fv(pu.u_LightAmbient, 1, vsConsts.u_LightAmbient);
				if (pu.u_GlobalLightUV >= 0 && memcmp(pu.lastVS.u_GlobalLightUV, vsConsts.u_GlobalLightUV, sizeof(vsConsts.u_GlobalLightUV)) != 0)
					pglUniform2fv(pu.u_GlobalLightUV, 1, vsConsts.u_GlobalLightUV);
				if (pu.u_LightingEnable >= 0 && pu.lastVS.u_LightingEnable != vsConsts.u_LightingEnable)
					pglUniform1i(pu.u_LightingEnable, vsConsts.u_LightingEnable);
				if (pu.u_UseTexGen >= 0 && pu.lastVS.u_UseTexGen != vsConsts.u_UseTexGen)
					pglUniform1i(pu.u_UseTexGen, vsConsts.u_UseTexGen);
				memcpy((uint8_t*)&pu.lastVS + vsTailOff, (const uint8_t*)&vsConsts + vsTailOff, sizeof(VSConstantBufferData) - vsTailOff);
			}

			if (memcmp(&pu.lastPS, &psConsts, sizeof(psConsts)) != 0)
			{
				if (pu.u_StateColor >= 0 && memcmp(pu.lastPS.u_StateColor, psConsts.u_StateColor, sizeof(psConsts.u_StateColor)) != 0)
					pglUniform4fv(pu.u_StateColor, 1, psConsts.u_StateColor);
				if (pu.u_HasTexture >= 0 && pu.lastPS.u_HasTexture != psConsts.u_HasTexture)
					pglUniform1i(pu.u_HasTexture, psConsts.u_HasTexture);
				if (pu.u_HasLightmap >= 0 && pu.lastPS.u_HasLightmap != psConsts.u_HasLightmap)
					pglUniform1i(pu.u_HasLightmap, psConsts.u_HasLightmap);
				if (pu.u_AlphaFunc >= 0 && pu.lastPS.u_AlphaFunc != psConsts.u_AlphaFunc)
					pglUniform1i(pu.u_AlphaFunc, psConsts.u_AlphaFunc);
				if (pu.u_AlphaCutoff >= 0 && pu.lastPS.u_AlphaCutoff != psConsts.u_AlphaCutoff)
					pglUniform1f(pu.u_AlphaCutoff, psConsts.u_AlphaCutoff);
				if (pu.u_FogColor >= 0 && memcmp(pu.lastPS.u_FogColor, psConsts.u_FogColor, sizeof(psConsts.u_FogColor)) != 0)
					pglUniform4fv(pu.u_FogColor, 1, psConsts.u_FogColor);
				if (pu.u_FogMode >= 0 && pu.lastPS.u_FogMode != psConsts.u_FogMode)
					pglUniform1i(pu.u_FogMode, psConsts.u_FogMode);
				if (pu.u_FogNear >= 0 && pu.lastPS.u_FogNear != psConsts.u_FogNear)
					pglUniform1f(pu.u_FogNear, psConsts.u_FogNear);
				if (pu.u_FogFar >= 0 && pu.lastPS.u_FogFar != psConsts.u_FogFar)
					pglUniform1f(pu.u_FogFar, psConsts.u_FogFar);
				if (pu.u_FogDensity >= 0 && pu.lastPS.u_FogDensity != psConsts.u_FogDensity)
					pglUniform1f(pu.u_FogDensity, psConsts.u_FogDensity);
				if (pu.u_UseProjection >= 0 && pu.lastPS.u_UseProjection != psConsts.u_UseProjection)
					pglUniform1i(pu.u_UseProjection, psConsts.u_UseProjection);
				pu.lastPS = psConsts;
			}
		}

		if (!pu.lastShaderValid || memcmp(&pu.lastShader, &s_shaderFrameParams, sizeof(s_shaderFrameParams)) != 0)
		{
			if (pu.u_SunDir >= 0)         pglUniform4fv(pu.u_SunDir, 1, s_shaderFrameParams.sunDir);
			if (pu.u_CameraPos >= 0)      pglUniform4fv(pu.u_CameraPos, 1, s_shaderFrameParams.cameraPos);
			if (pu.u_SkyColor >= 0)       pglUniform4fv(pu.u_SkyColor, 1, s_shaderFrameParams.skyColor);
			if (pu.u_ShaderFogColor >= 0) pglUniform4fv(pu.u_ShaderFogColor, 1, s_shaderFrameParams.fogColor);
			if (pu.u_WorldMeta >= 0)      pglUniform4fv(pu.u_WorldMeta, 1, s_shaderFrameParams.worldMeta);
			if (pu.u_WaterTint >= 0)      pglUniform4fv(pu.u_WaterTint, 1, s_shaderFrameParams.waterTint);
			if (pu.u_TorchTint >= 0)      pglUniform4fv(pu.u_TorchTint, 1, s_shaderFrameParams.torchTint);
			pu.lastShader = s_shaderFrameParams;
			pu.lastShaderValid = true;
		}

		if (!s_cacheValid || s_cachedTex0 != tex0)
		{
			pglActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tex0);
			s_cachedTex0 = tex0;
		}
		if (tex0 != 0 && (!td0 || td0->appliedLinear != (int)texLinear || td0->appliedClamp != (int)texClamp))
		{
			const bool hasMips = (td0 && td0->mipLevels > 1);
			const GLint minFilter = hasMips
				? (texLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR)
				: (texLinear ? GL_LINEAR : GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texLinear ? GL_LINEAR : GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
			if (hasMips)
			{
				glTexParameterf(GL_TEXTURE_2D, 0x8501 /* GL_TEXTURE_LOD_BIAS */, texLinear ? 0.0f : -1.0f);
				glTexParameterf(GL_TEXTURE_2D, 0x813B /* GL_TEXTURE_MAX_LOD */, texLinear ? 1000.0f : 2.0f);
			}
			if (td0) { td0->appliedLinear = (int)texLinear; td0->appliedClamp = (int)texClamp; }
		}
		if (!s_cacheValid || s_cachedTex1 != tex1)
		{
			pglActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, tex1);
			pglActiveTexture(GL_TEXTURE0);
			s_cachedTex1 = tex1;
		}

		if (!s_cacheValid || s_cachedBlend != blendEnable)
		{
			if (blendEnable) glEnable(GL_BLEND); else glDisable(GL_BLEND);
			s_cachedBlend = blendEnable;
		}
		if (blendEnable && (!s_cacheValid || s_cachedBlendSrc != blendSrc || s_cachedBlendDst != blendDst))
		{
			GLenum sCol = ToGLBlend(blendSrc);
			GLenum dCol = ToGLBlend(blendDst);
			GLenum sAlp = (sCol == GL_SRC_COLOR) ? GL_SRC_ALPHA : (sCol == GL_ONE_MINUS_SRC_COLOR ? GL_ONE_MINUS_SRC_ALPHA : (sCol == GL_DST_COLOR ? GL_DST_ALPHA : sCol));
			GLenum dAlp = (dCol == GL_SRC_COLOR) ? GL_SRC_ALPHA : (dCol == GL_ONE_MINUS_SRC_COLOR ? GL_ONE_MINUS_SRC_ALPHA : (dCol == GL_DST_COLOR ? GL_DST_ALPHA : dCol));
			if (pglBlendFuncSeparate) pglBlendFuncSeparate(sCol, dCol, sAlp, dAlp);
			else glBlendFunc(sCol, dCol);
			s_cachedBlendSrc = blendSrc;
			s_cachedBlendDst = blendDst;
		}
		if (!s_cacheValid || s_cachedBlendFactor != s_tls.blendFactorRGBA)
		{
			if (pglBlendColor)
			{
				float r = (float)((s_tls.blendFactorRGBA >> 16) & 0xFF) / 255.0f;
				float g = (float)((s_tls.blendFactorRGBA >> 8)  & 0xFF) / 255.0f;
				float b = (float)( s_tls.blendFactorRGBA        & 0xFF) / 255.0f;
				float a = (float)((s_tls.blendFactorRGBA >> 24) & 0xFF) / 255.0f;
				pglBlendColor(r, g, b, a);
			}
			s_cachedBlendFactor = s_tls.blendFactorRGBA;
		}

		if (!s_cacheValid || s_cachedDepthTest != depthTestEnable)
		{
			if (depthTestEnable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
			s_cachedDepthTest = depthTestEnable;
		}
		if (!s_cacheValid || s_cachedDepthMask != depthMask)
		{
			glDepthMask(depthMask ? GL_TRUE : GL_FALSE);
			s_cachedDepthMask = depthMask;
		}
		if (!s_cacheValid || s_cachedDepthFunc != s_tls.depthFunc)
		{
			glDepthFunc(ToGLCompare(s_tls.depthFunc));
			s_cachedDepthFunc = s_tls.depthFunc;
		}

		if (!s_cacheValid || s_cachedCull != cullEnable || s_cachedCullCW != cullCW)
		{
			if (cullEnable)
			{
				glEnable(GL_CULL_FACE);
				glFrontFace(GL_CCW);
				glCullFace(cullCW ? GL_BACK : GL_FRONT);
			}
			else
			{
				glDisable(GL_CULL_FACE);
			}
			s_cachedCull = cullEnable;
			s_cachedCullCW = cullCW;
		}

		if (!s_cacheValid || s_cachedColorMask != s_tls.colorWriteMask)
		{
			glColorMask(
				(s_tls.colorWriteMask & 1) ? GL_TRUE : GL_FALSE,
				(s_tls.colorWriteMask & 2) ? GL_TRUE : GL_FALSE,
				(s_tls.colorWriteMask & 4) ? GL_TRUE : GL_FALSE,
				(s_tls.colorWriteMask & 8) ? GL_TRUE : GL_FALSE);
			s_cachedColorMask = s_tls.colorWriteMask;
		}

		if (!s_cacheValid || s_cachedSlopeBias != s_tls.depthBiasSlope || s_cachedConstBias != s_tls.depthBiasConstant)
		{
			if (s_tls.depthBiasSlope != 0.0f || s_tls.depthBiasConstant != 0.0f)
			{
				glEnable(GL_POLYGON_OFFSET_FILL);
				/* glWrapper::glPolygonOffset divides factor/units by 65536.0f; restore OpenGL factor/units scale. */
				glPolygonOffset(s_tls.depthBiasSlope * 65536.0f, s_tls.depthBiasConstant * 65536.0f);
			}
			else
			{
				glDisable(GL_POLYGON_OFFSET_FILL);
			}
			s_cachedSlopeBias = s_tls.depthBiasSlope;
			s_cachedConstBias = s_tls.depthBiasConstant;
		}

		if (!s_cacheValid || s_cachedStencil != s_tls.stencilEnable || s_cachedStencilFunc != s_tls.stencilFunc ||
			s_cachedStencilRef != s_tls.stencilRef || s_cachedStencilRead != s_tls.stencilReadMask ||
			s_cachedStencilWrite != s_tls.stencilWriteMask)
		{
			if (s_tls.stencilEnable)
			{
				glEnable(GL_STENCIL_TEST);
				glStencilFunc(ToGLCompare(s_tls.stencilFunc), s_tls.stencilRef, s_tls.stencilReadMask);
				glStencilMask(s_tls.stencilWriteMask);
				glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			}
			else
			{
				glDisable(GL_STENCIL_TEST);
			}
			s_cachedStencil = s_tls.stencilEnable;
			s_cachedStencilFunc = s_tls.stencilFunc;
			s_cachedStencilRef = s_tls.stencilRef;
			s_cachedStencilRead = s_tls.stencilReadMask;
			s_cachedStencilWrite = s_tls.stencilWriteMask;
		}

		if (!s_cacheValid || s_cachedScissor)
		{
			glDisable(GL_SCISSOR_TEST);
			s_cachedScissor = false;
		}
		s_cacheValid = true;

		const bool isQuad = (primType == 3 /* QUAD_LIST */ || primType == 7);
		if (isQuad && s_quadIBO && count >= 4 && (count % 4) == 0)
		{
			int quads = count / 4;
			if (quads <= 65536)
			{
				if (staticVAO == 0 && (!s_cacheValid || s_cachedIBO != s_quadIBO))
				{
					pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_quadIBO);
					s_cachedIBO = s_quadIBO;
				}
				glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_INT, (const void*)0);
			}
			else
			{
				if (staticVAO == 0)
				{
					pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
					s_cachedIBO = 0;
				}
				/* Fallback for huge dynamic quad batches */
				std::vector<uint32_t> largeIdx((size_t)quads * 6);
				for (int q = 0; q < quads; ++q)
				{
					uint32_t b = (uint32_t)(q * 4);
					largeIdx[q * 6 + 0] = b + 0; largeIdx[q * 6 + 1] = b + 1; largeIdx[q * 6 + 2] = b + 2;
					largeIdx[q * 6 + 3] = b + 2; largeIdx[q * 6 + 4] = b + 3; largeIdx[q * 6 + 5] = b + 0;
				}
				glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_INT, largeIdx.data());
			}
		}
		else if (primType == 2 /* TRIANGLE_FAN */)
		{
			if (count >= 3)
			{
				int tris = count - 2;
				if (tris > 4096) tris = 4096;
				if (staticVAO == 0 && (!s_cacheValid || s_cachedIBO != s_fanIBO))
				{
					pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_fanIBO);
					s_cachedIBO = s_fanIBO;
				}
				glDrawElements(GL_TRIANGLES, tris * 3, GL_UNSIGNED_INT, (const void*)0);
			}
		}
		else
		{
			GLenum mode = GL_TRIANGLES;
			if (primType == 1) mode = GL_TRIANGLE_STRIP;
			else if (primType == 4) mode = GL_LINES;
			else if (primType == 5) mode = GL_LINE_STRIP;
			glDrawArrays(mode, 0, count);
		}
	}

	/* Lifecycle */
	static int GL_InitialiseWindow(HWND hwnd, int width, int height, int fullscreen, int vsync)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_hwnd = hwnd;
		s_width = (width > 0) ? width : 1280;
		s_height = (height > 0) ? height : 720;
		s_fullscreen = (fullscreen != 0);
		s_vsync = (vsync != 0);
		s_suspended = false;

		/* Step 1: Temp bootstrap window to query WGL extension entry points */
		WNDCLASSA wc = {};
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = GetModuleHandleA(NULL);
		wc.lpszClassName = "C4J_GL_Bootstrap";
		RegisterClassA(&wc);
		HWND tmpHwnd = CreateWindowExA(0, wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, NULL, NULL, wc.hInstance, NULL);
		if (tmpHwnd)
		{
			HDC tmpDC = GetDC(tmpHwnd);
			PIXELFORMATDESCRIPTOR tmpPfd = {};
			tmpPfd.nSize = sizeof(tmpPfd);
			tmpPfd.nVersion = 1;
			tmpPfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
			tmpPfd.iPixelType = PFD_TYPE_RGBA;
			tmpPfd.cColorBits = 32;
			tmpPfd.cDepthBits = 24;
			tmpPfd.cStencilBits = 8;
			int pf = ChoosePixelFormat(tmpDC, &tmpPfd);
			if (pf > 0 && SetPixelFormat(tmpDC, pf, &tmpPfd))
			{
				HGLRC tmpRC = wglCreateContext(tmpDC);
				if (tmpRC)
				{
					wglMakeCurrent(tmpDC, tmpRC);
					pfnWglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
					pfnWglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
					wglMakeCurrent(NULL, NULL);
					wglDeleteContext(tmpRC);
				}
			}
			ReleaseDC(tmpHwnd, tmpDC);
			DestroyWindow(tmpHwnd);
		}
		UnregisterClassA(wc.lpszClassName, GetModuleHandleA(NULL));

		/* Step 2: Configure actual HWND pixel format */
		s_hdc = GetDC(s_hwnd);
		if (!s_hdc) return 0;

		int chosenPF = 0;
		PIXELFORMATDESCRIPTOR pfd = {};
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cAlphaBits = 8;
		pfd.cDepthBits = 24;
		pfd.cStencilBits = 8;
		pfd.iLayerType = PFD_MAIN_PLANE;

		if (pfnWglChoosePixelFormatARB)
		{
			const int piAttrib[] = {
				WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
				WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
				WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
				WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
				WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
				WGL_COLOR_BITS_ARB,     32,
				WGL_ALPHA_BITS_ARB,     8,
				WGL_DEPTH_BITS_ARB,     24,
				WGL_STENCIL_BITS_ARB,   8,
				0
			};
			UINT numFmt = 0;
			pfnWglChoosePixelFormatARB(s_hdc, piAttrib, NULL, 1, &chosenPF, &numFmt);
			if (numFmt == 0) chosenPF = 0;
		}
		if (chosenPF == 0)
		{
			chosenPF = ChoosePixelFormat(s_hdc, &pfd);
		}
		if (chosenPF == 0 || !SetPixelFormat(s_hdc, chosenPF, &pfd))
		{
			ReleaseDC(s_hwnd, s_hdc);
			s_hdc = NULL;
			return 0;
		}

		/* Step 3: Create Compatibility Profile GL Context (4.6 -> 3.3 -> 3.0 -> legacy) */
		if (pfnWglCreateContextAttribsARB)
		{
			static const int versions[][2] = { {4, 6}, {4, 5}, {3, 3}, {3, 0} };
			for (int i = 0; i < 4 && !s_hglrc; ++i)
			{
				const int attribs[] = {
					WGL_CONTEXT_MAJOR_VERSION_ARB, versions[i][0],
					WGL_CONTEXT_MINOR_VERSION_ARB, versions[i][1],
					WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
					0
				};
				s_hglrc = pfnWglCreateContextAttribsARB(s_hdc, NULL, attribs);
			}
		}
		if (!s_hglrc)
		{
			s_hglrc = wglCreateContext(s_hdc);
		}
		if (!s_hglrc || !wglMakeCurrent(s_hdc, s_hglrc))
		{
			if (s_hglrc) { wglDeleteContext(s_hglrc); s_hglrc = NULL; }
			ReleaseDC(s_hwnd, s_hdc);
			s_hdc = NULL;
			return 0;
		}

		LoadGLFunctions();

		const char *rendererStr = (const char*)glGetString(GL_RENDERER);
		const char *versionStr  = (const char*)glGetString(GL_VERSION);
		if (rendererStr && rendererStr[0])
		{
			snprintf(s_adapterName, sizeof(s_adapterName), "%s", rendererStr);
			s_adapterName[sizeof(s_adapterName) - 1] = 0;
		}
		printf("[RenderGL] Initialized OpenGL (%s) on adapter: %s\n", versionStr ? versionStr : "?", s_adapterName);

		if (pfnWglSwapIntervalEXT)
		{
			pfnWglSwapIntervalEXT(s_vsync ? 1 : 0);
		}

		if (!pglCreateShader || !pglGenBuffers || !pglGenVertexArrays)
		{
			printf("[RenderGL] Missing required OpenGL 3.0+ extensions!\n");
			return 0;
		}

		std::string glslFSOpaque = "#version 150\n#define C4J_OPAQUE_ONLY 1\n";
		const char *fsBody = strchr(s_glslFS, '\n');
		glslFSOpaque += (fsBody ? (fsBody + 1) : s_glslFS);

		if (!BuildProgram(s_glslVSStd, s_glslFSVanilla, false, s_progVanillaStd) ||
			!BuildProgram(s_glslVSComp, s_glslFSVanilla, true, s_progVanillaComp) ||
			!BuildProgram(s_glslVSStd, glslFSOpaque.c_str(), false, s_progOpaqueStd) ||
			!BuildProgram(s_glslVSComp, glslFSOpaque.c_str(), true, s_progOpaqueComp) ||
			!BuildProgram(s_glslVSStd, s_glslFS, false, s_progStd) ||
			!BuildProgram(s_glslVSComp, s_glslFS, true, s_progComp))
		{
			return 0;
		}

		pglGenVertexArrays(1, &s_worldVAO);
		pglGenBuffers(1, &s_ringVBO);
		pglBindBuffer(GL_ARRAY_BUFFER, s_ringVBO);
		pglBufferData(GL_ARRAY_BUFFER, C4J_GL_RING_VB_BYTES, NULL, GL_STREAM_DRAW);
		pglBindBuffer(GL_ARRAY_BUFFER, 0);
		s_ringVBOOffset = 0;

		{
			const uint32_t maxQuads = 65536;
			std::vector<uint32_t> idx(maxQuads * 6);
			for (uint32_t q = 0; q < maxQuads; ++q)
			{
				uint32_t b = q * 4;
				idx[q * 6 + 0] = b + 0; idx[q * 6 + 1] = b + 1; idx[q * 6 + 2] = b + 2;
				idx[q * 6 + 3] = b + 2; idx[q * 6 + 4] = b + 3; idx[q * 6 + 5] = b + 0;
			}
			pglGenBuffers(1, &s_quadIBO);
			pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_quadIBO);
			pglBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
			pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		{
			const uint32_t maxTris = 4096;
			std::vector<uint32_t> idx(maxTris * 3);
			for (uint32_t t = 0; t < maxTris; ++t)
			{
				idx[t * 3 + 0] = 0; idx[t * 3 + 1] = t + 1; idx[t * 3 + 2] = t + 2;
			}
			pglGenBuffers(1, &s_fanIBO);
			pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_fanIBO);
			pglBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.data(), GL_STATIC_DRAW);
			pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}

		ApplyViewport(0);
		InvalidateStateCache();
		return 1;
	}

	static int GL_ResizeWindow(int width, int height, int fullscreen)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (width <= 0 || height <= 0) return 0;
		s_width = width;
		s_height = height;
		s_fullscreen = (fullscreen != 0);
		if (s_hglrc)
		{
			ApplyViewport(s_currentViewportType);
			InvalidateStateCache();
		}
		return 1;
	}

	static void GL_Shutdown(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		DrainDeferredDeletes();
		{
			std::lock_guard<std::mutex> cbLock(s_cbuffMutex);
			for (auto &kv : s_cbuffs)
			{
				if (kv.second.gpu)
				{
					if (!kv.second.gpu->vaos.empty() && pglDeleteVertexArrays)
					{
						pglDeleteVertexArrays((GLsizei)kv.second.gpu->vaos.size(), kv.second.gpu->vaos.data());
					}
					if (!kv.second.gpu->vbos.empty() && pglDeleteBuffers)
					{
						pglDeleteBuffers((GLsizei)kv.second.gpu->vbos.size(), kv.second.gpu->vbos.data());
					}
				}
			}
			s_cbuffs.clear();
			s_totalCBuffBytes.store(0, std::memory_order_relaxed);
		}
		for (auto &kv : s_textures)
		{
			if (kv.second.texId) glDeleteTextures(1, &kv.second.texId);
		}
		s_textures.clear();

		if (s_ringVBO && pglDeleteBuffers) { pglDeleteBuffers(1, &s_ringVBO); s_ringVBO = 0; }
		if (s_quadIBO && pglDeleteBuffers) { pglDeleteBuffers(1, &s_quadIBO); s_quadIBO = 0; }
		if (s_fanIBO  && pglDeleteBuffers) { pglDeleteBuffers(1, &s_fanIBO);  s_fanIBO = 0; }
		if (s_worldVAO && pglDeleteVertexArrays) { pglDeleteVertexArrays(1, &s_worldVAO); s_worldVAO = 0; }
		if (s_progVanillaStd.prog && pglDeleteProgram)  { pglDeleteProgram(s_progVanillaStd.prog); s_progVanillaStd.prog = 0; }
		if (s_progVanillaComp.prog && pglDeleteProgram) { pglDeleteProgram(s_progVanillaComp.prog); s_progVanillaComp.prog = 0; }
		if (s_progOpaqueStd.prog && pglDeleteProgram)   { pglDeleteProgram(s_progOpaqueStd.prog); s_progOpaqueStd.prog = 0; }
		if (s_progOpaqueComp.prog && pglDeleteProgram)  { pglDeleteProgram(s_progOpaqueComp.prog); s_progOpaqueComp.prog = 0; }
		if (s_progStd.prog && pglDeleteProgram)         { pglDeleteProgram(s_progStd.prog); s_progStd.prog = 0; }
		if (s_progComp.prog && pglDeleteProgram)       { pglDeleteProgram(s_progComp.prog); s_progComp.prog = 0; }
		if (s_customProgStd.prog && pglDeleteProgram)  { pglDeleteProgram(s_customProgStd.prog); s_customProgStd.prog = 0; }
		if (s_customProgComp.prog && pglDeleteProgram) { pglDeleteProgram(s_customProgComp.prog); s_customProgComp.prog = 0; }

		if (s_hglrc)
		{
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(s_hglrc);
			s_hglrc = NULL;
		}
		if (s_hwnd && s_hdc)
		{
			ReleaseDC(s_hwnd, s_hdc);
			s_hdc = NULL;
		}
	}

	static void GL_SetVsync(int enabled)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_vsync = (enabled != 0);
		if (pfnWglSwapIntervalEXT && s_hglrc)
		{
			pfnWglSwapIntervalEXT(s_vsync ? 1 : 0);
		}
	}
	static int GL_GetVsync(void) { return s_vsync ? 1 : 0; }
	static void GL_GetBackendInfo(C4JBackendInfo *outInfo)
	{
		if (!outInfo) return;
		outInfo->backendType = C4J_BACKEND_OPENGL;
		outInfo->vsyncEnabled = s_vsync ? 1 : 0;
		outInfo->screenWidth = s_width;
		outInfo->screenHeight = s_height;
		snprintf(outInfo->adapterName, sizeof(outInfo->adapterName), "%s", s_adapterName);
		outInfo->adapterName[sizeof(outInfo->adapterName) - 1] = 0;
		snprintf(outInfo->backendName, sizeof(outInfo->backendName), "OpenGL");
		outInfo->backendName[sizeof(outInfo->backendName) - 1] = 0;
	}

	static void GL_Tick(void) {}
	static void GL_UpdateGamma(unsigned short) {}
	static void GL_StartFrame(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_frameUploadedBytes = 0;
		s_frameUploadedCount = 0;
		DrainDeferredDeletes();
		s_tls.ResetFrameState();
		InvalidateStateCache();
	}
	static void GL_DoScreenGrabOnNextPresent(void) {}
	static void GL_Present(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (s_hdc && !s_suspended)
		{
			SwapBuffers(s_hdc);
		}
		DrainDeferredDeletes();
	}
	static void GL_Clear(int flags, const C4JClearRect *pRect)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (!s_hglrc) return;
		GLbitfield mask = 0;
		if (flags & C4J_CLEAR_COLOUR_FLAG) mask |= GL_COLOR_BUFFER_BIT;
		if (flags & C4J_CLEAR_DEPTH_FLAG)  mask |= GL_DEPTH_BUFFER_BIT;
		if (flags & GL_STENCIL_BUFFER_BIT) mask |= GL_STENCIL_BUFFER_BIT;
		if (mask == 0) return;

		/* Always allow clear writes cleanly regardless of prior mask state */
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		if (mask & GL_COLOR_BUFFER_BIT)
		{
			glClearColor(s_clearColour[0], s_clearColour[1], s_clearColour[2], s_clearColour[3]);
		}
		if (mask & GL_DEPTH_BUFFER_BIT)
		{
			glClearDepth(1.0);
		}
		if (pRect)
		{
			int rw = (int)(pRect->right - pRect->left);
			int rh = (int)(pRect->bottom - pRect->top);
			if (rw <= 0 || rh <= 0) return;
			glEnable(GL_SCISSOR_TEST);
			glScissor((int)pRect->left, s_height - (int)pRect->bottom, rw, rh);
			glClear(mask);
			glDisable(GL_SCISSOR_TEST);
		}
		else
		{
			glDisable(GL_SCISSOR_TEST);
			glClear(mask);
		}
		InvalidateStateCache();
	}
	static void GL_SetClearColour(const float colourRGBA[4]) { if (colourRGBA) memcpy(s_clearColour, colourRGBA, sizeof(float) * 4); }
	static int  GL_IsWidescreen(void) { return 1; }
	static int  GL_IsHiDef(void) { return 1; }
	static void GL_NoOpVoid(void) {}
	static void GL_NoOpInt(int) {}

	/* Matrix stack */
	static void GL_MatrixMode(int type) { if (type >= 0 && type < 3) { s_tls.matrixMode = type; s_tls.EnsureStackInit(type); } }
	static void GL_MatrixSetIdentity(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity(); s_tls.matrixDirty = true; }
	static void GL_MatrixTranslate(float x, float y, float z) { MatrixTranslate(s_tls, x, y, z); }
	static void GL_MatrixRotate(float angle, float x, float y, float z) { MatrixRotate(s_tls, angle, x, y, z); }
	static void GL_MatrixScale(float x, float y, float z) { MatrixScale(s_tls, x, y, z); }
	static void GL_MatrixPerspective(float fovy, float aspect, float zNear, float zFar) { MatrixPerspective(s_tls, fovy, aspect, zNear, zFar); }
	static void GL_MatrixOrthogonal(float left, float right, float bottom, float top, float zNear, float zFar) { MatrixOrthogonal(s_tls, left, right, bottom, top, zNear, zFar); }
	static void GL_MatrixPop(void)
	{
		s_tls.EnsureStackInit(s_tls.matrixMode);
		if (s_tls.matrixStacks[s_tls.matrixMode].size() > 1) s_tls.matrixStacks[s_tls.matrixMode].pop_back();
		else s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity();
		s_tls.matrixDirty = true;
	}
	static void GL_MatrixPush(void)
	{
		s_tls.EnsureStackInit(s_tls.matrixMode);
		if (s_tls.matrixStacks[s_tls.matrixMode].size() < 64)
			s_tls.matrixStacks[s_tls.matrixMode].push_back(s_tls.matrixStacks[s_tls.matrixMode].back());
	}
	static void GL_MatrixMult(float *mat)
	{
		if (!mat) return;
		s_tls.EnsureStackInit(s_tls.matrixMode);
		Matrix4x4 rhs; memcpy(rhs.m, mat, sizeof(rhs.m));
		s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Multiply(s_tls.matrixStacks[s_tls.matrixMode].back(), rhs);
		s_tls.matrixDirty = true;
	}
	static const float *GL_MatrixGet(int type)
	{
		if (type < 0 || type >= 3) type = 0;
		s_tls.EnsureStackInit(type);
		return s_tls.matrixStacks[type].back().m;
	}
	static void GL_SetMatrixDirty(void) { s_tls.matrixDirty = true; }

	/* Drawing */
	static void GL_DrawVertices(int primitiveType, int count, void *dataIn, int vType, int psType)
	{
		if (count <= 0 || !dataIn) return;
		size_t stride = (vType == 1) ? 16 : 32;
		size_t totalBytes = (size_t)count * stride;

		if (s_tls.recordingCBuffIndex != 0)
		{
			s_tls.EnsureStackInit(0);
			CBuffCommand cmd;
			cmd.primitiveType = primitiveType;
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

		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primitiveType, count, 0, 0, dataIn, totalBytes, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	static void GL_DrawVertexBuffer(int primType, int count, void *bufferHandle, int vType, int psType)
	{
		GLuint vbo = (GLuint)(uintptr_t)bufferHandle;
		if (vbo == 0 || count <= 0) return;
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, 0, vbo, NULL, 0, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	/* Command buffers with lazy static GL VBO+VAO baking */
	static void QueueSlotDeletesLocked(GLCBuffSlot &slot)
	{
		if (slot.gpu)
		{
			for (GLuint vbo : slot.gpu->vbos) if (vbo) s_pendingDeleteVBOs.push_back(vbo);
			for (GLuint vao : slot.gpu->vaos) if (vao) s_pendingDeleteVAOs.push_back(vao);
			slot.gpu.reset();
		}
	}

	static int GL_CBuffCreate(int count)
	{
		if (count <= 0) return 0;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		int first = s_nextCBuffId;
		s_nextCBuffId += count;
		return first;
	}
	static void GL_CBuffDelete(int first, int count)
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
					QueueSlotDeletesLocked(it->second);
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
				QueueSlotDeletesLocked(it->second);
				s_cbuffs.erase(it);
			}
		}
	}
	static void GL_CBuffStart(int index, int)
	{
		s_tls.recordingCBuffIndex = index;
		s_tls.recordingCommands.clear();
		s_tls.EnsureStackInit(0);
		s_tls.recordingBaseMatrix = s_tls.matrixStacks[0].back();
		Matrix4x4::Invert(s_tls.recordingBaseMatrix, s_tls.recordingInvBaseMatrix);
	}
	static void GL_CBuffClear(int index)
	{
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		if (it != s_cbuffs.end())
		{
			s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
			QueueSlotDeletesLocked(it->second);
			it->second.commands.reset();
			it->second.byteSize = 0;
			it->second.generation = ++s_nextCBuffGen;
			it->second.uploadedGen = 0;
		}
	}
	static int GL_CBuffSize(int index)
	{
		if (index == -1)
		{
			return (int)s_totalCBuffBytes.load(std::memory_order_relaxed);
		}
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		return (it != s_cbuffs.end()) ? (int)it->second.byteSize : 0;
	}
	static void GL_CBuffEnd(void)
	{
		if (s_tls.recordingCBuffIndex != 0)
		{
			size_t bytes = 0;
			for (const auto &c : s_tls.recordingCommands) bytes += c.vertexData.size();
			auto cmds = std::make_shared<std::vector<CBuffCommand>>(std::move(s_tls.recordingCommands));
			s_tls.recordingCommands.clear();

			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			GLCBuffSlot &slot = s_cbuffs[s_tls.recordingCBuffIndex];
			s_totalCBuffBytes.fetch_sub(slot.byteSize, std::memory_order_relaxed);
			QueueSlotDeletesLocked(slot);
			slot.commands = std::move(cmds);
			slot.byteSize = bytes;
			s_totalCBuffBytes.fetch_add(bytes, std::memory_order_relaxed);
			slot.generation = ++s_nextCBuffGen;
			slot.uploadedGen = 0;
			s_tls.recordingCBuffIndex = 0;
		}
	}
	static int GL_CBuffCall(int index, int)
	{
		std::shared_ptr<const std::vector<CBuffCommand>> cmds;
		std::shared_ptr<const GLCBuffGPU> gpu;
		size_t cbuffBytes = 0;
		uint64_t gen = 0;
		bool needsUpload = false;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(index);
			if (it == s_cbuffs.end() || !it->second.commands || it->second.commands->empty()) return 0;
			cmds = it->second.commands;
			cbuffBytes = it->second.byteSize;
			gen = it->second.generation;
			if (it->second.uploadedGen != gen)
			{
				needsUpload = true;
			}
			else
			{
				gpu = it->second.gpu;
			}
		}

		std::lock_guard<std::recursive_mutex> glLock(s_glMutex);
		if (!s_hglrc) return 0;

		if (needsUpload &&
			s_frameUploadedCount < C4J_GL_FRAME_UPLOAD_BUDGET_COUNT &&
			(s_frameUploadedBytes + cbuffBytes <= C4J_GL_FRAME_UPLOAD_BUDGET_BYTES || s_frameUploadedCount == 0))
		{
			auto newGPU = std::make_shared<GLCBuffGPU>();
			newGPU->vbos.assign(cmds->size(), 0);
			newGPU->vaos.assign(cmds->size(), 0);
			for (size_t i = 0; i < cmds->size(); ++i)
			{
				const auto &cmd = (*cmds)[i];
				if (cmd.vertexData.empty()) continue;
				GLuint vbo = 0;
				GLuint vao = 0;
				pglGenBuffers(1, &vbo);
				pglBindBuffer(GL_ARRAY_BUFFER, vbo);
				pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd.vertexData.size(), cmd.vertexData.data(), GL_STATIC_DRAW);

				pglGenVertexArrays(1, &vao);
				pglBindVertexArray(vao);
				pglBindBuffer(GL_ARRAY_BUFFER, vbo);
				SetupVAOAttribs(cmd.vType, (const uint8_t*)0, true);
				const bool isQuad = (cmd.primitiveType == 3 || cmd.primitiveType == 7);
				if (isQuad && s_quadIBO && cmd.count >= 4 && (cmd.count % 4) == 0 && (cmd.count / 4) <= 65536)
				{
					pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_quadIBO);
				}
				else if (cmd.primitiveType == 2 && s_fanIBO && cmd.count >= 3)
				{
					pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_fanIBO);
				}

				newGPU->vbos[i] = vbo;
				newGPU->vaos[i] = vao;
			}
			pglBindVertexArray(0);
			pglBindBuffer(GL_ARRAY_BUFFER, 0);
			s_cachedVAO = 0;
			s_cachedVBO = 0;
			s_frameUploadedBytes += cbuffBytes;
			s_frameUploadedCount++;

			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(index);
			if (it != s_cbuffs.end() && it->second.generation == gen)
			{
				QueueSlotDeletesLocked(it->second);
				it->second.gpu = newGPU;
				it->second.uploadedGen = gen;
				gpu = newGPU;
			}
			else
			{
				for (GLuint v : newGPU->vaos) if (v) s_pendingDeleteVAOs.push_back(v);
				for (GLuint v : newGPU->vbos) if (v) s_pendingDeleteVBOs.push_back(v);
				return 0;
			}
		}

		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		const Matrix4x4 &curModel = s_tls.matrixStacks[0].back();
		const Matrix4x4 &curProj  = s_tls.matrixStacks[1].back();

		for (size_t i = 0; i < cmds->size(); ++i)
		{
			const CBuffCommand &cmd = (*cmds)[i];
			Matrix4x4 effModel = Matrix4x4::Multiply(curModel, cmd.relativeMatrix);
			Matrix4x4 wvp = Matrix4x4::Multiply(curProj, effModel);
			int texId = (s_tls.boundTextureId != 0) ? s_tls.boundTextureId : cmd.boundTextureId;
			float combined[4] = {
				s_tls.stateColour[0] * cmd.stateColour[0],
				s_tls.stateColour[1] * cmd.stateColour[1],
				s_tls.stateColour[2] * cmd.stateColour[2],
				s_tls.stateColour[3] * cmd.stateColour[3]
			};
			GLuint staticVAO = (gpu && i < gpu->vaos.size()) ? gpu->vaos[i] : 0;
			GLuint staticVBO = (gpu && i < gpu->vbos.size()) ? gpu->vbos[i] : 0;
			ExecuteDrawInternal(cmd.primitiveType, cmd.count, staticVAO, staticVBO,
				staticVBO ? NULL : cmd.vertexData.data(), cmd.vertexData.size(),
				cmd.vType, cmd.psType, wvp, effModel, texId, combined,
				s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
				s_tls.depthTestEnable, s_tls.depthMask,
				s_tls.cullEnable, s_tls.cullCW,
				s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
		}
		return 1;
	}

	/* Textures */
	static int GL_TextureCreate(void) { return s_nextTextureId.fetch_add(1); }
	static void GL_TextureFree(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		auto it = s_textures.find(idx);
		if (it != s_textures.end())
		{
			if (it->second.texId) glDeleteTextures(1, &it->second.texId);
			s_textures.erase(it);
			InvalidateStateCache();
		}
	}
	static void GL_TextureBind(int idx)
	{
		if (s_tls.boundTextureId != idx)
		{
			s_tls.textureLevels = 1;
		}
		s_tls.boundTextureId = idx;
	}
	static void GL_TextureBindVertex(int idx) { s_tls.boundVertexTextureId = idx; }
	static void GL_TextureSetLevels(int levels) { s_tls.textureLevels = levels; }
	static int  GL_TextureGetLevels(void)
	{
		if (s_tls.boundTextureId > 0)
		{
			std::lock_guard<std::recursive_mutex> lock(s_glMutex);
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end()) return it->second.mipLevels;
		}
		return s_tls.textureLevels;
	}
	static void GL_TextureData(int width, int height, void *data, int level, int)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (!s_hglrc || s_tls.boundTextureId <= 0 || width <= 0 || height <= 0 || !data) return;

		pglActiveTexture(GL_TEXTURE0);
		if (level == 0)
		{
			bool prevLinear = false;
			bool prevClamp = false;
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end())
			{
				prevLinear = it->second.linearFilter;
				prevClamp = it->second.clampWrap;
				if (it->second.texId) glDeleteTextures(1, &it->second.texId);
				s_textures.erase(it);
			}
			uint32_t maxMips = ComputeMaxMipLevels(width, height);
			uint32_t mips = (s_tls.textureLevels > 1) ? (uint32_t)s_tls.textureLevels : 1u;
			if (mips > maxMips) mips = maxMips;
			s_tls.textureLevels = (int)mips;

			GLuint glTex = 0;
			glGenTextures(1, &glTex);
			glBindTexture(GL_TEXTURE_2D, glTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(mips - 1));
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (mips > 1) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

			GLTextureData td = { glTex, width, height, (int)mips, prevLinear, prevClamp, -1, -1 };
			s_textures[s_tls.boundTextureId] = td;
		}
		else
		{
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end() && it->second.texId && level >= 0 && level < it->second.mipLevels)
			{
				glBindTexture(GL_TEXTURE_2D, it->second.texId);
				glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			}
		}
		InvalidateStateCache();
	}
	static void GL_TextureDataUpdate(int xoff, int yoff, int width, int height, void *data, int level)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (!s_hglrc || s_tls.boundTextureId <= 0 || !data || width <= 0 || height <= 0) return;
		auto it = s_textures.find(s_tls.boundTextureId);
		if (it != s_textures.end() && it->second.texId && level >= 0 && level < it->second.mipLevels)
		{
			pglActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, it->second.texId);
			glTexSubImage2D(GL_TEXTURE_2D, level, xoff, yoff, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
			InvalidateStateCache();
		}
	}
	static void GL_TextureSetParam(int param, int value)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (s_tls.boundTextureId <= 0) return;
		GLTextureData &td = s_textures[s_tls.boundTextureId];
		if (param == 1 || param == 2) td.linearFilter = (value != 0);
		else if (param == 3 || param == 4) td.clampWrap = (value == 0);
	}
	static void *GL_TextureGetNativeHandle(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		auto it = s_textures.find(idx);
		return (it != s_textures.end()) ? (void*)(uintptr_t)it->second.texId : NULL;
	}

	/* State setters */
	static void GL_StateSetColour(float r, float g, float b, float a) { s_tls.stateColour[0]=r; s_tls.stateColour[1]=g; s_tls.stateColour[2]=b; s_tls.stateColour[3]=a; }
	static void GL_StateSetDepthMask(int enable) { s_tls.depthMask = (enable != 0); }
	static void GL_StateSetBlendEnable(int enable) { s_tls.blendEnable = (enable != 0); }
	static void GL_StateSetBlendFunc(int src, int dst) { s_tls.blendSrc = src; s_tls.blendDst = dst; }
	static void GL_StateSetBlendFactor(unsigned int colour) { s_tls.blendFactorRGBA = colour; }
	static void GL_StateSetAlphaFunc(int func, float param) { s_tls.alphaFunc = func; s_tls.alphaCutoff = param; }
	static void GL_StateSetDepthFunc(int func) { s_tls.depthFunc = func; }
	static void GL_StateSetFaceCull(int enable) { s_tls.cullEnable = (enable != 0); }
	static void GL_StateSetFaceCullCW(int enable) { s_tls.cullCW = (enable != 0); }
	static void GL_StateSetLineWidth(float width) { std::lock_guard<std::recursive_mutex> lock(s_glMutex); if (s_hglrc) glLineWidth(width); }
	static void GL_StateSetWriteEnable(int r, int g, int b, int a) { s_tls.colorWriteMask = (uint8_t)((r?1:0)|(g?2:0)|(b?4:0)|(a?8:0)); }
	static void GL_StateSetDepthTestEnable(int enable) { s_tls.depthTestEnable = (enable != 0); }
	static void GL_StateSetAlphaTestEnable(int enable) { s_tls.alphaTestEnable = (enable != 0); }
	static void GL_StateSetDepthSlopeAndBias(float slope, float bias) { s_tls.depthBiasSlope = slope; s_tls.depthBiasConstant = bias; }
	static void GL_StateSetFogEnable(int enable) { s_tls.fogEnable = (enable != 0); }
	static void GL_StateSetFogMode(int mode) { s_tls.fogMode = (mode == 2048 || mode == 2) ? 2 : 1; }
	static void GL_StateSetFogNear(float d) { s_tls.fogNear = d; }
	static void GL_StateSetFogFar(float d) { s_tls.fogFar = d; }
	static void GL_StateSetFogDensity(float d) { s_tls.fogDensity = d; }
	static void GL_StateSetFogColour(float r, float g, float b) { s_tls.fogColor[0]=r; s_tls.fogColor[1]=g; s_tls.fogColor[2]=b; }
	static void GL_StateSetLightingEnable(int enable) { s_tls.lightingEnable = (enable != 0); }
	static void GL_StateSetVertexTextureUV(float u, float v) { s_tls.vertexTexUV[0]=u; s_tls.vertexTexUV[1]=v; }
	static void GL_StateSetLightColour(int l, float r, float g, float b) { if (l==0||l==1) { s_tls.lightColor[l][0]=r; s_tls.lightColor[l][1]=g; s_tls.lightColor[l][2]=b; } }
	static void GL_StateSetLightAmbientColour(float r, float g, float b) { s_tls.lightAmbient[0]=r; s_tls.lightAmbient[1]=g; s_tls.lightAmbient[2]=b; s_tls.lightAmbient[3]=1.0f; }
	static void GL_StateSetLightDirection(int l, float x, float y, float z)
	{
		if (l == 0 || l == 1)
		{
			s_tls.EnsureStackInit(0);
			float tx, ty, tz;
			Matrix4x4::TransformVec3(s_tls.matrixStacks[0].back(), x, y, z, tx, ty, tz);
			s_tls.lightDir[l][0]=tx; s_tls.lightDir[l][1]=ty; s_tls.lightDir[l][2]=tz; s_tls.lightDir[l][3]=0.0f;
		}
	}
	static void GL_StateSetLightEnable(int l, int e) { if (l==0||l==1) s_tls.lightEnabled[l]=(e!=0); }
	static void GL_StateSetViewport(int viewportType)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_currentViewportType = viewportType;
		if (s_hglrc) ApplyViewport(viewportType);
	}
	static void GL_StateSetTexGenCol(int col, float x, float y, float z, float w, int eyeSpace)
	{
		if (col < 0 || col >= 4) return;
		if (!eyeSpace)
		{
			s_tls.texGenPlanes[col][0]=x; s_tls.texGenPlanes[col][1]=y; s_tls.texGenPlanes[col][2]=z; s_tls.texGenPlanes[col][3]=w;
		}
		else
		{
			s_tls.EnsureStackInit(0);
			const float *mv = s_tls.matrixStacks[0].back().m;
			float v[4] = { x, y, z, w };
			for (int row = 0; row < 4; ++row)
			{
				s_tls.texGenPlanes[col][row] = mv[row]*v[0] + mv[4+row]*v[1] + mv[8+row]*v[2] + mv[12+row]*v[3];
			}
		}
	}
	static void GL_StateSetStencil(int func, uint8_t ref, uint8_t rmask, uint8_t wmask)
	{
		s_tls.stencilEnable = (func != C4J_CMP_ALWAYS);
		s_tls.stencilFunc = func;
		s_tls.stencilRef = ref;
		s_tls.stencilReadMask = rmask;
		s_tls.stencilWriteMask = wmask;
	}

	static void GL_BeginEvent(const wchar_t*) {}
	static void GL_Suspend(void) { s_suspended = true; }
	static int  GL_Suspended(void) { return s_suspended ? 1 : 0; }
	static void GL_Resume(void) { s_suspended = false; }

	/* GDraw Bridge */
	static void *GL_GDrawCreateContext(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (pglBindVertexArray) pglBindVertexArray(0);
		if (pglUseProgram) pglUseProgram(0);
		if (pglBindBuffer)
		{
			pglBindBuffer(GL_ARRAY_BUFFER, 0);
			pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		GDrawFunctions *funcs = gdraw_GL_CreateContext(w, h, 0);
		if (funcs)
		{
			gdraw_GL_SetResourceLimits(GDRAW_GL_RESOURCE_vertexbuffer, 5000, 64 * 1024 * 1024);
			gdraw_GL_SetResourceLimits(GDRAW_GL_RESOURCE_texture, 5000, 128 * 1024 * 1024);
			gdraw_GL_SetResourceLimits(GDRAW_GL_RESOURCE_rendertarget, 10, 64 * 1024 * 1024);
		}
		InvalidateStateCache();
		return (void*)funcs;
	}
	static void GL_GDrawDestroyContext(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (pglBindVertexArray) pglBindVertexArray(0);
		gdraw_GL_DestroyContext();
		InvalidateStateCache();
	}
	static void GL_GDrawSetRendertargetSize(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		gdraw_GL_SetRendertargetSize(w, h);
	}
	static void GL_GDrawSetTileOrigin(int xPos, int yPos)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (pglBindVertexArray) pglBindVertexArray(0);
		gdraw_GL_SetTileOrigin(xPos, yPos, 0);
		InvalidateStateCache();
	}
	static void GL_GDrawNoMoreGDrawThisFrame(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		gdraw_GL_NoMoreGDrawThisFrame();
		InvalidateStateCache();
	}
	static void GL_GDrawBeginCustomDraw4J(void *region, float *mat16)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		gdraw_GL_BeginCustomDraw_4J((IggyCustomDrawCallbackRegion*)region, mat16);
		InvalidateStateCache();
	}
	static void GL_GDrawCalculateCustomDraw4J(void *region, float *mat16)
	{
		gdraw_GL_CalculateCustomDraw_4J((IggyCustomDrawCallbackRegion*)region, mat16);
	}
	static void GL_GDrawEndCustomDraw(void *region)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (pglBindVertexArray) pglBindVertexArray(0);
		gdraw_GL_EndCustomDraw((IggyCustomDrawCallbackRegion*)region);
		InvalidateStateCache();
	}
	static void GL_GDrawSetViewport4J(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		gdraw_GL_setViewport_4J();
		InvalidateStateCache();
	}
	static void *GL_GDrawWrappedTextureCreate(int textureId)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		auto it = s_textures.find(textureId);
		if (it == s_textures.end() || it->second.texId == 0) return NULL;
		return (void*)gdraw_GL_WrappedTextureCreate((S32)it->second.texId, it->second.width, it->second.height, (rrbool)(it->second.mipLevels > 1 ? 1 : 0));
	}
	static void GL_GDrawWrappedTextureDestroy(void *handle)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (handle) gdraw_GL_WrappedTextureDestroy((GDrawTexture*)handle);
	}

	static int GL_CaptureBackBufferRGBA(C4JCaptureAllocFn allocFn, void *user, int *outWidth, int *outHeight)
	{
		if (outWidth) *outWidth = 0;
		if (outHeight) *outHeight = 0;
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (!s_hglrc || !s_hdc || s_suspended || s_width <= 0 || s_height <= 0 || !allocFn) return 0;

		GLint prevReadFBO = 0;
		GLint prevReadBuf = GL_BACK;
		GLint prevPackAlign = 4;
		GLint prevPackRowLen = 0;
		GLint prevPackBuf = 0;
		glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);
		glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlign);
		glGetIntegerv(GL_PACK_ROW_LENGTH, &prevPackRowLen);
		if (pglBindFramebuffer)
		{
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFBO);
			pglBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}
		if (pglBindBuffer)
		{
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPackBuf);
			pglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		}

		GLboolean doubleBuffered = GL_TRUE;
		glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
		glReadBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);

		const size_t rowBytes = (size_t)s_width * 4u;
		std::vector<uint8_t> bottomUp(rowBytes * (size_t)s_height);
		glFinish();
		glReadPixels(0, 0, s_width, s_height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
		const GLenum glErr = glGetError();

		glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlign);
		glPixelStorei(GL_PACK_ROW_LENGTH, prevPackRowLen);
		glReadBuffer((GLenum)prevReadBuf);
		if (pglBindBuffer) pglBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)prevPackBuf);
		if (pglBindFramebuffer) pglBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevReadFBO);

		if (glErr != GL_NO_ERROR) return 0;

		uint8_t *dstPixels = NULL;
		if (!allocFn(s_width, s_height, &dstPixels, user) || !dstPixels) return 0;

		for (int y = 0; y < s_height; ++y)
		{
			const uint8_t *srcRow = bottomUp.data() + ((size_t)(s_height - 1 - y) * rowBytes);
			uint8_t *dstRow = dstPixels + ((size_t)y * rowBytes);
			for (int x = 0; x < s_width; ++x)
			{
				dstRow[(x * 4) + 0] = srcRow[(x * 4) + 0];
				dstRow[(x * 4) + 1] = srcRow[(x * 4) + 1];
				dstRow[(x * 4) + 2] = srcRow[(x * 4) + 2];
				dstRow[(x * 4) + 3] = 0xFFu;
			}
		}

		if (outWidth) *outWidth = s_width;
		if (outHeight) *outHeight = s_height;
		return 1;
	}

	static void GL_ShaderSetFrameParams(const C4JShaderFrameParams *params)
	{
		if (!params) return;
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		s_shaderFrameParams = *params;
	}

	static void GL_ShaderClearCustomProgram(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (s_customProgStd.prog && pglDeleteProgram)  { pglDeleteProgram(s_customProgStd.prog); }
		if (s_customProgComp.prog && pglDeleteProgram) { pglDeleteProgram(s_customProgComp.prog); }
		s_customProgStd = {};
		s_customProgComp = {};
		if (s_shaderMode == C4J_SHADER_MODE_CUSTOM)
			s_shaderMode = C4J_SHADER_MODE_DEFAULT;
		InvalidateStateCache();
	}

	static int GL_ShaderLoadCustomProgram(const C4JShaderProgramDesc *desc, char *errBuf, size_t errBufSize)
	{
		if (errBuf && errBufSize > 0) errBuf[0] = 0;
		if (!desc || !desc->glslFragSource || desc->glslFragSource[0] == 0)
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "Missing GLSL fragment shader source");
			return 0;
		}
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (!s_hglrc || !pglCreateShader)
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "OpenGL context not initialized");
			return 0;
		}

		const char *vsStd = (desc->glslVertSource && desc->glslVertSource[0]) ? desc->glslVertSource : s_glslVSStd;
		const char *vsComp = s_glslVSComp;
		const char *fsSrc = desc->glslFragSource;

		ProgramUniforms newStd = {};
		ProgramUniforms newComp = {};
		if (!BuildProgram(vsStd, fsSrc, false, newStd))
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "GLSL standard shader compile/link failed (see console)");
			return 0;
		}
		if (!BuildProgram(vsComp, fsSrc, true, newComp))
		{
			pglDeleteProgram(newStd.prog);
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "GLSL compressed shader compile/link failed (see console)");
			return 0;
		}

		if (s_customProgStd.prog && pglDeleteProgram)  pglDeleteProgram(s_customProgStd.prog);
		if (s_customProgComp.prog && pglDeleteProgram) pglDeleteProgram(s_customProgComp.prog);
		s_customProgStd = newStd;
		s_customProgComp = newComp;
		s_shaderMode = C4J_SHADER_MODE_CUSTOM;
		InvalidateStateCache();
		return 1;
	}

	static void GL_ShaderSetMode(int mode)
	{
		std::lock_guard<std::recursive_mutex> lock(s_glMutex);
		if (mode == C4J_SHADER_MODE_CUSTOM && (!s_customProgStd.prog || !s_customProgComp.prog))
			s_shaderMode = C4J_SHADER_MODE_DEFAULT;
		else if (mode >= C4J_SHADER_MODE_OFF && mode <= C4J_SHADER_MODE_CUSTOM)
			s_shaderMode = mode;
		InvalidateStateCache();
	}

	static int GL_ShaderGetMode(void)
	{
		return s_shaderMode;
	}

	static const C4JRenderBackendAPI s_glAPI =
	{
		sizeof(C4JRenderBackendAPI),
		C4J_RENDER_BACKEND_ABI_VERSION,
		C4J_BACKEND_OPENGL,
		"OpenGL",
		GL_InitialiseWindow,
		GL_ResizeWindow,
		GL_Shutdown,
		GL_SetVsync,
		GL_GetVsync,
		GL_GetBackendInfo,
		GL_Tick,
		GL_UpdateGamma,
		GL_StartFrame,
		GL_DoScreenGrabOnNextPresent,
		GL_Present,
		GL_Clear,
		GL_SetClearColour,
		GL_IsWidescreen,
		GL_IsHiDef,
		GL_NoOpInt,
		GL_NoOpVoid,
		GL_NoOpInt,
		GL_NoOpVoid,
		GL_MatrixMode,
		GL_MatrixSetIdentity,
		GL_MatrixTranslate,
		GL_MatrixRotate,
		GL_MatrixScale,
		GL_MatrixPerspective,
		GL_MatrixOrthogonal,
		GL_MatrixPop,
		GL_MatrixPush,
		GL_MatrixMult,
		GL_MatrixGet,
		GL_SetMatrixDirty,
		GL_DrawVertices,
		GL_DrawVertexBuffer,
		GL_NoOpVoid,
		GL_CBuffCreate,
		GL_CBuffDelete,
		GL_CBuffStart,
		GL_CBuffClear,
		GL_CBuffSize,
		GL_CBuffEnd,
		GL_CBuffCall,
		GL_NoOpVoid,
		GL_NoOpVoid,
		GL_NoOpVoid,
		GL_TextureCreate,
		GL_TextureFree,
		GL_TextureBind,
		GL_TextureBindVertex,
		GL_TextureSetLevels,
		GL_TextureGetLevels,
		GL_TextureData,
		GL_TextureDataUpdate,
		GL_TextureSetParam,
		GL_NoOpVoid,
		GL_NoOpVoid,
		GL_TextureGetNativeHandle,
		GL_StateSetColour,
		GL_StateSetDepthMask,
		GL_StateSetBlendEnable,
		GL_StateSetBlendFunc,
		GL_StateSetBlendFactor,
		GL_StateSetAlphaFunc,
		GL_StateSetDepthFunc,
		GL_StateSetFaceCull,
		GL_StateSetFaceCullCW,
		GL_StateSetLineWidth,
		GL_StateSetWriteEnable,
		GL_StateSetDepthTestEnable,
		GL_StateSetAlphaTestEnable,
		GL_StateSetDepthSlopeAndBias,
		GL_StateSetFogEnable,
		GL_StateSetFogMode,
		GL_StateSetFogNear,
		GL_StateSetFogFar,
		GL_StateSetFogDensity,
		GL_StateSetFogColour,
		GL_StateSetLightingEnable,
		GL_StateSetVertexTextureUV,
		GL_StateSetLightColour,
		GL_StateSetLightAmbientColour,
		GL_StateSetLightDirection,
		GL_StateSetLightEnable,
		GL_StateSetViewport,
		GL_NoOpInt,
		GL_StateSetTexGenCol,
		GL_StateSetStencil,
		GL_NoOpInt,
		GL_BeginEvent,
		GL_NoOpVoid,
		GL_Suspend,
		GL_Suspended,
		GL_Resume,
		GL_GDrawCreateContext,
		GL_GDrawDestroyContext,
		GL_GDrawSetRendertargetSize,
		GL_GDrawSetTileOrigin,
		GL_GDrawNoMoreGDrawThisFrame,
		GL_GDrawBeginCustomDraw4J,
		GL_GDrawCalculateCustomDraw4J,
		GL_GDrawEndCustomDraw,
		GL_GDrawSetViewport4J,
		GL_GDrawWrappedTextureCreate,
		GL_GDrawWrappedTextureDestroy,
		GL_CaptureBackBufferRGBA,
		GL_ShaderSetFrameParams,
		GL_ShaderLoadCustomProgram,
		GL_ShaderClearCustomProgram,
		GL_ShaderSetMode,
		GL_ShaderGetMode
	};
}

extern "C" __declspec(dllexport) const C4JRenderBackendAPI *C4JBackend_GetAPI(void)
{
	return &s_glAPI;
}
