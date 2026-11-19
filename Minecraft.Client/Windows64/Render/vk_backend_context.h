#pragma once

#include "vk_minimal.h"
#include "iggy.h"
#include "gdraw.h"

struct VkBackendGDrawHost
{
	VkDispatchTable *vk;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;
	VkPhysicalDeviceMemoryProperties memProps;

	VkRenderPass renderPass;
	VkFormat colorFormat;
	VkFormat depthStencilFormat;
	int width;
	int height;

	VkCommandBuffer activeCmd;
	int frameIndex;
	uint32_t frameSeq;

	uint32_t (*FindMemoryType)(uint32_t typeBits, VkFlags requiredProps);
	void (*EnsureRenderPassActive)(void);
	void (*EndRenderPassIfActive)(void);
	void (*InvalidateWorldPipelineCache)(void);
	void (*UploadImageData)(VkImage image, uint32_t width, uint32_t height, uint32_t mipLevel, const void *pixelsRGBA, size_t byteCount, bool isFinalUpload);
	void *(*GetMCImageView)(int textureId, int *outW, int *outH, bool *outLinear, bool *outClamp);
	void (*ApplyMCViewport)(void);
	void (*QueueGarbage)(VkBuffer buf, VkImage img, VkImageView iv, VkDeviceMemory mem);
};

extern GDrawFunctions *gdraw_VK_CreateContext(VkBackendGDrawHost *host, S32 w, S32 h);
extern void gdraw_VK_DestroyContext(void);
extern void gdraw_VK_SetRendertargetSize(S32 w, S32 h);
extern void gdraw_VK_SetTileOrigin(S32 x, S32 y);
extern void gdraw_VK_NoMoreGDrawThisFrame(void);
extern void gdraw_VK_BeginCustomDraw_4J(IggyCustomDrawCallbackRegion *region, F32 mat[16]);
extern void gdraw_VK_CalculateCustomDraw_4J(IggyCustomDrawCallbackRegion *region, F32 mat[16]);
extern void gdraw_VK_EndCustomDraw(IggyCustomDrawCallbackRegion *region);
extern void gdraw_VK_SetViewport_4J(void);
extern GDrawTexture *gdraw_VK_WrappedTextureCreate(int textureId);
extern void gdraw_VK_WrappedTextureDestroy(GDrawTexture *tex);
