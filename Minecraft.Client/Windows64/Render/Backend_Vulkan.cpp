#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include <algorithm>

#include "RenderCommon.h"
#include "vk_minimal.h"
#include "vk_shaders_spv.h"
#include "vk_backend_context.h"

using namespace C4JRenderCommon;

#define C4J_VK_FRAMES_IN_FLIGHT         2
#define C4J_VK_RING_VB_BYTES            (16u * 1024u * 1024u)
#define C4J_VK_UBO_PS_OFFSET            512u /* 256B-aligned offset; VS UBO <= 512B, PS UBO <= 256B */
#define C4J_VK_UBO_STRIDE               768u /* 512B VS + 256B PS per draw slot (256B-aligned) */
#define C4J_VK_UBO_CHUNK_DRAWS          32768u /* 24 MiB per growable UBO chunk, no hard draw cap */
#define C4J_VK_FRAME_STAGE_BYTES        (32u * 1024u * 1024u)
#define C4J_VK_FRAME_CBUFF_STAGE_BUDGET (24u * 1024u * 1024u)
#define C4J_VK_VB_SLAB_BYTES            (32u * 1024u * 1024u)
#define C4J_VK_MAX_DESC_SETS_PER_POOL   4096u
#define C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ((VkDescriptorType)8)

namespace
{
	static HWND     s_hwnd = NULL;
	static int      s_width = 1280;
	static int      s_height = 720;
	static bool     s_fullscreen = false;
	static bool     s_vsync = false;
	static bool     s_suspended = false;
	static char     s_adapterName[128] = "Vulkan Hardware";

	static VkDispatchTable                  s_vk;
	static VkInstance                       s_instance = VK_NULL_HANDLE;
	static VkSurfaceKHR                     s_surface = VK_NULL_HANDLE;
	static VkPhysicalDevice                 s_physDev = VK_NULL_HANDLE;
	static VkPhysicalDeviceMemoryProperties s_memProps = {};
	static uint32_t                         s_gfxFamily = 0;
	static VkDevice                         s_device = VK_NULL_HANDLE;
	static VkQueue                          s_queue = VK_NULL_HANDLE;

	static VkSwapchainKHR                   s_swapchain = VK_NULL_HANDLE;
	static VkFormat                         s_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
	static VkFormat                         s_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
	static std::vector<VkImage>             s_swapImages;
	static std::vector<VkImageView>         s_swapViews;
	static std::vector<VkImageLayout>       s_swapLayouts;
	static VkImage                          s_depthImage = VK_NULL_HANDLE;
	static VkDeviceMemory                   s_depthMem = VK_NULL_HANDLE;
	static VkImageView                      s_depthView = VK_NULL_HANDLE;
	static VkImageLayout                    s_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	static VkRenderPass                     s_renderPass = VK_NULL_HANDLE;
	static std::vector<VkFramebuffer>       s_framebuffers;
	static bool                             s_swapchainDirty = false;

	struct TexDescKey
	{
		VkImageView view0;
		VkImageView view1;
		uint32_t    smpIdx;
		bool operator==(const TexDescKey &o) const
		{
			return view0 == o.view0 && view1 == o.view1 && smpIdx == o.smpIdx;
		}
	};
	struct TexDescKeyHash
	{
		size_t operator()(const TexDescKey &k) const
		{
			size_t h = (size_t)(uintptr_t)k.view0 * 0x9E3779B97F4A7C15ULL;
			h ^= ((size_t)(uintptr_t)k.view1 + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
			h ^= ((size_t)k.smpIdx + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2));
			return h;
		}
	};

	struct FrameRingVBChunk
	{
		VkBuffer       buf = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		void          *ptr = NULL;
		uint32_t       offset = 0;
		uint32_t       capacity = 0;
	};

	struct FrameUBOChunk
	{
		VkBuffer       buf = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		void          *ptr = NULL;
		uint32_t       offset = 0;
		uint32_t       capacity = 0;
		std::unordered_map<TexDescKey, VkDescriptorSet, TexDescKeyHash> descCache;
	};

	struct FrameSync
	{
		VkCommandPool   cmdPool = VK_NULL_HANDLE;
		VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
		VkSemaphore     imgAvailSem = VK_NULL_HANDLE;
		VkSemaphore     renderDoneSem = VK_NULL_HANDLE;
		VkFence         inFlightFence = VK_NULL_HANDLE;

		std::vector<VkDescriptorPool> descPools;
		uint32_t        descPoolUsed = 0;
		uint64_t        texCacheEpoch = 0;

		std::vector<FrameRingVBChunk> ringVBs;
		size_t          activeRingVB = 0;

		std::vector<FrameUBOChunk> uboChunks;
		size_t          activeUBOChunk = 0;

		VkBuffer        stageBuf = VK_NULL_HANDLE;
		VkDeviceMemory  stageMem = VK_NULL_HANDLE;
		void           *stagePtr = NULL;
		uint32_t        stageOff = 0;
		bool            imgAvailPending = false;
	};
	static FrameSync s_frames[C4J_VK_FRAMES_IN_FLIGHT];
	static int       s_frameIdx = 0;
	static uint32_t  s_frameSeq = 1;
	static uint32_t  s_activeImgIdx = 0;
	static bool      s_frameActive = false;
	static bool      s_inRenderPass = false;
	static uint64_t  s_texCacheEpoch = 1;

	/* Transient upload command pool & staging buffer */
	static VkCommandPool   s_uploadPool = VK_NULL_HANDLE;
	static VkFence         s_uploadFence = VK_NULL_HANDLE;
	static VkBuffer        s_stageBuf = VK_NULL_HANDLE;
	static VkDeviceMemory  s_stageMem = VK_NULL_HANDLE;
	static void           *s_stagePtr = NULL;
	static VkDeviceSize    s_stageSize = 0;

	/* World pipeline resources */
	static VkShaderModule        s_smMcStd = VK_NULL_HANDLE;
	static VkShaderModule        s_smMcComp = VK_NULL_HANDLE;
	static VkShaderModule        s_smMcFrag = VK_NULL_HANDLE;
	static VkShaderModule        s_smCustomStd = VK_NULL_HANDLE;
	static VkShaderModule        s_smCustomComp = VK_NULL_HANDLE;
	static VkShaderModule        s_smCustomFrag = VK_NULL_HANDLE;
	static uint32_t              s_customShaderGen = 0;
	static int                   s_shaderMode = C4J_SHADER_MODE_DEFAULT;
	static C4JShaderFrameParams  s_shaderFrameParams = {};
	struct VKPSUniformBlock
	{
		PSConstantBufferData ps;
		C4JShaderFrameParams shader;
	};
	static_assert(sizeof(VKPSUniformBlock) <= 256, "VKPSUniformBlock exceeds 256-byte PS UBO slot");
	static VkDescriptorSetLayout s_worldDSL = VK_NULL_HANDLE;
	static VkPipelineLayout      s_worldPLLayout = VK_NULL_HANDLE;
	static VkSampler             s_samplers[4] = {};
	static VkBuffer              s_quadIB = VK_NULL_HANDLE;
	static VkDeviceMemory        s_quadIBMem = VK_NULL_HANDLE;
	static VkBuffer              s_fanIB = VK_NULL_HANDLE;
	static VkDeviceMemory        s_fanIBMem = VK_NULL_HANDLE;

