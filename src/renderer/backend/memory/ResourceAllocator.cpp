#include "pch.h"

#include "ResourceAllocator.h"
#include "AllocatedBuffer.h"
#include "../ImageUtils.h"
#include "../../../profiler/ProfilerTypes.h"
#include "BindlessImageTable.h"

static VmaMemoryUsage HeapTypeToVma(HeapType heap) noexcept;
static VmaAllocationCreateFlags HeapTypeToVmaFlags(HeapType heap, size_t size) noexcept;

// ----------
// Lifecycle
// ----------

void Allocator::Init(const DeviceContext& ctx)
{
	VmaAllocatorCreateInfo allocInfo{
		.flags =
		VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
		VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT,
		.physicalDevice = ctx.physicalDevice,
		.device = ctx.device,
		.instance = ctx.instance,
		.vulkanApiVersion = VK_API_VERSION_1_4
	};

	VK_CHECK(vmaCreateAllocator(&allocInfo, &m_vmaAlloc));

	m_deviceCtx = ctx;
}

void Allocator::Shutdown()
{
	FrameStaging.Shutdown();
	GlobalStaging.Shutdown();
	if (m_vmaAlloc != nullptr)
		vmaDestroyAllocator(m_vmaAlloc);

	m_deviceCtx = {};
}

