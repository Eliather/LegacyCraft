#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#define VKAPI_ATTR
#define VKAPI_CALL __stdcall
#define VKAPI_PTR  __stdcall

#define VK_MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#define VK_API_VERSION_1_1 VK_MAKE_VERSION(1, 1, 0)

#define VK_DEFINE_HANDLE(object) typedef struct object##_T *object;
#if defined(_WIN64) || defined(__LP64__)
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
#else
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
#endif
#define VK_NULL_HANDLE 0

typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkFlags;
typedef uint32_t VkSampleMask;

#define VK_TRUE  1u
#define VK_FALSE 0u
#define VK_WHOLE_SIZE (~0ULL)
#define VK_QUEUE_FAMILY_IGNORED (~0U)
#define VK_SUBPASS_EXTERNAL (~0U)
#define VK_LOD_CLAMP_NONE 1000.0f
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_UUID_SIZE 16
#define VK_MAX_MEMORY_TYPES 32
#define VK_MAX_MEMORY_HEAPS 16

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkCommandBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSwapchainKHR)

typedef enum VkResult {
	VK_SUCCESS = 0,
	VK_NOT_READY = 1,
	VK_TIMEOUT = 2,
	VK_SUBOPTIMAL_KHR = 1000001003,
	VK_ERROR_OUT_OF_DATE_KHR = -1000001004,
} VkResult;

typedef enum VkStructureType {
	VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
	VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
	VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
	VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
	VK_STRUCTURE_TYPE_SUBMIT_INFO = 4,
	VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
	VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE = 6,
	VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
	VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
	VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
	VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
	VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15,
	VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
	VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18,
	VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO = 19,
	VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO = 20,
	VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO = 22,
	VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO = 23,
	VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO = 24,
	VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO = 25,
	VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO = 26,
	VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO = 27,
	VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28,
	VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
	VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO = 31,
	VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
	VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33,
	VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34,
	VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35,
	VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 37,
	VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO = 38,
	VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39,
	VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
	VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42,
	VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO = 43,
	VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER = 45,
	VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
	VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
	VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR = 1000009000,
} VkStructureType;

typedef enum VkFormat {
	VK_FORMAT_UNDEFINED = 0,
	VK_FORMAT_R8_UNORM = 9,
	VK_FORMAT_R32G32_SFLOAT = 103,
	VK_FORMAT_R32G32B32_SFLOAT = 106,
	VK_FORMAT_R16G16B16A16_SINT = 96,
	VK_FORMAT_R32_UINT = 98,
	VK_FORMAT_R8G8B8A8_UNORM = 37,
	VK_FORMAT_B8G8R8A8_UNORM = 44,
	VK_FORMAT_D32_SFLOAT = 126,
	VK_FORMAT_D24_UNORM_S8_UINT = 129,
	VK_FORMAT_D32_SFLOAT_S8_UINT = 130,
} VkFormat;

typedef enum VkColorSpaceKHR {
	VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0,
} VkColorSpaceKHR;

typedef enum VkPresentModeKHR {
	VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
	VK_PRESENT_MODE_MAILBOX_KHR = 1,
	VK_PRESENT_MODE_FIFO_KHR = 2,
	VK_PRESENT_MODE_FIFO_RELAXED_KHR = 3,
} VkPresentModeKHR;

typedef enum VkPhysicalDeviceType {
	VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
	VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
	VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
	VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
	VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
} VkPhysicalDeviceType;

typedef enum VkImageLayout {
	VK_IMAGE_LAYOUT_UNDEFINED = 0,
	VK_IMAGE_LAYOUT_GENERAL = 1,
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
	VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 5,
	VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6,
	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 7,
	VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002,
} VkImageLayout;

typedef enum VkAttachmentLoadOp {
	VK_ATTACHMENT_LOAD_OP_LOAD = 0,
	VK_ATTACHMENT_LOAD_OP_CLEAR = 1,
	VK_ATTACHMENT_LOAD_OP_DONT_CARE = 2,
} VkAttachmentLoadOp;

typedef enum VkAttachmentStoreOp {
	VK_ATTACHMENT_STORE_OP_STORE = 0,
	VK_ATTACHMENT_STORE_OP_DONT_CARE = 1,
} VkAttachmentStoreOp;

typedef enum VkImageType {
	VK_IMAGE_TYPE_1D = 0,
	VK_IMAGE_TYPE_2D = 1,
	VK_IMAGE_TYPE_3D = 2,
} VkImageType;

typedef enum VkImageViewType {
	VK_IMAGE_VIEW_TYPE_2D = 1,
} VkImageViewType;

typedef enum VkImageTiling {
	VK_IMAGE_TILING_OPTIMAL = 0,
	VK_IMAGE_TILING_LINEAR = 1,
} VkImageTiling;

typedef enum VkSharingMode {
	VK_SHARING_MODE_EXCLUSIVE = 0,
} VkSharingMode;

typedef enum VkSampleCountFlagBits {
	VK_SAMPLE_COUNT_1_BIT = 0x00000001,
} VkSampleCountFlagBits;

typedef enum VkPipelineBindPoint {
	VK_PIPELINE_BIND_POINT_GRAPHICS = 0,
} VkPipelineBindPoint;

typedef enum VkPrimitiveTopology {
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST = 1,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP = 2,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 4,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN = 5,
} VkPrimitiveTopology;

typedef enum VkVertexInputRate {
	VK_VERTEX_INPUT_RATE_VERTEX = 0,
} VkVertexInputRate;

typedef enum VkPolygonMode {
	VK_POLYGON_MODE_FILL = 0,
} VkPolygonMode;

