#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shader pipeline modes */
enum C4JShaderMode
{
	C4J_SHADER_MODE_OFF     = 0, /* Vanilla lighting (default game shaders) */
	C4J_SHADER_MODE_DEFAULT = 1, /* Built-in LegacyCraft Enhanced lighting shader */
	C4J_SHADER_MODE_CUSTOM  = 2  /* External shader pack loaded from shaders/<pack>/ */
};

/* Per-frame scene lighting & atmosphere uniforms passed to GPU shaders (16-byte aligned).
   Total size: 7 * 16 = 112 bytes. */
typedef struct C4JShaderFrameParams
{
	float sunDir[4];    /* xyz: world-space sun/moon direction, w: daylight factor [0..1] */
	float cameraPos[4]; /* xyz: world-space camera position, w: elapsed time in seconds */
	float skyColor[4];  /* rgb: current sky color, w: rain strength [0..1] */
	float fogColor[4];  /* rgb: current fog/horizon color, w: thunder strength [0..1] */
	float worldMeta[4]; /* x: sunAngle [0..1], y: worldTime [0..24000], z: dimensionId (-1,0,1), w: inWorldPass (0 or 1) */
	float waterTint[4]; /* rgb: water specular/sky tint, w: shaderMode (0, 1, 2) */
	float torchTint[4]; /* rgb: warm block-light tint, w: water/torch intensity scale */
} C4JShaderFrameParams;

/* Custom shader pack payload passed to render backends */
typedef struct C4JShaderProgramDesc
{
	/* HLSL source (DX11 / DX12): must define VSMainStandard, VSMainCompressed, PSMain */
	const char *hlslSource;
	/* GLSL sources (OpenGL) */
	const char *glslVertSource;
	const char *glslFragSource;
	/* SPIR-V binaries (Vulkan) */
	const uint32_t *vkVertStdSpv;
	size_t vkVertStdSpvBytes;
	const uint32_t *vkVertCmpSpv;
	size_t vkVertCmpSpvBytes;
	const uint32_t *vkFragSpv;
	size_t vkFragSpvBytes;
} C4JShaderProgramDesc;

#ifdef __cplusplus
}
#endif
