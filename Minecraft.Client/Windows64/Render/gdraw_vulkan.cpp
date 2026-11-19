#include "vk_backend_context.h"
#include "vk_shaders_spv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <unordered_map>
#include <algorithm>

#define GDRAW_VK_RING_BYTES      (8u * 1024u * 1024u)
#define GDRAW_VK_UBO_STRIDE      512u
#define GDRAW_VK_MAX_DRAWS_FRAME 8192u
#define GDRAW_VK_MAX_STACK       8

namespace
{
	struct GDrawVulkanTex
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFramebuffer fb = VK_NULL_HANDLE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		int width = 0;
		int height = 0;
		int format = GDRAW_TEXTURE_FORMAT_rgba32;
		bool isWrapped = false;
		bool hasMipmaps = false;
		bool isRT = false;
		int rtSlot = -1;
		void *uniqueId = NULL;
		std::vector<uint8_t> cpuShadow; /* Used for partial UpdateTextureRect re-uploads */
	};

	struct GDrawVulkanVB
	{
		VkBuffer vbuf = VK_NULL_HANDLE;
		VkDeviceMemory vmem = VK_NULL_HANDLE;
		VkBuffer ibuf = VK_NULL_HANDLE;
		VkDeviceMemory imem = VK_NULL_HANDLE;
		S32 vsize = 0;
		S32 isize = 0;
		void *uniqueId = NULL;
	};

	struct GDrawUBOData
	{
		float world0[4];
		float world1[4];
		float x_off[4];
		float texgen_s[4];
		float texgen_t[4];
		float viewproj[4];
		float color_mul[4];
		float color_add[4];
		float focal[4];
		int32_t tex0_mode;
		int32_t additive_mode;
		int32_t has_tex0;
		int32_t pad;
		float blur_taps[9][4];
		float blur_clamp[4];
		float filter_data[4];
	};

	struct GDrawPipelineKey
	{
		int vformat;
		int blendMode;
		int testId;
		int setId;
		int stencilTest;
		int stencilSet;
		int psMode;

		bool operator==(const GDrawPipelineKey &o) const
		{
			return vformat == o.vformat && blendMode == o.blendMode &&
			       testId == o.testId && setId == o.setId &&
			       stencilTest == o.stencilTest && stencilSet == o.stencilSet &&
			       psMode == o.psMode;
		}
	};

	struct GDrawPipelineKeyHash
	{
		size_t operator()(const GDrawPipelineKey &k) const
		{
			size_t h = (size_t)(k.vformat & 7);
			h = (h << 3) | (size_t)(k.blendMode & 7);
			h = (h << 1) | (size_t)(k.testId & 1);
			h = (h << 1) | (size_t)(k.setId & 1);
			h = (h << 8) | (size_t)(k.stencilTest & 0xFF);
			h = (h << 8) | (size_t)(k.stencilSet & 0xFF);
			h = (h << 3) | (size_t)(k.psMode & 7);
			return h;
		}
	};

	struct RTStackEntry
	{
		GDrawVulkanTex *tex;
		S32 baseX, baseY, width, height;
		bool cached;
	};

	static VkBackendGDrawHost *s_host = NULL;
	static GDrawFunctions s_funcs = {};

	static S32 s_fw = 1280, s_fh = 720;
	static S32 s_vx = 0, s_vy = 0;
	static S32 s_tx0 = 0, s_ty0 = 0, s_tw = 1280, s_th = 720;
	static S32 s_tx0p = 0, s_ty0p = 0, s_tpw = 1280, s_tph = 720;
	static S32 s_frametexW = 1280, s_frametexH = 720;
	static bool s_tileEnabled = false;
	static bool s_inBlur = false;
	static F32 s_worldToPixel[2] = { 1.0f, 1.0f };
	static F32 s_projection[4] = { 2.0f / 1280.0f, -2.0f / 720.0f, -1.0f, 1.0f };
	static F32 s_xform3d[3][4] = {};
	static bool s_use3d = false;

	static RTStackEntry s_rtStack[GDRAW_VK_MAX_STACK] = {};
	static int s_rtDepth = 0;
	static GDrawVulkanTex *s_rtPool[GDRAW_VK_MAX_STACK] = {};
	static bool s_rtSlotUsed[GDRAW_VK_MAX_STACK] = {};
	static VkImage s_rtDepthImage = VK_NULL_HANDLE;
	static VkDeviceMemory s_rtDepthMem = VK_NULL_HANDLE;
	static VkImageView s_rtDepthView = VK_NULL_HANDLE;
	static S32 s_rtDepthW = 0, s_rtDepthH = 0;

	/* Vulkan pipeline / shader objects for GDraw */
	static VkShaderModule s_smBasic      = VK_NULL_HANDLE;
	static VkShaderModule s_smAA         = VK_NULL_HANDLE;
	static VkShaderModule s_smTC2        = VK_NULL_HANDLE;
	static VkShaderModule s_smFrag       = VK_NULL_HANDLE;
	static VkShaderModule s_smBlurFrag   = VK_NULL_HANDLE;
	static VkShaderModule s_smCMFrag     = VK_NULL_HANDLE;
	static VkShaderModule s_smDSFrag     = VK_NULL_HANDLE;

	static VkDescriptorSetLayout s_dsl = VK_NULL_HANDLE;
	static VkPipelineLayout      s_plLayout = VK_NULL_HANDLE;
	static VkDescriptorPool      s_descPools[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	static uint32_t              s_descPoolResetFrame[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
	static uint32_t              s_frameEpoch = 1;

	/* Persistent ring buffers per frame for streaming vertices, indices & UBOs */
	struct FrameRing
	{
		VkBuffer vb = VK_NULL_HANDLE;
		VkDeviceMemory vbMem = VK_NULL_HANDLE;
		void *vbPtr = NULL;
		uint32_t vbOff = 0;

		VkBuffer ubo = VK_NULL_HANDLE;
		VkDeviceMemory uboMem = VK_NULL_HANDLE;
		void *uboPtr = NULL;
		uint32_t uboOff = 0;
	};
	static FrameRing s_rings[2];

	/* Samplers: [linear? 0:1][wrap: 0=clamp, 1=repeat, 2=mirror] */
	static VkSampler s_samplers[2][3] = {};

	/* Antialias LUT texture & fallback 1x1 white texture */
	static GDrawVulkanTex *s_aaTex = NULL;
	static GDrawVulkanTex *s_whiteTex = NULL;

	/* Pipeline cache */
	static std::unordered_map<GDrawPipelineKey, VkPipeline, GDrawPipelineKeyHash> s_pipes;
	static std::vector<GDrawVulkanTex*> s_allTextures;
	static std::vector<GDrawVulkanVB*>  s_allVBs;

	/* Staging heap allocations while Iggy builds textures/VBs */
	struct PendingTexUpload
	{
		S32 w, h;
		gdraw_texture_format fmt;
		bool mipmap;
		void *uniqueId;
		uint8_t *rgbaBytes;
	};

	struct PendingVBBuild
	{
		gdraw_vformat vformat;
		S32 vlen;
		S32 ilen;
		void *uniqueId;
		uint8_t *vdata;
		uint8_t *idata;
	};

	static bool CreateHostVisibleBuffer(VkDeviceSize sz, VkFlags usage, VkBuffer &outBuf, VkDeviceMemory &outMem, void **outPtr)
	{
		VkDispatchTable *vk = s_host->vk;
		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sz, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		if (vk->vkCreateBuffer(s_host->device, &bci, NULL, &outBuf) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		vk->vkGetBufferMemoryRequirements(s_host->device, outBuf, &mr);
		uint32_t mt = s_host->FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt };
		if (vk->vkAllocateMemory(s_host->device, &mai, NULL, &outMem) != VK_SUCCESS)
		{
			vk->vkDestroyBuffer(s_host->device, outBuf, NULL);
			outBuf = VK_NULL_HANDLE;
			return false;
		}
		vk->vkBindBufferMemory(s_host->device, outBuf, outMem, 0);
		if (outPtr) vk->vkMapMemory(s_host->device, outMem, 0, sz, 0, outPtr);
		return true;
	}

	static bool CreateTexture2D(int w, int h, const uint8_t *rgba, GDrawVulkanTex *tex)
	{
		VkDispatchTable *vk = s_host->vk;
		VkImageCreateInfo ici = {};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_R8G8B8A8_UNORM;
		ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vk->vkCreateImage(s_host->device, &ici, NULL, &tex->image) != VK_SUCCESS) return false;

		VkMemoryRequirements mr = {};
		vk->vkGetImageMemoryRequirements(s_host->device, tex->image, &mr);
		uint32_t mt = s_host->FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt };
		if (vk->vkAllocateMemory(s_host->device, &mai, NULL, &tex->mem) != VK_SUCCESS) return false;
		vk->vkBindImageMemory(s_host->device, tex->image, tex->mem, 0);

		VkImageViewCreateInfo ivci = {};
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = tex->image;
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
		ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vk->vkCreateImageView(s_host->device, &ivci, NULL, &tex->view) != VK_SUCCESS) return false;

		tex->width = w;
		tex->height = h;
		if (rgba)
		{
			s_host->UploadImageData(tex->image, (uint32_t)w, (uint32_t)h, 0, rgba, (size_t)w * (size_t)h * 4u, true);
		}
		return true;
	}

	static void DestroyTextureObj(GDrawVulkanTex *tex)
	{
		if (!tex) return;
		if (!tex->isWrapped && s_host && s_host->device)
		{
			if (s_host->QueueGarbage)
			{
				s_host->QueueGarbage(VK_NULL_HANDLE, tex->image, tex->view, tex->mem);
			}
			else
			{
				VkDispatchTable *vk = s_host->vk;
				if (tex->view)  vk->vkDestroyImageView(s_host->device, tex->view, NULL);
				if (tex->image) vk->vkDestroyImage(s_host->device, tex->image, NULL);
				if (tex->mem)   vk->vkFreeMemory(s_host->device, tex->mem, NULL);
			}
		}
		delete tex;
	}

	static void DestroyVBObj(GDrawVulkanVB *vb)
	{
		if (!vb) return;
		if (s_host && s_host->device)
		{
			if (s_host->QueueGarbage)
			{
				s_host->QueueGarbage(vb->vbuf, VK_NULL_HANDLE, VK_NULL_HANDLE, vb->vmem);
				s_host->QueueGarbage(vb->ibuf, VK_NULL_HANDLE, VK_NULL_HANDLE, vb->imem);
			}
			else
			{
				VkDispatchTable *vk = s_host->vk;
				if (vb->vbuf) vk->vkDestroyBuffer(s_host->device, vb->vbuf, NULL);
				if (vb->vmem) vk->vkFreeMemory(s_host->device, vb->vmem, NULL);
				if (vb->ibuf) vk->vkDestroyBuffer(s_host->device, vb->ibuf, NULL);
				if (vb->imem) vk->vkFreeMemory(s_host->device, vb->imem, NULL);
			}
		}
		delete vb;
	}

	static void SetProjectionRaw(S32 x0, S32 x1, S32 y0, S32 y1)
	{
		s_projection[0] = 2.0f / (float)(x1 - x0);
		s_projection[1] = 2.0f / (float)(y1 - y0);
		s_projection[2] = (float)(x1 + x0) / (float)(x0 - x1);
		s_projection[3] = (float)(y1 + y0) / (float)(y0 - y1);
	}

	static void UpdateProjection(void)
	{
		if (s_inBlur)
		{
			SetProjectionRaw(0, s_tpw, s_tph, 0);
		}
		else if (s_rtDepth == 0)
		{
			SetProjectionRaw(s_tx0, s_tx0 + s_tw, s_ty0 + s_th, s_ty0);
		}
		else
		{
			const RTStackEntry &e = s_rtStack[s_rtDepth - 1];
			SetProjectionRaw(e.baseX, e.baseX + e.width, e.baseY + e.height, e.baseY);
		}
	}

	static void ApplyViewport(void)
	{
		if (!s_host || !s_host->activeCmd) return;
		VkViewport vp = {};
		if (s_inBlur)
		{
			vp.x = 0.0f;
			vp.y = (float)s_tph;
			vp.width = (float)s_tpw;
			vp.height = -(float)s_tph;
		}
		else if (s_rtDepth == 0)
		{
			vp.x = (float)s_vx;
			vp.y = (float)(s_vy + s_th);
			vp.width = (float)s_tw;
			vp.height = -(float)s_th;
		}
		else
		{
			const RTStackEntry &e = s_rtStack[s_rtDepth - 1];
			vp.x = 0.0f;
			vp.y = (float)e.height;
			vp.width = (float)e.width;
			vp.height = -(float)e.height;
		}
		vp.minDepth = 0.0f;
		vp.maxDepth = 1.0f;
		s_host->vk->vkCmdSetViewport(s_host->activeCmd, 0, 1, &vp);
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

	static float DepthFromId(S32 id)
	{
		/* Keep [0, 1] range for Vulkan depth buffer (cleared to 1.0f, COMPARE_OP_LESS) */
		return 1.0f - (float)(id + 1) * (1.0f / 65536.0f);
	}

	static void ComputeWorldRows(const GDrawRenderState *r, float world0[4], float world1[4])
	{
		float d = DepthFromId(r->id);
		if (!r->use_world_space && r->o2w)
		{
			world0[0] = r->o2w->m00; world0[1] = r->o2w->m01; world0[2] = d; world0[3] = r->o2w->trans[0];
			world1[0] = r->o2w->m10; world1[1] = r->o2w->m11; world1[2] = 0.0f; world1[3] = r->o2w->trans[1];
		}
		else
		{
			world0[0] = s_worldToPixel[0]; world0[1] = 0.0f; world0[2] = d; world0[3] = 0.0f;
			world1[0] = 0.0f; world1[1] = s_worldToPixel[1]; world1[2] = 0.0f; world1[3] = 0.0f;
		}
	}

	static VkPipeline GetOrCreatePipeline(const GDrawPipelineKey &key)
	{
		auto it = s_pipes.find(key);
		if (it != s_pipes.end()) return it->second;

		VkShaderModule vsMod = s_smBasic;
		VkVertexInputBindingDescription vbDesc = { 0, 8, VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vaDesc[2] = {};
		uint32_t vaCount = 1;

		vaDesc[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
		if (key.vformat == GDRAW_vformat_v2aa)
		{
			vsMod = s_smAA;
			vbDesc.stride = 16;
			vaDesc[1] = { 1, 0, VK_FORMAT_R16G16B16A16_SINT, 8 };
			vaCount = 2;
		}
		else if (key.vformat == GDRAW_vformat_v2tc2)
		{
			vsMod = s_smTC2;
			vbDesc.stride = 16;
			vaDesc[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 };
			vaCount = 2;
		}

		VkShaderModule fsMod = s_smFrag;
		if (key.psMode == 1 && s_smBlurFrag) fsMod = s_smBlurFrag;
		else if (key.psMode == 2 && s_smCMFrag) fsMod = s_smCMFrag;
		else if (key.psMode == 3 && s_smDSFrag) fsMod = s_smDSFrag;

		VkPipelineShaderStageCreateInfo stages[2] = {
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vsMod, "main", NULL },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsMod, "main", NULL },
		};

		VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 1, &vbDesc, vaCount, vaDesc };
		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
		VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL };

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = key.testId ? VK_TRUE : VK_FALSE;
		ds.depthWriteEnable = key.setId ? VK_TRUE : VK_FALSE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;
		ds.stencilTestEnable = (key.stencilTest | key.stencilSet) ? VK_TRUE : VK_FALSE;
		ds.front.failOp = VK_STENCIL_OP_KEEP;
		ds.front.passOp = VK_STENCIL_OP_REPLACE;
		ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
		ds.front.compareOp = VK_COMPARE_OP_EQUAL;
		ds.front.compareMask = (uint32_t)(key.stencilTest & 0xFF);
		ds.front.writeMask = (uint32_t)(key.stencilSet & 0xFF);
		ds.front.reference = 255;
		ds.back = ds.front;

		VkPipelineColorBlendAttachmentState cba = {};
		cba.colorWriteMask = key.stencilSet ? 0u : 0xFu;
		if (key.blendMode == GDRAW_BLEND_alpha)
		{
			cba.blendEnable = VK_TRUE;
			cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			cba.colorBlendOp = VK_BLEND_OP_ADD;
			cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			cba.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		else if (key.blendMode == GDRAW_BLEND_multiply)
		{
			cba.blendEnable = VK_TRUE;
			cba.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
			cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			cba.colorBlendOp = VK_BLEND_OP_ADD;
			cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
			cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			cba.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		else if (key.blendMode == GDRAW_BLEND_add)
		{
			cba.blendEnable = VK_TRUE;
			cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.colorBlendOp = VK_BLEND_OP_ADD;
			cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			cba.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		else
		{
			cba.blendEnable = VK_FALSE;
		}

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cba;

		VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dynStates };

		VkGraphicsPipelineCreateInfo gp = {};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dyn;
		gp.layout = s_plLayout;
		gp.renderPass = s_host->renderPass;

		VkPipeline pipe = VK_NULL_HANDLE;
		if (s_host->vk->vkCreateGraphicsPipelines(s_host->device, VK_NULL_HANDLE, 1, &gp, NULL, &pipe) == VK_SUCCESS)
		{
			s_pipes[key] = pipe;
		}
		return pipe;
	}

	/* GDraw Callbacks */
	static void RADLINK gdraw_vk_GetInfo(GDrawInfo *d)
	{
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

	static void RADLINK gdraw_vk_SetViewSizeAndWorldScale(S32 w, S32 h, F32 scalex, F32 scaley)
	{
		s_fw = w > 0 ? w : 1;
		s_fh = h > 0 ? h : 1;
		s_worldToPixel[0] = scalex;
		s_worldToPixel[1] = scaley;
	}

	static void RADLINK gdraw_vk_Set3DTransform(F32 *mat)
	{
		if (!mat) s_use3d = false;
		else { s_use3d = true; memcpy(s_xform3d, mat, sizeof(s_xform3d)); }
	}

	static void RADLINK gdraw_vk_RenderingBegin(void) {}
	static void RADLINK gdraw_vk_RenderingEnd(void) {}

	static void EnsureGDrawRenderPassActive(void)
	{
		if (s_rtDepth == 0 && s_host) s_host->EnsureRenderPassActive();
	}

	static void RADLINK gdraw_vk_RenderTileBegin(S32 x0, S32 y0, S32 x1, S32 y1, S32 pad, GDrawStats *)
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
		s_tx0p = x0 - pad; s_ty0p = y0 - pad;
		if (s_tpw > s_frametexW) s_frametexW = s_tpw;
		if (s_tph > s_frametexH) s_frametexH = s_tph;
		s_rtDepth = 0;
		UpdateProjection();
		if (s_host)
		{
			s_host->EnsureRenderPassActive();
			ApplyViewport();
		}
	}

	static void RADLINK gdraw_vk_RenderTileEnd(GDrawStats *)
	{
		s_rtDepth = 0;
		if (s_host) s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_vk_SetAntialiasTexture(S32 width, U8 *rgba)
	{
		if (s_aaTex) { DestroyTextureObj(s_aaTex); s_aaTex = NULL; }
		if (width > 0 && rgba)
		{
			s_aaTex = new GDrawVulkanTex();
			CreateTexture2D(width, 1, rgba, s_aaTex);
		}
	}

	static void RADLINK gdraw_vk_ClearStencilBits(U32)
	{
		if (!s_host || !s_host->activeCmd) return;
		EnsureGDrawRenderPassActive();
		uint32_t cw = (s_rtDepth > 0) ? (uint32_t)s_frametexW : (uint32_t)s_host->width;
		uint32_t ch = (s_rtDepth > 0) ? (uint32_t)s_frametexH : (uint32_t)s_host->height;
		VkRect2D sc = { { 0, 0 }, { cw, ch } };
		s_host->vk->vkCmdSetScissor(s_host->activeCmd, 0, 1, &sc);
		VkClearAttachment ca = {};
		ca.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		ca.clearValue.depthStencil.stencil = 0;
		VkClearRect cr = { sc, 0, 1 };
		s_host->vk->vkCmdClearAttachments(s_host->activeCmd, 1, &ca, 1, &cr);
	}

	static void RADLINK gdraw_vk_ClearID(void)
	{
		if (!s_host || !s_host->activeCmd) return;
		EnsureGDrawRenderPassActive();
		uint32_t cw = (s_rtDepth > 0) ? (uint32_t)s_frametexW : (uint32_t)s_host->width;
		uint32_t ch = (s_rtDepth > 0) ? (uint32_t)s_frametexH : (uint32_t)s_host->height;
		VkRect2D sc = { { 0, 0 }, { cw, ch } };
		s_host->vk->vkCmdSetScissor(s_host->activeCmd, 0, 1, &sc);
		VkClearAttachment ca = {};
		ca.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		ca.clearValue.depthStencil.depth = 1.0f;
		VkClearRect cr = { sc, 0, 1 };
		s_host->vk->vkCmdClearAttachments(s_host->activeCmd, 1, &ca, 1, &cr);
	}

	static void RADLINK gdraw_vk_DrawIndexedTriangles(GDrawRenderState *r, GDrawPrimitive *p, GDrawVertexBuffer *buf, GDrawStats *gstats)
	{
		if (!s_host || !r || !p || p->num_vertices <= 0) return;
		if (p->vertex_format < 0 || p->vertex_format >= GDRAW_vformat__basic_count) return;

		EnsureGDrawRenderPassActive();
		if (!s_host->activeCmd) return;

		int fi = s_host->frameIndex & 1;
		VkDispatchTable *vk = s_host->vk;

		if (s_descPoolResetFrame[fi] != s_host->frameSeq)
		{
			vk->vkResetDescriptorPool(s_host->device, s_descPools[fi], 0);
			s_descPoolResetFrame[fi] = s_host->frameSeq;
			s_rings[fi].vbOff = 0;
			s_rings[fi].uboOff = 0;
		}

		/* Prepare vertex & index buffer slices */
		VkBuffer vbuf = VK_NULL_HANDLE, ibuf = VK_NULL_HANDLE;
		VkDeviceSize voff = 0, ioff = 0;
		uint32_t drawIndices = (p->num_indices > 0) ? (uint32_t)p->num_indices : 0u;
		if (buf)
		{
			if (drawIndices == 0) return;
			GDrawVulkanVB *gvb = (GDrawVulkanVB*)buf;
			vbuf = gvb->vbuf;
			ibuf = gvb->ibuf;
			voff = (VkDeviceSize)(uintptr_t)p->vertices;
			ioff = (VkDeviceSize)(uintptr_t)p->indices;
		}
		else if (p->indices && p->num_indices > 0)
		{
			if (!p->vertices) return;
			size_t vstride = (p->vertex_format == GDRAW_vformat_v2) ? 8 : 16;
			uint32_t vbytes = (uint32_t)((size_t)p->num_vertices * vstride);
			uint32_t ibytes = (uint32_t)((size_t)p->num_indices * sizeof(uint16_t));
			FrameRing &ring = s_rings[fi];
			uint32_t va = (ring.vbOff + 15u) & ~15u;
			if (va + vbytes + ibytes + 16u > GDRAW_VK_RING_BYTES) return;
			memcpy((uint8_t*)ring.vbPtr + va, p->vertices, vbytes);
			uint32_t ia = (va + vbytes + 15u) & ~15u;
			memcpy((uint8_t*)ring.vbPtr + ia, p->indices, ibytes);
			ring.vbOff = ia + ibytes;
			vbuf = ring.vb; voff = va;
			ibuf = ring.vb; ioff = ia;
		}
		else
		{
			if (!p->vertices || (p->num_vertices % 4) != 0) return;
			size_t vstride = (p->vertex_format == GDRAW_vformat_v2) ? 8 : 16;
			uint32_t numQuads = (uint32_t)(p->num_vertices / 4);
			drawIndices = numQuads * 6u;
			uint32_t vbytes = (uint32_t)((size_t)p->num_vertices * vstride);
			uint32_t ibytes = drawIndices * (uint32_t)sizeof(uint16_t);
			FrameRing &ring = s_rings[fi];
			uint32_t va = (ring.vbOff + 15u) & ~15u;
			if (va + vbytes + ibytes + 16u > GDRAW_VK_RING_BYTES) return;
			memcpy((uint8_t*)ring.vbPtr + va, p->vertices, vbytes);
			uint32_t ia = (va + vbytes + 15u) & ~15u;
			uint16_t *iq = (uint16_t*)((uint8_t*)ring.vbPtr + ia);
			for (uint32_t q = 0; q < numQuads; ++q)
			{
				uint16_t b = (uint16_t)(q * 4u);
				iq[q * 6u + 0] = (uint16_t)(b + 0u);
				iq[q * 6u + 1] = (uint16_t)(b + 1u);
				iq[q * 6u + 2] = (uint16_t)(b + 2u);
				iq[q * 6u + 3] = (uint16_t)(b + 0u);
				iq[q * 6u + 4] = (uint16_t)(b + 2u);
				iq[q * 6u + 5] = (uint16_t)(b + 3u);
			}
			ring.vbOff = ia + ibytes;
			vbuf = ring.vb; voff = va;
			ibuf = ring.vb; ioff = ia;
		}

		/* Write UBO slice */
		FrameRing &ring = s_rings[fi];
		uint32_t uboAligned = (ring.uboOff + GDRAW_VK_UBO_STRIDE - 1u) & ~(GDRAW_VK_UBO_STRIDE - 1u);
		if (uboAligned + GDRAW_VK_UBO_STRIDE > GDRAW_VK_MAX_DRAWS_FRAME * GDRAW_VK_UBO_STRIDE) return;

		GDrawUBOData *ud = (GDrawUBOData*)((uint8_t*)ring.uboPtr + uboAligned);
		memset(ud, 0, sizeof(*ud));
		ComputeWorldRows(r, ud->world0, ud->world1);
		if (r->edge_matrix) memcpy(ud->x_off, r->edge_matrix, sizeof(float) * 4);
		if (r->texgen0_enabled)
		{
			memcpy(ud->texgen_s, r->s0_texgen, sizeof(float) * 4);
			memcpy(ud->texgen_t, r->t0_texgen, sizeof(float) * 4);
		}
		memcpy(ud->viewproj, s_projection, sizeof(float) * 4);
		memcpy(ud->color_mul, r->color, sizeof(float) * 4);
		if (r->cxf_add)
		{
			ud->color_add[0] = r->cxf_add[0] / 255.0f;
			ud->color_add[1] = r->cxf_add[1] / 255.0f;
			ud->color_add[2] = r->cxf_add[2] / 255.0f;
			ud->color_add[3] = r->cxf_add[3] / 255.0f;
			ud->additive_mode = r->cxf_add[3] ? 2 : 1;
		}
		memcpy(ud->focal, r->focal_point, sizeof(float) * 4);
		ud->tex0_mode = (int32_t)r->tex0_mode;
		ud->has_tex0 = (r->tex[0] != NULL && r->tex0_mode != GDRAW_TEXTURE_none) ? 1 : 0;
		ring.uboOff = uboAligned + GDRAW_VK_UBO_STRIDE;

		/* Allocate & update descriptor set */
		VkDescriptorSet dset = VK_NULL_HANDLE;
		VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, s_descPools[fi], 1, &s_dsl };
		if (vk->vkAllocateDescriptorSets(s_host->device, &dsai, &dset) != VK_SUCCESS) return;

		GDrawVulkanTex *t0 = (GDrawVulkanTex*)r->tex[0];
		VkImageView iv0 = (t0 && t0->view) ? t0->view : s_whiteTex->view;
		int wrapIdx = (r->wrap0 == GDRAW_WRAP_repeat) ? 1 : (r->wrap0 == GDRAW_WRAP_mirror ? 2 : 0);
		int filtIdx = r->nearest0 ? 1 : 0;
		VkSampler smp0 = s_samplers[filtIdx][wrapIdx];
		VkImageView iv1 = (s_aaTex && s_aaTex->view) ? s_aaTex->view : s_whiteTex->view;
		VkSampler smp1 = s_samplers[0][0]; /* Linear clamp for AA LUT */

		VkDescriptorBufferInfo bi = { ring.ubo, uboAligned, sizeof(GDrawUBOData) };
		VkDescriptorImageInfo ii0 = { smp0, iv0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo ii1 = { smp1, iv1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet writes[3] = {
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii0, NULL, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii1, NULL, NULL },
		};
		vk->vkUpdateDescriptorSets(s_host->device, 3, writes, 0, NULL);

		GDrawPipelineKey pk = {
			p->vertex_format,
			(int)r->blend_mode,
			r->test_id ? 1 : 0,
			r->set_id ? 1 : 0,
			(int)r->stencil_test,
			(int)r->stencil_set,
			0
		};
		VkPipeline pipe = GetOrCreatePipeline(pk);
		if (!pipe) return;

		ApplyViewport();

		const S32 rtMaxW = (s_rtDepth > 0) ? s_frametexW : s_host->width;
		const S32 rtMaxH = (s_rtDepth > 0) ? s_frametexH : s_host->height;
		VkRect2D scissor = { { 0, 0 }, { (uint32_t)rtMaxW, (uint32_t)rtMaxH } };
		if (r->scissor)
		{
			S32 x0 = r->scissor_rect.x0;
			S32 y0 = r->scissor_rect.y0;
			S32 x1 = r->scissor_rect.x1;
			S32 y1 = r->scissor_rect.y1;
			if (s_rtDepth > 0)
			{
				if (!s_rtStack[s_rtDepth - 1].cached) { x0 -= s_tx0p; x1 -= s_tx0p; y0 -= s_ty0p; y1 -= s_ty0p; }
			}
			else
			{
				if (s_tileEnabled)
				{
					x0 -= s_tx0; x1 -= s_tx0;
					y0 -= s_ty0; y1 -= s_ty0;
				}
				x0 += s_vx; x1 += s_vx;
				y0 += s_vy; y1 += s_vy;
			}
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > rtMaxW) x1 = rtMaxW;
			if (y1 > rtMaxH) y1 = rtMaxH;
			if (x1 <= x0 || y1 <= y0) return;
			scissor.offset = { x0, y0 };
			scissor.extent = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) };
		}

		vk->vkCmdSetScissor(s_host->activeCmd, 0, 1, &scissor);
		vk->vkCmdBindPipeline(s_host->activeCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
		vk->vkCmdBindDescriptorSets(s_host->activeCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_plLayout, 0, 1, &dset, 0, NULL);
		vk->vkCmdBindVertexBuffers(s_host->activeCmd, 0, 1, &vbuf, &voff);
		vk->vkCmdBindIndexBuffer(s_host->activeCmd, ibuf, ioff, VK_INDEX_TYPE_UINT16);
		vk->vkCmdDrawIndexed(s_host->activeCmd, drawIndices, 1, 0, 0, 0);

		if (gstats)
		{
			gstats->nonzero_flags |= GDRAW_STATS_batches;
			gstats->num_batches += 1;
			gstats->drawn_indices += (U32)drawIndices;
			gstats->drawn_vertices += (U32)p->num_vertices;
		}
		s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_vk_FilterQuad(GDrawRenderState *r, S32 x0, S32 y0, S32 x1, S32 y1, GDrawStats *stats);

	static rrbool RADLINK gdraw_vk_MakeTextureBegin(void *unique_id, S32 width, S32 height, gdraw_texture_format format, gdraw_maketexture_flags flags, GDraw_MakeTexture_ProcessingInfo *info, GDrawStats *)
	{
		if (width <= 0 || height <= 0 || !info) return 0;
		PendingTexUpload *pu = (PendingTexUpload*)calloc(1, sizeof(PendingTexUpload));
		pu->w = width;
		pu->h = height;
		pu->fmt = format;
		pu->mipmap = (flags & GDRAW_MAKETEXTURE_FLAGS_mipmap) != 0;
		pu->uniqueId = unique_id;
		size_t bpp = (format == GDRAW_TEXTURE_FORMAT_font) ? 1 : 4;
		pu->rgbaBytes = (uint8_t*)malloc((size_t)width * (size_t)height * bpp);
		if (!pu->rgbaBytes) { free(pu); return 0; }

		memset(info, 0, sizeof(*info));
		info->texture_data = pu->rgbaBytes;
		info->num_rows = height;
		info->stride_in_bytes = width * (S32)bpp;
		info->texture_type = GDRAW_TEXTURE_TYPE_rgba;
		info->p0 = pu;
		return 1;
	}

	static GDrawTexture * RADLINK gdraw_vk_MakeTextureEnd(GDraw_MakeTexture_ProcessingInfo *info, GDrawStats *stats)
	{
		if (!info || !info->p0) return NULL;
		PendingTexUpload *pu = (PendingTexUpload*)info->p0;
		std::vector<uint8_t> rgba((size_t)pu->w * (size_t)pu->h * 4u);
		if (pu->fmt == GDRAW_TEXTURE_FORMAT_font)
		{
			size_t n = (size_t)pu->w * (size_t)pu->h;
			for (size_t i = 0; i < n; ++i)
			{
				rgba[i * 4 + 0] = 255;
				rgba[i * 4 + 1] = 255;
				rgba[i * 4 + 2] = 255;
				rgba[i * 4 + 3] = pu->rgbaBytes[i];
			}
		}
		else
		{
			memcpy(rgba.data(), pu->rgbaBytes, rgba.size());
		}

		GDrawVulkanTex *tex = new GDrawVulkanTex();
		tex->format = (int)pu->fmt;
		tex->uniqueId = pu->uniqueId;
		tex->cpuShadow = rgba;
		if (!CreateTexture2D(pu->w, pu->h, rgba.data(), tex))
		{
			DestroyTextureObj(tex);
			free(pu->rgbaBytes);
			free(pu);
			return NULL;
		}
		s_allTextures.push_back(tex);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_alloc_tex;
			stats->alloc_tex += 1;
			stats->alloc_tex_bytes += (U32)rgba.size();
		}
		free(pu->rgbaBytes);
		free(pu);
		return (GDrawTexture*)tex;
	}

	static rrbool RADLINK gdraw_vk_UpdateTextureBegin(GDrawTexture *, void *, GDrawStats *) { return 1; }

	static void RADLINK gdraw_vk_UpdateTextureRect(GDrawTexture *t, void *, S32 x, S32 y, S32 stride, S32 w, S32 h, U8 *data, gdraw_texture_format format)
	{
		GDrawVulkanTex *tex = (GDrawVulkanTex*)t;
		if (!tex || !data || w <= 0 || h <= 0 || tex->isWrapped) return;
		if (tex->cpuShadow.size() != (size_t)tex->width * (size_t)tex->height * 4u) return;

		for (S32 row = 0; row < h; ++row)
		{
			S32 dy = y + row;
			if (dy < 0 || dy >= tex->height) continue;
			const U8 *srcRow = data + row * stride;
			uint8_t *dstRow = tex->cpuShadow.data() + ((size_t)dy * (size_t)tex->width + (size_t)x) * 4u;
			for (S32 col = 0; col < w; ++col)
			{
				if (x + col < 0 || x + col >= tex->width) continue;
				if (format == GDRAW_TEXTURE_FORMAT_font)
				{
					dstRow[col * 4 + 0] = 255;
					dstRow[col * 4 + 1] = 255;
					dstRow[col * 4 + 2] = 255;
					dstRow[col * 4 + 3] = srcRow[col];
				}
				else
				{
					dstRow[col * 4 + 0] = srcRow[col * 4 + 0];
					dstRow[col * 4 + 1] = srcRow[col * 4 + 1];
					dstRow[col * 4 + 2] = srcRow[col * 4 + 2];
					dstRow[col * 4 + 3] = srcRow[col * 4 + 3];
				}
			}
		}
		s_host->UploadImageData(tex->image, (uint32_t)tex->width, (uint32_t)tex->height, 0,
			tex->cpuShadow.data(), tex->cpuShadow.size(), true);
	}

	static void RADLINK gdraw_vk_UpdateTextureEnd(GDrawTexture *, void *, GDrawStats *) {}

	static void RADLINK gdraw_vk_FreeTexture(GDrawTexture *t, void *, GDrawStats *)
	{
		GDrawVulkanTex *tex = (GDrawVulkanTex*)t;
		if (!tex) return;
		if (tex->isRT)
		{
			if (tex->rtSlot >= 0 && tex->rtSlot < GDRAW_VK_MAX_STACK)
				s_rtSlotUsed[tex->rtSlot] = false;
			return;
		}
		auto it = std::find(s_allTextures.begin(), s_allTextures.end(), tex);
		if (it != s_allTextures.end()) s_allTextures.erase(it);
		DestroyTextureObj(tex);
	}

	static rrbool RADLINK gdraw_vk_TryToLockTexture(GDrawTexture *t, void *, GDrawStats *)
	{
		return t != NULL ? 1 : 0;
	}

	static rrbool RADLINK gdraw_vk_MakeVertexBufferBegin(void *unique_id, gdraw_vformat vformat, S32 vdata_len, S32 idata_len, GDraw_MakeVertexBuffer_ProcessingInfo *info, GDrawStats *)
	{
		if (!info || vdata_len <= 0 || idata_len <= 0) return 0;
		PendingVBBuild *pv = (PendingVBBuild*)calloc(1, sizeof(PendingVBBuild));
		pv->vformat = vformat;
		pv->vlen = vdata_len;
		pv->ilen = idata_len;
		pv->uniqueId = unique_id;
		pv->vdata = (uint8_t*)malloc((size_t)vdata_len);
		pv->idata = (uint8_t*)malloc((size_t)idata_len);
		if (!pv->vdata || !pv->idata)
		{
			free(pv->vdata); free(pv->idata); free(pv);
			return 0;
		}
		memset(info, 0, sizeof(*info));
		info->vertex_data = pv->vdata;
		info->index_data = pv->idata;
		info->vertex_data_length = vdata_len;
		info->index_data_length = idata_len;
		info->p0 = pv;
		return 1;
	}

	static rrbool RADLINK gdraw_vk_MakeVertexBufferMore(GDraw_MakeVertexBuffer_ProcessingInfo *) { return 0; }

	static GDrawVertexBuffer * RADLINK gdraw_vk_MakeVertexBufferEnd(GDraw_MakeVertexBuffer_ProcessingInfo *info, GDrawStats *stats)
	{
		if (!info || !info->p0) return NULL;
		PendingVBBuild *pv = (PendingVBBuild*)info->p0;
		GDrawVulkanVB *vb = new GDrawVulkanVB();
		vb->vsize = pv->vlen;
		vb->isize = pv->ilen;
		vb->uniqueId = pv->uniqueId;

		void *vp = NULL, *ip = NULL;
		if (!CreateHostVisibleBuffer((VkDeviceSize)pv->vlen, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vb->vbuf, vb->vmem, &vp) ||
			!CreateHostVisibleBuffer((VkDeviceSize)pv->ilen, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vb->ibuf, vb->imem, &ip))
		{
			DestroyVBObj(vb);
			free(pv->vdata); free(pv->idata); free(pv);
			return NULL;
		}
		memcpy(vp, pv->vdata, (size_t)pv->vlen);
		memcpy(ip, pv->idata, (size_t)pv->ilen);
		s_host->vk->vkUnmapMemory(s_host->device, vb->vmem);
		s_host->vk->vkUnmapMemory(s_host->device, vb->imem);

		s_allVBs.push_back(vb);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_alloc_tex;
			stats->alloc_tex += 1;
			stats->alloc_tex_bytes += (U32)(pv->vlen + pv->ilen);
		}
		free(pv->vdata); free(pv->idata); free(pv);
		return (GDrawVertexBuffer*)vb;
	}

	static rrbool RADLINK gdraw_vk_TryToLockVertexBuffer(GDrawVertexBuffer *vb, void *, GDrawStats *) { return vb != NULL ? 1 : 0; }

	static void RADLINK gdraw_vk_FreeVertexBuffer(GDrawVertexBuffer *buf, void *, GDrawStats *)
	{
		GDrawVulkanVB *vb = (GDrawVulkanVB*)buf;
		if (!vb) return;
		auto it = std::find(s_allVBs.begin(), s_allVBs.end(), vb);
		if (it != s_allVBs.end()) s_allVBs.erase(it);
		DestroyVBObj(vb);
	}

	static void RADLINK gdraw_vk_UnlockHandles(GDrawStats *) {}

	static void RADLINK gdraw_vk_SetTextureUniqueID(GDrawTexture *t, void *, void *new_id)
	{
		GDrawVulkanTex *tex = (GDrawVulkanTex*)t;
		if (tex) tex->uniqueId = new_id;
	}

	static VkImageLayout s_rtDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	static void TransitionVKImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkFlags aspect)
	{
		if (!cmd || !image || oldLayout == newLayout) return;
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.oldLayout = oldLayout;
		b.newLayout = newLayout;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = { aspect, 0, 1, 0, 1 };

		VkFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}

		if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		s_host->vk->vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
	}

	static void DestroyRTPoolResources(void)
	{
		if (!s_host || !s_host->device) return;
		VkDispatchTable *vk = s_host->vk;
		if (vk->vkDeviceWaitIdle) vk->vkDeviceWaitIdle(s_host->device);
		for (int i = 0; i < GDRAW_VK_MAX_STACK; ++i)
		{
			GDrawVulkanTex *rt = s_rtPool[i];
			if (!rt) continue;
			if (rt->fb) { vk->vkDestroyFramebuffer(s_host->device, rt->fb, NULL); rt->fb = VK_NULL_HANDLE; }
			if (rt->view) { vk->vkDestroyImageView(s_host->device, rt->view, NULL); rt->view = VK_NULL_HANDLE; }
			if (rt->image) { vk->vkDestroyImage(s_host->device, rt->image, NULL); rt->image = VK_NULL_HANDLE; }
			if (rt->mem) { vk->vkFreeMemory(s_host->device, rt->mem, NULL); rt->mem = VK_NULL_HANDLE; }
			delete rt;
			s_rtPool[i] = NULL;
			s_rtSlotUsed[i] = false;
		}
		if (s_rtDepthView) { vk->vkDestroyImageView(s_host->device, s_rtDepthView, NULL); s_rtDepthView = VK_NULL_HANDLE; }
		if (s_rtDepthImage) { vk->vkDestroyImage(s_host->device, s_rtDepthImage, NULL); s_rtDepthImage = VK_NULL_HANDLE; }
		if (s_rtDepthMem) { vk->vkFreeMemory(s_host->device, s_rtDepthMem, NULL); s_rtDepthMem = VK_NULL_HANDLE; }
		s_rtDepthW = 0;
		s_rtDepthH = 0;
		s_rtDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	static bool EnsureRTDepth(S32 w, S32 h)
	{
		if (!s_host || !s_host->device) return false;
		if (s_rtDepthImage && (s_rtDepthW != w || s_rtDepthH != h))
		{
			DestroyRTPoolResources();
		}
		if (s_rtDepthImage) return true;
		VkDispatchTable *vk = s_host->vk;
		VkImageCreateInfo ici = {};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = s_host->depthStencilFormat;
		ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vk->vkCreateImage(s_host->device, &ici, NULL, &s_rtDepthImage) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		vk->vkGetImageMemoryRequirements(s_host->device, s_rtDepthImage, &mr);
		uint32_t mt = s_host->FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt };
		if (vk->vkAllocateMemory(s_host->device, &mai, NULL, &s_rtDepthMem) != VK_SUCCESS) return false;
		vk->vkBindImageMemory(s_host->device, s_rtDepthImage, s_rtDepthMem, 0);

		VkImageViewCreateInfo ivci = {};
		ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivci.image = s_rtDepthImage;
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = s_host->depthStencilFormat;
		ivci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
		if (vk->vkCreateImageView(s_host->device, &ivci, NULL, &s_rtDepthView) != VK_SUCCESS) return false;
		s_rtDepthW = w;
		s_rtDepthH = h;
		s_rtDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		return true;
	}

	static GDrawVulkanTex *AcquirePooledRT(S32 w, S32 h)
	{
		int slot = -1;
		for (int i = 0; i < GDRAW_VK_MAX_STACK; ++i)
		{
			if (!s_rtSlotUsed[i]) { slot = i; break; }
		}
		if (slot < 0) return NULL;
		if (!EnsureRTDepth(w, h)) return NULL;
		GDrawVulkanTex *rt = s_rtPool[slot];
		if (!rt)
		{
			VkDispatchTable *vk = s_host->vk;
			rt = new GDrawVulkanTex();
			rt->width = w;
			rt->height = h;
			rt->isRT = true;
			rt->rtSlot = slot;
			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = s_host->colorFormat;
			ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
			ici.mipLevels = 1;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (vk->vkCreateImage(s_host->device, &ici, NULL, &rt->image) != VK_SUCCESS) { delete rt; return NULL; }
			VkMemoryRequirements mr = {};
			vk->vkGetImageMemoryRequirements(s_host->device, rt->image, &mr);
			uint32_t mt = s_host->FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt };
			if (vk->vkAllocateMemory(s_host->device, &mai, NULL, &rt->mem) != VK_SUCCESS) { delete rt; return NULL; }
			vk->vkBindImageMemory(s_host->device, rt->image, rt->mem, 0);

			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = rt->image;
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = s_host->colorFormat;
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (vk->vkCreateImageView(s_host->device, &ivci, NULL, &rt->view) != VK_SUCCESS) { delete rt; return NULL; }

			VkImageView views[2] = { rt->view, s_rtDepthView };
			VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, s_host->renderPass, 2, views, (uint32_t)w, (uint32_t)h, 1 };
			if (vk->vkCreateFramebuffer(s_host->device, &fbci, NULL, &rt->fb) != VK_SUCCESS) { delete rt; return NULL; }
			rt->layout = VK_IMAGE_LAYOUT_UNDEFINED;
			s_rtPool[slot] = rt;
		}
		s_rtSlotUsed[slot] = true;
		return rt;
	}

	static void BeginRTRenderPass(GDrawVulkanTex *rt, VkImageLayout parentTransitionTarget)
	{
		(void)parentTransitionTarget;
		VkDispatchTable *vk = s_host->vk;
		VkCommandBuffer cmd = s_host->activeCmd;
		if (s_rtDepthLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			TransitionVKImageLayout(cmd, s_rtDepthImage, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
			s_rtDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		if (rt->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			TransitionVKImageLayout(cmd, rt->image, rt->layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
			rt->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		VkRenderPassBeginInfo rpbi = {};
		rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpbi.renderPass = s_host->renderPass;
		rpbi.framebuffer = rt->fb;
		rpbi.renderArea = { { 0, 0 }, { (uint32_t)rt->width, (uint32_t)rt->height } };
		vk->vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	}

	static rrbool RADLINK gdraw_vk_TextureDrawBufferBegin(gswf_recti *reg, gdraw_texture_format, U32, void *owner, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || s_rtDepth >= GDRAW_VK_MAX_STACK) return 0;
		S32 needW = std::max(s_frametexW, s_tpw);
		S32 needH = std::max(s_frametexH, s_tph);
		if (owner && reg)
		{
			needW = std::max(needW, reg->x1 - reg->x0);
			needH = std::max(needH, reg->y1 - reg->y0);
		}
		if (needW <= 0 || needH <= 0) return 0;
		if (needW > s_frametexW) s_frametexW = needW;
		if (needH > s_frametexH) s_frametexH = needH;

		if (s_rtDepth == 0) s_host->EndRenderPassIfActive();
		else s_host->vk->vkCmdEndRenderPass(s_host->activeCmd);

		GDrawVulkanTex *rt = AcquirePooledRT(s_frametexW, s_frametexH);
		if (!rt)
		{
			if (s_rtDepth == 0) s_host->EnsureRenderPassActive();
			else BeginRTRenderPass(s_rtStack[s_rtDepth - 1].tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			return 0;
		}

		RTStackEntry &e = s_rtStack[s_rtDepth++];
		e.tex = rt;
		e.cached = (owner != NULL);
		if (owner && reg)
		{
			e.baseX = reg->x0; e.baseY = reg->y0;
			e.width = reg->x1 - reg->x0; e.height = reg->y1 - reg->y0;
		}
		else
		{
			e.baseX = s_tx0p; e.baseY = s_ty0p;
			e.width = s_tpw; e.height = s_tph;
		}

		BeginRTRenderPass(rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		VkClearAttachment clears[2] = {};
		clears[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clears[0].colorAttachment = 0;
		clears[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		clears[1].clearValue.depthStencil = { 1.0f, 0 };
		VkClearRect cr = { { { 0, 0 }, { (uint32_t)rt->width, (uint32_t)rt->height } }, 0, 1 };
		VkRect2D sc = cr.rect;
		s_host->vk->vkCmdSetScissor(s_host->activeCmd, 0, 1, &sc);
		s_host->vk->vkCmdClearAttachments(s_host->activeCmd, (s_rtDepth == 1) ? 2u : 1u, clears, 1, &cr);

		UpdateProjection();
		ApplyViewport();
		if (stats)
		{
			stats->nonzero_flags |= (GDRAW_STATS_rendtarg | GDRAW_STATS_clears);
			stats->rendertarget_changes += 1;
			stats->num_clears += 1;
		}
		return 1;
	}

	static GDrawTexture * RADLINK gdraw_vk_TextureDrawBufferEnd(GDrawStats *stats)
	{
		if (s_rtDepth <= 0 || !s_host || !s_host->activeCmd) return NULL;
		VkDispatchTable *vk = s_host->vk;
		VkCommandBuffer cmd = s_host->activeCmd;
		GDrawVulkanTex *rt = s_rtStack[--s_rtDepth].tex;
		vk->vkCmdEndRenderPass(cmd);
		TransitionVKImageLayout(cmd, rt->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		rt->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		if (s_rtDepth == 0)
			s_host->EnsureRenderPassActive();
		else
			BeginRTRenderPass(s_rtStack[s_rtDepth - 1].tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		UpdateProjection();
		ApplyViewport();
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_rendtarg;
			stats->rendertarget_changes += 1;
		}
		return (GDrawTexture*)rt;
	}

	static void DrawFilterScreenQuadVK(const GDrawRenderState *r, const gswf_recti &s, const F32 tc[4],
		int psMode, int blendMode, const float (*blurTaps)[4], int numTaps, const float *blurClamp,
		const float *filterData, bool forceLinear, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || s.x1 <= s.x0 || s.y1 <= s.y0) return;
		EnsureGDrawRenderPassActive();
		int fi = s_host->frameIndex & 1;
		VkDispatchTable *vk = s_host->vk;
		if (s_descPoolResetFrame[fi] != s_host->frameSeq)
		{
			vk->vkResetDescriptorPool(s_host->device, s_descPools[fi], 0);
			s_descPoolResetFrame[fi] = s_host->frameSeq;
			s_rings[fi].vbOff = 0;
			s_rings[fi].uboOff = 0;
		}
		auto &ring = s_rings[fi];
		struct QuadV { float x, y, u, v; };
		const QuadV verts[4] = {
			{ (float)s.x0, (float)s.y0, tc[0], tc[1] },
			{ (float)s.x1, (float)s.y0, tc[2], tc[1] },
			{ (float)s.x1, (float)s.y1, tc[2], tc[3] },
			{ (float)s.x0, (float)s.y1, tc[0], tc[3] }
		};
		const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

		uint32_t voff = (ring.vbOff + 15u) & ~15u;
		uint32_t ioff = (voff + sizeof(verts) + 15u) & ~15u;
		if (ioff + sizeof(idx) > GDRAW_VK_RING_BYTES) return;
		memcpy((uint8_t*)ring.vbPtr + voff, verts, sizeof(verts));
		memcpy((uint8_t*)ring.vbPtr + ioff, idx, sizeof(idx));
		ring.vbOff = ioff + sizeof(idx);

		uint32_t uboAligned = (ring.uboOff + GDRAW_VK_UBO_STRIDE - 1u) & ~(GDRAW_VK_UBO_STRIDE - 1u);
		if (uboAligned + GDRAW_VK_UBO_STRIDE > GDRAW_VK_MAX_DRAWS_FRAME * GDRAW_VK_UBO_STRIDE) return;
		GDrawUBOData *ud = (GDrawUBOData*)((uint8_t*)ring.uboPtr + uboAligned);
		memset(ud, 0, sizeof(*ud));
		ud->world0[0] = 1.0f; ud->world0[2] = 0.0f;
		ud->world1[1] = 1.0f;
		memcpy(ud->viewproj, s_projection, sizeof(float) * 4);
		ud->tex0_mode = GDRAW_TEXTURE_normal;
		ud->has_tex0 = 1;
		if (r)
		{
			memcpy(ud->color_mul, r->color, sizeof(float) * 4);
			if (r->cxf_add && psMode == 0)
			{
				ud->color_add[0] = r->cxf_add[0] / 255.0f;
				ud->color_add[1] = r->cxf_add[1] / 255.0f;
				ud->color_add[2] = r->cxf_add[2] / 255.0f;
				ud->color_add[3] = r->cxf_add[3] / 255.0f;
				ud->additive_mode = r->cxf_add[3] ? 2 : 1;
			}
		}
		else
		{
			ud->color_mul[0] = ud->color_mul[1] = ud->color_mul[2] = ud->color_mul[3] = 1.0f;
		}
		if (psMode == 1 && blurTaps)
		{
			int n = std::min(numTaps, 9);
			memcpy(ud->blur_taps, blurTaps, (size_t)n * sizeof(float) * 4);
		}
		else if (psMode == 2 && r && r->shader_data)
		{
			for (int i = 0; i < 5; ++i) memcpy(ud->blur_taps[i], &r->shader_data[i * 4], sizeof(float) * 4);
		}
		else if (psMode == 3 && r && r->shader_data)
		{
			memcpy(ud->color_mul, r->shader_data, sizeof(float) * 4);
		}
		if (blurClamp) memcpy(ud->blur_clamp, blurClamp, sizeof(float) * 4);
		else { ud->blur_clamp[0] = 0.0f; ud->blur_clamp[1] = 0.0f; ud->blur_clamp[2] = 1.0f; ud->blur_clamp[3] = 1.0f; }
		if (filterData) memcpy(ud->filter_data, filterData, sizeof(float) * 4);
		ring.uboOff = uboAligned + GDRAW_VK_UBO_STRIDE;

		VkDescriptorSet dset = VK_NULL_HANDLE;
		VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, s_descPools[fi], 1, &s_dsl };
		if (vk->vkAllocateDescriptorSets(s_host->device, &dsai, &dset) != VK_SUCCESS) return;

		GDrawVulkanTex *t0 = r ? (GDrawVulkanTex*)r->tex[0] : NULL;
		GDrawVulkanTex *t1 = r ? (GDrawVulkanTex*)r->tex[1] : NULL;
		VkImageView iv0 = (t0 && t0->view) ? t0->view : s_whiteTex->view;
		VkImageView iv1 = (psMode == 3 && t1 && t1->view) ? t1->view : ((s_aaTex && s_aaTex->view) ? s_aaTex->view : s_whiteTex->view);
		VkSampler smp0 = s_samplers[forceLinear ? 0 : 1][0];
		VkSampler smp1 = s_samplers[0][0];

		VkDescriptorBufferInfo bi = { ring.ubo, uboAligned, sizeof(GDrawUBOData) };
		VkDescriptorImageInfo ii0 = { smp0, iv0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo ii1 = { smp1, iv1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet writes[3] = {
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii0, NULL, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii1, NULL, NULL },
		};
		vk->vkUpdateDescriptorSets(s_host->device, 3, writes, 0, NULL);

		GDrawPipelineKey pk = { GDRAW_vformat_v2tc2, blendMode, 0, 0, 0, 0, psMode };
		VkPipeline pipe = GetOrCreatePipeline(pk);
		if (!pipe) return;

		ApplyViewport();
		uint32_t sw = (s_rtDepth > 0 || s_inBlur) ? (uint32_t)s_frametexW : (uint32_t)s_host->width;
		uint32_t sh = (s_rtDepth > 0 || s_inBlur) ? (uint32_t)s_frametexH : (uint32_t)s_host->height;
		VkRect2D scissor = { { 0, 0 }, { sw, sh } };
		vk->vkCmdSetScissor(s_host->activeCmd, 0, 1, &scissor);
		vk->vkCmdBindPipeline(s_host->activeCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
		vk->vkCmdBindDescriptorSets(s_host->activeCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_plLayout, 0, 1, &dset, 0, NULL);
		VkDeviceSize voffSz = voff;
		vk->vkCmdBindVertexBuffers(s_host->activeCmd, 0, 1, &ring.vb, &voffSz);
		vk->vkCmdBindIndexBuffer(s_host->activeCmd, ring.vb, ioff, VK_INDEX_TYPE_UINT16);
		vk->vkCmdDrawIndexed(s_host->activeCmd, 6u, 1u, 0u, 0, 0u);
		if (stats)
		{
			stats->nonzero_flags |= GDRAW_STATS_batches;
			stats->num_batches += 1;
			stats->drawn_indices += 6;
			stats->drawn_vertices += 4;
		}
	}

	static void ExpandRectClampedVK(gswf_recti &dst, const gswf_recti &src, S32 ex, S32 ey, S32 maxW, S32 maxH)
	{
		dst.x0 = std::max<S32>(0, src.x0 - ex);
		dst.y0 = std::max<S32>(0, src.y0 - ey);
		dst.x1 = std::min<S32>(maxW, src.x1 + ex);
		dst.y1 = std::min<S32>(maxH, src.y1 + ey);
	}

	static void RunBlurAxisVK(int axis, GDrawRenderState *r, F32 blurWidth, F32 texel,
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
		ExpandRectClampedVK(drawBounds, drawBounds, axis ? 0 : expand, axis ? expand : 0, s_tpw, s_tph);
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
		if (!gdraw_vk_TextureDrawBufferBegin(&region, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats)) return;
		DrawFilterScreenQuadVK(r, drawBounds, tc, 1, GDRAW_BLEND_none, data, taps, clampv, NULL, true, stats);
		GDrawTexture *outTex = gdraw_vk_TextureDrawBufferEnd(stats);
		if (r->tex[0] && r->tex[0] != protect && r->tex[0] != outTex)
			gdraw_vk_FreeTexture(r->tex[0], NULL, stats);
		r->tex[0] = outTex;
		ExpandRectClampedVK(sampleBounds, drawBounds, 1, 1, s_tpw, s_tph);
	}

	static void RADLINK gdraw_vk_FilterQuad(GDrawRenderState *r, S32 x0, S32 y0, S32 x1, S32 y1, GDrawStats *stats)
	{
		if (!s_host || !s_host->activeCmd || !r) return;
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
				ExpandRectClampedVK(sb, sb, 1, 1, s_tpw, s_tph);
				GDrawTexture *protect = r->tex[0];
				const F32 dx = 1.0f / (F32)s_frametexW;
				const F32 dy = 1.0f / (F32)s_frametexH;
				s_inBlur = true;
				for (S32 p = 0; p < r->blur_passes; ++p)
				{
					RunBlurAxisVK(0, r, r->blur_x, dx, db, sb, protect, stats);
					RunBlurAxisVK(1, r, r->blur_y, dy, db, sb, protect, stats);
				}
				s_inBlur = false;
				UpdateProjection();
				ApplyViewport();
			}
			else if (r->filter == GDRAW_FILTER_colormatrix)
			{
				GDrawTexture *oldTex = r->tex[0];
				if (gdraw_vk_TextureDrawBufferBegin(&s, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats))
				{
					DrawFilterScreenQuadVK(r, s, tc, 2, GDRAW_BLEND_none, NULL, 0, NULL, NULL, true, stats);
					GDrawTexture *outTex = gdraw_vk_TextureDrawBufferEnd(stats);
					if (oldTex && oldTex != outTex) gdraw_vk_FreeTexture(oldTex, NULL, stats);
					r->tex[0] = outTex;
				}
			}
			else if (r->filter == GDRAW_FILTER_dropshadow || r->filter == GDRAW_FILTER_bevel)
			{
				GDrawTexture *oldTex0 = r->tex[0];
				GDrawTexture *oldTex1 = r->tex[1];
				if (gdraw_vk_TextureDrawBufferBegin(&s, GDRAW_TEXTURE_FORMAT_rgba32, 0, NULL, stats))
				{
					const float fdata[4] = {
						r->shader_data ? (-r->shader_data[4] / (F32)s_frametexW) : 0.0f,
						r->shader_data ? (-r->shader_data[5] / (F32)s_frametexH) : 0.0f,
						r->shader_data ? r->shader_data[6] : 1.0f,
						0.0f
					};
					DrawFilterScreenQuadVK(r, s, tc, 3, GDRAW_BLEND_none, NULL, 0, NULL, fdata, true, stats);
					GDrawTexture *outTex = gdraw_vk_TextureDrawBufferEnd(stats);
					if (oldTex0 && oldTex0 != outTex) gdraw_vk_FreeTexture(oldTex0, NULL, stats);
					if (oldTex1 && oldTex1 != oldTex0 && oldTex1 != outTex) gdraw_vk_FreeTexture(oldTex1, NULL, stats);
					r->tex[0] = outTex;
				}
			}
		}
		else
		{
			DrawFilterScreenQuadVK(r, s, tc, 0, (int)r->blend_mode, NULL, 0, NULL, NULL, (r->nearest0 == 0), stats);
		}
		s_host->InvalidateWorldPipelineCache();
	}

	static void RADLINK gdraw_vk_DescribeTexture(GDrawTexture *t, GDraw_Texture_Description *desc)
	{
		GDrawVulkanTex *tex = (GDrawVulkanTex*)t;
		if (!desc || !tex) return;
		desc->width = tex->width;
		desc->height = tex->height;
		desc->size_in_bytes = (U32)(tex->width * tex->height * 4);
	}

	static void RADLINK gdraw_vk_DescribeVertexBuffer(GDrawVertexBuffer *b, GDraw_VertexBuffer_Description *desc)
	{
		GDrawVulkanVB *vb = (GDrawVulkanVB*)b;
		if (!desc || !vb) return;
		desc->size_in_bytes = vb->vsize + vb->isize;
	}

	static GDrawTexture * RADLINK gdraw_vk_MakeTextureFromResource(U8 *, S32, void *) { return NULL; }
	static void RADLINK gdraw_vk_DestroyTextureFromResource(GDrawTexture *t) { gdraw_vk_FreeTexture(t, NULL, NULL); }
}

GDrawFunctions *gdraw_VK_CreateContext(VkBackendGDrawHost *host, S32 w, S32 h)
{
	s_host = host;
	s_fw = w > 0 ? w : 1280;
	s_fh = h > 0 ? h : 720;
	s_frametexW = s_fw;
	s_frametexH = s_fh;
	s_frameEpoch = 1;

	VkDispatchTable *vk = host->vk;
	auto makeMod = [&](const uint32_t *code, size_t sz) -> VkShaderModule {
		VkShaderModuleCreateInfo sm = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sz, code };
		VkShaderModule mod = VK_NULL_HANDLE;
		vk->vkCreateShaderModule(host->device, &sm, NULL, &mod);
		return mod;
	};
	s_smBasic    = makeMod(spv_gdraw_basic_vert, spv_gdraw_basic_vert_size);
	s_smAA       = makeMod(spv_gdraw_aa_vert, spv_gdraw_aa_vert_size);
	s_smTC2      = makeMod(spv_gdraw_tc2_vert, spv_gdraw_tc2_vert_size);
	s_smFrag     = makeMod(spv_gdraw_ui_frag, spv_gdraw_ui_frag_size);
	s_smBlurFrag = makeMod(spv_gdraw_blur_frag, spv_gdraw_blur_frag_size);
	s_smCMFrag   = makeMod(spv_gdraw_colormatrix_frag, spv_gdraw_colormatrix_frag_size);
	s_smDSFrag   = makeMod(spv_gdraw_dropshadow_frag, spv_gdraw_dropshadow_frag_size);

	VkDescriptorSetLayoutBinding bindings[3] = {
		{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
		{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
	};
	VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, bindings };
	vk->vkCreateDescriptorSetLayout(host->device, &dslci, NULL, &s_dsl);

	VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &s_dsl, 0, NULL };
	vk->vkCreatePipelineLayout(host->device, &plci, NULL, &s_plLayout);

	VkDescriptorPoolSize poolSizes[2] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, GDRAW_VK_MAX_DRAWS_FRAME },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GDRAW_VK_MAX_DRAWS_FRAME * 2u },
	};
	VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, GDRAW_VK_MAX_DRAWS_FRAME, 2, poolSizes };

	for (int f = 0; f < 2; ++f)
	{
		vk->vkCreateDescriptorPool(host->device, &dpci, NULL, &s_descPools[f]);
		s_descPoolResetFrame[f] = 0xFFFFFFFFu;
		CreateHostVisibleBuffer(GDRAW_VK_RING_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			s_rings[f].vb, s_rings[f].vbMem, &s_rings[f].vbPtr);
		CreateHostVisibleBuffer(GDRAW_VK_MAX_DRAWS_FRAME * GDRAW_VK_UBO_STRIDE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			s_rings[f].ubo, s_rings[f].uboMem, &s_rings[f].uboPtr);
	}

	const VkSamplerAddressMode wrapModes[3] = {
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
	};
	for (int f = 0; f < 2; ++f)
	{
		for (int a = 0; a < 3; ++a)
		{
			VkSamplerCreateInfo sci = {};
			sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sci.magFilter = (f == 0) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.minFilter = (f == 0) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.mipmapMode = (f == 0) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sci.addressModeU = wrapModes[a];
			sci.addressModeV = wrapModes[a];
			sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.maxLod = 1.0f;
			vk->vkCreateSampler(host->device, &sci, NULL, &s_samplers[f][a]);
		}
	}

	const uint8_t whitePx[4] = { 255, 255, 255, 255 };
	s_whiteTex = new GDrawVulkanTex();
	CreateTexture2D(1, 1, whitePx, s_whiteTex);

	memset(&s_funcs, 0, sizeof(s_funcs));
	s_funcs.GetInfo                 = gdraw_vk_GetInfo;
	s_funcs.SetViewSizeAndWorldScale= gdraw_vk_SetViewSizeAndWorldScale;
	s_funcs.RenderTileBegin         = gdraw_vk_RenderTileBegin;
	s_funcs.RenderTileEnd           = gdraw_vk_RenderTileEnd;
	s_funcs.SetAntialiasTexture     = gdraw_vk_SetAntialiasTexture;
	s_funcs.ClearStencilBits        = gdraw_vk_ClearStencilBits;
	s_funcs.ClearID                 = gdraw_vk_ClearID;
	s_funcs.FilterQuad              = gdraw_vk_FilterQuad;
	s_funcs.DrawIndexedTriangles    = gdraw_vk_DrawIndexedTriangles;
	s_funcs.MakeTextureBegin        = gdraw_vk_MakeTextureBegin;
	s_funcs.MakeTextureMore         = NULL;
	s_funcs.MakeTextureEnd          = gdraw_vk_MakeTextureEnd;
	s_funcs.MakeVertexBufferBegin   = gdraw_vk_MakeVertexBufferBegin;
	s_funcs.MakeVertexBufferMore    = gdraw_vk_MakeVertexBufferMore;
	s_funcs.MakeVertexBufferEnd     = gdraw_vk_MakeVertexBufferEnd;
	s_funcs.TryToLockTexture        = gdraw_vk_TryToLockTexture;
	s_funcs.TryToLockVertexBuffer   = gdraw_vk_TryToLockVertexBuffer;
	s_funcs.UnlockHandles           = gdraw_vk_UnlockHandles;
	s_funcs.FreeTexture             = gdraw_vk_FreeTexture;
	s_funcs.FreeVertexBuffer        = gdraw_vk_FreeVertexBuffer;
	s_funcs.UpdateTextureBegin      = gdraw_vk_UpdateTextureBegin;
	s_funcs.UpdateTextureRect       = gdraw_vk_UpdateTextureRect;
	s_funcs.UpdateTextureEnd        = gdraw_vk_UpdateTextureEnd;
	s_funcs.TextureDrawBufferBegin  = gdraw_vk_TextureDrawBufferBegin;
	s_funcs.TextureDrawBufferEnd    = gdraw_vk_TextureDrawBufferEnd;
	s_funcs.DescribeTexture         = gdraw_vk_DescribeTexture;
	s_funcs.DescribeVertexBuffer    = gdraw_vk_DescribeVertexBuffer;
	s_funcs.SetTextureUniqueID      = gdraw_vk_SetTextureUniqueID;
	s_funcs.RenderingBegin          = gdraw_vk_RenderingBegin;
	s_funcs.RenderingEnd            = gdraw_vk_RenderingEnd;
	s_funcs.MakeTextureFromResource = gdraw_vk_MakeTextureFromResource;
	s_funcs.FreeTextureFromResource = gdraw_vk_DestroyTextureFromResource;
	s_funcs.Set3DTransform          = gdraw_vk_Set3DTransform;
	return &s_funcs;
}