VRAMStats Allocator::GetTotalVRAMUsage() const
{
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(m_vmaAlloc, budgets);

	VkPhysicalDeviceMemoryProperties memoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(
		m_deviceCtx.physicalDevice,
		&memoryProperties);

	VRAMStats stats{};

	for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
	{
		const bool isDeviceLocal =
			(memoryProperties.memoryHeaps[heapIndex].flags &
				VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;

		if (!isDeviceLocal) continue;

		const VmaBudget& heap = budgets[heapIndex];
		stats.used += heap.statistics.allocationBytes;
		stats.committed += heap.statistics.blockBytes;
		stats.driverUsed += heap.usage;
		stats.budget += heap.budget;
	}

	return stats;
}

// ------------------
// Buffer allocation
// ------------------

AllocatedBuffer Allocator::AllocateGPUBuffer(
	RD::Renderer_Buffer slot,
	size_t size)
{
	BufferDesc desc{};
	desc.size = size;
	desc.usage = Vulkan_BufferUsage::BDA_POINTER;
	desc.heap = HeapType::GPU_Local;
	desc.bIsConcurrent = true;
	//desc.debugName     = "Generic_Buffer";

	switch (slot)
	{
	case RD::Renderer_Buffer::WorldProbeFrameInfo:
		desc.heap = HeapType::Upload;
		break;
	case RD::Renderer_Buffer::WorldProbeSchedule:
	case RD::Renderer_Buffer::IndirectDrawCounts:
	case RD::Renderer_Buffer::DispatchIndirectArgs:
	case RD::Renderer_Buffer::TaskDispatch:
	case RD::Renderer_Buffer::DebugDraw:
	case RD::Renderer_Buffer::RTRayList:
		desc.usage = Vulkan_BufferUsage::INDIRECT;
		break;
	case RD::Renderer_Buffer::Vertex:
	case RD::Renderer_Buffer::DebugVertex:
	case RD::Renderer_Buffer::MeshletVertices:
		desc.usage = Vulkan_BufferUsage::VERTEX;
		break;
	case RD::Renderer_Buffer::Index:
		desc.usage = Vulkan_BufferUsage::INDEX;
		break;
	case RD::Renderer_Buffer::DrawStats:
		desc.usage = Vulkan_BufferUsage::BDA_SRC_COPY;
		break;
	case RD::Renderer_Buffer::RTInstances:
		desc.usage = Vulkan_BufferUsage::AS_BUILD_INPUT;
		break;
	}

	return AllocateBuffer(desc);
}

AllocatedBuffer Allocator::AllocateUniformRaw(const void* data, size_t size)
{
	BufferDesc desc{};
	desc.size = size;
	desc.usage = Vulkan_BufferUsage::UNIFORM;
	desc.heap = HeapType::Upload;

	AllocatedBuffer buf = AllocateBuffer(desc);
	memcpy(buf.m_mappedPtr, data, size);
	vmaFlushAllocation(m_vmaAlloc, buf.m_allocation, 0, size);
	return buf;
}

AllocatedBuffer Allocator::AllocateBuffer(const BufferDesc& desc)
{
	AllocatedBuffer newBuffer;

	auto allocSize = desc.size;

	if (desc.size == 0)
	{
		fmt::println("[Allocator::AllocateBuffer] Warning: Attempting to create 0-byte buffer.");
		allocSize = 4;
	}

	newBuffer.m_bytesSize = allocSize;

	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = static_cast<VkBufferUsageFlags>(desc.usage);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// buffer to be shared across different queues
	std::array<uint32_t, 3> qFamilies{};
	uint32_t qFamCount = 0;
	uint8_t mask = 0;

	if (desc.bIsConcurrent)
	{
		const uint32_t g = m_deviceCtx.queueIndices.graphicsFamily.value();
		const uint32_t t = m_deviceCtx.queueIndices.transferFamily.value();
		const uint32_t c = m_deviceCtx.queueIndices.computeFamily.value();

		auto PushUnique = [&](uint32_t fam, uint8_t bit) {
			for (uint32_t i = 0; i < qFamCount; ++i)
			{
				if (qFamilies[i] == fam) return; // already present
			}
			qFamilies[qFamCount++] = fam;
			mask |= bit;
			};

		PushUnique(g, 0x1);
		PushUnique(t, 0x2);
		PushUnique(c, 0x4);

		if (qFamCount > 1)
		{
			bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			bufferInfo.queueFamilyIndexCount = qFamCount;
			bufferInfo.pQueueFamilyIndices = qFamilies.data();
		}
	}

	newBuffer.m_bIsConcurrent = (qFamCount > 1);
	newBuffer.m_qmask = mask;

	auto vmaUsage = HeapTypeToVma(desc.heap);

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = vmaUsage;
	vmaallocInfo.flags = HeapTypeToVmaFlags(desc.heap, desc.size);

	VmaAllocationInfo info;
	VK_CHECK(vmaCreateBuffer(m_vmaAlloc, &bufferInfo, &vmaallocInfo, &newBuffer.m_buffer, &newBuffer.m_allocation, &info));

	newBuffer.m_address = 0;
	if (bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = newBuffer.m_buffer;
		newBuffer.m_address = vkGetBufferDeviceAddress(m_deviceCtx.device, &addressInfo);
	}

	if (vmaallocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)
	{
		newBuffer.m_mappedPtr = info.pMappedData;
		REQUIRE_HARDWARE(newBuffer.m_mappedPtr != nullptr, "Buffer is not host-visible.");
	}

	return newBuffer;
}

void Allocator::FreeBuffer(AllocatedBuffer buf) const
{
	if (buf.IsValid())
		vmaDestroyBuffer(m_vmaAlloc, buf.m_buffer, buf.m_allocation);
}


// -----------------
// Image allocation
// -----------------

static constexpr VkImageAspectFlags GetAspectMaskFromFormat(VkFormat format)
{
	switch (format)
	{
		// Depth only
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

		// Stencil only
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

		// Depth + stencil
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

AllocatedImage Allocator::AllocateImage(const ImageDesc& desc) const
{
	AllocatedImage newImage;
	newImage.m_extent = desc.extent;
	newImage.m_name = desc.debugName;

	VkExtent3D vkExtent = { newImage.m_extent.Width(), newImage.m_extent.Height(), newImage.m_extent.Depth() };

	auto imgFormat = static_cast<VkFormat>(desc.format);
	newImage.m_pixelBytes = static_cast<uint32_t>(ImageUtils::GetPixelSize(imgFormat));
	auto bufUsage = static_cast<VkImageUsageFlags>(desc.usage);

	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType = desc.imageType;
	imgInfo.extent = vkExtent;
	imgInfo.format = imgFormat;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = bufUsage;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	std::array<uint32_t, 2> qFamilies{};
	if (desc.bIsConcurrent)
	{
		const uint32_t g = m_deviceCtx.queueIndices.graphicsFamily.value();
		const uint32_t c = m_deviceCtx.queueIndices.computeFamily.value();

		if (g != c)
		{
			qFamilies = { g, c };
			imgInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			imgInfo.queueFamilyIndexCount = 2u;
			imgInfo.pQueueFamilyIndices = qFamilies.data();
		}
	}

	// mipmapping
	imgInfo.mipLevels = 1;
	newImage.m_mipLevels = 1;

	// auto compute mip levels
	if (desc.mipLevels == 0)
	{
		imgInfo.mipLevels = ImageUtils::CalculateMipLevels(newImage.Width(), newImage.Height());
		newImage.m_mipLevels = imgInfo.mipLevels;
		newImage.m_bIsMipmapped = true;
	}
	// mip count already predefined
	else if (desc.mipLevels > 1)
	{
		imgInfo.mipLevels = desc.mipLevels;
		newImage.m_mipLevels = desc.mipLevels;
		newImage.m_bIsMipmapped = true;
	}

	// array/cubemap
	imgInfo.arrayLayers = 1;
	newImage.m_arrayLayers = 1;

	newImage.m_bIsCubemap = desc.bIsCubemap;

	if (newImage.m_bIsCubemap)
	{
		imgInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imgInfo.arrayLayers = 6;
		newImage.m_arrayLayers = 6;
	}
	else if (desc.arrayLayers > 1)
	{
		imgInfo.arrayLayers = desc.arrayLayers;
		newImage.m_arrayLayers = desc.arrayLayers;
	}

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	imgAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VK_CHECK(vmaCreateImage(m_vmaAlloc, &imgInfo, &imgAllocInfo, &newImage.m_image, &newImage.m_allocation, nullptr));

	auto aspectMask = GetAspectMaskFromFormat(imgFormat);

	// sampled view creation
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = newImage.m_image;
	viewInfo.format = imgFormat;
	viewInfo.subresourceRange.aspectMask = aspectMask;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = newImage.m_arrayLayers;

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	if (desc.imageType & VK_IMAGE_TYPE_3D)
	{
		viewType = VK_IMAGE_VIEW_TYPE_3D;
	}

	viewInfo.viewType = newImage.m_bIsCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : viewType;

	newImage.m_aspect = aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT ? ImageAspect::Depth : ImageAspect::Color;

	VK_CHECK(vkCreateImageView(m_deviceCtx.device, &viewInfo, nullptr, &newImage.m_imageView));

	// storage view creation
	if (desc.usage == Vulkan_ImageUsage::DrawColor ||
		desc.usage == Vulkan_ImageUsage::ComputeRWTransfer ||
		desc.usage == Vulkan_ImageUsage::ComputeOnly)
	{
		// Per-mip storage views
		if (imgInfo.mipLevels > 1 && desc.bPerMipStorage)
		{
			newImage.m_bPerMipStorageViews = true;
			newImage.m_vStorageViews.resize(imgInfo.mipLevels);

			for (uint32_t mip = 0; mip < imgInfo.mipLevels; ++mip)
			{
				VkImageViewCreateInfo mipViewInfo = viewInfo;
				mipViewInfo.subresourceRange.baseMipLevel = mip;
				mipViewInfo.subresourceRange.levelCount = 1;

				if (newImage.m_bIsCubemap)
				{
					mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
					mipViewInfo.subresourceRange.layerCount = 6;
				}
				else
				{
					mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					mipViewInfo.subresourceRange.layerCount = 1;
				}

				VK_CHECK(vkCreateImageView(m_deviceCtx.device, &mipViewInfo, nullptr, &newImage.m_vStorageViews[mip]));
			}
		}
		else if (imgInfo.arrayLayers > 1 || newImage.m_bIsCubemap)
		{
			// single m_image storage view at index 0
			newImage.m_vStorageViews.resize(1);

			VkImageViewCreateInfo storageViewInfo = viewInfo;
			storageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			storageViewInfo.subresourceRange.baseMipLevel = 0;
			storageViewInfo.subresourceRange.levelCount = 1;

			VK_CHECK(vkCreateImageView(m_deviceCtx.device, &storageViewInfo, nullptr, &newImage.m_vStorageViews[0]));
		}
	}

	return newImage;
}

void Allocator::FreeImage(AllocatedImage& img) const
{
	ImageUtils::DestroyImageView(m_deviceCtx.device, img.m_imageView);

	for (auto& view : img.m_vStorageViews)
	{
		ImageUtils::DestroyImageView(m_deviceCtx.device, view);
	}

	vmaDestroyImage(m_vmaAlloc, img.m_image, img.m_allocation);
}

void Allocator::ResetGlobalStaging(size_t size, size_t atomicSize)
{
	GlobalStaging.Shutdown();
	GlobalStaging.Init(size, m_vmaAlloc, m_deviceCtx.device, atomicSize);
}

size_t Allocator::CalcBaseGlobalStagingSize(const BindlessImageTable& imageTable) const
{
	size_t total = 0;

	// ---------------------------
	// 1. Static textures
	// ---------------------------
	total += imageTable.CalcStaticTexturesStagingSize();

	//// ---------------------------
	//// 2. Equirect HDR textures
	//// ---------------------------
	//// We must match UploadEquirects() exactly:
	//// - RGBA32F = 16 bytes per pixel
	//// - layers = 1
	//// - mip = 0 upload (single level)
	//for (const auto& envSet : imageTable.GetEnvironmentSetSpan())
	//{
	//	if (!envSet.IsValid()) continue;

	//	if (!envSet.equirect.IsValid()) continue;

	//	const uint32_t width  = envSet.equirect.Width();
	//	const uint32_t height = envSet.equirect.Height();

	//	const uint32_t depth  = 1u;
	//	const uint32_t layers = 1u;

	//	const size_t pixelBytes = envSet.equirect.m_pixelBytes;

	//	const size_t rawSize =
	//		static_cast<size_t>(width) *
	//		static_cast<size_t>(height) *
	//		static_cast<size_t>(depth) *
	//		static_cast<size_t>(layers) *
	//		pixelBytes;

	//	const size_t paddedSize = AllocatedBuffer::AlignUp(rawSize, 4u);

	//	total += paddedSize;
	//}

	// ----------------------------
	// 3. Safety floor for buffers
	// ----------------------------
	const size_t minSize = 64u * 1024u * 1024u;
	total = std::max(total, minSize);

	return total;
}


VmaMemoryUsage HeapTypeToVma(HeapType heap) noexcept
{
	switch (heap)
	{
	case HeapType::GPU_Local:
		return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	case HeapType::Upload:
	case HeapType::Readback:
	case HeapType::Staging:
		return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	default:
		return VMA_MEMORY_USAGE_AUTO;
	}
}

VmaAllocationCreateFlags HeapTypeToVmaFlags(HeapType heap, size_t size) noexcept
{
	VmaAllocationCreateFlags flags = 0;

	switch (heap)
	{
	case HeapType::Upload:
	case HeapType::Staging:
		flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		break;
	case HeapType::Readback:
		flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		break;
	case HeapType::GPU_Local:
		flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		break;
	}

	if (size >= (512 * 1024))
		flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	return flags;
}
