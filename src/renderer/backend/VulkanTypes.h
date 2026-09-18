#pragma once

#include <vulkan/vulkan.h>
#include "EngineTypes.h"
#include <string>
#include <optional>
#include <fmt/base.h>
#include <iterator>
#include "../RendererDefinitions.h"

namespace RD = RendererDefinitions;

constexpr const char* vkResultToString(VkResult result)
{
	switch (result)
	{
		case VK_SUCCESS:                       return "VK_SUCCESS";
		case VK_NOT_READY:                     return "VK_NOT_READY";
		case VK_TIMEOUT:                       return "VK_TIMEOUT";
		case VK_EVENT_SET:                     return "VK_EVENT_SET";
		case VK_EVENT_RESET:                   return "VK_EVENT_RESET";
		case VK_INCOMPLETE:                    return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:      return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:   return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:             return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:       return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:       return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:   return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:     return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:     return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:        return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL:         return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_UNKNOWN:                 return "VK_ERROR_UNKNOWN";
		default:                               return "UNKNOWN_ERROR";
	}
}

// Vulkan error checker
#define VK_CHECK(x)                                                \
	do {                                                           \
		VkResult err = x;                                          \
		if (err != VK_SUCCESS) {                                   \
			fmt::println(stderr,                                   \
				"[VK_CHECK] Vulkan Error: {} in file {} at line {}", \
				vkResultToString(err), __FILE__, __LINE__);        \
			std::abort();                                          \
		}                                                          \
	} while (0)