void gdraw_VK_DestroyContext(void)
{
	if (!s_host || !s_host->device) return;
	VkDispatchTable *vk = s_host->vk;
	vk->vkDeviceWaitIdle(s_host->device);

	for (auto *t : s_allTextures) DestroyTextureObj(t);
	s_allTextures.clear();
	for (auto *vb : s_allVBs) DestroyVBObj(vb);
	s_allVBs.clear();

	if (s_aaTex)    { DestroyTextureObj(s_aaTex); s_aaTex = NULL; }
	if (s_whiteTex) { DestroyTextureObj(s_whiteTex); s_whiteTex = NULL; }

	for (auto &kv : s_pipes) if (kv.second) vk->vkDestroyPipeline(s_host->device, kv.second, NULL);
	s_pipes.clear();

	for (int f = 0; f < 2; ++f)
	{
		for (int a = 0; a < 3; ++a)
		{
			if (s_samplers[f][a]) { vk->vkDestroySampler(s_host->device, s_samplers[f][a], NULL); s_samplers[f][a] = VK_NULL_HANDLE; }
		}
		if (s_descPools[f]) { vk->vkDestroyDescriptorPool(s_host->device, s_descPools[f], NULL); s_descPools[f] = VK_NULL_HANDLE; }
		if (s_rings[f].vbPtr) { vk->vkUnmapMemory(s_host->device, s_rings[f].vbMem); s_rings[f].vbPtr = NULL; }
		if (s_rings[f].vb)    { vk->vkDestroyBuffer(s_host->device, s_rings[f].vb, NULL); s_rings[f].vb = VK_NULL_HANDLE; }
		if (s_rings[f].vbMem) { vk->vkFreeMemory(s_host->device, s_rings[f].vbMem, NULL); s_rings[f].vbMem = VK_NULL_HANDLE; }
		if (s_rings[f].uboPtr){ vk->vkUnmapMemory(s_host->device, s_rings[f].uboMem); s_rings[f].uboPtr = NULL; }
		if (s_rings[f].ubo)   { vk->vkDestroyBuffer(s_host->device, s_rings[f].ubo, NULL); s_rings[f].ubo = VK_NULL_HANDLE; }
		if (s_rings[f].uboMem){ vk->vkFreeMemory(s_host->device, s_rings[f].uboMem, NULL); s_rings[f].uboMem = VK_NULL_HANDLE; }
	}

	DestroyRTPoolResources();

	if (s_plLayout)   { vk->vkDestroyPipelineLayout(s_host->device, s_plLayout, NULL); s_plLayout = VK_NULL_HANDLE; }
	if (s_dsl)        { vk->vkDestroyDescriptorSetLayout(s_host->device, s_dsl, NULL); s_dsl = VK_NULL_HANDLE; }
	if (s_smBasic)    { vk->vkDestroyShaderModule(s_host->device, s_smBasic, NULL); s_smBasic = VK_NULL_HANDLE; }
	if (s_smAA)       { vk->vkDestroyShaderModule(s_host->device, s_smAA, NULL); s_smAA = VK_NULL_HANDLE; }
	if (s_smTC2)      { vk->vkDestroyShaderModule(s_host->device, s_smTC2, NULL); s_smTC2 = VK_NULL_HANDLE; }
	if (s_smFrag)     { vk->vkDestroyShaderModule(s_host->device, s_smFrag, NULL); s_smFrag = VK_NULL_HANDLE; }
	if (s_smBlurFrag) { vk->vkDestroyShaderModule(s_host->device, s_smBlurFrag, NULL); s_smBlurFrag = VK_NULL_HANDLE; }
	if (s_smCMFrag)   { vk->vkDestroyShaderModule(s_host->device, s_smCMFrag, NULL); s_smCMFrag = VK_NULL_HANDLE; }
	if (s_smDSFrag)   { vk->vkDestroyShaderModule(s_host->device, s_smDSFrag, NULL); s_smDSFrag = VK_NULL_HANDLE; }
	s_host = NULL;
}