typedef enum VkCullModeFlagBits {
	VK_CULL_MODE_NONE = 0,
	VK_CULL_MODE_FRONT_BIT = 1,
	VK_CULL_MODE_BACK_BIT = 2,
} VkCullModeFlagBits;

typedef enum VkFrontFace {
	VK_FRONT_FACE_COUNTER_CLOCKWISE = 0,
	VK_FRONT_FACE_CLOCKWISE = 1,
} VkFrontFace;

typedef enum VkCompareOp {
	VK_COMPARE_OP_NEVER = 0,
	VK_COMPARE_OP_LESS = 1,
	VK_COMPARE_OP_EQUAL = 2,
	VK_COMPARE_OP_LESS_OR_EQUAL = 3,
	VK_COMPARE_OP_GREATER = 4,
	VK_COMPARE_OP_NOT_EQUAL = 5,
	VK_COMPARE_OP_GREATER_OR_EQUAL = 6,
	VK_COMPARE_OP_ALWAYS = 7,
} VkCompareOp;

typedef enum VkStencilOp {
	VK_STENCIL_OP_KEEP = 0,
	VK_STENCIL_OP_ZERO = 1,
	VK_STENCIL_OP_REPLACE = 2,
} VkStencilOp;

typedef enum VkBlendFactor {
	VK_BLEND_FACTOR_ZERO = 0,
	VK_BLEND_FACTOR_ONE = 1,
	VK_BLEND_FACTOR_SRC_COLOR = 2,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR = 3,
	VK_BLEND_FACTOR_DST_COLOR = 4,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR = 5,
	VK_BLEND_FACTOR_SRC_ALPHA = 6,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7,
	VK_BLEND_FACTOR_DST_ALPHA = 8,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 9,
	VK_BLEND_FACTOR_CONSTANT_COLOR = 10,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR = 11,
	VK_BLEND_FACTOR_CONSTANT_ALPHA = 12,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA = 13,
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE = 14,
} VkBlendFactor;

typedef enum VkBlendOp {
	VK_BLEND_OP_ADD = 0,
} VkBlendOp;

typedef enum VkDynamicState {
	VK_DYNAMIC_STATE_VIEWPORT = 0,
	VK_DYNAMIC_STATE_SCISSOR = 1,
	VK_DYNAMIC_STATE_LINE_WIDTH = 2,
	VK_DYNAMIC_STATE_DEPTH_BIAS = 3,
	VK_DYNAMIC_STATE_BLEND_CONSTANTS = 4,
	VK_DYNAMIC_STATE_STENCIL_REFERENCE = 8,
} VkDynamicState;

typedef enum VkDescriptorType {
	VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 6,
} VkDescriptorType;

typedef enum VkFilter {
	VK_FILTER_NEAREST = 0,
	VK_FILTER_LINEAR = 1,
} VkFilter;

typedef enum VkSamplerMipmapMode {
	VK_SAMPLER_MIPMAP_MODE_NEAREST = 0,
	VK_SAMPLER_MIPMAP_MODE_LINEAR = 1,
} VkSamplerMipmapMode;

typedef enum VkSamplerAddressMode {
	VK_SAMPLER_ADDRESS_MODE_REPEAT = 0,
	VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
} VkSamplerAddressMode;

typedef enum VkBorderColor {
	VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK = 0,
} VkBorderColor;

typedef enum VkCommandBufferLevel {
	VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0,
} VkCommandBufferLevel;

typedef enum VkSubpassContents {
	VK_SUBPASS_CONTENTS_INLINE = 0,
} VkSubpassContents;

typedef enum VkIndexType {
	VK_INDEX_TYPE_UINT16 = 0,
	VK_INDEX_TYPE_UINT32 = 1,
} VkIndexType;

typedef enum VkComponentSwizzle {
	VK_COMPONENT_SWIZZLE_IDENTITY = 0,
} VkComponentSwizzle;

typedef enum VkLogicOp {
	VK_LOGIC_OP_COPY = 3,
} VkLogicOp;

/* Bitflags */
#define VK_QUEUE_GRAPHICS_BIT 0x00000001u
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x00000001u
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x00000002u
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004u

#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 0x00000001u
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 0x00000002u
#define VK_IMAGE_USAGE_SAMPLED_BIT      0x00000004u
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x00000010u
#define VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 0x00000020u

#define VK_IMAGE_ASPECT_COLOR_BIT   0x00000001u
#define VK_IMAGE_ASPECT_DEPTH_BIT   0x00000002u
#define VK_IMAGE_ASPECT_STENCIL_BIT 0x00000004u

#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT   0x00000001u
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT   0x00000002u
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 0x00000010u
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT   0x00000040u
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT  0x00000080u

#define VK_SHADER_STAGE_VERTEX_BIT   0x00000001u
#define VK_SHADER_STAGE_FRAGMENT_BIT 0x00000010u
#define VK_SHADER_STAGE_ALL_GRAPHICS 0x0000001Fu

#define VK_COLOR_COMPONENT_R_BIT 0x00000001u
#define VK_COLOR_COMPONENT_G_BIT 0x00000002u
#define VK_COLOR_COMPONENT_B_BIT 0x00000004u
#define VK_COLOR_COMPONENT_A_BIT 0x00000008u

#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 0x00000001u
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 0x00000001u

#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT            0x00000001u
#define VK_PIPELINE_STAGE_VERTEX_SHADER_BIT          0x00000008u
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT        0x00000080u
#define VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT   0x00000100u
#define VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT    0x00000200u
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x00000400u
#define VK_PIPELINE_STAGE_TRANSFER_BIT               0x00001000u
#define VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT         0x00002000u