#define REQUIRE_HARDWARE(x, msg)                                             \
	do {                                                                     \
		if (!(x)) {                                                          \
			fmt::println(stderr,                                             \
				"[HARDWARE REQUIREMENT NOT MET] {} | {}",                   \
				#x, msg);                                                    \
			std::fflush(stderr);                                             \
			std::abort();                                                    \
		}                                                                    \
	} while (0)

// For fatal errors involving the gpu, primarly sync or dirty buffer pointers issues.
// Prevent the gpu from fucking freezing.
#define INVARIANT(x)                                           \
	do {                                                       \
		if (!(x)) {                                            \
			fmt::println(stderr,                               \
				"[FATAL INVARIANT FAILURE] {}:{} - {}",       \
				__FILE__, __LINE__, #x);                      \
			std::fflush(stderr);                              \
			std::abort();                                      \
		}                                                      \
	} while (0)

// Defines push constants usages
struct PushConstantDef
{
	uint32_t offset = 0;
	uint32_t size = 0;
	VkFlags stageFlags;
};

struct DescriptorInfo
{
	VkDescriptorType type;
	uint32_t binding = UINT32_MAX;
	VkFlags stageFlags;

	void* pNext = nullptr;
};

// --------------
// Image formats
// --------------

enum class Vulkan_Format
{
	RGBA16F     = VK_FORMAT_R16G16B16A16_SFLOAT,
	RGBA32F     = VK_FORMAT_R32G32B32A32_SFLOAT,
	D32         = VK_FORMAT_D32_SFLOAT,
	D16         = VK_FORMAT_D16_UNORM,
	BGRpacked   = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
	R8unorm     = VK_FORMAT_R8_UNORM,
	RG8unorm    = VK_FORMAT_R8G8_UNORM,
	RGBA8unorm  = VK_FORMAT_R8G8B8A8_UNORM,
	RGBA8srgb   = VK_FORMAT_R8G8B8A8_SRGB,
	RG16unorm   = VK_FORMAT_R16G16_UNORM,
	ABGRpacked  = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
	R16F        = VK_FORMAT_R16_SFLOAT,
	RG16F       = VK_FORMAT_R16G16_SFLOAT,
	R16U        = VK_FORMAT_R16_UINT,
	R32U        = VK_FORMAT_R32_UINT,
	RG32U       = VK_FORMAT_R32G32_UINT,
	R32F        = VK_FORMAT_R32_SFLOAT,
	R8U         = VK_FORMAT_R8_UINT,
	R16unorm    = VK_FORMAT_R16_UNORM,
	RG16snorm   = VK_FORMAT_R16G16_SNORM,
	RGBA16snorm = VK_FORMAT_R16G16B16A16_SNORM,
	RG32F       = VK_FORMAT_R32G32_SFLOAT,
	BC7srgb     = VK_FORMAT_BC7_SRGB_BLOCK,
	BC7unorm    = VK_FORMAT_BC7_UNORM_BLOCK,
	BC5unorm    = VK_FORMAT_BC5_UNORM_BLOCK,
	Undefined   = VK_FORMAT_UNDEFINED
};

enum class Vulkan_ImageUsage
{
	DrawColor =
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT,

	DrawDepth =    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				 | VK_IMAGE_USAGE_SAMPLED_BIT
				 | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
				 | VK_IMAGE_USAGE_TRANSFER_DST_BIT,

	ComputeRWTransfer = VK_IMAGE_USAGE_STORAGE_BIT
					 | VK_IMAGE_USAGE_SAMPLED_BIT
					 | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
					 | VK_IMAGE_USAGE_TRANSFER_DST_BIT,

	ComputeOnly = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,

	TextureSampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,

	ShadowMap = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
};

enum class Vulkan_ImageLayout
{
	Read               = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	Write              = VK_IMAGE_LAYOUT_GENERAL,
	ColorAttach        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	DepthAttach        = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
	DepthStencilAttach = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	DepthRead          = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
	TransferSrc        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	TransferDst        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	Present            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

	Undefined          = VK_IMAGE_LAYOUT_UNDEFINED,
};

// ---------------
// Pipeline types
// ---------------

enum class Vulkan_ShaderStage
{
	COMPUTE_STAGE  = VK_SHADER_STAGE_COMPUTE_BIT,
	VERTEX_STAGE   = VK_SHADER_STAGE_VERTEX_BIT,
	FRAGMENT_STAGE = VK_SHADER_STAGE_FRAGMENT_BIT,
	TASK_STAGE     = VK_SHADER_STAGE_TASK_BIT_EXT,
	MESH_STAGE     = VK_SHADER_STAGE_MESH_BIT_EXT,
	IMAGE_STAGES   = COMPUTE_STAGE | FRAGMENT_STAGE | TASK_STAGE,
	ALL_STAGES     = COMPUTE_STAGE | VERTEX_STAGE | FRAGMENT_STAGE | TASK_STAGE | MESH_STAGE
};

// Holds pipeline layout and push constant data
// All pipelines use the same setup so its globally accessible
struct PipelineLayoutConst
{
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	PushConstantDef pushConstantDef;
};

struct PipelineHandle
{
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	PipelineLayoutConst layout;
	std::string_view debugName = {};
};

template<typename SlotEnum>
class PipelineBundle
{
public:
	using Slot = SlotEnum;

	void Set(Slot slot, const PipelineHandle& handle)
	{
		m_handles[static_cast<size_t>(slot)] = handle;
	}

	PipelineHandle& Get(Slot slot)
	{
		return m_handles[static_cast<size_t>(slot)];
	}

	const PipelineHandle& Get(Slot slot) const
	{
		return m_handles[static_cast<size_t>(slot)];
	}

private:
	std::array<PipelineHandle, static_cast<size_t>(Slot::Count)> m_handles;
};

constexpr uint64_t TrianglesFromNonIndexed(
	VkPrimitiveTopology topology,
	uint64_t vertexCount)
{
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		return vertexCount / 3u;

	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		return vertexCount >= 3u ? (vertexCount - 2u) : 0u;

	default:
		return 0u;
	}
}

constexpr uint64_t TrianglesFromIndexed(
	VkPrimitiveTopology topology,
	uint32_t indexCount,
	uint32_t instanceCount)
{
	uint64_t baseTriangleCount = 0;

	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		baseTriangleCount = static_cast<uint64_t>(indexCount / 3u);
		break;

	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		baseTriangleCount = indexCount >= 3u
			? static_cast<uint64_t>(indexCount - 2u)
			: 0u;
		break;

	default:
		return 0u;
	}

	return baseTriangleCount * static_cast<uint64_t>(instanceCount);
}