void gdraw_VK_SetRendertargetSize(S32 w, S32 h)
{
	s_fw = w > 0 ? w : 1;
	s_fh = h > 0 ? h : 1;
	s_frametexW = std::max(s_frametexW, s_fw);
	s_frametexH = std::max(s_frametexH, s_fh);
}

void gdraw_VK_SetTileOrigin(S32 x, S32 y)
{
	s_vx = x;
	s_vy = y;
}

void gdraw_VK_NoMoreGDrawThisFrame(void)
{
	++s_frameEpoch;
	if (s_host) s_host->InvalidateWorldPipelineCache();
}

void gdraw_VK_BeginCustomDraw_4J(IggyCustomDrawCallbackRegion *region, F32 mat[16])
{
	if (!region || !mat) return;
	ComputeObjectSpaceMatrix(mat, region->o2w, s_projection, DepthFromId(0), 0);
	if (s_host) s_host->InvalidateWorldPipelineCache();
}

void gdraw_VK_CalculateCustomDraw_4J(IggyCustomDrawCallbackRegion *region, F32 mat[16])
{
	if (!region || !mat) return;
	ComputeObjectSpaceMatrix(mat, region->o2w, s_projection, DepthFromId(0), 0);
}

void gdraw_VK_EndCustomDraw(IggyCustomDrawCallbackRegion *)
{
	ApplyViewport();
	if (s_host) s_host->InvalidateWorldPipelineCache();
}

void gdraw_VK_SetViewport_4J(void)
{
	ApplyViewport();
}

GDrawTexture *gdraw_VK_WrappedTextureCreate(int textureId)
{
	if (!s_host || !s_host->GetMCImageView) return NULL;
	int w = 0, h = 0;
	bool lin = false, clmp = false;
	VkImageView iv = (VkImageView)s_host->GetMCImageView(textureId, &w, &h, &lin, &clmp);
	if (!iv) return NULL;
	GDrawVulkanTex *tex = new GDrawVulkanTex();
	tex->view = iv;
	tex->width = w;
	tex->height = h;
	tex->isWrapped = true;
	return (GDrawTexture*)tex;
}

void gdraw_VK_WrappedTextureDestroy(GDrawTexture *tex)
{
	DestroyTextureObj((GDrawVulkanTex*)tex);
}