#define VK_ACCESS_SHADER_READ_BIT                    0x00000020u
#define VK_ACCESS_COLOR_ATTACHMENT_READ_BIT          0x00000080u
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT         0x00000100u
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT  0x00000200u
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT 0x00000400u
#define VK_ACCESS_TRANSFER_READ_BIT                  0x00000800u
#define VK_ACCESS_TRANSFER_WRITE_BIT                 0x00001000u

#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x00000002u
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT     0x00000001u
#define VK_FENCE_CREATE_SIGNALED_BIT                    0x00000001u
#define VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT 0x00000001u

/* Structs */
typedef struct VkExtent2D { uint32_t width, height; } VkExtent2D;
typedef struct VkExtent3D { uint32_t width, height, depth; } VkExtent3D;
typedef struct VkOffset2D { int32_t x, y; } VkOffset2D;
typedef struct VkOffset3D { int32_t x, y, z; } VkOffset3D;
typedef struct VkRect2D   { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct VkViewport { float x, y, width, height, minDepth, maxDepth; } VkViewport;

typedef struct VkApplicationInfo {
	VkStructureType sType;
	const void *pNext;
	const char *pApplicationName;
	uint32_t applicationVersion;
	const char *pEngineName;
	uint32_t engineVersion;
	uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	const VkApplicationInfo *pApplicationInfo;
	uint32_t enabledLayerCount;
	const char * const *ppEnabledLayerNames;
	uint32_t enabledExtensionCount;
	const char * const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkWin32SurfaceCreateInfoKHR {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	HINSTANCE hinstance;
	HWND hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef struct VkPhysicalDeviceLimits {
	uint32_t maxImageDimension1D;
	uint32_t maxImageDimension2D;
	uint8_t  _rest[560];
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceSparseProperties {
	VkBool32 residencyStandard2DBlockShape;
	VkBool32 residencyStandard2DMultisampleBlockShape;
	VkBool32 residencyStandard3DBlockShape;
	VkBool32 residencyAlignedMipSize;
	VkBool32 residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties {
	uint32_t apiVersion;
	uint32_t driverVersion;
	uint32_t vendorID;
	uint32_t deviceID;
	VkPhysicalDeviceType deviceType;
	char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
	uint8_t pipelineCacheUUID[VK_UUID_SIZE];
	VkPhysicalDeviceLimits limits;
	VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

typedef struct VkMemoryType {
	VkFlags propertyFlags;
	uint32_t heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
	VkDeviceSize size;
	VkFlags flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
	uint32_t memoryTypeCount;
	VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES];
	uint32_t memoryHeapCount;
	VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkQueueFamilyProperties {
	VkFlags queueFlags;
	uint32_t queueCount;
	uint32_t timestampValidBits;
	VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkPhysicalDeviceFeatures {
	VkBool32 features[55];
} VkPhysicalDeviceFeatures;

typedef struct VkDeviceQueueCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t queueFamilyIndex;
	uint32_t queueCount;
	const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t queueCreateInfoCount;
	const VkDeviceQueueCreateInfo *pQueueCreateInfos;
	uint32_t enabledLayerCount;
	const char * const *ppEnabledLayerNames;
	uint32_t enabledExtensionCount;
	const char * const *ppEnabledExtensionNames;
	const VkPhysicalDeviceFeatures *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkSurfaceCapabilitiesKHR {
	uint32_t minImageCount;
	uint32_t maxImageCount;
	VkExtent2D currentExtent;
	VkExtent2D minImageExtent;
	VkExtent2D maxImageExtent;
	uint32_t maxImageArrayLayers;
	VkFlags supportedTransforms;
	VkFlags currentTransform;
	VkFlags supportedCompositeAlpha;
	VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
	VkFormat format;
	VkColorSpaceKHR colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkSwapchainCreateInfoKHR {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkSurfaceKHR surface;
	uint32_t minImageCount;
	VkFormat imageFormat;
	VkColorSpaceKHR imageColorSpace;
	VkExtent2D imageExtent;
	uint32_t imageArrayLayers;
	VkFlags imageUsage;
	VkSharingMode imageSharingMode;
	uint32_t queueFamilyIndexCount;
	const uint32_t *pQueueFamilyIndices;
	VkFlags preTransform;
	VkFlags compositeAlpha;
	VkPresentModeKHR presentMode;
	VkBool32 clipped;
	VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkAttachmentDescription {
	VkFlags flags;
	VkFormat format;
	VkSampleCountFlagBits samples;
	VkAttachmentLoadOp loadOp;
	VkAttachmentStoreOp storeOp;
	VkAttachmentLoadOp stencilLoadOp;
	VkAttachmentStoreOp stencilStoreOp;
	VkImageLayout initialLayout;
	VkImageLayout finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
	uint32_t attachment;
	VkImageLayout layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
	VkFlags flags;
	VkPipelineBindPoint pipelineBindPoint;
	uint32_t inputAttachmentCount;
	const VkAttachmentReference *pInputAttachments;
	uint32_t colorAttachmentCount;
	const VkAttachmentReference *pColorAttachments;
	const VkAttachmentReference *pResolveAttachments;
	const VkAttachmentReference *pDepthStencilAttachment;
	uint32_t preserveAttachmentCount;
	const uint32_t *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
	uint32_t srcSubpass;
	uint32_t dstSubpass;
	VkFlags srcStageMask;
	VkFlags dstStageMask;
	VkFlags srcAccessMask;
	VkFlags dstAccessMask;
	VkFlags dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t attachmentCount;
	const VkAttachmentDescription *pAttachments;
	uint32_t subpassCount;
	const VkSubpassDescription *pSubpasses;
	uint32_t dependencyCount;
	const VkSubpassDependency *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkRenderPass renderPass;
	uint32_t attachmentCount;
	const VkImageView *pAttachments;
	uint32_t width;
	uint32_t height;
	uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkShaderModuleCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	size_t codeSize;
	const uint32_t *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkDescriptorSetLayoutBinding {
	uint32_t binding;
	VkDescriptorType descriptorType;
	uint32_t descriptorCount;
	VkFlags stageFlags;
	const VkSampler *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t bindingCount;
	const VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkPushConstantRange {
	VkFlags stageFlags;
	uint32_t offset;
	uint32_t size;
} VkPushConstantRange;

typedef struct VkPipelineLayoutCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t setLayoutCount;
	const VkDescriptorSetLayout *pSetLayouts;
	uint32_t pushConstantRangeCount;
	const VkPushConstantRange *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkFlags stage;
	VkShaderModule module;
	const char *pName;
	const void *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkVertexInputBindingDescription {
	uint32_t binding;
	uint32_t stride;
	VkVertexInputRate inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
	uint32_t location;
	uint32_t binding;
	VkFormat format;
	uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t vertexBindingDescriptionCount;
	const VkVertexInputBindingDescription *pVertexBindingDescriptions;
	uint32_t vertexAttributeDescriptionCount;
	const VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkPrimitiveTopology topology;
	VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t viewportCount;
	const VkViewport *pViewports;
	uint32_t scissorCount;
	const VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkBool32 depthClampEnable;
	VkBool32 rasterizerDiscardEnable;
	VkPolygonMode polygonMode;
	VkFlags cullMode;
	VkFrontFace frontFace;
	VkBool32 depthBiasEnable;
	float depthBiasConstantFactor;
	float depthBiasClamp;
	float depthBiasSlopeFactor;
	float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkSampleCountFlagBits rasterizationSamples;
	VkBool32 sampleShadingEnable;
	float minSampleShading;
	const VkSampleMask *pSampleMask;
	VkBool32 alphaToCoverageEnable;
	VkBool32 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkStencilOpState {
	VkStencilOp failOp;
	VkStencilOp passOp;
	VkStencilOp depthFailOp;
	VkCompareOp compareOp;
	uint32_t compareMask;
	uint32_t writeMask;
	uint32_t reference;
} VkStencilOpState;

typedef struct VkPipelineDepthStencilStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkBool32 depthTestEnable;
	VkBool32 depthWriteEnable;
	VkCompareOp depthCompareOp;
	VkBool32 depthBoundsTestEnable;
	VkBool32 stencilTestEnable;
	VkStencilOpState front;
	VkStencilOpState back;
	float minDepthBounds;
	float maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
	VkBool32 blendEnable;
	VkBlendFactor srcColorBlendFactor;
	VkBlendFactor dstColorBlendFactor;
	VkBlendOp colorBlendOp;
	VkBlendFactor srcAlphaBlendFactor;
	VkBlendFactor dstAlphaBlendFactor;
	VkBlendOp alphaBlendOp;
	VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkBool32 logicOpEnable;
	VkLogicOp logicOp;
	uint32_t attachmentCount;
	const VkPipelineColorBlendAttachmentState *pAttachments;
	float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineDynamicStateCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t dynamicStateCount;
	const VkDynamicState *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t stageCount;
	const VkPipelineShaderStageCreateInfo *pStages;
	const VkPipelineVertexInputStateCreateInfo *pVertexInputState;
	const VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
	const void *pTessellationState;
	const VkPipelineViewportStateCreateInfo *pViewportState;
	const VkPipelineRasterizationStateCreateInfo *pRasterizationState;
	const VkPipelineMultisampleStateCreateInfo *pMultisampleState;
	const VkPipelineDepthStencilStateCreateInfo *pDepthStencilState;
	const VkPipelineColorBlendStateCreateInfo *pColorBlendState;
	const VkPipelineDynamicStateCreateInfo *pDynamicState;
	VkPipelineLayout layout;
	VkRenderPass renderPass;
	uint32_t subpass;
	VkPipeline basePipelineHandle;
	int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkBufferCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkDeviceSize size;
	VkFlags usage;
	VkSharingMode sharingMode;
	uint32_t queueFamilyIndexCount;
	const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkMemoryRequirements {
	VkDeviceSize size;
	VkDeviceSize alignment;
	uint32_t memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkMemoryAllocateInfo {
	VkStructureType sType;
	const void *pNext;
	VkDeviceSize allocationSize;
	uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkBufferCopy {
	VkDeviceSize srcOffset;
	VkDeviceSize dstOffset;
	VkDeviceSize size;
} VkBufferCopy;

typedef struct VkImageCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkImageType imageType;
	VkFormat format;
	VkExtent3D extent;
	uint32_t mipLevels;
	uint32_t arrayLayers;
	VkSampleCountFlagBits samples;
	VkImageTiling tiling;
	VkFlags usage;
	VkSharingMode sharingMode;
	uint32_t queueFamilyIndexCount;
	const uint32_t *pQueueFamilyIndices;
	VkImageLayout initialLayout;
} VkImageCreateInfo;

typedef struct VkComponentMapping {
	VkComponentSwizzle r, g, b, a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
	VkFlags aspectMask;
	uint32_t baseMipLevel;
	uint32_t levelCount;
	uint32_t baseArrayLayer;
	uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkImageViewCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkImage image;
	VkImageViewType viewType;
	VkFormat format;
	VkComponentMapping components;
	VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkSamplerCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	VkFilter magFilter;
	VkFilter minFilter;
	VkSamplerMipmapMode mipmapMode;
	VkSamplerAddressMode addressModeU;
	VkSamplerAddressMode addressModeV;
	VkSamplerAddressMode addressModeW;
	float mipLodBias;
	VkBool32 anisotropyEnable;
	float maxAnisotropy;
	VkBool32 compareEnable;
	VkCompareOp compareOp;
	float minLod;
	float maxLod;
	VkBorderColor borderColor;
	VkBool32 unnormalizedCoordinates;
} VkSamplerCreateInfo;

typedef struct VkImageMemoryBarrier {
	VkStructureType sType;
	const void *pNext;
	VkFlags srcAccessMask;
	VkFlags dstAccessMask;
	VkImageLayout oldLayout;
	VkImageLayout newLayout;
	uint32_t srcQueueFamilyIndex;
	uint32_t dstQueueFamilyIndex;
	VkImage image;
	VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;

typedef struct VkImageSubresourceLayers {
	VkFlags aspectMask;
	uint32_t mipLevel;
	uint32_t baseArrayLayer;
	uint32_t layerCount;
} VkImageSubresourceLayers;

typedef struct VkBufferImageCopy {
	VkDeviceSize bufferOffset;
	uint32_t bufferRowLength;
	uint32_t bufferImageHeight;
	VkImageSubresourceLayers imageSubresource;
	VkOffset3D imageOffset;
	VkExtent3D imageExtent;
} VkBufferImageCopy;

typedef struct VkDescriptorPoolSize {
	VkDescriptorType type;
	uint32_t descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t maxSets;
	uint32_t poolSizeCount;
	const VkDescriptorPoolSize *pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetAllocateInfo {
	VkStructureType sType;
	const void *pNext;
	VkDescriptorPool descriptorPool;
	uint32_t descriptorSetCount;
	const VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorBufferInfo {
	VkBuffer buffer;
	VkDeviceSize offset;
	VkDeviceSize range;
} VkDescriptorBufferInfo;

typedef struct VkDescriptorImageInfo {
	VkSampler sampler;
	VkImageView imageView;
	VkImageLayout imageLayout;
} VkDescriptorImageInfo;

typedef struct VkWriteDescriptorSet {
	VkStructureType sType;
	const void *pNext;
	VkDescriptorSet dstSet;
	uint32_t dstBinding;
	uint32_t dstArrayElement;
	uint32_t descriptorCount;
	VkDescriptorType descriptorType;
	const VkDescriptorImageInfo *pImageInfo;
	const VkDescriptorBufferInfo *pBufferInfo;
	const void *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkCommandPoolCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
	VkStructureType sType;
	const void *pNext;
	VkCommandPool commandPool;
	VkCommandBufferLevel level;
	uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
	const void *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef union VkClearColorValue {
	float float32[4];
	int32_t int32[4];
	uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue {
	float depth;
	uint32_t stencil;
} VkClearDepthStencilValue;

typedef union VkClearValue {
	VkClearColorValue color;
	VkClearDepthStencilValue depthStencil;
} VkClearValue;

typedef struct VkClearAttachment {
	VkFlags aspectMask;
	uint32_t colorAttachment;
	VkClearValue clearValue;
} VkClearAttachment;

typedef struct VkClearRect {
	VkRect2D rect;
	uint32_t baseArrayLayer;
	uint32_t layerCount;
} VkClearRect;

typedef struct VkRenderPassBeginInfo {
	VkStructureType sType;
	const void *pNext;
	VkRenderPass renderPass;
	VkFramebuffer framebuffer;
	VkRect2D renderArea;
	uint32_t clearValueCount;
	const VkClearValue *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkSubmitInfo {
	VkStructureType sType;
	const void *pNext;
	uint32_t waitSemaphoreCount;
	const VkSemaphore *pWaitSemaphores;
	const VkFlags *pWaitDstStageMask;
	uint32_t commandBufferCount;
	const VkCommandBuffer *pCommandBuffers;
	uint32_t signalSemaphoreCount;
	const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkPresentInfoKHR {
	VkStructureType sType;
	const void *pNext;
	uint32_t waitSemaphoreCount;
	const VkSemaphore *pWaitSemaphores;
	uint32_t swapchainCount;
	const VkSwapchainKHR *pSwapchains;
	const uint32_t *pImageIndices;
	VkResult *pResults;
} VkPresentInfoKHR;

typedef struct VkSemaphoreCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
} VkSemaphoreCreateInfo;

typedef struct VkFenceCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkFlags flags;
} VkFenceCreateInfo;

/* Function pointer types */
typedef void (VKAPI_PTR *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetDeviceProcAddr)(VkDevice, const char*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef void     (VKAPI_PTR *PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef void     (VKAPI_PTR *PFN_vkGetPhysicalDeviceFormatProperties)(VkPhysicalDevice, VkFormat, uint32_t*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDevice)(VkDevice, const void*);
typedef void     (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (VKAPI_PTR *PFN_vkDeviceWaitIdle)(VkDevice);
typedef VkResult (VKAPI_PTR *PFN_vkQueueWaitIdle)(VkQueue);
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);

typedef VkResult (VKAPI_PTR *PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*, const void*, VkSurfaceKHR*);
typedef void     (VKAPI_PTR *PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
typedef void     (VKAPI_PTR *PFN_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
typedef VkResult (VKAPI_PTR *PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
typedef VkResult (VKAPI_PTR *PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateRenderPass)(VkDevice, const VkRenderPassCreateInfo*, const void*, VkRenderPass*);
typedef void     (VKAPI_PTR *PFN_vkDestroyRenderPass)(VkDevice, VkRenderPass, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateFramebuffer)(VkDevice, const VkFramebufferCreateInfo*, const void*, VkFramebuffer*);
typedef void     (VKAPI_PTR *PFN_vkDestroyFramebuffer)(VkDevice, VkFramebuffer, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateShaderModule)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
typedef void     (VKAPI_PTR *PFN_vkDestroyShaderModule)(VkDevice, VkShaderModule, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorSetLayout)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDescriptorSetLayout)(VkDevice, VkDescriptorSetLayout, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreatePipelineLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
typedef void     (VKAPI_PTR *PFN_vkDestroyPipelineLayout)(VkDevice, VkPipelineLayout, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateGraphicsPipelines)(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const void*, VkPipeline*);
typedef void     (VKAPI_PTR *PFN_vkDestroyPipeline)(VkDevice, VkPipeline, const void*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateBuffer)(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*);
typedef void     (VKAPI_PTR *PFN_vkDestroyBuffer)(VkDevice, VkBuffer, const void*);
typedef void     (VKAPI_PTR *PFN_vkGetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements*);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*);
typedef void     (VKAPI_PTR *PFN_vkFreeMemory)(VkDevice, VkDeviceMemory, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_PTR *PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
typedef void     (VKAPI_PTR *PFN_vkUnmapMemory)(VkDevice, VkDeviceMemory);

typedef VkResult (VKAPI_PTR *PFN_vkCreateImage)(VkDevice, const VkImageCreateInfo*, const void*, VkImage*);
typedef void     (VKAPI_PTR *PFN_vkDestroyImage)(VkDevice, VkImage, const void*);
typedef void     (VKAPI_PTR *PFN_vkGetImageMemoryRequirements)(VkDevice, VkImage, VkMemoryRequirements*);
typedef VkResult (VKAPI_PTR *PFN_vkBindImageMemory)(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_PTR *PFN_vkCreateImageView)(VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*);
typedef void     (VKAPI_PTR *PFN_vkDestroyImageView)(VkDevice, VkImageView, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateSampler)(VkDevice, const VkSamplerCreateInfo*, const void*, VkSampler*);
typedef void     (VKAPI_PTR *PFN_vkDestroySampler)(VkDevice, VkSampler, const void*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorPool)(VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*);
typedef void     (VKAPI_PTR *PFN_vkDestroyDescriptorPool)(VkDevice, VkDescriptorPool, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkResetDescriptorPool)(VkDevice, VkDescriptorPool, VkFlags);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateDescriptorSets)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
typedef void     (VKAPI_PTR *PFN_vkUpdateDescriptorSets)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);

typedef VkResult (VKAPI_PTR *PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
typedef void     (VKAPI_PTR *PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandPool)(VkDevice, VkCommandPool, VkFlags);
typedef VkResult (VKAPI_PTR *PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef VkResult (VKAPI_PTR *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef VkResult (VKAPI_PTR *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandBuffer)(VkCommandBuffer, VkFlags);

typedef VkResult (VKAPI_PTR *PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);
typedef void     (VKAPI_PTR *PFN_vkDestroySemaphore)(VkDevice, VkSemaphore, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice, const VkFenceCreateInfo*, const void*, VkFence*);
typedef void     (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice, VkFence, const void*);
typedef VkResult (VKAPI_PTR *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
typedef VkResult (VKAPI_PTR *PFN_vkResetFences)(VkDevice, uint32_t, const VkFence*);

typedef void (VKAPI_PTR *PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
typedef void (VKAPI_PTR *PFN_vkCmdEndRenderPass)(VkCommandBuffer);
typedef void (VKAPI_PTR *PFN_vkCmdBindPipeline)(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
typedef void (VKAPI_PTR *PFN_vkCmdSetViewport)(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
typedef void (VKAPI_PTR *PFN_vkCmdSetScissor)(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
typedef void (VKAPI_PTR *PFN_vkCmdSetLineWidth)(VkCommandBuffer, float);
typedef void (VKAPI_PTR *PFN_vkCmdSetDepthBias)(VkCommandBuffer, float, float, float);
typedef void (VKAPI_PTR *PFN_vkCmdSetBlendConstants)(VkCommandBuffer, const float[4]);
typedef void (VKAPI_PTR *PFN_vkCmdSetStencilReference)(VkCommandBuffer, VkFlags, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdBindDescriptorSets)(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
typedef void (VKAPI_PTR *PFN_vkCmdBindVertexBuffers)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
typedef void (VKAPI_PTR *PFN_vkCmdBindIndexBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
typedef void (VKAPI_PTR *PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndexed)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
typedef void (VKAPI_PTR *PFN_vkCmdClearAttachments)(VkCommandBuffer, uint32_t, const VkClearAttachment*, uint32_t, const VkClearRect*);
typedef void (VKAPI_PTR *PFN_vkCmdPipelineBarrier)(VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const void*, uint32_t, const void*, uint32_t, const VkImageMemoryBarrier*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyBuffer)(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyBufferToImage)(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdCopyImageToBuffer)(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy*);
typedef void (VKAPI_PTR *PFN_vkCmdPushConstants)(VkCommandBuffer, VkPipelineLayout, VkFlags, uint32_t, uint32_t, const void*);

#define VK_STENCIL_FACE_FRONT_AND_BACK 0x00000003u

struct VkDispatchTable
{
	HMODULE dll = NULL;
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;
	PFN_vkGetDeviceProcAddr   vkGetDeviceProcAddr   = NULL;

	PFN_vkCreateInstance vkCreateInstance = NULL;
	PFN_vkDestroyInstance vkDestroyInstance = NULL;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = NULL;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = NULL;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = NULL;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = NULL;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = NULL;
	PFN_vkCreateDevice vkCreateDevice = NULL;
	PFN_vkDestroyDevice vkDestroyDevice = NULL;
	PFN_vkGetDeviceQueue vkGetDeviceQueue = NULL;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle = NULL;
	PFN_vkQueueWaitIdle vkQueueWaitIdle = NULL;
	PFN_vkQueueSubmit vkQueueSubmit = NULL;

	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = NULL;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = NULL;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = NULL;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = NULL;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = NULL;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = NULL;

	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = NULL;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = NULL;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = NULL;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = NULL;
	PFN_vkQueuePresentKHR vkQueuePresentKHR = NULL;

	PFN_vkCreateRenderPass vkCreateRenderPass = NULL;
	PFN_vkDestroyRenderPass vkDestroyRenderPass = NULL;
	PFN_vkCreateFramebuffer vkCreateFramebuffer = NULL;
	PFN_vkDestroyFramebuffer vkDestroyFramebuffer = NULL;
	PFN_vkCreateShaderModule vkCreateShaderModule = NULL;
	PFN_vkDestroyShaderModule vkDestroyShaderModule = NULL;
	PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = NULL;
	PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = NULL;
	PFN_vkCreatePipelineLayout vkCreatePipelineLayout = NULL;
	PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = NULL;
	PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = NULL;
	PFN_vkDestroyPipeline vkDestroyPipeline = NULL;

	PFN_vkCreateBuffer vkCreateBuffer = NULL;
	PFN_vkDestroyBuffer vkDestroyBuffer = NULL;
	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = NULL;
	PFN_vkAllocateMemory vkAllocateMemory = NULL;
	PFN_vkFreeMemory vkFreeMemory = NULL;
	PFN_vkBindBufferMemory vkBindBufferMemory = NULL;
	PFN_vkMapMemory vkMapMemory = NULL;
	PFN_vkUnmapMemory vkUnmapMemory = NULL;

	PFN_vkCreateImage vkCreateImage = NULL;
	PFN_vkDestroyImage vkDestroyImage = NULL;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = NULL;
	PFN_vkBindImageMemory vkBindImageMemory = NULL;
	PFN_vkCreateImageView vkCreateImageView = NULL;
	PFN_vkDestroyImageView vkDestroyImageView = NULL;
	PFN_vkCreateSampler vkCreateSampler = NULL;
	PFN_vkDestroySampler vkDestroySampler = NULL;

	PFN_vkCreateDescriptorPool vkCreateDescriptorPool = NULL;
	PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = NULL;
	PFN_vkResetDescriptorPool vkResetDescriptorPool = NULL;
	PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = NULL;
	PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = NULL;

	PFN_vkCreateCommandPool vkCreateCommandPool = NULL;
	PFN_vkDestroyCommandPool vkDestroyCommandPool = NULL;
	PFN_vkResetCommandPool vkResetCommandPool = NULL;
	PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = NULL;
	PFN_vkBeginCommandBuffer vkBeginCommandBuffer = NULL;
	PFN_vkEndCommandBuffer vkEndCommandBuffer = NULL;
	PFN_vkResetCommandBuffer vkResetCommandBuffer = NULL;

	PFN_vkCreateSemaphore vkCreateSemaphore = NULL;
	PFN_vkDestroySemaphore vkDestroySemaphore = NULL;
	PFN_vkCreateFence vkCreateFence = NULL;
	PFN_vkDestroyFence vkDestroyFence = NULL;
	PFN_vkWaitForFences vkWaitForFences = NULL;
	PFN_vkResetFences vkResetFences = NULL;

	PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = NULL;
	PFN_vkCmdEndRenderPass vkCmdEndRenderPass = NULL;
	PFN_vkCmdBindPipeline vkCmdBindPipeline = NULL;
	PFN_vkCmdSetViewport vkCmdSetViewport = NULL;
	PFN_vkCmdSetScissor vkCmdSetScissor = NULL;
	PFN_vkCmdSetLineWidth vkCmdSetLineWidth = NULL;
	PFN_vkCmdSetDepthBias vkCmdSetDepthBias = NULL;
	PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants = NULL;
	PFN_vkCmdSetStencilReference vkCmdSetStencilReference = NULL;
	PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = NULL;
	PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = NULL;
	PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = NULL;
	PFN_vkCmdDraw vkCmdDraw = NULL;
	PFN_vkCmdDrawIndexed vkCmdDrawIndexed = NULL;
	PFN_vkCmdClearAttachments vkCmdClearAttachments = NULL;
	PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = NULL;
	PFN_vkCmdCopyBuffer vkCmdCopyBuffer = NULL;
	PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = NULL;
	PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = NULL;
	PFN_vkCmdPushConstants vkCmdPushConstants = NULL;

	bool LoadGlobal()
	{
		if (!dll) dll = LoadLibraryA("vulkan-1.dll");
		if (!dll) return false;
		vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(dll, "vkGetInstanceProcAddr");
		if (!vkGetInstanceProcAddr) return false;
		vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
		return vkCreateInstance != NULL;
	}

	bool LoadInstance(VkInstance inst)
	{
		if (!vkGetInstanceProcAddr || !inst) return false;
#define VK_INST_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(inst, #fn)
		VK_INST_FN(vkDestroyInstance);
		VK_INST_FN(vkEnumeratePhysicalDevices);
		VK_INST_FN(vkGetPhysicalDeviceProperties);
		VK_INST_FN(vkGetPhysicalDeviceMemoryProperties);
		VK_INST_FN(vkGetPhysicalDeviceQueueFamilyProperties);
		VK_INST_FN(vkGetPhysicalDeviceFormatProperties);
		VK_INST_FN(vkCreateDevice);
		VK_INST_FN(vkGetDeviceProcAddr);
		VK_INST_FN(vkCreateWin32SurfaceKHR);
		VK_INST_FN(vkDestroySurfaceKHR);
		VK_INST_FN(vkGetPhysicalDeviceSurfaceSupportKHR);
		VK_INST_FN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
		VK_INST_FN(vkGetPhysicalDeviceSurfaceFormatsKHR);
		VK_INST_FN(vkGetPhysicalDeviceSurfacePresentModesKHR);
#undef VK_INST_FN
		return vkEnumeratePhysicalDevices && vkCreateDevice && vkCreateWin32SurfaceKHR;
	}

	bool LoadDevice(VkDevice dev)
	{
		if (!vkGetDeviceProcAddr || !dev) return false;
#define VK_DEV_FN(fn) fn = (PFN_##fn)vkGetDeviceProcAddr(dev, #fn)
		VK_DEV_FN(vkDestroyDevice);
		VK_DEV_FN(vkGetDeviceQueue);
		VK_DEV_FN(vkDeviceWaitIdle);
		VK_DEV_FN(vkQueueWaitIdle);
		VK_DEV_FN(vkQueueSubmit);
		VK_DEV_FN(vkCreateSwapchainKHR);
		VK_DEV_FN(vkDestroySwapchainKHR);
		VK_DEV_FN(vkGetSwapchainImagesKHR);
		VK_DEV_FN(vkAcquireNextImageKHR);
		VK_DEV_FN(vkQueuePresentKHR);
		VK_DEV_FN(vkCreateRenderPass);
		VK_DEV_FN(vkDestroyRenderPass);
		VK_DEV_FN(vkCreateFramebuffer);
		VK_DEV_FN(vkDestroyFramebuffer);
		VK_DEV_FN(vkCreateShaderModule);
		VK_DEV_FN(vkDestroyShaderModule);
		VK_DEV_FN(vkCreateDescriptorSetLayout);
		VK_DEV_FN(vkDestroyDescriptorSetLayout);
		VK_DEV_FN(vkCreatePipelineLayout);
		VK_DEV_FN(vkDestroyPipelineLayout);
		VK_DEV_FN(vkCreateGraphicsPipelines);
		VK_DEV_FN(vkDestroyPipeline);
		VK_DEV_FN(vkCreateBuffer);
		VK_DEV_FN(vkDestroyBuffer);
		VK_DEV_FN(vkGetBufferMemoryRequirements);
		VK_DEV_FN(vkAllocateMemory);
		VK_DEV_FN(vkFreeMemory);
		VK_DEV_FN(vkBindBufferMemory);
		VK_DEV_FN(vkMapMemory);
		VK_DEV_FN(vkUnmapMemory);
		VK_DEV_FN(vkCreateImage);
		VK_DEV_FN(vkDestroyImage);
		VK_DEV_FN(vkGetImageMemoryRequirements);
		VK_DEV_FN(vkBindImageMemory);
		VK_DEV_FN(vkCreateImageView);
		VK_DEV_FN(vkDestroyImageView);
		VK_DEV_FN(vkCreateSampler);
		VK_DEV_FN(vkDestroySampler);
		VK_DEV_FN(vkCreateDescriptorPool);
		VK_DEV_FN(vkDestroyDescriptorPool);
		VK_DEV_FN(vkResetDescriptorPool);
		VK_DEV_FN(vkAllocateDescriptorSets);
		VK_DEV_FN(vkUpdateDescriptorSets);
		VK_DEV_FN(vkCreateCommandPool);
		VK_DEV_FN(vkDestroyCommandPool);
		VK_DEV_FN(vkResetCommandPool);
		VK_DEV_FN(vkAllocateCommandBuffers);
		VK_DEV_FN(vkBeginCommandBuffer);
		VK_DEV_FN(vkEndCommandBuffer);
		VK_DEV_FN(vkResetCommandBuffer);
		VK_DEV_FN(vkCreateSemaphore);
		VK_DEV_FN(vkDestroySemaphore);
		VK_DEV_FN(vkCreateFence);
		VK_DEV_FN(vkDestroyFence);
		VK_DEV_FN(vkWaitForFences);
		VK_DEV_FN(vkResetFences);
		VK_DEV_FN(vkCmdBeginRenderPass);
		VK_DEV_FN(vkCmdEndRenderPass);
		VK_DEV_FN(vkCmdBindPipeline);
		VK_DEV_FN(vkCmdSetViewport);
		VK_DEV_FN(vkCmdSetScissor);
		VK_DEV_FN(vkCmdSetLineWidth);
		VK_DEV_FN(vkCmdSetDepthBias);
		VK_DEV_FN(vkCmdSetBlendConstants);
		VK_DEV_FN(vkCmdSetStencilReference);
		VK_DEV_FN(vkCmdBindDescriptorSets);
		VK_DEV_FN(vkCmdBindVertexBuffers);
		VK_DEV_FN(vkCmdBindIndexBuffer);
		VK_DEV_FN(vkCmdDraw);
		VK_DEV_FN(vkCmdDrawIndexed);
		VK_DEV_FN(vkCmdClearAttachments);
		VK_DEV_FN(vkCmdPipelineBarrier);
		VK_DEV_FN(vkCmdCopyBuffer);
		VK_DEV_FN(vkCmdCopyBufferToImage);
		VK_DEV_FN(vkCmdCopyImageToBuffer);
		VK_DEV_FN(vkCmdPushConstants);
#undef VK_DEV_FN
		return vkCreateSwapchainKHR && vkCreateGraphicsPipelines && vkQueueSubmit;
	}
};