constexpr uint64_t SumTrianglesIndirectRange(
	const std::vector<VkDrawIndexedIndirectCommand>& drawCommands,
	uint32_t firstCommand,
	uint32_t commandCount,
	VkPrimitiveTopology topology)
{
	uint64_t totalTriangles = 0;
	const size_t baseIndex = static_cast<size_t>(firstCommand);

	for (uint32_t commandIndex = 0; commandIndex < commandCount; ++commandIndex)
	{
		const auto& drawCommand = drawCommands[baseIndex + static_cast<size_t>(commandIndex)];

		totalTriangles += TrianglesFromIndexed(
			topology,
			drawCommand.indexCount,
			drawCommand.instanceCount
		);
	}

	return totalTriangles;
}

struct AttachmentDesc
{
	VkImageView imageView = VK_NULL_HANDLE;
	VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkClearValue clearValue{ 0.0f };

	AttachmentDesc() = default;
	AttachmentDesc(
		VkImageView view,
		VkImageLayout layout) : imageView(view), imageLayout(layout) {}

	void SetDepth(uint32_t value) { clearValue.depthStencil.depth = static_cast<float>(value); }
	// Default float
	void SetColor(VkClearColorValue value) { clearValue.color = value; }
	void SetColorU32(uint32_t (&value)[4])
	{
		std::copy(std::begin(value), std::end(value), std::begin(clearValue.color.uint32));
	}

	VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE;
	VkImageView resolveView = VK_NULL_HANDLE;
	VkImageLayout resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// ------------
// Descriptors
// ------------

struct DescriptorDef
{
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};

enum class Vulkan_DescriptorType
{
	COPY_STAGING     = VK_DESCRIPTOR_TYPE_MAX_ENUM, // Doesn't need descriptor
	SSBO             = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	UNIFORM          = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	INLINE           = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
	COMBINED_SAMPLER = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	STORAGE          = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	ACCEL_STRUCT     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
};

// -------------
// Buffer enums
// -------------

enum class Vulkan_BufferUsage
{
	BDA_POINTER    = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT   |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	INDIRECT       = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT    |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	VERTEX         = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT   |
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT    |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,

	INDEX          = VK_BUFFER_USAGE_INDEX_BUFFER_BIT    |
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT    |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,

	BDA_SRC_COPY   = VK_BUFFER_USAGE_TRANSFER_SRC_BIT    |
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT    |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	UNIFORM        = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,

	VERTEX_PULL    = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	AS_BUILD_INPUT = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT    |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,

	AS_STORAGE     = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	AS_SCRATCH     = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	READ_BACK      = VK_BUFFER_USAGE_TRANSFER_DST_BIT
};

// -----
// Sync
// -----
struct TimelineSync
{
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t signalValue = UINT64_MAX;

	uint64_t AdvanceTimeline() { return ++signalValue; }
};

struct ImageBarrierInfo
{
	VkPipelineStageFlags2 stageMask;
	VkAccessFlags2 accessMask;
	VkImageLayout layout;
};

inline VkSemaphoreSubmitInfo TimelineWait(
	VkSemaphore sem,
	uint64_t value,
	VkPipelineStageFlags2 stageMask)
{
	return VkSemaphoreSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = sem,
		.value = value,
		.stageMask = stageMask
	};
}

inline VkSemaphoreSubmitInfo TimelineSignal(
	VkSemaphore sem,
	uint64_t value,
	VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
{
	return VkSemaphoreSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = sem,
		.value = value,
		.stageMask = stageMask
	};
}