	struct VKTextureData
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		int width = 0;
		int height = 0;
		int mipLevels = 1;
		bool linearFilter = false;
		bool clampWrap = false;
	};
	static VKTextureData s_defaultWhiteTex;

	/* DEVICE_LOCAL slab sub-allocator for static CBuff vertex buffers */
	struct VKSlabFreeBlock
	{
		VkDeviceSize offset;
		VkDeviceSize size;
	};
	struct VKVBSlab
	{
		VkBuffer buf = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		VkDeviceSize capacity = 0;
		std::vector<VKSlabFreeBlock> freeList;
	};
	struct VKSlabAlloc
	{
		size_t slabIdx = SIZE_MAX;
		VkBuffer buf = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
		VkDeviceSize size = 0;
	};
	struct PendingSlabFree
	{
		uint32_t retireSeq;
		size_t slabIdx;
		VkDeviceSize offset;
		VkDeviceSize size;
	};
	static std::vector<VKVBSlab> s_vbSlabs;
	static std::vector<PendingSlabFree> s_pendingSlabFrees;

	struct VKCBuffSlot
	{
		std::shared_ptr<const std::vector<CBuffCommand>> commands;
		size_t byteSize = 0;
		uint64_t generation = 0;
		uint64_t uploadedGen = 0;
		uint64_t queuedGen = 0;
		std::shared_ptr<std::vector<VKSlabAlloc>> gpuAllocs;
	};

	struct PendingCBuffUpload
	{
		int index;
		uint64_t generation;
	};
	static std::deque<PendingCBuffUpload> s_pendingCBuffUploads;

	struct PendingTexOp
	{
		int texId;
		VkImage image;
		uint32_t level;
		uint32_t mipLevels;
		int32_t xoff, yoff;
		uint32_t width, height;
		bool firstInitAllMips;
		std::vector<uint8_t> pixels;
	};
	static std::vector<PendingTexOp> s_pendingTexOps;

	struct GarbageItem
	{
		uint32_t retireSeq;
		VkBuffer buf;
		VkImage img;
		VkImageView iv;
		VkDeviceMemory mem;
	};
	static std::vector<GarbageItem> s_garbage;

	static float            s_clearColour[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	static std::atomic<int> s_nextTextureId(100);
	static int              s_nextCBuffId = 1;
	static uint64_t         s_nextCBuffGen = 1;
	static std::atomic<size_t> s_totalCBuffBytes(0);
	static int              s_currentViewportType = 0;
	static bool             s_gdrawInitialized = false;

	static std::unordered_map<int, VKTextureData> s_textures;
	static std::unordered_map<int, VKCBuffSlot>   s_cbuffs;
	static std::mutex                             s_cbuffMutex;
	static std::mutex                             s_garbageMutex;
	static std::recursive_mutex                   s_vkMutex;
	thread_local static ThreadRenderState         s_tls;

	struct WorldPipeKey
	{
		uint8_t compressed;
		uint8_t topo;
		uint8_t blendEnable;
		uint8_t blendSrc;
		uint8_t blendDst;
		uint8_t colorMask;
		uint8_t depthTest;
		uint8_t depthMask;
		uint8_t depthFunc;
		uint8_t cullEnable;
		uint8_t cullCW;
		uint8_t stencilEnable;
		uint8_t stencilFunc;
		uint8_t stencilRead;
		uint8_t stencilWrite;
		uint8_t depthBiasEnable;
		uint32_t customShaderGen;
		uint32_t _pad;

		bool operator==(const WorldPipeKey &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
	};
	struct WorldPipeKeyHash
	{
		size_t operator()(const WorldPipeKey &k) const
		{
			const uint64_t *p = (const uint64_t*)&k;
			return (size_t)(p[0] ^ (p[1] * 0x9E3779B97F4A7C15ULL) ^ (p[2] * 0xBF58476D1CE4E5B9ULL));
		}
	};
	static std::unordered_map<WorldPipeKey, VkPipeline, WorldPipeKeyHash> s_worldPipes;

	static VkPipeline s_boundPipe = VK_NULL_HANDLE;
	static VkBuffer   s_boundVB = VK_NULL_HANDLE;
	static VkDeviceSize s_boundVBOff = 0xFFFFFFFFFFFFFFFFULL;
	static VkBuffer   s_boundIB = VK_NULL_HANDLE;
	static bool       s_boundDepthBiasValid = false;
	static float      s_boundDepthBiasConst = 0.0f;
	static float      s_boundDepthBiasSlope = 0.0f;
	static bool       s_boundBlendConstValid = false;
	static float      s_boundBlendConst[4] = {};
	static bool       s_boundStencilRefValid = false;
	static uint32_t   s_boundStencilRef = 0;
	static VkBackendGDrawHost s_gdrawHost = {};

	static void InvalidateWorldPipelineCache(void)
	{
		s_boundPipe = VK_NULL_HANDLE;
		s_boundVB = VK_NULL_HANDLE;
		s_boundVBOff = 0xFFFFFFFFFFFFFFFFULL;
		s_boundIB = VK_NULL_HANDLE;
		s_boundDepthBiasValid = false;
		s_boundBlendConstValid = false;
		s_boundStencilRefValid = false;
	}

	static uint32_t FindMemoryType(uint32_t typeBits, VkFlags requiredProps)
	{
		for (uint32_t i = 0; i < s_memProps.memoryTypeCount; ++i)
		{
			if ((typeBits & (1u << i)) && (s_memProps.memoryTypes[i].propertyFlags & requiredProps) == requiredProps)
				return i;
		}
		for (uint32_t i = 0; i < s_memProps.memoryTypeCount; ++i)
		{
			if (typeBits & (1u << i)) return i;
		}
		return 0;
	}

	static void QueueGarbage(VkBuffer b, VkImage img, VkImageView iv, VkDeviceMemory mem)
	{
		if (!b && !img && !iv && !mem) return;
		std::lock_guard<std::mutex> lock(s_garbageMutex);
		s_garbage.push_back({ s_frameSeq, b, img, iv, mem });
	}

	static void SlabReturnFreeBlock(size_t slabIdx, VkDeviceSize offset, VkDeviceSize size)
	{
		if (slabIdx >= s_vbSlabs.size() || size == 0) return;
		auto &fl = s_vbSlabs[slabIdx].freeList;
		VKSlabFreeBlock blk = { offset, size };
		auto pos = std::lower_bound(fl.begin(), fl.end(), blk,
			[](const VKSlabFreeBlock &a, const VKSlabFreeBlock &b) { return a.offset < b.offset; });
		auto it = fl.insert(pos, blk);
		if (it != fl.begin())
		{
			auto prev = it - 1;
			if (prev->offset + prev->size == it->offset)
			{
				prev->size += it->size;
				it = fl.erase(it);
			}
		}
		if (it + 1 != fl.end())
		{
			auto next = it + 1;
			if (it->offset + it->size == next->offset)
			{
				it->size += next->size;
				fl.erase(next);
			}
		}
	}

	static bool SlabAllocVB(VkDeviceSize rawSize, VKSlabAlloc &out)
	{
		if (rawSize == 0) return false;
		const VkDeviceSize alignedSz = (rawSize + 31u) & ~((VkDeviceSize)31u);
		for (size_t s = 0; s < s_vbSlabs.size(); ++s)
		{
			auto &fl = s_vbSlabs[s].freeList;
			for (size_t i = 0; i < fl.size(); ++i)
			{
				if (fl[i].size >= alignedSz)
				{
					out.slabIdx = s;
					out.buf = s_vbSlabs[s].buf;
					out.offset = fl[i].offset;
					out.size = alignedSz;
					if (fl[i].size == alignedSz) fl.erase(fl.begin() + (ptrdiff_t)i);
					else { fl[i].offset += alignedSz; fl[i].size -= alignedSz; }
					return true;
				}
			}
		}

		const VkDeviceSize slabCap = std::max<VkDeviceSize>(C4J_VK_VB_SLAB_BYTES, alignedSz);
		VKVBSlab slab = {};
		slab.capacity = slabCap;
		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, slabCap,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		if (s_vk.vkCreateBuffer(s_device, &bci, NULL, &slab.buf) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		s_vk.vkGetBufferMemoryRequirements(s_device, slab.buf, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
			FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
		if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &slab.mem) != VK_SUCCESS)
		{
			s_vk.vkDestroyBuffer(s_device, slab.buf, NULL);
			return false;
		}
		s_vk.vkBindBufferMemory(s_device, slab.buf, slab.mem, 0);

		const size_t newIdx = s_vbSlabs.size();
		if (slabCap > alignedSz)
		{
			slab.freeList.push_back({ alignedSz, slabCap - alignedSz });
		}
		s_vbSlabs.push_back(std::move(slab));
		out.slabIdx = newIdx;
		out.buf = s_vbSlabs[newIdx].buf;
		out.offset = 0;
		out.size = alignedSz;
		return true;
	}

	static void QueueSlabFree(const VKSlabAlloc &a)
	{
		if (a.slabIdx == SIZE_MAX || a.size == 0) return;
		std::lock_guard<std::mutex> lock(s_garbageMutex);
		s_pendingSlabFrees.push_back({ s_frameSeq, a.slabIdx, a.offset, a.size });
	}

	static void QueueSlabFrees(const std::shared_ptr<std::vector<VKSlabAlloc>> &allocs)
	{
		if (!allocs || allocs->empty()) return;
		std::lock_guard<std::mutex> lock(s_garbageMutex);
		for (size_t i = 0; i < allocs->size(); ++i)
		{
			const VKSlabAlloc &a = (*allocs)[i];
			if (a.slabIdx != SIZE_MAX && a.size > 0)
			{
				s_pendingSlabFrees.push_back({ s_frameSeq, a.slabIdx, a.offset, a.size });
			}
		}
	}

	static void DrainGarbage(bool forceAll)
	{
		if (!s_device) return;
		std::vector<GarbageItem> retired;
		std::vector<PendingSlabFree> retiredSlabs;
		{
			std::lock_guard<std::mutex> lock(s_garbageMutex);
			size_t keep = 0;
			for (size_t i = 0; i < s_garbage.size(); ++i)
			{
				const GarbageItem &g = s_garbage[i];
				if (forceAll || (s_frameSeq > g.retireSeq + 2u))
				{
					retired.push_back(g);
				}
				else
				{
					s_garbage[keep++] = g;
				}
			}
			s_garbage.resize(keep);

			size_t keepSlab = 0;
			for (size_t i = 0; i < s_pendingSlabFrees.size(); ++i)
			{
				const PendingSlabFree &sf = s_pendingSlabFrees[i];
				if (forceAll || (s_frameSeq > sf.retireSeq + 2u))
				{
					retiredSlabs.push_back(sf);
				}
				else
				{
					s_pendingSlabFrees[keepSlab++] = sf;
				}
			}
			s_pendingSlabFrees.resize(keepSlab);
		}
		for (size_t i = 0; i < retired.size(); ++i)
		{
			const GarbageItem &g = retired[i];
			if (g.iv)  s_vk.vkDestroyImageView(s_device, g.iv, NULL);
			if (g.img) s_vk.vkDestroyImage(s_device, g.img, NULL);
			if (g.buf) s_vk.vkDestroyBuffer(s_device, g.buf, NULL);
			if (g.mem) s_vk.vkFreeMemory(s_device, g.mem, NULL);
		}
		for (size_t i = 0; i < retiredSlabs.size(); ++i)
		{
			SlabReturnFreeBlock(retiredSlabs[i].slabIdx, retiredSlabs[i].offset, retiredSlabs[i].size);
		}
	}

	static bool EnsureStagingSize(VkDeviceSize needed)
	{
		if (needed <= s_stageSize && s_stageBuf) return true;
		if (s_stagePtr) { s_vk.vkUnmapMemory(s_device, s_stageMem); s_stagePtr = NULL; }
		if (s_stageBuf) { s_vk.vkDestroyBuffer(s_device, s_stageBuf, NULL); s_stageBuf = VK_NULL_HANDLE; }
		if (s_stageMem) { s_vk.vkFreeMemory(s_device, s_stageMem, NULL); s_stageMem = VK_NULL_HANDLE; }

		VkDeviceSize allocSz = std::max<VkDeviceSize>(needed, 1024u * 1024u);
		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, allocSz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		if (s_vk.vkCreateBuffer(s_device, &bci, NULL, &s_stageBuf) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		s_vk.vkGetBufferMemoryRequirements(s_device, s_stageBuf, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
		if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &s_stageMem) != VK_SUCCESS) return false;
		s_vk.vkBindBufferMemory(s_device, s_stageBuf, s_stageMem, 0);
		s_vk.vkMapMemory(s_device, s_stageMem, 0, allocSz, 0, &s_stagePtr);
		s_stageSize = allocSz;
		return true;
	}

	static void ApplyMCViewport(void)
	{
		if (!s_frameActive || !s_inRenderPass) return;
		float vx, vy, vw, vh;
		ComputeViewportRect(s_currentViewportType, s_width, s_height, vx, vy, vw, vh);
		VkViewport vp = { vx, vy + vh, vw, -vh, 0.0f, 1.0f };
		VkRect2D sc = { { (int32_t)vx, (int32_t)vy }, { (uint32_t)vw, (uint32_t)vh } };
		if (sc.extent.width == 0) sc.extent.width = 1;
		if (sc.extent.height == 0) sc.extent.height = 1;
		s_vk.vkCmdSetViewport(s_frames[s_frameIdx].cmdBuf, 0, 1, &vp);
		s_vk.vkCmdSetScissor(s_frames[s_frameIdx].cmdBuf, 0, 1, &sc);
	}

	static void TransitionImage(VkCommandBuffer cmd, VkImage img, VkFlags aspect, VkImageLayout oldL, VkImageLayout newL,
		uint32_t baseMip, uint32_t mipCount, VkFlags srcAccess, VkFlags dstAccess, VkFlags srcStage, VkFlags dstStage)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.srcAccessMask = srcAccess;
		b.dstAccessMask = dstAccess;
		b.oldLayout = oldL;
		b.newLayout = newL;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = img;
		b.subresourceRange = { aspect, baseMip, mipCount, 0, 1 };
		s_vk.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
	}

	static void EnsureRenderPassActive(void)
	{
		if (!s_frameActive || s_inRenderPass) return;
		VkCommandBuffer cmd = s_frames[s_frameIdx].cmdBuf;
		VkRenderPassBeginInfo rpbi = {};
		rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpbi.renderPass = s_renderPass;
		rpbi.framebuffer = s_framebuffers[s_activeImgIdx];
		rpbi.renderArea = { { 0, 0 }, { (uint32_t)s_width, (uint32_t)s_height } };
		s_vk.vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
		s_inRenderPass = true;
		ApplyMCViewport();
		InvalidateWorldPipelineCache();
	}

	static void EndRenderPassIfActive(void)
	{
		if (!s_frameActive || !s_inRenderPass) return;
		s_vk.vkCmdEndRenderPass(s_frames[s_frameIdx].cmdBuf);
		s_inRenderPass = false;
	}

	static VkCommandBuffer BeginOneTimeCmd(void)
	{
		VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, s_uploadPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		s_vk.vkAllocateCommandBuffers(s_device, &cbai, &cmd);
		VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		s_vk.vkBeginCommandBuffer(cmd, &cbi);
		return cmd;
	}

	static void EndOneTimeCmd(VkCommandBuffer cmd)
	{
		s_vk.vkEndCommandBuffer(cmd);
		s_vk.vkResetFences(s_device, 1, &s_uploadFence);
		VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL };
		s_vk.vkQueueSubmit(s_queue, 1, &si, s_uploadFence);
		s_vk.vkWaitForFences(s_device, 1, &s_uploadFence, VK_TRUE, ~0ULL);
		s_vk.vkResetCommandPool(s_device, s_uploadPool, 0);
	}

	static void UploadImageData(VkImage image, uint32_t width, uint32_t height, uint32_t mipLevel, const void *pixelsRGBA, size_t byteCount, bool firstInitAllMips)
	{
		if (!s_device || !image || !pixelsRGBA || byteCount == 0) return;

		/* Always upload via sync staging pool so mid-frame uploads never corrupt Swapchain passes */
		if (!EnsureStagingSize(byteCount)) return;
		memcpy(s_stagePtr, pixelsRGBA, byteCount);

		VkCommandBuffer cmd = BeginOneTimeCmd();
		if (firstInitAllMips)
		{
			/* Put mip 0 in DST, then transition all mips when finished; for simplicity transition mipLevel only, and caller transitions rest */
		}
		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			firstInitAllMips ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			mipLevel, 1,
			firstInitAllMips ? 0u : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			firstInitAllMips ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT);

		VkBufferImageCopy reg = {};
		reg.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, 0, 1 };
		reg.imageExtent = { width, height, 1 };
		s_vk.vkCmdCopyBufferToImage(cmd, s_stageBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);

		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mipLevel, 1,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		EndOneTimeCmd(cmd);
	}

	static bool CreateBufferDeviceLocal(VkDeviceSize sz, VkFlags usage, const void *data, VkBuffer &outBuf, VkDeviceMemory &outMem)
	{
		/* Upload via staging buffer to DEVICE_LOCAL VRAM for maximum RTX throughput */
		if (!EnsureStagingSize(sz)) return false;
		memcpy(s_stagePtr, data, (size_t)sz);

		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sz, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		if (s_vk.vkCreateBuffer(s_device, &bci, NULL, &outBuf) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		s_vk.vkGetBufferMemoryRequirements(s_device, outBuf, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
		if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &outMem) != VK_SUCCESS)
		{
			s_vk.vkDestroyBuffer(s_device, outBuf, NULL);
			outBuf = VK_NULL_HANDLE;
			return false;
		}
		s_vk.vkBindBufferMemory(s_device, outBuf, outMem, 0);

		VkCommandBuffer cmd = BeginOneTimeCmd();
		VkBufferCopy cpy = { 0, 0, sz };
		s_vk.vkCmdCopyBuffer(cmd, s_stageBuf, outBuf, 1, &cpy);
		EndOneTimeCmd(cmd);
		return true;
	}

	static bool AllocFrameStaging(VkDeviceSize sz, VkDeviceSize align, VkBuffer &outStageBuf, VkDeviceSize &outOffset, void **outPtr)
	{
		if (!s_frameActive) return false;
		FrameSync &fs = s_frames[s_frameIdx];
		if (!fs.stageBuf || !fs.stagePtr) return false;
		VkDeviceSize mask = (align > 1) ? (align - 1) : 0;
		VkDeviceSize off = (fs.stageOff + mask) & ~mask;
		if (off + sz > C4J_VK_FRAME_STAGE_BYTES) return false;
		outStageBuf = fs.stageBuf;
		outOffset = off;
		*outPtr = (uint8_t*)fs.stagePtr + off;
		fs.stageOff = (uint32_t)(off + sz);
		return true;
	}

	static void RecordAsyncTexInit(VkCommandBuffer cmd, VkImage image, uint32_t mipLevels, uint32_t width, uint32_t height, VkBuffer stageBuf, VkDeviceSize stageOff)
	{
		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, mipLevels, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferImageCopy reg = {};
		reg.bufferOffset = stageOff;
		reg.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		reg.imageExtent = { width, height, 1 };
		s_vk.vkCmdCopyBufferToImage(cmd, stageBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			0, mipLevels, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	static void RecordAsyncTexUpdate(VkCommandBuffer cmd, VkImage image, uint32_t level, int32_t xoff, int32_t yoff, uint32_t width, uint32_t height, VkBuffer stageBuf, VkDeviceSize stageOff)
	{
		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			level, 1, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferImageCopy reg = {};
		reg.bufferOffset = stageOff;
		reg.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
		reg.imageOffset = { xoff, yoff, 0 };
		reg.imageExtent = { width, height, 1 };
		s_vk.vkCmdCopyBufferToImage(cmd, stageBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
		TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			level, 1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	static void FlushPendingTexOps(void)
	{
		if (s_pendingTexOps.empty() || !s_frameActive) return;
		VkCommandBuffer frameCmd = s_frames[s_frameIdx].cmdBuf;
		std::vector<PendingTexOp> fallback;
		for (size_t i = 0; i < s_pendingTexOps.size(); ++i)
		{
			const PendingTexOp &op = s_pendingTexOps[i];
			auto it = s_textures.find(op.texId);
			if (it == s_textures.end() || it->second.image != op.image) continue;
			VkBuffer stageBuf = VK_NULL_HANDLE;
			VkDeviceSize stageOff = 0;
			void *dst = NULL;
			if (AllocFrameStaging(op.pixels.size(), 4, stageBuf, stageOff, &dst))
			{
				memcpy(dst, op.pixels.data(), op.pixels.size());
				if (op.firstInitAllMips)
				{
					RecordAsyncTexInit(frameCmd, op.image, op.mipLevels, op.width, op.height, stageBuf, stageOff);
				}
				else
				{
					RecordAsyncTexUpdate(frameCmd, op.image, op.level, op.xoff, op.yoff, op.width, op.height, stageBuf, stageOff);
				}
			}
			else
			{
				fallback.push_back(op);
			}
		}
		s_pendingTexOps.clear();
		for (size_t i = 0; i < fallback.size(); ++i)
		{
			const PendingTexOp &op = fallback[i];
			if (!EnsureStagingSize(op.pixels.size())) continue;
			memcpy(s_stagePtr, op.pixels.data(), op.pixels.size());
			VkCommandBuffer cmd = BeginOneTimeCmd();
			if (op.firstInitAllMips)
			{
				RecordAsyncTexInit(cmd, op.image, op.mipLevels, op.width, op.height, s_stageBuf, 0);
			}
			else
			{
				RecordAsyncTexUpdate(cmd, op.image, op.level, op.xoff, op.yoff, op.width, op.height, s_stageBuf, 0);
			}
			EndOneTimeCmd(cmd);
		}
	}

	static void FlushPendingCBuffUploads(void)
	{
		if (!s_frameActive || !s_device) return;
		struct BatchItem
		{
			int index;
			uint64_t gen;
			std::shared_ptr<const std::vector<CBuffCommand>> cmds;
		};
		std::vector<BatchItem> batch;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			size_t queuedBytes = 0;
			while (!s_pendingCBuffUploads.empty())
			{
				const PendingCBuffUpload &req = s_pendingCBuffUploads.front();
				auto it = s_cbuffs.find(req.index);
				if (it == s_cbuffs.end() || it->second.generation != req.generation ||
					it->second.uploadedGen == req.generation || !it->second.commands || it->second.commands->empty())
				{
					s_pendingCBuffUploads.pop_front();
					continue;
				}
				if (!batch.empty() && queuedBytes + it->second.byteSize > C4J_VK_FRAME_CBUFF_STAGE_BUDGET)
				{
					break;
				}
				batch.push_back({ req.index, req.generation, it->second.commands });
				queuedBytes += it->second.byteSize;
				s_pendingCBuffUploads.pop_front();
			}
		}
		if (batch.empty()) return;

		VkCommandBuffer frameCmd = s_frames[s_frameIdx].cmdBuf;
		std::vector<VkBuffer> touchedSlabs;

		for (size_t b = 0; b < batch.size(); ++b)
		{
			const BatchItem &item = batch[b];
			auto newAllocs = std::make_shared<std::vector<VKSlabAlloc>>(item.cmds->size());
			bool ok = true;
			for (size_t i = 0; i < item.cmds->size(); ++i)
			{
				const auto &cmd = (*item.cmds)[i];
				if (cmd.vertexData.empty()) continue;
				const VkDeviceSize sz = (VkDeviceSize)cmd.vertexData.size();
				VkBuffer stageBuf = VK_NULL_HANDLE;
				VkDeviceSize stageOff = 0;
				void *stagePtr = NULL;
				if (!AllocFrameStaging(sz, 16, stageBuf, stageOff, &stagePtr))
				{
					ok = false;
					break;
				}
				VKSlabAlloc alloc;
				if (!SlabAllocVB(sz, alloc))
				{
					ok = false;
					break;
				}
				(*newAllocs)[i] = alloc;
				memcpy(stagePtr, cmd.vertexData.data(), (size_t)sz);
				VkBufferCopy cpy = { stageOff, alloc.offset, sz };
				s_vk.vkCmdCopyBuffer(frameCmd, stageBuf, alloc.buf, 1, &cpy);
				if (std::find(touchedSlabs.begin(), touchedSlabs.end(), alloc.buf) == touchedSlabs.end())
				{
					touchedSlabs.push_back(alloc.buf);
				}
			}

			if (!ok)
			{
				/* Rollback allocations from this incomplete item and re-queue remaining items for next frame */
				for (size_t i = 0; i < newAllocs->size(); ++i)
				{
					const VKSlabAlloc &a = (*newAllocs)[i];
					if (a.slabIdx != SIZE_MAX && a.size > 0)
					{
						SlabReturnFreeBlock(a.slabIdx, a.offset, a.size);
					}
				}
				std::lock_guard<std::mutex> lock(s_cbuffMutex);
				for (size_t r = batch.size(); r > b; --r)
				{
					s_pendingCBuffUploads.push_front({ batch[r - 1].index, batch[r - 1].gen });
				}
				break;
			}

			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(item.index);
			if (it != s_cbuffs.end() && it->second.generation == item.gen)
			{
				QueueSlabFrees(it->second.gpuAllocs);
				it->second.gpuAllocs = newAllocs;
				it->second.uploadedGen = item.gen;
			}
			else
			{
				QueueSlabFrees(newAllocs);
			}
		}

		if (!touchedSlabs.empty())
		{
			struct VkBufMemBarrier
			{
				VkStructureType sType;
				const void *pNext;
				VkFlags srcAccessMask;
				VkFlags dstAccessMask;
				uint32_t srcQueueFamilyIndex;
				uint32_t dstQueueFamilyIndex;
				VkBuffer buffer;
				VkDeviceSize offset;
				VkDeviceSize size;
			};
			std::vector<VkBufMemBarrier> barriers;
			barriers.reserve(touchedSlabs.size());
			for (size_t i = 0; i < touchedSlabs.size(); ++i)
			{
				barriers.push_back({
					(VkStructureType)44, NULL,
					VK_ACCESS_TRANSFER_WRITE_BIT, 0x00000008u /* VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT */,
					VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
					touchedSlabs[i], 0, VK_WHOLE_SIZE
				});
			}
			s_vk.vkCmdPipelineBarrier(frameCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 0x00000004u /* VK_PIPELINE_STAGE_VERTEX_INPUT_BIT */,
				0, 0, NULL, (uint32_t)barriers.size(), barriers.data(), 0, NULL);
		}
	}

	static bool CreateHostBuffer(VkDeviceSize sz, VkFlags usage, VkBuffer &outBuf, VkDeviceMemory &outMem, void **outPtr)
	{
		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sz, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		if (s_vk.vkCreateBuffer(s_device, &bci, NULL, &outBuf) != VK_SUCCESS) return false;
		VkMemoryRequirements mr = {};
		s_vk.vkGetBufferMemoryRequirements(s_device, outBuf, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
		if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &outMem) != VK_SUCCESS)
		{
			s_vk.vkDestroyBuffer(s_device, outBuf, NULL);
			outBuf = VK_NULL_HANDLE;
			return false;
		}
		s_vk.vkBindBufferMemory(s_device, outBuf, outMem, 0);
		if (outPtr) s_vk.vkMapMemory(s_device, outMem, 0, sz, 0, outPtr);
		return true;
	}

	static VkDescriptorPool CreateWorldDescPool(void)
	{
		VkDescriptorPoolSize dps[2] = {
			{ C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, C4J_VK_MAX_DESC_SETS_PER_POOL * 2u },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, C4J_VK_MAX_DESC_SETS_PER_POOL * 2u },
		};
		VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, C4J_VK_MAX_DESC_SETS_PER_POOL, 2, dps };
		VkDescriptorPool pool = VK_NULL_HANDLE;
		if (s_vk.vkCreateDescriptorPool(s_device, &dpci, NULL, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
		return pool;
	}

	static FrameRingVBChunk *AllocFrameRingVB(FrameSync &fs, VkDeviceSize neededBytes, uint32_t &outOffset)
	{
		while (fs.activeRingVB < fs.ringVBs.size())
		{
			FrameRingVBChunk &c = fs.ringVBs[fs.activeRingVB];
			uint32_t aligned = (c.offset + 15u) & ~15u;
			if ((VkDeviceSize)aligned + neededBytes <= (VkDeviceSize)c.capacity)
			{
				outOffset = aligned;
				c.offset = aligned + (uint32_t)neededBytes;
				return &c;
			}
			fs.activeRingVB++;
		}
		const uint32_t cap = (uint32_t)std::max<VkDeviceSize>(C4J_VK_RING_VB_BYTES, (neededBytes + 15u) & ~15u);
		FrameRingVBChunk nc = {};
		nc.capacity = cap;
		if (!CreateHostBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, nc.buf, nc.mem, &nc.ptr) || !nc.ptr)
		{
			if (nc.buf) s_vk.vkDestroyBuffer(s_device, nc.buf, NULL);
			if (nc.mem) s_vk.vkFreeMemory(s_device, nc.mem, NULL);
			return NULL;
		}
		outOffset = 0;
		nc.offset = (uint32_t)neededBytes;
		fs.ringVBs.push_back(std::move(nc));
		fs.activeRingVB = fs.ringVBs.size() - 1;
		return &fs.ringVBs.back();
	}

	static FrameUBOChunk *AllocFrameUBOSlot(FrameSync &fs, uint32_t &outOffset)
	{
		while (fs.activeUBOChunk < fs.uboChunks.size())
		{
			FrameUBOChunk &c = fs.uboChunks[fs.activeUBOChunk];
			if (c.offset + C4J_VK_UBO_STRIDE <= c.capacity)
			{
				outOffset = c.offset;
				c.offset += C4J_VK_UBO_STRIDE;
				return &c;
			}
			fs.activeUBOChunk++;
		}
		const uint32_t cap = C4J_VK_UBO_CHUNK_DRAWS * C4J_VK_UBO_STRIDE;
		FrameUBOChunk nc = {};
		nc.capacity = cap;
		if (!CreateHostBuffer(cap, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nc.buf, nc.mem, &nc.ptr) || !nc.ptr)
		{
			if (nc.buf) s_vk.vkDestroyBuffer(s_device, nc.buf, NULL);
			if (nc.mem) s_vk.vkFreeMemory(s_device, nc.mem, NULL);
			return NULL;
		}
		outOffset = 0;
		nc.offset = C4J_VK_UBO_STRIDE;
		fs.uboChunks.push_back(std::move(nc));
		fs.activeUBOChunk = fs.uboChunks.size() - 1;
		return &fs.uboChunks.back();
	}

	static VkDescriptorSet GetOrCreateWorldDescSet(FrameSync &fs, FrameUBOChunk &uboChunk,
		VkImageView view0, int smpIdx, VkImageView view1)
	{
		TexDescKey key = { view0, view1, (uint32_t)smpIdx };
		auto it = uboChunk.descCache.find(key);
		if (it != uboChunk.descCache.end()) return it->second;

		if (fs.descPools.empty() || fs.descPoolUsed >= C4J_VK_MAX_DESC_SETS_PER_POOL)
		{
			VkDescriptorPool np = CreateWorldDescPool();
			if (!np) return VK_NULL_HANDLE;
			fs.descPools.push_back(np);
			fs.descPoolUsed = 0;
		}
		VkDescriptorSet dset = VK_NULL_HANDLE;
		VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, fs.descPools.back(), 1, &s_worldDSL };
		if (s_vk.vkAllocateDescriptorSets(s_device, &dsai, &dset) != VK_SUCCESS)
		{
			VkDescriptorPool np = CreateWorldDescPool();
			if (!np) return VK_NULL_HANDLE;
			fs.descPools.push_back(np);
			fs.descPoolUsed = 0;
			dsai.descriptorPool = np;
			if (s_vk.vkAllocateDescriptorSets(s_device, &dsai, &dset) != VK_SUCCESS) return VK_NULL_HANDLE;
		}
		fs.descPoolUsed++;

		VkDescriptorBufferInfo biVS = { uboChunk.buf, 0, sizeof(VSConstantBufferData) };
		VkDescriptorBufferInfo biPS = { uboChunk.buf, 0, sizeof(VKPSUniformBlock) };
		VkDescriptorImageInfo  iiT0 = { s_samplers[smpIdx], view0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo  iiT1 = { s_samplers[3], view1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet writes[4] = {
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 0, 0, 1, C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &biVS, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 1, 0, 1, C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &biPS, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &iiT0, NULL, NULL },
			{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, dset, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &iiT1, NULL, NULL },
		};
		s_vk.vkUpdateDescriptorSets(s_device, 4, writes, 0, NULL);
		uboChunk.descCache[key] = dset;
		return dset;
	}

	static VkBlendFactor ToVKBlend(int b)
	{
		switch (b)
		{
		case C4J_BLEND_ZERO:             return VK_BLEND_FACTOR_ZERO;
		case C4J_BLEND_ONE:              return VK_BLEND_FACTOR_ONE;
		case C4J_BLEND_SRC_COLOR:        return VK_BLEND_FACTOR_SRC_COLOR;
		case C4J_BLEND_INV_SRC_COLOR:    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case C4J_BLEND_SRC_ALPHA:        return VK_BLEND_FACTOR_SRC_ALPHA;
		case C4J_BLEND_INV_SRC_ALPHA:    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case C4J_BLEND_DEST_ALPHA:       return VK_BLEND_FACTOR_DST_ALPHA;
		case C4J_BLEND_INV_DEST_ALPHA:   return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case C4J_BLEND_DEST_COLOR:       return VK_BLEND_FACTOR_DST_COLOR;
		case C4J_BLEND_INV_DEST_COLOR:   return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case C4J_BLEND_SRC_ALPHA_SAT:    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		case C4J_BLEND_FACTOR_ALPHA:     return VK_BLEND_FACTOR_CONSTANT_ALPHA;
		case C4J_BLEND_INV_FACTOR_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
		default:                         return VK_BLEND_FACTOR_SRC_ALPHA;
		}
	}

	static VkBlendFactor SanitizeAlphaVK(VkBlendFactor f)
	{
		if (f == VK_BLEND_FACTOR_SRC_COLOR) return VK_BLEND_FACTOR_SRC_ALPHA;
		if (f == VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		if (f == VK_BLEND_FACTOR_DST_COLOR) return VK_BLEND_FACTOR_DST_ALPHA;
		if (f == VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR) return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		return f;
	}

	static VkCompareOp ToVKCompare(int c)
	{
		switch (c)
		{
		case C4J_CMP_NEVER:         return VK_COMPARE_OP_NEVER;
		case C4J_CMP_LESS:          return VK_COMPARE_OP_LESS;
		case C4J_CMP_EQUAL:         return VK_COMPARE_OP_EQUAL;
		case C4J_CMP_LESS_EQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
		case C4J_CMP_GREATER:       return VK_COMPARE_OP_GREATER;
		case C4J_CMP_NOT_EQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
		case C4J_CMP_GREATER_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case C4J_CMP_ALWAYS:        return VK_COMPARE_OP_ALWAYS;
		default:                    return VK_COMPARE_OP_LESS_OR_EQUAL;
		}
	}

	static VkPipeline GetOrCreateWorldPipeline(const WorldPipeKey &k)
	{
		auto it = s_worldPipes.find(k);
		if (it != s_worldPipes.end()) return it->second;

		const bool useCustom = (k.customShaderGen != 0 && s_smCustomFrag != VK_NULL_HANDLE);
		VkShaderModule vsMod = k.compressed
			? ((useCustom && s_smCustomComp) ? s_smCustomComp : s_smMcComp)
			: ((useCustom && s_smCustomStd) ? s_smCustomStd : s_smMcStd);
		VkShaderModule fsMod = useCustom ? s_smCustomFrag : s_smMcFrag;
		VkPipelineShaderStageCreateInfo stages[2] = {
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vsMod, "main", NULL },
			{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsMod, "main", NULL },
		};

		VkVertexInputBindingDescription vbStd = { 0, 32, VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vaStd[5] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
			{ 1, 0, VK_FORMAT_R32G32_SFLOAT,    12 },
			{ 2, 0, VK_FORMAT_R32_UINT,         20 },
			{ 3, 0, VK_FORMAT_R32_UINT,         24 },
			{ 4, 0, VK_FORMAT_R32_UINT,         28 },
		};
		VkVertexInputBindingDescription vbComp = { 0, 16, VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vaComp[2] = {
			{ 0, 0, VK_FORMAT_R16G16B16A16_SINT, 0 },
			{ 1, 0, VK_FORMAT_R16G16B16A16_SINT, 8 },
		};

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = k.compressed ? &vbComp : &vbStd;
		vi.vertexAttributeDescriptionCount = k.compressed ? 2u : 5u;
		vi.pVertexAttributeDescriptions = k.compressed ? vaComp : vaStd;

		VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		if (k.topo == 1) vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		else if (k.topo == 2) vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		else if (k.topo == 3) vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, vkTopo, VK_FALSE };
		VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL };

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = !k.cullEnable ? VK_CULL_MODE_NONE : (k.cullCW ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.depthBiasEnable = k.depthBiasEnable ? VK_TRUE : VK_FALSE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = k.depthTest ? VK_TRUE : VK_FALSE;
		ds.depthWriteEnable = k.depthMask ? VK_TRUE : VK_FALSE;
		ds.depthCompareOp = ToVKCompare(k.depthFunc);
		ds.stencilTestEnable = k.stencilEnable ? VK_TRUE : VK_FALSE;
		ds.front.failOp = VK_STENCIL_OP_KEEP;
		ds.front.passOp = VK_STENCIL_OP_KEEP;
		ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
		ds.front.compareOp = ToVKCompare(k.stencilFunc);
		ds.front.compareMask = k.stencilRead;
		ds.front.writeMask = k.stencilWrite;
		ds.back = ds.front;

		VkPipelineColorBlendAttachmentState cba = {};
		cba.blendEnable = k.blendEnable ? VK_TRUE : VK_FALSE;
		cba.srcColorBlendFactor = ToVKBlend(k.blendSrc);
		cba.dstColorBlendFactor = ToVKBlend(k.blendDst);
		cba.colorBlendOp = VK_BLEND_OP_ADD;
		cba.srcAlphaBlendFactor = SanitizeAlphaVK(cba.srcColorBlendFactor);
		cba.dstAlphaBlendFactor = SanitizeAlphaVK(cba.dstColorBlendFactor);
		cba.alphaBlendOp = VK_BLEND_OP_ADD;
		cba.colorWriteMask = k.colorMask & 0xFu;

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cba;

		VkDynamicState dynStates[5] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_DEPTH_BIAS,
			VK_DYNAMIC_STATE_BLEND_CONSTANTS,
			VK_DYNAMIC_STATE_STENCIL_REFERENCE,
		};
		VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 5, dynStates };

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
		gp.layout = s_worldPLLayout;
		gp.renderPass = s_renderPass;

		VkPipeline pipe = VK_NULL_HANDLE;
		if (s_vk.vkCreateGraphicsPipelines(s_device, VK_NULL_HANDLE, 1, &gp, NULL, &pipe) == VK_SUCCESS)
		{
			s_worldPipes[k] = pipe;
		}
		return pipe;
	}

	static void DestroySwapchainObjects()
	{
		for (auto fb : s_framebuffers) if (fb) s_vk.vkDestroyFramebuffer(s_device, fb, NULL);
		s_framebuffers.clear();
		if (s_depthView)  { s_vk.vkDestroyImageView(s_device, s_depthView, NULL); s_depthView = VK_NULL_HANDLE; }
		if (s_depthImage) { s_vk.vkDestroyImage(s_device, s_depthImage, NULL); s_depthImage = VK_NULL_HANDLE; }
		if (s_depthMem)   { s_vk.vkFreeMemory(s_device, s_depthMem, NULL); s_depthMem = VK_NULL_HANDLE; }
		for (auto iv : s_swapViews) if (iv) s_vk.vkDestroyImageView(s_device, iv, NULL);
		s_swapViews.clear();
		s_swapImages.clear();
		s_swapLayouts.clear();
		if (s_swapchain)  { s_vk.vkDestroySwapchainKHR(s_device, s_swapchain, NULL); s_swapchain = VK_NULL_HANDLE; }
	}

	static bool CreateSwapchainObjects(int width, int height)
	{
		VkSurfaceCapabilitiesKHR caps = {};
		if (s_vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_physDev, s_surface, &caps) != VK_SUCCESS) return false;

		uint32_t fmtCount = 0;
		s_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(s_physDev, s_surface, &fmtCount, NULL);
		std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
		s_vk.vkGetPhysicalDeviceSurfaceFormatsKHR(s_physDev, s_surface, &fmtCount, fmts.data());
		VkSurfaceFormatKHR chosenFmt = fmts.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : fmts[0];
		for (const auto &f : fmts)
		{
			if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
				f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosenFmt = f;
				break;
			}
		}
		s_colorFormat = chosenFmt.format;

		uint32_t pmCount = 0;
		s_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(s_physDev, s_surface, &pmCount, NULL);
		std::vector<VkPresentModeKHR> pms(pmCount);
		s_vk.vkGetPhysicalDeviceSurfacePresentModesKHR(s_physDev, s_surface, &pmCount, pms.data());

		VkPresentModeKHR chosenPM = VK_PRESENT_MODE_FIFO_KHR;
		if (!s_vsync)
		{
			for (auto pm : pms) if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) { chosenPM = pm; break; }
			if (chosenPM == VK_PRESENT_MODE_FIFO_KHR)
			{
				for (auto pm : pms) if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { chosenPM = pm; break; }
			}
		}

		VkExtent2D ext = caps.currentExtent;
		if (ext.width == 0xFFFFFFFFu || ext.height == 0xFFFFFFFFu)
		{
			ext.width = (uint32_t)std::clamp<int>(width, (int)caps.minImageExtent.width, (int)caps.maxImageExtent.width);
			ext.height = (uint32_t)std::clamp<int>(height, (int)caps.minImageExtent.height, (int)caps.maxImageExtent.height);
		}
		if (ext.width == 0 || ext.height == 0) return false;
		s_width = (int)ext.width;
		s_height = (int)ext.height;

		uint32_t imgCount = std::max<uint32_t>(caps.minImageCount, s_vsync ? 2u : 3u);
		if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

		VkSwapchainCreateInfoKHR sci = {};
		sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		sci.surface = s_surface;
		sci.minImageCount = imgCount;
		sci.imageFormat = chosenFmt.format;
		sci.imageColorSpace = chosenFmt.colorSpace;
		sci.imageExtent = ext;
		sci.imageArrayLayers = 1;
		sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		sci.preTransform = (caps.currentTransform != 0) ? caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		sci.presentMode = chosenPM;
		sci.clipped = VK_TRUE;

		if (s_vk.vkCreateSwapchainKHR(s_device, &sci, NULL, &s_swapchain) != VK_SUCCESS) return false;

		uint32_t scImgCount = 0;
		s_vk.vkGetSwapchainImagesKHR(s_device, s_swapchain, &scImgCount, NULL);
		s_swapImages.resize(scImgCount);
		s_vk.vkGetSwapchainImagesKHR(s_device, s_swapchain, &scImgCount, s_swapImages.data());
		s_swapLayouts.assign(scImgCount, VK_IMAGE_LAYOUT_UNDEFINED);
		s_swapViews.resize(scImgCount, VK_NULL_HANDLE);

		for (uint32_t i = 0; i < scImgCount; ++i)
		{
			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = s_swapImages[i];
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = s_colorFormat;
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (s_vk.vkCreateImageView(s_device, &ivci, NULL, &s_swapViews[i]) != VK_SUCCESS) return false;
		}

		/* Depth/Stencil attachment */
		VkImageCreateInfo dci = {};
		dci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		dci.imageType = VK_IMAGE_TYPE_2D;
		dci.format = s_depthFormat;
		dci.extent = { (uint32_t)s_width, (uint32_t)s_height, 1 };
		dci.mipLevels = 1;
		dci.arrayLayers = 1;
		dci.samples = VK_SAMPLE_COUNT_1_BIT;
		dci.tiling = VK_IMAGE_TILING_OPTIMAL;
		dci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		dci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		dci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (s_vk.vkCreateImage(s_device, &dci, NULL, &s_depthImage) != VK_SUCCESS) return false;

		VkMemoryRequirements mr = {};
		s_vk.vkGetImageMemoryRequirements(s_device, s_depthImage, &mr);
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
		if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &s_depthMem) != VK_SUCCESS) return false;
		s_vk.vkBindImageMemory(s_device, s_depthImage, s_depthMem, 0);

		VkImageViewCreateInfo div = {};
		div.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		div.image = s_depthImage;
		div.viewType = VK_IMAGE_VIEW_TYPE_2D;
		div.format = s_depthFormat;
		div.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
		if (s_vk.vkCreateImageView(s_device, &div, NULL, &s_depthView) != VK_SUCCESS) return false;
		s_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (!s_renderPass)
		{
			VkAttachmentDescription atts[2] = {};
			atts[0].format = s_colorFormat;
			atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
			atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			atts[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			atts[1].format = s_depthFormat;
			atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
			atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			atts[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

			VkSubpassDescription sp = {};
			sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			sp.colorAttachmentCount = 1;
			sp.pColorAttachments = &cref;
			sp.pDepthStencilAttachment = &dref;

			VkSubpassDependency dep = {};
			dep.srcSubpass = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass = 0;
			dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 2, atts, 1, &sp, 1, &dep };
			if (s_vk.vkCreateRenderPass(s_device, &rpci, NULL, &s_renderPass) != VK_SUCCESS) return false;
		}

		s_framebuffers.resize(scImgCount, VK_NULL_HANDLE);
		for (uint32_t i = 0; i < scImgCount; ++i)
		{
			VkImageView views[2] = { s_swapViews[i], s_depthView };
			VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, s_renderPass, 2, views, (uint32_t)s_width, (uint32_t)s_height, 1 };
			if (s_vk.vkCreateFramebuffer(s_device, &fbci, NULL, &s_framebuffers[i]) != VK_SUCCESS) return false;
		}

		s_gdrawHost.renderPass = s_renderPass;
		s_gdrawHost.colorFormat = s_colorFormat;
		s_gdrawHost.width = s_width;
		s_gdrawHost.height = s_height;
		return true;
	}

	static void RecreateSwapchain()
	{
		if (!s_device || !s_hwnd) return;
		RECT rc = {};
		GetClientRect(s_hwnd, &rc);
		int cw = rc.right - rc.left;
		int ch = rc.bottom - rc.top;
		if (cw <= 0 || ch <= 0) return;

		s_vk.vkDeviceWaitIdle(s_device);
		for (auto &kv : s_worldPipes) if (kv.second) s_vk.vkDestroyPipeline(s_device, kv.second, NULL);
		s_worldPipes.clear();
		DestroySwapchainObjects();
		CreateSwapchainObjects(cw, ch);
		s_swapchainDirty = false;
	}

	static void ExecuteDrawInternal(
		int primType, int count,
		VkBuffer staticVB, VkDeviceSize staticVBOff,
		const void *dynamicBytes, size_t dynamicByteCount,
		int vType, int psType,
		const Matrix4x4 &wvp, const Matrix4x4 &mv,
		int texId, const float stateColor[4],
		bool blendEnable, int blendSrc, int blendDst,
		bool depthTestEnable, bool depthMask,
		bool cullEnable, bool cullCW,
		bool alphaTestEnable, int alphaFunc, float alphaCutoff)
	{
		if (!s_device || !s_frameActive || count <= 0) return;
		EnsureRenderPassActive();

		FrameSync &fs = s_frames[s_frameIdx];
		VkCommandBuffer cmd = fs.cmdBuf;

		VkBuffer vb = staticVB;
		VkDeviceSize vbOff = staticVBOff;
		if (!vb)
		{
			if (!dynamicBytes || dynamicByteCount == 0) return;
			uint32_t ringOff = 0;
			FrameRingVBChunk *vbChunk = AllocFrameRingVB(fs, (VkDeviceSize)dynamicByteCount, ringOff);
			if (!vbChunk || !vbChunk->ptr) return;
			memcpy((uint8_t*)vbChunk->ptr + ringOff, dynamicBytes, dynamicByteCount);
			vb = vbChunk->buf;
			vbOff = ringOff;
		}

		uint32_t uboSlot = 0;
		FrameUBOChunk *uboChunk = AllocFrameUBOSlot(fs, uboSlot);
		if (!uboChunk || !uboChunk->ptr) return;

		const VKTextureData *td = NULL;
		if (texId > 0)
		{
			auto it = s_textures.find(texId);
			if (it != s_textures.end() && it->second.view) td = &it->second;
		}
		const VKTextureData *ltd = NULL;
		if (s_tls.boundVertexTextureId > 0)
		{
			auto itL = s_textures.find(s_tls.boundVertexTextureId);
			if (itL != s_textures.end() && itL->second.view) ltd = &itL->second;
		}

		VSConstantBufferData *vsPtr = (VSConstantBufferData*)((uint8_t*)uboChunk->ptr + uboSlot);
		VKPSUniformBlock *psBlock = (VKPSUniformBlock*)((uint8_t*)uboChunk->ptr + uboSlot + C4J_VK_UBO_PS_OFFSET);
		FillVSConstants(s_tls, wvp, mv, vType, psType, *vsPtr);
		FillPSConstants(s_tls, stateColor, td != NULL, ltd != NULL, alphaTestEnable, alphaFunc, alphaCutoff, psType, psBlock->ps);
		psBlock->shader = s_shaderFrameParams;

		int smpIdx = td ? ((td->linearFilter ? 2 : 0) + (td->clampWrap ? 1 : 0)) : 0;
		VkImageView view0 = td ? td->view : s_defaultWhiteTex.view;
		VkImageView view1 = ltd ? ltd->view : s_defaultWhiteTex.view;
		VkDescriptorSet dset = GetOrCreateWorldDescSet(fs, *uboChunk, view0, smpIdx, view1);
		if (!dset) return;

		const bool isQuad = (primType == 3 || primType == 7);
		const bool isFan  = (primType == 2);
		uint8_t topoCode = 0;
		if (!isQuad && !isFan)
		{
			if (primType == 1) topoCode = 1;
			else if (primType == 4) topoCode = 2;
			else if (primType == 5) topoCode = 3;
		}

		WorldPipeKey pk = {};
		pk.compressed = (vType == 1) ? 1 : 0;
		pk.topo = topoCode;
		pk.blendEnable = blendEnable ? 1 : 0;
		pk.blendSrc = (uint8_t)(blendSrc & 0xFF);
		pk.blendDst = (uint8_t)(blendDst & 0xFF);
		pk.colorMask = s_tls.colorWriteMask & 0xF;
		pk.depthTest = depthTestEnable ? 1 : 0;
		pk.depthMask = depthMask ? 1 : 0;
		pk.depthFunc = (uint8_t)(s_tls.depthFunc & 0xF);
		pk.cullEnable = cullEnable ? 1 : 0;
		pk.cullCW = cullCW ? 1 : 0;
		pk.stencilEnable = s_tls.stencilEnable ? 1 : 0;
		pk.stencilFunc = (uint8_t)(s_tls.stencilFunc & 0xF);
		pk.stencilRead = s_tls.stencilReadMask;
		pk.stencilWrite = s_tls.stencilWriteMask;
		pk.depthBiasEnable = (s_tls.depthBiasSlope != 0.0f || s_tls.depthBiasConstant != 0.0f) ? 1 : 0;
		pk.customShaderGen = (s_shaderMode == C4J_SHADER_MODE_CUSTOM && s_smCustomFrag != VK_NULL_HANDLE) ? s_customShaderGen : 0u;
		pk._pad = 0u;

		VkPipeline pipe = GetOrCreateWorldPipeline(pk);
		if (!pipe) return;

		if (s_boundPipe != pipe)
		{
			s_vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
			s_boundPipe = pipe;
		}
		/* glWrapper::glPolygonOffset divides OpenGL factor/units by 65536.0f; restore scale for vkCmdSetDepthBias. */
		const float dbConst = pk.depthBiasEnable ? (s_tls.depthBiasConstant * 65536.0f) : 0.0f;
		const float dbSlope = pk.depthBiasEnable ? (s_tls.depthBiasSlope * 65536.0f) : 0.0f;
		if (!s_boundDepthBiasValid || s_boundDepthBiasConst != dbConst || s_boundDepthBiasSlope != dbSlope)
		{
			s_vk.vkCmdSetDepthBias(cmd, dbConst, 0.0f, dbSlope);
			s_boundDepthBiasConst = dbConst;
			s_boundDepthBiasSlope = dbSlope;
			s_boundDepthBiasValid = true;
		}

		float blendFactor[4] = {
			(float)((s_tls.blendFactorRGBA >> 16) & 0xFF) / 255.0f,
			(float)((s_tls.blendFactorRGBA >> 8)  & 0xFF) / 255.0f,
			(float)( s_tls.blendFactorRGBA        & 0xFF) / 255.0f,
			(float)((s_tls.blendFactorRGBA >> 24) & 0xFF) / 255.0f
		};
		if (blendSrc == C4J_BLEND_FACTOR_ALPHA || blendDst == C4J_BLEND_INV_FACTOR_ALPHA ||
			blendSrc == C4J_BLEND_INV_FACTOR_ALPHA || blendDst == C4J_BLEND_FACTOR_ALPHA)
		{
			blendFactor[0] = blendFactor[1] = blendFactor[2] = blendFactor[3];
		}
		if (!s_boundBlendConstValid || memcmp(s_boundBlendConst, blendFactor, sizeof(blendFactor)) != 0)
		{
			s_vk.vkCmdSetBlendConstants(cmd, blendFactor);
			memcpy(s_boundBlendConst, blendFactor, sizeof(blendFactor));
			s_boundBlendConstValid = true;
		}
		if (pk.stencilEnable && (!s_boundStencilRefValid || s_boundStencilRef != (uint32_t)s_tls.stencilRef))
		{
			s_vk.vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s_tls.stencilRef);
			s_boundStencilRef = s_tls.stencilRef;
			s_boundStencilRefValid = true;
		}

		uint32_t dynOffsets[2] = { uboSlot, uboSlot + C4J_VK_UBO_PS_OFFSET };
		s_vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_worldPLLayout, 0, 1, &dset, 2, dynOffsets);
		if (s_boundVB != vb || s_boundVBOff != vbOff)
		{
			s_vk.vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOff);
			s_boundVB = vb;
			s_boundVBOff = vbOff;
		}

		if (isQuad && s_quadIB && count >= 4 && (count % 4) == 0)
		{
			if (s_boundIB != s_quadIB) { s_vk.vkCmdBindIndexBuffer(cmd, s_quadIB, 0, VK_INDEX_TYPE_UINT32); s_boundIB = s_quadIB; }
			uint32_t quadsRemaining = (uint32_t)(count / 4);
			uint32_t vertStart = 0;
			while (quadsRemaining > 0)
			{
				uint32_t batch = (quadsRemaining > 65536u) ? 65536u : quadsRemaining;
				s_vk.vkCmdDrawIndexed(cmd, batch * 6u, 1, 0, (int32_t)vertStart, 0);
				quadsRemaining -= batch;
				vertStart += batch * 4u;
			}
		}
		else if (isFan)
		{
			if (count >= 3 && s_fanIB)
			{
				uint32_t tris = (uint32_t)(count - 2);
				if (tris > 4096u) tris = 4096u;
				if (s_boundIB != s_fanIB) { s_vk.vkCmdBindIndexBuffer(cmd, s_fanIB, 0, VK_INDEX_TYPE_UINT32); s_boundIB = s_fanIB; }
				s_vk.vkCmdDrawIndexed(cmd, tris * 3u, 1, 0, 0, 0);
			}
		}
		else
		{
			s_vk.vkCmdDraw(cmd, (uint32_t)count, 1, 0, 0);
		}
	}

	static void *GetMCImageViewCallback(int textureId, int *outW, int *outH, bool *outLinear, bool *outClamp)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		auto it = s_textures.find(textureId);
		if (it == s_textures.end() || !it->second.view) return NULL;
		if (outW) *outW = it->second.width;
		if (outH) *outH = it->second.height;
		if (outLinear) *outLinear = it->second.linearFilter;
		if (outClamp) *outClamp = it->second.clampWrap;
		return (void*)it->second.view;
	}

	static int VK_InitialiseWindow(HWND hwnd, int width, int height, int fullscreen, int vsync)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_hwnd = hwnd;
		s_width = (width > 0) ? width : 1280;
		s_height = (height > 0) ? height : 720;
		s_fullscreen = (fullscreen != 0);
		s_vsync = (vsync != 0);
		s_suspended = false;

		if (!s_vk.LoadGlobal())
		{
			printf("[RenderVulkan] Failed to load vulkan-1.dll\n");
			return 0;
		}

		VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "LegacyCraft", 1, "LegacyCraft", 1, VK_API_VERSION_1_1 };
		const char *instExts[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
		VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &appInfo, 0, NULL, 2, instExts };
		if (s_vk.vkCreateInstance(&ici, NULL, &s_instance) != VK_SUCCESS)
		{
			appInfo.apiVersion = VK_API_VERSION_1_0;
			if (s_vk.vkCreateInstance(&ici, NULL, &s_instance) != VK_SUCCESS) return 0;
		}
		if (!s_vk.LoadInstance(s_instance)) return 0;

		VkWin32SurfaceCreateInfoKHR wsci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, NULL, 0, GetModuleHandleA(NULL), s_hwnd };
		if (s_vk.vkCreateWin32SurfaceKHR(s_instance, &wsci, NULL, &s_surface) != VK_SUCCESS) return 0;

		uint32_t devCount = 0;
		s_vk.vkEnumeratePhysicalDevices(s_instance, &devCount, NULL);
		if (devCount == 0) return 0;
		std::vector<VkPhysicalDevice> devs(devCount);
		s_vk.vkEnumeratePhysicalDevices(s_instance, &devCount, devs.data());

		uint64_t bestScore = 0;
		uint32_t chosenGfxQ = 0;
		for (auto pd : devs)
		{
			uint32_t qCount = 0;
			s_vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, NULL);
			std::vector<VkQueueFamilyProperties> qps(qCount);
			s_vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qps.data());

			uint32_t qIdx = 0xFFFFFFFFu;
			for (uint32_t i = 0; i < qCount; ++i)
			{
				VkBool32 sup = VK_FALSE;
				s_vk.vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, s_surface, &sup);
				if ((qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && sup == VK_TRUE)
				{
					qIdx = i;
					break;
				}
			}
			if (qIdx == 0xFFFFFFFFu) continue;

			VkPhysicalDeviceProperties props = {};
			VkPhysicalDeviceMemoryProperties mp = {};
			s_vk.vkGetPhysicalDeviceProperties(pd, &props);
			s_vk.vkGetPhysicalDeviceMemoryProperties(pd, &mp);

			uint64_t vramMB = 0;
			for (uint32_t h = 0; h < mp.memoryHeapCount; ++h)
			{
				if (mp.memoryHeaps[h].flags & 1u /* DEVICE_LOCAL */) vramMB += mp.memoryHeaps[h].size / (1024u * 1024u);
			}
			uint64_t score = vramMB;
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 100000000ULL;
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 10000000ULL;
			if (props.vendorID == 0x10DEu || props.vendorID == 0x1002u) score += 50000000ULL;

			if (!s_physDev || score > bestScore)
			{
				bestScore = score;
				s_physDev = pd;
				s_memProps = mp;
				chosenGfxQ = qIdx;
				snprintf(s_adapterName, sizeof(s_adapterName), "%s", props.deviceName);
				s_adapterName[sizeof(s_adapterName) - 1] = 0;
			}
		}
		if (!s_physDev) return 0;
		s_gfxFamily = chosenGfxQ;
		printf("[RenderVulkan] Selected GPU: %s\n", s_adapterName);

		uint32_t fmtProps[3] = {};
		s_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
		s_vk.vkGetPhysicalDeviceFormatProperties(s_physDev, VK_FORMAT_D24_UNORM_S8_UINT, fmtProps);
		if ((fmtProps[1] & 0x00000200u /* DEPTH_STENCIL_ATTACHMENT_BIT */) == 0)
		{
			s_depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
		}

		float qPrio = 1.0f;
		VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, s_gfxFamily, 1, &qPrio };
		const char *devExts[] = { "VK_KHR_swapchain" };
		VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 1, devExts, NULL };
		if (s_vk.vkCreateDevice(s_physDev, &dci, NULL, &s_device) != VK_SUCCESS) return 0;
		if (!s_vk.LoadDevice(s_device)) return 0;
		s_vk.vkGetDeviceQueue(s_device, s_gfxFamily, 0, &s_queue);

		VkCommandPoolCreateInfo upci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, s_gfxFamily };
		s_vk.vkCreateCommandPool(s_device, &upci, NULL, &s_uploadPool);
		VkFenceCreateInfo ufci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0 };
		s_vk.vkCreateFence(s_device, &ufci, NULL, &s_uploadFence);

		if (!CreateSwapchainObjects(s_width, s_height)) return 0;

		/* Frame sync + ring buffers */
		VkSemaphoreCreateInfo smci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
		VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

		for (int i = 0; i < C4J_VK_FRAMES_IN_FLIGHT; ++i)
		{
			s_vk.vkCreateCommandPool(s_device, &upci, NULL, &s_frames[i].cmdPool);
			VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, s_frames[i].cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
			s_vk.vkAllocateCommandBuffers(s_device, &cbai, &s_frames[i].cmdBuf);
			s_vk.vkCreateSemaphore(s_device, &smci, NULL, &s_frames[i].imgAvailSem);
			s_vk.vkCreateSemaphore(s_device, &smci, NULL, &s_frames[i].renderDoneSem);
			s_vk.vkCreateFence(s_device, &fci, NULL, &s_frames[i].inFlightFence);
			VkDescriptorPool pool = CreateWorldDescPool();
			if (pool) s_frames[i].descPools.push_back(pool);
			s_frames[i].descPoolUsed = 0;
			s_frames[i].texCacheEpoch = s_texCacheEpoch;

			FrameRingVBChunk vbChunk = {};
			vbChunk.capacity = C4J_VK_RING_VB_BYTES;
			CreateHostBuffer(vbChunk.capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vbChunk.buf, vbChunk.mem, &vbChunk.ptr);
			s_frames[i].ringVBs.push_back(std::move(vbChunk));
			s_frames[i].activeRingVB = 0;

			FrameUBOChunk uboChunk = {};
			uboChunk.capacity = C4J_VK_UBO_CHUNK_DRAWS * C4J_VK_UBO_STRIDE;
			CreateHostBuffer(uboChunk.capacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboChunk.buf, uboChunk.mem, &uboChunk.ptr);
			s_frames[i].uboChunks.push_back(std::move(uboChunk));
			s_frames[i].activeUBOChunk = 0;

			CreateHostBuffer(C4J_VK_FRAME_STAGE_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				s_frames[i].stageBuf, s_frames[i].stageMem, &s_frames[i].stagePtr);
		}

		/* Shaders, layouts & static IBs */
		auto makeMod = [&](const uint32_t *code, size_t sz) -> VkShaderModule {
			VkShaderModuleCreateInfo sm = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sz, code };
			VkShaderModule mod = VK_NULL_HANDLE;
			s_vk.vkCreateShaderModule(s_device, &sm, NULL, &mod);
			return mod;
		};
		s_smMcStd  = makeMod(spv_mc_std_vert, spv_mc_std_vert_size);
		s_smMcComp = makeMod(spv_mc_comp_vert, spv_mc_comp_vert_size);
		s_smMcFrag = makeMod(spv_mc_world_frag, spv_mc_world_frag_size);

		VkDescriptorSetLayoutBinding wB[4] = {
			{ 0, C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL },
			{ 1, C4J_VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
			{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
		};
		VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 4, wB };
		s_vk.vkCreateDescriptorSetLayout(s_device, &dslci, NULL, &s_worldDSL);
		VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &s_worldDSL, 0, NULL };
		s_vk.vkCreatePipelineLayout(s_device, &plci, NULL, &s_worldPLLayout);

		for (int i = 0; i < 4; ++i)
		{
			VkSamplerCreateInfo sci = {};
			const bool linear = (i >= 2);
			sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sci.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sci.addressModeU = (i & 1) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sci.addressModeV = sci.addressModeU;
			sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.mipLodBias = linear ? 0.0f : -1.0f;
			sci.maxLod = linear ? 16.0f : 2.0f;
			s_vk.vkCreateSampler(s_device, &sci, NULL, &s_samplers[i]);
		}

		{
			const uint32_t maxQuads = 65536;
			std::vector<uint32_t> indices(maxQuads * 6);
			for (uint32_t q = 0; q < maxQuads; ++q)
			{
				uint32_t b = q * 4;
				indices[q * 6 + 0] = b + 0; indices[q * 6 + 1] = b + 1; indices[q * 6 + 2] = b + 2;
				indices[q * 6 + 3] = b + 2; indices[q * 6 + 4] = b + 3; indices[q * 6 + 5] = b + 0;
			}
			CreateBufferDeviceLocal(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), s_quadIB, s_quadIBMem);
		}
		{
			const uint32_t maxTris = 4096;
			std::vector<uint32_t> indices(maxTris * 3);
			for (uint32_t t = 0; t < maxTris; ++t)
			{
				indices[t * 3 + 0] = 0; indices[t * 3 + 1] = t + 1; indices[t * 3 + 2] = t + 2;
			}
			CreateBufferDeviceLocal(indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), s_fanIB, s_fanIBMem);
		}

		/* 1x1 white fallback texture */
		{
			const uint8_t white[4] = { 255, 255, 255, 255 };
			VkImageCreateInfo iciW = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {1,1,1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
			s_vk.vkCreateImage(s_device, &iciW, NULL, &s_defaultWhiteTex.image);
			VkMemoryRequirements mr = {};
			s_vk.vkGetImageMemoryRequirements(s_device, s_defaultWhiteTex.image, &mr);
			VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
			s_vk.vkAllocateMemory(s_device, &mai, NULL, &s_defaultWhiteTex.mem);
			s_vk.vkBindImageMemory(s_device, s_defaultWhiteTex.image, s_defaultWhiteTex.mem, 0);
			VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, s_defaultWhiteTex.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {VK_COMPONENT_SWIZZLE_IDENTITY}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
			s_vk.vkCreateImageView(s_device, &ivci, NULL, &s_defaultWhiteTex.view);
			s_defaultWhiteTex.width = s_defaultWhiteTex.height = 1;
			UploadImageData(s_defaultWhiteTex.image, 1, 1, 0, white, 4, true);
		}

		s_gdrawHost.vk = &s_vk;
		s_gdrawHost.physicalDevice = s_physDev;
		s_gdrawHost.device = s_device;
		s_gdrawHost.graphicsQueue = s_queue;
		s_gdrawHost.graphicsQueueFamily = s_gfxFamily;
		s_gdrawHost.memProps = s_memProps;
		s_gdrawHost.renderPass = s_renderPass;
		s_gdrawHost.colorFormat = s_colorFormat;
		s_gdrawHost.depthStencilFormat = s_depthFormat;
		s_gdrawHost.width = s_width;
		s_gdrawHost.height = s_height;
		s_gdrawHost.FindMemoryType = FindMemoryType;
		s_gdrawHost.EnsureRenderPassActive = EnsureRenderPassActive;
		s_gdrawHost.EndRenderPassIfActive = EndRenderPassIfActive;
		s_gdrawHost.InvalidateWorldPipelineCache = InvalidateWorldPipelineCache;
		s_gdrawHost.UploadImageData = UploadImageData;
		s_gdrawHost.GetMCImageView = GetMCImageViewCallback;
		s_gdrawHost.ApplyMCViewport = ApplyMCViewport;
		s_gdrawHost.QueueGarbage = QueueGarbage;
		return 1;
	}

	static int VK_ResizeWindow(int width, int height, int fullscreen)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (width <= 0 || height <= 0) return 0;
		s_width = width;
		s_height = height;
		s_fullscreen = (fullscreen != 0);
		if (s_device && !s_frameActive)
		{
			RecreateSwapchain();
		}
		else
		{
			s_swapchainDirty = true;
		}
		return 1;
	}

	static void VK_Shutdown(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (s_device)
		{
			s_vk.vkDeviceWaitIdle(s_device);
			if (s_gdrawInitialized) { gdraw_VK_DestroyContext(); s_gdrawInitialized = false; }

			DrainGarbage(true);
			s_pendingTexOps.clear();
			{
				std::lock_guard<std::mutex> cbLock(s_cbuffMutex);
				s_pendingCBuffUploads.clear();
				s_cbuffs.clear();
				s_totalCBuffBytes.store(0, std::memory_order_relaxed);
			}
			for (size_t i = 0; i < s_vbSlabs.size(); ++i)
			{
				if (s_vbSlabs[i].buf) s_vk.vkDestroyBuffer(s_device, s_vbSlabs[i].buf, NULL);
				if (s_vbSlabs[i].mem) s_vk.vkFreeMemory(s_device, s_vbSlabs[i].mem, NULL);
			}
			s_vbSlabs.clear();
			s_pendingSlabFrees.clear();
			for (auto &kv : s_textures)
			{
				if (kv.second.view) s_vk.vkDestroyImageView(s_device, kv.second.view, NULL);
				if (kv.second.image) s_vk.vkDestroyImage(s_device, kv.second.image, NULL);
				if (kv.second.mem) s_vk.vkFreeMemory(s_device, kv.second.mem, NULL);
			}
			s_textures.clear();

			if (s_defaultWhiteTex.view)  s_vk.vkDestroyImageView(s_device, s_defaultWhiteTex.view, NULL);
			if (s_defaultWhiteTex.image) s_vk.vkDestroyImage(s_device, s_defaultWhiteTex.image, NULL);
			if (s_defaultWhiteTex.mem)   s_vk.vkFreeMemory(s_device, s_defaultWhiteTex.mem, NULL);

			for (auto &kv : s_worldPipes) if (kv.second) s_vk.vkDestroyPipeline(s_device, kv.second, NULL);
			s_worldPipes.clear();

			for (int i = 0; i < 4; ++i) if (s_samplers[i]) { s_vk.vkDestroySampler(s_device, s_samplers[i], NULL); s_samplers[i] = VK_NULL_HANDLE; }
			if (s_quadIB)    s_vk.vkDestroyBuffer(s_device, s_quadIB, NULL);
			if (s_quadIBMem) s_vk.vkFreeMemory(s_device, s_quadIBMem, NULL);
			if (s_fanIB)     s_vk.vkDestroyBuffer(s_device, s_fanIB, NULL);
			if (s_fanIBMem)  s_vk.vkFreeMemory(s_device, s_fanIBMem, NULL);
			if (s_worldPLLayout) s_vk.vkDestroyPipelineLayout(s_device, s_worldPLLayout, NULL);
			if (s_worldDSL)      s_vk.vkDestroyDescriptorSetLayout(s_device, s_worldDSL, NULL);
			if (s_smMcStd)       s_vk.vkDestroyShaderModule(s_device, s_smMcStd, NULL);
			if (s_smMcComp)      s_vk.vkDestroyShaderModule(s_device, s_smMcComp, NULL);
			if (s_smMcFrag)      s_vk.vkDestroyShaderModule(s_device, s_smMcFrag, NULL);
			if (s_smCustomStd)   { s_vk.vkDestroyShaderModule(s_device, s_smCustomStd, NULL); s_smCustomStd = VK_NULL_HANDLE; }
			if (s_smCustomComp)  { s_vk.vkDestroyShaderModule(s_device, s_smCustomComp, NULL); s_smCustomComp = VK_NULL_HANDLE; }
			if (s_smCustomFrag)  { s_vk.vkDestroyShaderModule(s_device, s_smCustomFrag, NULL); s_smCustomFrag = VK_NULL_HANDLE; }

			for (int i = 0; i < C4J_VK_FRAMES_IN_FLIGHT; ++i)
			{
				for (auto &vc : s_frames[i].ringVBs)
				{
					if (vc.ptr) s_vk.vkUnmapMemory(s_device, vc.mem);
					if (vc.buf) s_vk.vkDestroyBuffer(s_device, vc.buf, NULL);
					if (vc.mem) s_vk.vkFreeMemory(s_device, vc.mem, NULL);
				}
				s_frames[i].ringVBs.clear();
				for (auto &uc : s_frames[i].uboChunks)
				{
					if (uc.ptr) s_vk.vkUnmapMemory(s_device, uc.mem);
					if (uc.buf) s_vk.vkDestroyBuffer(s_device, uc.buf, NULL);
					if (uc.mem) s_vk.vkFreeMemory(s_device, uc.mem, NULL);
				}
				s_frames[i].uboChunks.clear();
				if (s_frames[i].stagePtr)   s_vk.vkUnmapMemory(s_device, s_frames[i].stageMem);
				if (s_frames[i].stageBuf)   s_vk.vkDestroyBuffer(s_device, s_frames[i].stageBuf, NULL);
				if (s_frames[i].stageMem)   s_vk.vkFreeMemory(s_device, s_frames[i].stageMem, NULL);
				for (auto dp : s_frames[i].descPools)
				{
					if (dp) s_vk.vkDestroyDescriptorPool(s_device, dp, NULL);
				}
				s_frames[i].descPools.clear();
				if (s_frames[i].inFlightFence) s_vk.vkDestroyFence(s_device, s_frames[i].inFlightFence, NULL);
				if (s_frames[i].imgAvailSem)   s_vk.vkDestroySemaphore(s_device, s_frames[i].imgAvailSem, NULL);
				if (s_frames[i].renderDoneSem) s_vk.vkDestroySemaphore(s_device, s_frames[i].renderDoneSem, NULL);
				if (s_frames[i].cmdPool)    s_vk.vkDestroyCommandPool(s_device, s_frames[i].cmdPool, NULL);
			}

			DestroySwapchainObjects();
			if (s_renderPass)  s_vk.vkDestroyRenderPass(s_device, s_renderPass, NULL);
			if (s_stagePtr)    s_vk.vkUnmapMemory(s_device, s_stageMem);
			if (s_stageBuf)    s_vk.vkDestroyBuffer(s_device, s_stageBuf, NULL);
			if (s_stageMem)    s_vk.vkFreeMemory(s_device, s_stageMem, NULL);
			if (s_uploadFence) s_vk.vkDestroyFence(s_device, s_uploadFence, NULL);
			if (s_uploadPool)  s_vk.vkDestroyCommandPool(s_device, s_uploadPool, NULL);
			s_vk.vkDestroyDevice(s_device, NULL);
			s_device = VK_NULL_HANDLE;
		}
		if (s_surface)  { s_vk.vkDestroySurfaceKHR(s_instance, s_surface, NULL); s_surface = VK_NULL_HANDLE; }
		if (s_instance) { s_vk.vkDestroyInstance(s_instance, NULL); s_instance = VK_NULL_HANDLE; }
	}

	static void VK_SetVsync(int enabled)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		bool want = (enabled != 0);
		if (s_vsync != want)
		{
			s_vsync = want;
			s_swapchainDirty = true;
		}
	}
	static int VK_GetVsync(void) { return s_vsync ? 1 : 0; }
	static void VK_GetBackendInfo(C4JBackendInfo *outInfo)
	{
		if (!outInfo) return;
		outInfo->backendType = C4J_BACKEND_VULKAN;
		outInfo->vsyncEnabled = s_vsync ? 1 : 0;
		outInfo->screenWidth = s_width;
		outInfo->screenHeight = s_height;
		snprintf(outInfo->adapterName, sizeof(outInfo->adapterName), "%s", s_adapterName);
		outInfo->adapterName[sizeof(outInfo->adapterName) - 1] = 0;
		snprintf(outInfo->backendName, sizeof(outInfo->backendName), "Vulkan");
		outInfo->backendName[sizeof(outInfo->backendName) - 1] = 0;
	}

	static void VK_Tick(void) {}
	static void VK_UpdateGamma(unsigned short) {}

	static void VK_StartFrame(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_tls.ResetFrameState();
		if (!s_device || s_suspended) return;
		if (s_frameActive)
		{
			EnsureRenderPassActive();
			ApplyMCViewport();
			InvalidateWorldPipelineCache();
			return;
		}

		if (s_swapchainDirty)
		{
			RecreateSwapchain();
		}
		if (!s_swapchain) return;

		FrameSync &fs = s_frames[s_frameIdx];
		s_vk.vkWaitForFences(s_device, 1, &fs.inFlightFence, VK_TRUE, ~0ULL);
		DrainGarbage(false);

		VkResult acq = s_vk.vkAcquireNextImageKHR(s_device, s_swapchain, ~0ULL, fs.imgAvailSem, VK_NULL_HANDLE, &s_activeImgIdx);
		if (acq == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return;
		}
		if (acq == VK_SUCCESS || acq == VK_SUBOPTIMAL_KHR)
		{
			fs.imgAvailPending = true;
		}

		s_vk.vkResetFences(s_device, 1, &fs.inFlightFence);
		s_vk.vkResetCommandPool(s_device, fs.cmdPool, 0);
		if (fs.texCacheEpoch != s_texCacheEpoch)
		{
			for (auto dp : fs.descPools)
			{
				if (dp) s_vk.vkResetDescriptorPool(s_device, dp, 0);
			}
			for (auto &uc : fs.uboChunks) uc.descCache.clear();
			fs.descPoolUsed = 0;
			fs.texCacheEpoch = s_texCacheEpoch;
		}
		for (auto &vc : fs.ringVBs) vc.offset = 0;
		fs.activeRingVB = 0;
		for (auto &uc : fs.uboChunks) uc.offset = 0;
		fs.activeUBOChunk = 0;
		fs.stageOff = 0;

		VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		s_vk.vkBeginCommandBuffer(fs.cmdBuf, &cbi);

		TransitionImage(fs.cmdBuf, s_swapImages[s_activeImgIdx], VK_IMAGE_ASPECT_COLOR_BIT,
			s_swapLayouts[s_activeImgIdx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, 1, 0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		s_swapLayouts[s_activeImgIdx] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		if (s_depthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			TransitionImage(fs.cmdBuf, s_depthImage, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				s_depthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				0, 1, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
			s_depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		s_frameSeq++;
		s_frameActive = true;
		s_inRenderPass = false;
		s_gdrawHost.activeCmd = fs.cmdBuf;
		s_gdrawHost.frameIndex = s_frameIdx;
		s_gdrawHost.frameSeq = s_frameSeq;
		FlushPendingTexOps();
		FlushPendingCBuffUploads();
		EnsureRenderPassActive();
	}

	static void VK_DoScreenGrabOnNextPresent(void) {}

	static void VK_Present(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device || !s_frameActive || s_suspended) return;

		FrameSync &fs = s_frames[s_frameIdx];
		EnsureRenderPassActive();
		EndRenderPassIfActive();

		TransitionImage(fs.cmdBuf, s_swapImages[s_activeImgIdx], VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			0, 1, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		s_swapLayouts[s_activeImgIdx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		s_vk.vkEndCommandBuffer(fs.cmdBuf);

		VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo si = {
			VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL,
			fs.imgAvailPending ? 1u : 0u,
			fs.imgAvailPending ? &fs.imgAvailSem : NULL,
			fs.imgAvailPending ? &waitStage : NULL,
			1, &fs.cmdBuf, 1, &fs.renderDoneSem
		};
		fs.imgAvailPending = false;
		s_vk.vkQueueSubmit(s_queue, 1, &si, fs.inFlightFence);

		VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &fs.renderDoneSem, 1, &s_swapchain, &s_activeImgIdx, NULL };
		VkResult pr = s_vk.vkQueuePresentKHR(s_queue, &pi);
		if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
		{
			s_swapchainDirty = true;
		}

		s_frameActive = false;
		s_gdrawHost.activeCmd = VK_NULL_HANDLE;
		s_frameIdx = (s_frameIdx + 1) % C4J_VK_FRAMES_IN_FLIGHT;
	}

	static void VK_Clear(int flags, const C4JClearRect *pRect)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device || !s_frameActive) return;
		EnsureRenderPassActive();

		VkClearAttachment atts[2] = {};
		uint32_t attCount = 0;
		if (flags & C4J_CLEAR_COLOUR_FLAG)
		{
			atts[attCount].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			atts[attCount].colorAttachment = 0;
			memcpy(atts[attCount].clearValue.color.float32, s_clearColour, sizeof(float) * 4);
			attCount++;
		}
		if (flags & (C4J_CLEAR_DEPTH_FLAG | GL_STENCIL_BUFFER_BIT))
		{
			if (flags & C4J_CLEAR_DEPTH_FLAG)  atts[attCount].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			if (flags & GL_STENCIL_BUFFER_BIT) atts[attCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			atts[attCount].clearValue.depthStencil.depth = 1.0f;
			atts[attCount].clearValue.depthStencil.stencil = 0;
			attCount++;
		}
		if (attCount == 0) return;

		VkRect2D rect = { { 0, 0 }, { (uint32_t)s_width, (uint32_t)s_height } };
		if (pRect)
		{
			int32_t x0 = std::max<int32_t>(0, (int32_t)pRect->left);
			int32_t y0 = std::max<int32_t>(0, (int32_t)pRect->top);
			int32_t x1 = std::min<int32_t>(s_width, (int32_t)pRect->right);
			int32_t y1 = std::min<int32_t>(s_height, (int32_t)pRect->bottom);
			if (x1 <= x0 || y1 <= y0) return;
			rect.offset = { x0, y0 };
			rect.extent = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) };
		}
		VkClearRect cr = { rect, 0, 1 };
		s_vk.vkCmdClearAttachments(s_frames[s_frameIdx].cmdBuf, attCount, atts, 1, &cr);
	}

	static void VK_SetClearColour(const float colourRGBA[4]) { if (colourRGBA) memcpy(s_clearColour, colourRGBA, sizeof(float) * 4); }
	static int  VK_IsWidescreen(void) { return 1; }
	static int  VK_IsHiDef(void) { return 1; }
	static void VK_NoOpVoid(void) {}
	static void VK_NoOpInt(int) {}

	/* Matrix stack */
	static void VK_MatrixMode(int type) { if (type >= 0 && type < 3) { s_tls.matrixMode = type; s_tls.EnsureStackInit(type); } }
	static void VK_MatrixSetIdentity(void) { s_tls.EnsureStackInit(s_tls.matrixMode); s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity(); s_tls.matrixDirty = true; }
	static void VK_MatrixTranslate(float x, float y, float z) { MatrixTranslate(s_tls, x, y, z); }
	static void VK_MatrixRotate(float a, float x, float y, float z) { MatrixRotate(s_tls, a, x, y, z); }
	static void VK_MatrixScale(float x, float y, float z) { MatrixScale(s_tls, x, y, z); }
	static void VK_MatrixPerspective(float f, float a, float zn, float zf) { MatrixPerspective(s_tls, f, a, zn, zf); }
	static void VK_MatrixOrthogonal(float l, float r, float b, float t, float zn, float zf) { MatrixOrthogonal(s_tls, l, r, b, t, zn, zf); }
	static void VK_MatrixPop(void)
	{
		s_tls.EnsureStackInit(s_tls.matrixMode);
		if (s_tls.matrixStacks[s_tls.matrixMode].size() > 1) s_tls.matrixStacks[s_tls.matrixMode].pop_back();
		else s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Identity();
		s_tls.matrixDirty = true;
	}
	static void VK_MatrixPush(void)
	{
		s_tls.EnsureStackInit(s_tls.matrixMode);
		if (s_tls.matrixStacks[s_tls.matrixMode].size() < 64)
			s_tls.matrixStacks[s_tls.matrixMode].push_back(s_tls.matrixStacks[s_tls.matrixMode].back());
	}
	static void VK_MatrixMult(float *mat)
	{
		if (!mat) return;
		s_tls.EnsureStackInit(s_tls.matrixMode);
		Matrix4x4 rhs; memcpy(rhs.m, mat, sizeof(rhs.m));
		s_tls.matrixStacks[s_tls.matrixMode].back() = Matrix4x4::Multiply(s_tls.matrixStacks[s_tls.matrixMode].back(), rhs);
		s_tls.matrixDirty = true;
	}
	static const float *VK_MatrixGet(int type) { if (type<0||type>=3) type=0; s_tls.EnsureStackInit(type); return s_tls.matrixStacks[type].back().m; }
	static void VK_SetMatrixDirty(void) { s_tls.matrixDirty = true; }

	/* Drawing */
	static void VK_DrawVertices(int primitiveType, int count, void *dataIn, int vType, int psType)
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

		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primitiveType, count, VK_NULL_HANDLE, 0, dataIn, totalBytes, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	static void VK_DrawVertexBuffer(int primType, int count, void *bufferHandle, int vType, int psType)
	{
		if (!bufferHandle || count <= 0) return;
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_tls.EnsureStackInit(0);
		s_tls.EnsureStackInit(1);
		Matrix4x4 mv = s_tls.matrixStacks[0].back();
		Matrix4x4 wvp = Matrix4x4::Multiply(s_tls.matrixStacks[1].back(), mv);
		ExecuteDrawInternal(primType, count, (VkBuffer)bufferHandle, 0, NULL, 0, vType, psType,
			wvp, mv, s_tls.boundTextureId, s_tls.stateColour,
			s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
			s_tls.depthTestEnable, s_tls.depthMask,
			s_tls.cullEnable, s_tls.cullCW,
			s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
	}

	/* Command buffers with slab-allocated static GPU VBs and pre-renderpass batched uploads */
	static int VK_CBuffCreate(int count)
	{
		if (count <= 0) return 0;
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		int first = s_nextCBuffId;
		s_nextCBuffId += count;
		return first;
	}
	static void VK_CBuffDelete(int first, int count)
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
					QueueSlabFrees(it->second.gpuAllocs);
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
				QueueSlabFrees(it->second.gpuAllocs);
				s_cbuffs.erase(it);
			}
		}
	}
	static void VK_CBuffStart(int index, int)
	{
		s_tls.recordingCBuffIndex = index;
		s_tls.recordingCommands.clear();
		s_tls.EnsureStackInit(0);
		s_tls.recordingBaseMatrix = s_tls.matrixStacks[0].back();
		Matrix4x4::Invert(s_tls.recordingBaseMatrix, s_tls.recordingInvBaseMatrix);
	}
	static void VK_CBuffClear(int index)
	{
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		if (it != s_cbuffs.end())
		{
			s_totalCBuffBytes.fetch_sub(it->second.byteSize, std::memory_order_relaxed);
			QueueSlabFrees(it->second.gpuAllocs);
			it->second.gpuAllocs.reset();
			it->second.commands.reset();
			it->second.byteSize = 0;
			it->second.generation = ++s_nextCBuffGen;
			it->second.uploadedGen = 0;
			it->second.queuedGen = 0;
		}
	}
	static int VK_CBuffSize(int index)
	{
		if (index == -1)
		{
			return (int)s_totalCBuffBytes.load(std::memory_order_relaxed);
		}
		std::lock_guard<std::mutex> lock(s_cbuffMutex);
		auto it = s_cbuffs.find(index);
		return (it != s_cbuffs.end()) ? (int)it->second.byteSize : 0;
	}
	static void VK_CBuffEnd(void)
	{
		if (s_tls.recordingCBuffIndex != 0)
		{
			const int cbuffIdx = s_tls.recordingCBuffIndex;
			size_t bytes = 0;
			for (const auto &c : s_tls.recordingCommands) bytes += c.vertexData.size();
			auto cmds = std::make_shared<std::vector<CBuffCommand>>(std::move(s_tls.recordingCommands));
			s_tls.recordingCommands.clear();

			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			VKCBuffSlot &slot = s_cbuffs[cbuffIdx];
			s_totalCBuffBytes.fetch_sub(slot.byteSize, std::memory_order_relaxed);
			QueueSlabFrees(slot.gpuAllocs);
			slot.gpuAllocs.reset();
			slot.commands = std::move(cmds);
			slot.byteSize = bytes;
			s_totalCBuffBytes.fetch_add(bytes, std::memory_order_relaxed);
			slot.generation = ++s_nextCBuffGen;
			slot.uploadedGen = 0;
			if (bytes > 0 && slot.commands && !slot.commands->empty())
			{
				slot.queuedGen = slot.generation;
				s_pendingCBuffUploads.push_back({ cbuffIdx, slot.generation });
			}
			else
			{
				slot.queuedGen = 0;
			}
			s_tls.recordingCBuffIndex = 0;
		}
	}
	static int VK_CBuffCall(int index, int)
	{
		std::shared_ptr<const std::vector<CBuffCommand>> cmds;
		std::shared_ptr<std::vector<VKSlabAlloc>> gpuAllocs;
		{
			std::lock_guard<std::mutex> lock(s_cbuffMutex);
			auto it = s_cbuffs.find(index);
			if (it == s_cbuffs.end() || !it->second.commands || it->second.commands->empty()) return 0;
			cmds = it->second.commands;
			if (it->second.uploadedGen == it->second.generation)
			{
				gpuAllocs = it->second.gpuAllocs;
			}
			else if (it->second.queuedGen != it->second.generation)
			{
				it->second.queuedGen = it->second.generation;
				s_pendingCBuffUploads.push_back({ index, it->second.generation });
			}
		}

		std::lock_guard<std::recursive_mutex> vkLock(s_vkMutex);
		if (!s_device) return 0;

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
			VkBuffer staticVB = VK_NULL_HANDLE;
			VkDeviceSize staticVBOff = 0;
			if (gpuAllocs && i < gpuAllocs->size())
			{
				staticVB = (*gpuAllocs)[i].buf;
				staticVBOff = (*gpuAllocs)[i].offset;
			}
			ExecuteDrawInternal(cmd.primitiveType, cmd.count, staticVB, staticVBOff,
				staticVB ? NULL : cmd.vertexData.data(), cmd.vertexData.size(),
				cmd.vType, cmd.psType, wvp, effModel, texId, combined,
				s_tls.blendEnable, s_tls.blendSrc, s_tls.blendDst,
				s_tls.depthTestEnable, s_tls.depthMask,
				s_tls.cullEnable, s_tls.cullCW,
				s_tls.alphaTestEnable, s_tls.alphaFunc, s_tls.alphaCutoff);
		}
		return 1;
	}

	/* Textures */
	static int VK_TextureCreate(void) { return s_nextTextureId.fetch_add(1); }
	static void VK_TextureFree(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		auto it = s_textures.find(idx);
		if (it != s_textures.end())
		{
			QueueGarbage(VK_NULL_HANDLE, it->second.image, it->second.view, it->second.mem);
			s_textures.erase(it);
			s_texCacheEpoch++;
		}
	}
	static void VK_TextureBind(int idx)
	{
		if (s_tls.boundTextureId != idx)
		{
			s_tls.textureLevels = 1;
		}
		s_tls.boundTextureId = idx;
	}
	static void VK_TextureBindVertex(int idx) { s_tls.boundVertexTextureId = idx; }
	static void VK_TextureSetLevels(int levels) { s_tls.textureLevels = levels; }
	static int  VK_TextureGetLevels(void)
	{
		if (s_tls.boundTextureId > 0)
		{
			std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end()) return it->second.mipLevels;
		}
		return s_tls.textureLevels;
	}
	static void VK_TextureData(int width, int height, void *data, int level, int)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device || s_tls.boundTextureId <= 0 || width <= 0 || height <= 0 || !data) return;

		if (level == 0)
		{
			bool prevLinear = false;
			bool prevClamp = false;
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end())
			{
				prevLinear = it->second.linearFilter;
				prevClamp = it->second.clampWrap;
				QueueGarbage(VK_NULL_HANDLE, it->second.image, it->second.view, it->second.mem);
				s_textures.erase(it);
				s_texCacheEpoch++;
			}
			uint32_t maxMips = ComputeMaxMipLevels(width, height);
			uint32_t mips = (s_tls.textureLevels > 1) ? (uint32_t)s_tls.textureLevels : 1u;
			if (mips > maxMips) mips = maxMips;
			s_tls.textureLevels = (int)mips;

			VKTextureData td = {};
			td.width = width;
			td.height = height;
			td.mipLevels = (int)mips;
			td.linearFilter = prevLinear;
			td.clampWrap = prevClamp;

			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = VK_FORMAT_R8G8B8A8_UNORM;
			ici.extent = { (uint32_t)width, (uint32_t)height, 1 };
			ici.mipLevels = mips;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (s_vk.vkCreateImage(s_device, &ici, NULL, &td.image) != VK_SUCCESS) return;

			VkMemoryRequirements mr = {};
			s_vk.vkGetImageMemoryRequirements(s_device, td.image, &mr);
			VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
			if (s_vk.vkAllocateMemory(s_device, &mai, NULL, &td.mem) != VK_SUCCESS)
			{
				s_vk.vkDestroyImage(s_device, td.image, NULL);
				return;
			}
			s_vk.vkBindImageMemory(s_device, td.image, td.mem, 0);

			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = td.image;
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1 };
			if (s_vk.vkCreateImageView(s_device, &ivci, NULL, &td.view) != VK_SUCCESS)
			{
				s_vk.vkDestroyImage(s_device, td.image, NULL);
				s_vk.vkFreeMemory(s_device, td.mem, NULL);
				return;
			}

			s_textures[s_tls.boundTextureId] = td;

			size_t bytes0 = (size_t)width * (size_t)height * 4u;
			VkBuffer stageBuf = VK_NULL_HANDLE;
			VkDeviceSize stageOff = 0;
			void *dst = NULL;
			if (s_frameActive && AllocFrameStaging(bytes0, 4, stageBuf, stageOff, &dst))
			{
				EndRenderPassIfActive();
				memcpy(dst, data, bytes0);
				RecordAsyncTexInit(s_frames[s_frameIdx].cmdBuf, td.image, mips, (uint32_t)width, (uint32_t)height, stageBuf, stageOff);
			}
			else if (!s_frameActive)
			{
				PendingTexOp op;
				op.texId = s_tls.boundTextureId;
				op.image = td.image;
				op.level = 0;
				op.mipLevels = mips;
				op.xoff = 0;
				op.yoff = 0;
				op.width = (uint32_t)width;
				op.height = (uint32_t)height;
				op.firstInitAllMips = true;
				op.pixels.assign((const uint8_t*)data, (const uint8_t*)data + bytes0);
				s_pendingTexOps.push_back(std::move(op));
			}
			else if (EnsureStagingSize(bytes0))
			{
				memcpy(s_stagePtr, data, bytes0);
				VkCommandBuffer cmd = BeginOneTimeCmd();
				RecordAsyncTexInit(cmd, td.image, mips, (uint32_t)width, (uint32_t)height, s_stageBuf, 0);
				EndOneTimeCmd(cmd);
			}
		}
		else
		{
			auto it = s_textures.find(s_tls.boundTextureId);
			if (it != s_textures.end() && it->second.image && level >= 0 && level < it->second.mipLevels)
			{
				size_t bytes = (size_t)width * (size_t)height * 4u;
				VkBuffer stageBuf = VK_NULL_HANDLE;
				VkDeviceSize stageOff = 0;
				void *dst = NULL;
				if (s_frameActive && AllocFrameStaging(bytes, 4, stageBuf, stageOff, &dst))
				{
					EndRenderPassIfActive();
					memcpy(dst, data, bytes);
					RecordAsyncTexUpdate(s_frames[s_frameIdx].cmdBuf, it->second.image, (uint32_t)level,
						0, 0, (uint32_t)width, (uint32_t)height, stageBuf, stageOff);
				}
				else if (!s_frameActive)
				{
					PendingTexOp op;
					op.texId = s_tls.boundTextureId;
					op.image = it->second.image;
					op.level = (uint32_t)level;
					op.mipLevels = (uint32_t)it->second.mipLevels;
					op.xoff = 0;
					op.yoff = 0;
					op.width = (uint32_t)width;
					op.height = (uint32_t)height;
					op.firstInitAllMips = false;
					op.pixels.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
					s_pendingTexOps.push_back(std::move(op));
				}
				else
				{
					UploadImageData(it->second.image, (uint32_t)width, (uint32_t)height, (uint32_t)level, data, bytes, false);
				}
			}
		}
	}
	static void VK_TextureDataUpdate(int xoff, int yoff, int width, int height, void *data, int level)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device || s_tls.boundTextureId <= 0 || !data || width <= 0 || height <= 0) return;
		auto it = s_textures.find(s_tls.boundTextureId);
		if (it != s_textures.end() && it->second.image && level >= 0 && level < it->second.mipLevels)
		{
			size_t bytes = (size_t)width * (size_t)height * 4u;
			VkBuffer stageBuf = VK_NULL_HANDLE;
			VkDeviceSize stageOff = 0;
			void *dst = NULL;
			if (s_frameActive && AllocFrameStaging(bytes, 4, stageBuf, stageOff, &dst))
			{
				EndRenderPassIfActive();
				memcpy(dst, data, bytes);
				RecordAsyncTexUpdate(s_frames[s_frameIdx].cmdBuf, it->second.image, (uint32_t)level,
					xoff, yoff, (uint32_t)width, (uint32_t)height, stageBuf, stageOff);
				return;
			}
			if (!s_frameActive)
			{
				PendingTexOp op;
				op.texId = s_tls.boundTextureId;
				op.image = it->second.image;
				op.level = (uint32_t)level;
				op.mipLevels = (uint32_t)it->second.mipLevels;
				op.xoff = xoff;
				op.yoff = yoff;
				op.width = (uint32_t)width;
				op.height = (uint32_t)height;
				op.firstInitAllMips = false;
				op.pixels.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
				s_pendingTexOps.push_back(std::move(op));
				return;
			}
			if (!EnsureStagingSize(bytes)) return;
			memcpy(s_stagePtr, data, bytes);
			VkCommandBuffer cmd = BeginOneTimeCmd();
			RecordAsyncTexUpdate(cmd, it->second.image, (uint32_t)level,
				xoff, yoff, (uint32_t)width, (uint32_t)height, s_stageBuf, 0);
			EndOneTimeCmd(cmd);
		}
	}
	static void VK_TextureSetParam(int param, int value)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (s_tls.boundTextureId <= 0) return;
		VKTextureData &td = s_textures[s_tls.boundTextureId];
		if (param == 1 || param == 2) td.linearFilter = (value != 0);
		else if (param == 3 || param == 4) td.clampWrap = (value == 0);
	}
	static void *VK_TextureGetNativeHandle(int idx)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		auto it = s_textures.find(idx);
		return (it != s_textures.end()) ? (void*)it->second.view : NULL;
	}

	/* State setters */
	static void VK_StateSetColour(float r, float g, float b, float a) { s_tls.stateColour[0]=r; s_tls.stateColour[1]=g; s_tls.stateColour[2]=b; s_tls.stateColour[3]=a; }
	static void VK_StateSetDepthMask(int e) { s_tls.depthMask = (e!=0); }
	static void VK_StateSetBlendEnable(int e) { s_tls.blendEnable = (e!=0); }
	static void VK_StateSetBlendFunc(int s, int d) { s_tls.blendSrc=s; s_tls.blendDst=d; }
	static void VK_StateSetBlendFactor(unsigned int c) { s_tls.blendFactorRGBA=c; }
	static void VK_StateSetAlphaFunc(int f, float p) { s_tls.alphaFunc=f; s_tls.alphaCutoff=p; }
	static void VK_StateSetDepthFunc(int f) { s_tls.depthFunc=f; }
	static void VK_StateSetFaceCull(int e) { s_tls.cullEnable=(e!=0); }
	static void VK_StateSetFaceCullCW(int e) { s_tls.cullCW=(e!=0); }
	static void VK_StateSetLineWidth(float) {}
	static void VK_StateSetWriteEnable(int r, int g, int b, int a) { s_tls.colorWriteMask=(uint8_t)((r?1:0)|(g?2:0)|(b?4:0)|(a?8:0)); }
	static void VK_StateSetDepthTestEnable(int e) { s_tls.depthTestEnable=(e!=0); }
	static void VK_StateSetAlphaTestEnable(int e) { s_tls.alphaTestEnable=(e!=0); }
	static void VK_StateSetDepthSlopeAndBias(float s, float b) { s_tls.depthBiasSlope=s; s_tls.depthBiasConstant=b; }
	static void VK_StateSetFogEnable(int e) { s_tls.fogEnable=(e!=0); }
	static void VK_StateSetFogMode(int m) { s_tls.fogMode=(m==2048||m==2)?2:1; }
	static void VK_StateSetFogNear(float d) { s_tls.fogNear=d; }
	static void VK_StateSetFogFar(float d) { s_tls.fogFar=d; }
	static void VK_StateSetFogDensity(float d) { s_tls.fogDensity=d; }
	static void VK_StateSetFogColour(float r, float g, float b) { s_tls.fogColor[0]=r; s_tls.fogColor[1]=g; s_tls.fogColor[2]=b; }
	static void VK_StateSetLightingEnable(int e) { s_tls.lightingEnable=(e!=0); }
	static void VK_StateSetVertexTextureUV(float u, float v) { s_tls.vertexTexUV[0]=u; s_tls.vertexTexUV[1]=v; }
	static void VK_StateSetLightColour(int l, float r, float g, float b) { if (l==0||l==1) { s_tls.lightColor[l][0]=r; s_tls.lightColor[l][1]=g; s_tls.lightColor[l][2]=b; } }
	static void VK_StateSetLightAmbientColour(float r, float g, float b) { s_tls.lightAmbient[0]=r; s_tls.lightAmbient[1]=g; s_tls.lightAmbient[2]=b; s_tls.lightAmbient[3]=1.0f; }
	static void VK_StateSetLightDirection(int l, float x, float y, float z)
	{
		if (l==0||l==1)
		{
			s_tls.EnsureStackInit(0);
			float tx, ty, tz;
			Matrix4x4::TransformVec3(s_tls.matrixStacks[0].back(), x, y, z, tx, ty, tz);
			s_tls.lightDir[l][0]=tx; s_tls.lightDir[l][1]=ty; s_tls.lightDir[l][2]=tz; s_tls.lightDir[l][3]=0.0f;
		}
	}
	static void VK_StateSetLightEnable(int l, int e) { if (l==0||l==1) s_tls.lightEnabled[l]=(e!=0); }
	static void VK_StateSetViewport(int viewportType)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_currentViewportType = viewportType;
		ApplyMCViewport();
	}
	static void VK_StateSetTexGenCol(int col, float x, float y, float z, float w, int eyeSpace)
	{
		if (col<0||col>=4) return;
		if (!eyeSpace) { s_tls.texGenPlanes[col][0]=x; s_tls.texGenPlanes[col][1]=y; s_tls.texGenPlanes[col][2]=z; s_tls.texGenPlanes[col][3]=w; }
		else
		{
			s_tls.EnsureStackInit(0);
			const float *mv = s_tls.matrixStacks[0].back().m;
			float v[4] = { x, y, z, w };
			for (int row = 0; row < 4; ++row)
				s_tls.texGenPlanes[col][row] = mv[row]*v[0] + mv[4+row]*v[1] + mv[8+row]*v[2] + mv[12+row]*v[3];
		}
	}
	static void VK_StateSetStencil(int func, uint8_t ref, uint8_t rmask, uint8_t wmask)
	{
		s_tls.stencilEnable = (func != C4J_CMP_ALWAYS);
		s_tls.stencilFunc = func;
		s_tls.stencilRef = ref;
		s_tls.stencilReadMask = rmask;
		s_tls.stencilWriteMask = wmask;
	}

	static void VK_BeginEvent(const wchar_t*) {}
	static void VK_Suspend(void) { s_suspended = true; }
	static int  VK_Suspended(void) { return s_suspended ? 1 : 0; }
	static void VK_Resume(void) { s_suspended = false; }

	/* GDraw Bridge */
	static void *VK_GDrawCreateContext(int w, int h)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		GDrawFunctions *funcs = gdraw_VK_CreateContext(&s_gdrawHost, w, h);
		s_gdrawInitialized = (funcs != NULL);
		return (void*)funcs;
	}
	static void VK_GDrawDestroyContext(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (s_gdrawInitialized) { gdraw_VK_DestroyContext(); s_gdrawInitialized = false; }
	}
	static void VK_GDrawSetRendertargetSize(int w, int h) { gdraw_VK_SetRendertargetSize(w, h); }
	static void VK_GDrawSetTileOrigin(int xPos, int yPos) { gdraw_VK_SetTileOrigin(xPos, yPos); }
	static void VK_GDrawNoMoreGDrawThisFrame(void) { gdraw_VK_NoMoreGDrawThisFrame(); }
	static void VK_GDrawBeginCustomDraw4J(void *r, float *m) { gdraw_VK_BeginCustomDraw_4J((IggyCustomDrawCallbackRegion*)r, m); }
	static void VK_GDrawCalculateCustomDraw4J(void *r, float *m) { gdraw_VK_CalculateCustomDraw_4J((IggyCustomDrawCallbackRegion*)r, m); }
	static void VK_GDrawEndCustomDraw(void *r) { gdraw_VK_EndCustomDraw((IggyCustomDrawCallbackRegion*)r); }
	static void VK_GDrawSetViewport4J(void) { gdraw_VK_SetViewport_4J(); }
	static void *VK_GDrawWrappedTextureCreate(int id) { return (void*)gdraw_VK_WrappedTextureCreate(id); }
	static void VK_GDrawWrappedTextureDestroy(void *h) { gdraw_VK_WrappedTextureDestroy((GDrawTexture*)h); }

	static int VK_CaptureBackBufferRGBA(C4JCaptureAllocFn allocFn, void *user, int *outWidth, int *outHeight)
	{
		if (outWidth) *outWidth = 0;
		if (outHeight) *outHeight = 0;
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device || !s_frameActive || s_suspended || s_swapchainDirty || !s_swapchain ||
			s_activeImgIdx >= s_swapImages.size() || s_width <= 0 || s_height <= 0 || !allocFn)
		{
			return 0;
		}
		const bool isBgra = (s_colorFormat == VK_FORMAT_B8G8R8A8_UNORM || (int)s_colorFormat == 50 /* VK_FORMAT_B8G8R8A8_SRGB */);
		const bool isRgba = (s_colorFormat == VK_FORMAT_R8G8B8A8_UNORM || (int)s_colorFormat == 43 /* VK_FORMAT_R8G8B8A8_SRGB */);
		if (!isBgra && !isRgba) return 0;

		const uint32_t w = (uint32_t)s_width;
		const uint32_t h = (uint32_t)s_height;
		const VkDeviceSize copyBytes = (VkDeviceSize)w * (VkDeviceSize)h * 4u;
		VkBuffer readBuf = VK_NULL_HANDLE;
		VkDeviceMemory readMem = VK_NULL_HANDLE;
		void *mappedPtr = NULL;
		if (!CreateHostBuffer(copyBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readBuf, readMem, &mappedPtr) || !mappedPtr)
		{
			if (readBuf) s_vk.vkDestroyBuffer(s_device, readBuf, NULL);
			if (readMem) s_vk.vkFreeMemory(s_device, readMem, NULL);
			return 0;
		}

		FrameSync &fs = s_frames[s_frameIdx];
		EndRenderPassIfActive();

		TransitionImage(fs.cmdBuf, s_swapImages[s_activeImgIdx], VK_IMAGE_ASPECT_COLOR_BIT,
			s_swapLayouts[s_activeImgIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			0, 1, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		VkBufferImageCopy region = {};
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageExtent = { w, h, 1 };
		s_vk.vkCmdCopyImageToBuffer(fs.cmdBuf, s_swapImages[s_activeImgIdx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readBuf, 1, &region);

		TransitionImage(fs.cmdBuf, s_swapImages[s_activeImgIdx], VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, 1, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		s_swapLayouts[s_activeImgIdx] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		s_vk.vkEndCommandBuffer(fs.cmdBuf);

		VkFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo si = {
			VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL,
			fs.imgAvailPending ? 1u : 0u,
			fs.imgAvailPending ? &fs.imgAvailSem : NULL,
			fs.imgAvailPending ? &waitStage : NULL,
			1, &fs.cmdBuf, 0, NULL
		};
		fs.imgAvailPending = false;
		s_vk.vkQueueSubmit(s_queue, 1, &si, fs.inFlightFence);
		s_vk.vkWaitForFences(s_device, 1, &fs.inFlightFence, VK_TRUE, ~0ULL);
		s_vk.vkResetFences(s_device, 1, &fs.inFlightFence);

		int ok = 0;
		uint8_t *dstPixels = NULL;
		if (allocFn((int)w, (int)h, &dstPixels, user) && dstPixels)
		{
			const uint8_t *src = (const uint8_t *)mappedPtr;
			const size_t pixelCount = (size_t)w * (size_t)h;
			for (size_t i = 0; i < pixelCount; ++i)
			{
				const uint8_t c0 = src[(i * 4u) + 0u];
				const uint8_t c1 = src[(i * 4u) + 1u];
				const uint8_t c2 = src[(i * 4u) + 2u];
				if (isBgra)
				{
					dstPixels[(i * 4u) + 0u] = c2;
					dstPixels[(i * 4u) + 1u] = c1;
					dstPixels[(i * 4u) + 2u] = c0;
				}
				else
				{
					dstPixels[(i * 4u) + 0u] = c0;
					dstPixels[(i * 4u) + 1u] = c1;
					dstPixels[(i * 4u) + 2u] = c2;
				}
				dstPixels[(i * 4u) + 3u] = 0xFFu;
			}
			if (outWidth) *outWidth = (int)w;
			if (outHeight) *outHeight = (int)h;
			ok = 1;
		}

		s_vk.vkUnmapMemory(s_device, readMem);
		s_vk.vkDestroyBuffer(s_device, readBuf, NULL);
		s_vk.vkFreeMemory(s_device, readMem, NULL);

		s_vk.vkResetCommandPool(s_device, fs.cmdPool, 0);
		VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		s_vk.vkBeginCommandBuffer(fs.cmdBuf, &cbi);
		s_inRenderPass = false;
		EnsureRenderPassActive();
		return ok;
	}

	static void VK_ShaderSetFrameParams(const C4JShaderFrameParams *params)
	{
		if (!params) return;
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		s_shaderFrameParams = *params;
	}

	static void VK_ShaderClearCustomProgram(void)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (s_device)
		{
			s_vk.vkDeviceWaitIdle(s_device);
			for (auto it = s_worldPipes.begin(); it != s_worldPipes.end(); )
			{
				if (it->first.customShaderGen != 0)
				{
					if (it->second) s_vk.vkDestroyPipeline(s_device, it->second, NULL);
					it = s_worldPipes.erase(it);
				}
				else
				{
					++it;
				}
			}
			if (s_smCustomStd)  { s_vk.vkDestroyShaderModule(s_device, s_smCustomStd, NULL); s_smCustomStd = VK_NULL_HANDLE; }
			if (s_smCustomComp) { s_vk.vkDestroyShaderModule(s_device, s_smCustomComp, NULL); s_smCustomComp = VK_NULL_HANDLE; }
			if (s_smCustomFrag) { s_vk.vkDestroyShaderModule(s_device, s_smCustomFrag, NULL); s_smCustomFrag = VK_NULL_HANDLE; }
		}
		if (s_shaderMode == C4J_SHADER_MODE_CUSTOM)
			s_shaderMode = C4J_SHADER_MODE_DEFAULT;
		InvalidateWorldPipelineCache();
	}

	static int VK_ShaderLoadCustomProgram(const C4JShaderProgramDesc *desc, char *errBuf, size_t errBufSize)
	{
		if (errBuf && errBufSize > 0) errBuf[0] = 0;
		if (!desc || !desc->vkFragSpv || desc->vkFragSpvBytes == 0 || (desc->vkFragSpvBytes % 4) != 0)
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "Missing or invalid Vulkan fragment SPIR-V");
			return 0;
		}
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (!s_device)
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "Vulkan device not initialized");
			return 0;
		}

		VkShaderModule newFrag = VK_NULL_HANDLE;
		VkShaderModule newStd  = VK_NULL_HANDLE;
		VkShaderModule newComp = VK_NULL_HANDLE;

		VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, desc->vkFragSpvBytes, (const uint32_t*)desc->vkFragSpv };
		if (s_vk.vkCreateShaderModule(s_device, &smci, NULL, &newFrag) != VK_SUCCESS || !newFrag)
		{
			if (errBuf && errBufSize > 0) snprintf(errBuf, errBufSize, "Failed to create Vulkan fragment shader module");
			return 0;
		}
		if (desc->vkVertStdSpv && desc->vkVertStdSpvBytes > 0 && (desc->vkVertStdSpvBytes % 4) == 0)
		{
			VkShaderModuleCreateInfo vsi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, desc->vkVertStdSpvBytes, (const uint32_t*)desc->vkVertStdSpv };
			s_vk.vkCreateShaderModule(s_device, &vsi, NULL, &newStd);
		}
		if (desc->vkVertCmpSpv && desc->vkVertCmpSpvBytes > 0 && (desc->vkVertCmpSpvBytes % 4) == 0)
		{
			VkShaderModuleCreateInfo vci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, desc->vkVertCmpSpvBytes, (const uint32_t*)desc->vkVertCmpSpv };
			s_vk.vkCreateShaderModule(s_device, &vci, NULL, &newComp);
		}

		s_vk.vkDeviceWaitIdle(s_device);
		for (auto it = s_worldPipes.begin(); it != s_worldPipes.end(); )
		{
			if (it->first.customShaderGen != 0)
			{
				if (it->second) s_vk.vkDestroyPipeline(s_device, it->second, NULL);
				it = s_worldPipes.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (s_smCustomStd)  s_vk.vkDestroyShaderModule(s_device, s_smCustomStd, NULL);
		if (s_smCustomComp) s_vk.vkDestroyShaderModule(s_device, s_smCustomComp, NULL);
		if (s_smCustomFrag) s_vk.vkDestroyShaderModule(s_device, s_smCustomFrag, NULL);

		s_smCustomStd  = newStd;
		s_smCustomComp = newComp;
		s_smCustomFrag = newFrag;
		s_customShaderGen++;
		if (s_customShaderGen == 0) s_customShaderGen = 1;
		s_shaderMode = C4J_SHADER_MODE_CUSTOM;
		InvalidateWorldPipelineCache();
		return 1;
	}

	static void VK_ShaderSetMode(int mode)
	{
		std::lock_guard<std::recursive_mutex> lock(s_vkMutex);
		if (mode == C4J_SHADER_MODE_CUSTOM && s_smCustomFrag == VK_NULL_HANDLE)
			s_shaderMode = C4J_SHADER_MODE_DEFAULT;
		else if (mode >= C4J_SHADER_MODE_OFF && mode <= C4J_SHADER_MODE_CUSTOM)
			s_shaderMode = mode;
		InvalidateWorldPipelineCache();
	}

	static int VK_ShaderGetMode(void)
	{
		return s_shaderMode;
	}

	static const C4JRenderBackendAPI s_vkAPI =
	{
		sizeof(C4JRenderBackendAPI),
		C4J_RENDER_BACKEND_ABI_VERSION,
		C4J_BACKEND_VULKAN,
		"Vulkan",
		VK_InitialiseWindow,
		VK_ResizeWindow,
		VK_Shutdown,
		VK_SetVsync,
		VK_GetVsync,
		VK_GetBackendInfo,
		VK_Tick,
		VK_UpdateGamma,
		VK_StartFrame,
		VK_DoScreenGrabOnNextPresent,
		VK_Present,
		VK_Clear,
		VK_SetClearColour,
		VK_IsWidescreen,
		VK_IsHiDef,
		VK_NoOpInt,
		VK_NoOpVoid,
		VK_NoOpInt,
		VK_NoOpVoid,
		VK_MatrixMode,
		VK_MatrixSetIdentity,
		VK_MatrixTranslate,
		VK_MatrixRotate,
		VK_MatrixScale,
		VK_MatrixPerspective,
		VK_MatrixOrthogonal,
		VK_MatrixPop,
		VK_MatrixPush,
		VK_MatrixMult,
		VK_MatrixGet,
		VK_SetMatrixDirty,
		VK_DrawVertices,
		VK_DrawVertexBuffer,
		VK_NoOpVoid,
		VK_CBuffCreate,
		VK_CBuffDelete,
		VK_CBuffStart,
		VK_CBuffClear,
		VK_CBuffSize,
		VK_CBuffEnd,
		VK_CBuffCall,
		VK_NoOpVoid,
		VK_NoOpVoid,
		VK_NoOpVoid,
		VK_TextureCreate,
		VK_TextureFree,
		VK_TextureBind,
		VK_TextureBindVertex,
		VK_TextureSetLevels,
		VK_TextureGetLevels,
		VK_TextureData,
		VK_TextureDataUpdate,
		VK_TextureSetParam,
		VK_NoOpVoid,
		VK_NoOpVoid,
		VK_TextureGetNativeHandle,
		VK_StateSetColour,
		VK_StateSetDepthMask,
		VK_StateSetBlendEnable,
		VK_StateSetBlendFunc,
		VK_StateSetBlendFactor,
		VK_StateSetAlphaFunc,
		VK_StateSetDepthFunc,
		VK_StateSetFaceCull,
		VK_StateSetFaceCullCW,
		VK_StateSetLineWidth,
		VK_StateSetWriteEnable,
		VK_StateSetDepthTestEnable,
		VK_StateSetAlphaTestEnable,
		VK_StateSetDepthSlopeAndBias,
		VK_StateSetFogEnable,
		VK_StateSetFogMode,
		VK_StateSetFogNear,
		VK_StateSetFogFar,
		VK_StateSetFogDensity,
		VK_StateSetFogColour,
		VK_StateSetLightingEnable,
		VK_StateSetVertexTextureUV,
		VK_StateSetLightColour,
		VK_StateSetLightAmbientColour,
		VK_StateSetLightDirection,
		VK_StateSetLightEnable,
		VK_StateSetViewport,
		VK_NoOpInt,
		VK_StateSetTexGenCol,
		VK_StateSetStencil,
		VK_NoOpInt,
		VK_BeginEvent,
		VK_NoOpVoid,
		VK_Suspend,
		VK_Suspended,
		VK_Resume,
		VK_GDrawCreateContext,
		VK_GDrawDestroyContext,
		VK_GDrawSetRendertargetSize,
		VK_GDrawSetTileOrigin,
		VK_GDrawNoMoreGDrawThisFrame,
		VK_GDrawBeginCustomDraw4J,
		VK_GDrawCalculateCustomDraw4J,
		VK_GDrawEndCustomDraw,
		VK_GDrawSetViewport4J,
		VK_GDrawWrappedTextureCreate,
		VK_GDrawWrappedTextureDestroy,
		VK_CaptureBackBufferRGBA,
		VK_ShaderSetFrameParams,
		VK_ShaderLoadCustomProgram,
		VK_ShaderClearCustomProgram,
		VK_ShaderSetMode,
		VK_ShaderGetMode
	};
}

extern "C" __declspec(dllexport) const C4JRenderBackendAPI *C4JBackend_GetAPI(void)
{
	return &s_vkAPI;
}