inline VkSemaphoreSubmitInfo BinaryWait(
	VkSemaphore sem,
	VkPipelineStageFlags2 stageMask)
{
	return VkSemaphoreSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = sem,
		.stageMask = stageMask
	};
}

inline VkSemaphoreSubmitInfo BinarySignal(
	VkSemaphore sem,
	VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
{
	return VkSemaphoreSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = sem,
		.stageMask = stageMask
	};
}

// Returns false if any query was VK_NOT_READY — caller retries next frame.
struct TimestampResult { float gpuMs = 0.f; bool valid = false; };

struct TimestampReadback
{
	bool allReady = true;

	std::array<TimestampResult, static_cast<uint32_t>(RD::PASS_COUNT)> passResults{};

	TimestampResult frameResult;
};


static constexpr ImageBarrierInfo GetImageSyncScope(RD::ImageAccess access)
{
	switch (access)
	{
		case RD::ImageAccess::Undefined:
			return { VK_PIPELINE_STAGE_2_NONE,
					 VK_ACCESS_2_NONE,
					 VK_IMAGE_LAYOUT_UNDEFINED };

		case RD::ImageAccess::TransferSrc:
			return { VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					 VK_ACCESS_2_TRANSFER_READ_BIT,
					 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };

		case RD::ImageAccess::TransferDst:
			return { VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					 VK_ACCESS_2_TRANSFER_WRITE_BIT,
					 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };

		case RD::ImageAccess::Read:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT  |
					 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
					 VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
					 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		case RD::ImageAccess::Write:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
					 VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					 VK_IMAGE_LAYOUT_GENERAL };

		case RD::ImageAccess::ComputeRead:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		case RD::ImageAccess::ComputeWrite:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_IMAGE_LAYOUT_GENERAL };

		case RD::ImageAccess::ComputeReadWrite:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
					 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					 VK_IMAGE_LAYOUT_GENERAL };

		case RD::ImageAccess::ComputeReadStorage:
			return { VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
					 VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					 VK_IMAGE_LAYOUT_GENERAL };

		case RD::ImageAccess::GraphicsColorWrite:
			return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
					 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
					 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		case RD::ImageAccess::GraphicsDepthWrite:
			return { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
					 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
					 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
					 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };

		case RD::ImageAccess::DepthRead:
			return { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
					 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT  |
					 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT       |
					 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
					 VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
					 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
					 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					 VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL };

		case RD::ImageAccess::MeshShaderRead:
			return { VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
					 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		case RD::ImageAccess::Present:
			return { VK_PIPELINE_STAGE_2_NONE,
					 VK_ACCESS_2_NONE,
					 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
	}

	return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
			 VK_IMAGE_LAYOUT_GENERAL };
}

static void PrepassPhaseBarrier(VkCommandBuffer cmd)
{
	VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	mb.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
					   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
					   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	mb.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
					   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
	mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
					   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
					   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
					   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.memoryBarrierCount = 1;
	di.pMemoryBarriers    = &mb;
	vkCmdPipelineBarrier2(cmd, &di);
}

static void ASMemoryBarrier(
	VkCommandBuffer cmd,
	VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
	VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	mb.srcStageMask  = srcStage;
	mb.srcAccessMask = srcAccess;
	mb.dstStageMask  = dstStage;
	mb.dstAccessMask = dstAccess;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.memoryBarrierCount = 1;
	di.pMemoryBarriers    = &mb;
	vkCmdPipelineBarrier2(cmd, &di);
}

// -------------------
// Devices and queues
// -------------------

enum class QueueType
{
	Graphics,
	Transfer,
	Compute,
	Present,
	Nothing
};

struct QueueFamilyIndices
{
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> transferFamily;
	std::optional<uint32_t> computeFamily;

	bool IsComplete() const noexcept
	{
		return
			graphicsFamily.has_value() &&
			presentFamily.has_value() &&
			transferFamily.has_value() &&
			computeFamily.has_value();
	}
};

struct DeviceContext
{
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	QueueFamilyIndices queueIndices;
};

struct SwapchainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities{};
	std::vector<VkSurfaceFormatKHR> formats{};
	std::vector<VkPresentModeKHR> presentModes{};
};

struct DeviceProperties
{
	VkPhysicalDeviceProperties2                        core{};
	VkPhysicalDeviceLimits                             limits{};
	VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStruct{};
	VkPhysicalDeviceMeshShaderPropertiesEXT            meshShader{};
	VkPhysicalDeviceSubgroupProperties                 subgroup{};
	VkPhysicalDeviceDescriptorIndexingProperties       descriptorIndexing{};
	VkPhysicalDevicePushDescriptorProperties           pushDescriptor{};

	void Query(VkPhysicalDevice pDevice)
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR as{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		VkPhysicalDeviceMeshShaderPropertiesEXT ms{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };
		VkPhysicalDeviceSubgroupProperties sg{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		VkPhysicalDeviceDescriptorIndexingProperties di{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };
		VkPhysicalDevicePushDescriptorProperties pd{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES };

		VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		props2.pNext = &as;
		as.pNext     = &ms;
		ms.pNext     = &sg;
		sg.pNext     = &di;
		di.pNext     = &pd;
		pd.pNext     = nullptr;

		vkGetPhysicalDeviceProperties2(pDevice, &props2);

		core        = props2;
		core.pNext  = nullptr;
		limits      = props2.properties.limits;

		accelStruct = as;  accelStruct.pNext        = nullptr;
		meshShader  = ms;  meshShader.pNext         = nullptr;
		subgroup    = sg;  subgroup.pNext           = nullptr;
		descriptorIndexing = di; descriptorIndexing.pNext = nullptr;
		pushDescriptor     = pd; pushDescriptor.pNext     = nullptr;
	}
};

struct PhysicalDeviceCandidate
{
	std::string name;
	VkPhysicalDevice pDevice = VK_NULL_HANDLE;

	DeviceProperties props;

	QueueFamilyIndices queueIndices;
	SwapchainSupportDetails swapchainSupport;
};

// Commands
struct ThreadCommandPool
{
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool computePool  = VK_NULL_HANDLE;

	ThreadCommandPool() = default;

	// Disallow copy
	ThreadCommandPool(const ThreadCommandPool&) = delete;
	ThreadCommandPool& operator=(const ThreadCommandPool&) = delete;

	// Allow move
	ThreadCommandPool(ThreadCommandPool&&) = default;
	ThreadCommandPool& operator=(ThreadCommandPool&&) = default;
};


// ---------------
// Resource usage
// ---------------

enum class HeapType
{
	GPU_Local,  // VMA_MEMORY_USAGE_GPU_ONLY
	Upload,     // CPU->GPU, persistently mapped
	Readback,   // GPU->CPU
	Staging,    // Transient upload, pooled internally
	Count
};

struct BufferDesc
{
	size_t              size          = 0;
	Vulkan_BufferUsage  usage         = Vulkan_BufferUsage::BDA_POINTER;
	HeapType            heap          = HeapType::GPU_Local;
	bool                bIsConcurrent = false;
	std::string         debugName;
};

struct ImageDesc
{
	Vulkan_Format            format         = Vulkan_Format::Undefined;
	Extents3D                extent         = { 0, 0, 0 };
	Vulkan_ImageUsage        usage          = Vulkan_ImageUsage::ComputeOnly;
	VkImageType              imageType      = VK_IMAGE_TYPE_2D;
	uint32_t                 mipLevels      = 1;   // 0 = auto-calculate
	uint32_t                 arrayLayers    = 1;
	bool                     bIsCubemap     = false;
	bool                     bPerMipStorage = false;
	bool                     bIsConcurrent  = false;
	std::string              debugName;
};
