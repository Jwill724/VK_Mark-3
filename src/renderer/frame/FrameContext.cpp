#include "pch.h"

#include "FrameContext.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../backend/memory/Budgets.h"
#include "../backend/Device.h"
#include "../backend/descriptors/DescriptorManager.h"
#include "../backend/descriptors/DescriptorWriter.h"
#include "FrameResources.h"
#include "../scene/WorldProbeTypes.h"

void FrameContext::Init(
	uint32_t frameIndex,
	uint32_t threadSlotCount,
	Device& device,
	DescriptorManager& descriptorsManager,
	Allocator& allocator,
	VkDescriptorSet frameSet)
{
	const auto& deviceCtx = device.GetContext();
	auto logicalDevice = deviceCtx.device;
	auto qIndices = deviceCtx.queueIndices;
	m_frameIndex = frameIndex;

	m_graphicsPool = device.CreateCommandPool(QueueType::Graphics);
	m_transferPool = device.CreateCommandPool(QueueType::Transfer);
	m_computePool = device.CreateCommandPool(QueueType::Compute);

	device.CreateCommandBuffers(m_graphicsPool, m_graphicsPrimaries.data(), RD::MAX_GRAPHICS_PRIMARIES);
	m_asyncComputeCmd = device.CreateCommandBuffer(m_computePool);

	const uint32_t gfxFamily = device.GetGraphicsQueue().GetFamilyIndex();
	const uint32_t computeFamily = device.GetComputeQueue().GetFamilyIndex();

	if (gfxFamily != computeFamily)
	{
		m_secondaryArena.Init(
			device.GetContext().device,
			threadSlotCount,
			computeFamily);
	}

	// Allocated serially before the parallel frame initialization jobs.
	ASSERT(frameSet != VK_NULL_HANDLE);
	m_frameSet = frameSet;

	// GPU address table
	m_gpuAddressTable.Init(allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::WorldProbeFrameInfo,
		sizeof(WorldProbeFrameInfo),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::WorldProbeCacheInfo,
		sizeof(WorldProbeCacheInfo),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DynamicTransforms,
		GPU_BYTES_DYNAMIC_TRANSFORMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::MotionMatrices,
		GPU_BYTES_DYNAMIC_TRANSFORMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Lights,
		GPU_BYTES_LIGHTS,
		allocator);

	// Cull outputs
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceVisibility,
		GPU_BYTES_INSTANCE_VISIBILITY,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleCount,
		sizeof(uint32_t),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleInstances,
		GPU_BYTES_VISIBLE_INSTANCES,
		allocator);

	// Draw build intermediates
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceStreams,
		GPU_BYTES_INSTANCE_STREAMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceCursors,
		GPU_BYTES_INSTANCE_STREAMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawInstanceIDs,
		GPU_BYTES_DRAW_INSTANCE_IDS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::IndirectDrawCounts,
		GPU_BYTES_INDIRECT_DRAW_COUNTS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBins,
		GPU_BYTES_DRAW_BINS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBinCounters,
		GPU_BYTES_DRAW_BIN_COUNTERS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ShadowCullData,
		GPU_BYTES_SHADOW_CULL_DATA,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ShadowInvalidVolumes,
		RD::SHADOW_INVALID_VOLUME_BYTES,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DispatchIndirectArgs,
		GPU_BYTES_DISPATCH_INDIRECT_ARGS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawStats,
		sizeof(GPUStats),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightCount,
		sizeof(uint32_t),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightIDs,
		GPU_BYTES_VISIBLE_LIGHT_IDS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::MeshletVisibilityA,
		GPU_BYTES_MESHLET_VISIBILITY,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::MeshletVisibilityB,
		GPU_BYTES_MESHLET_VISIBILITY,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::TaskDispatch,
		GPU_BYTES_TASK_DISPATCH,
		allocator);

	m_statsReadback = allocator.AllocateBuffer({
		sizeof(GPUStats),
		Vulkan_BufferUsage::READ_BACK,
		HeapType::Readback
		});

	vmaMapMemory(allocator.GetVma(),
		m_statsReadback.m_allocation,
		reinterpret_cast<void**>(const_cast<GPUStats**>(&m_statsMapped)));

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::RTInstances,
		GPU_BYTES_RT_INSTANCES,
		allocator);

	CreateTLAS(device, allocator);

	VkQueryPoolCreateInfo queryPoolInfo{};
	queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = TIMESTAMP_QUERY_COUNT;

	VK_CHECK(vkCreateQueryPool(
		logicalDevice,
		&queryPoolInfo,
		nullptr,
		&m_graphicsTimestampPool
	));

	VK_CHECK(vkCreateQueryPool(
		logicalDevice,
		&queryPoolInfo,
		nullptr,
		&m_computeTimestampPool
	));

	for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
	{
		m_passTimestampRanges[passIndex].beginQuery = passIndex * 2;
		m_passTimestampRanges[passIndex].endQuery = passIndex * 2 + 1;
	}

	m_timestampResults.fill(0);
}

void FrameContext::CreateTLAS(Device& device, Allocator& allocator)
{
	const auto& ctx = device.GetContext();

	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	props2.pNext = &asProps;
	vkGetPhysicalDeviceProperties2(ctx.physicalDevice, &props2);

	VkAccelerationStructureGeometryKHR geom{
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geom.flags = 0;

	auto& inst = geom.geometry.instances;
	inst.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	inst.arrayOfPointers = VK_FALSE;
	inst.data.deviceAddress =
		m_gpuAddressTable.GetGPUBuffer(RD::Renderer_Buffer::RTInstances).m_address;

	VkAccelerationStructureBuildGeometryInfoKHR build{
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = 1;
	build.pGeometries = &geom;

	const uint32_t maxPrims = RD::MAX_RT_INSTANCES;

	VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(
		ctx.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&build, &maxPrims, &sizeInfo);

	m_tlasStorage = allocator.AllocateBuffer({
		sizeInfo.accelerationStructureSize,
		Vulkan_BufferUsage::AS_STORAGE,
		HeapType::GPU_Local,
		false,
		"TLASStorage" });

	VkAccelerationStructureCreateInfoKHR ci{
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	ci.buffer = m_tlasStorage.m_buffer;
	ci.offset = 0;
	ci.size = sizeInfo.accelerationStructureSize;
	ci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	VK_CHECK(vkCreateAccelerationStructureKHR(ctx.device, &ci, nullptr, &m_tlas));

	const VkDeviceSize scratchAlign = asProps.minAccelerationStructureScratchOffsetAlignment;

	m_tlasScratch = allocator.AllocateBuffer({
		sizeInfo.buildScratchSize + scratchAlign,
		Vulkan_BufferUsage::AS_SCRATCH,
		HeapType::GPU_Local,
		false,
		"TLASScratch" });

	m_tlasScratchAddress =
		(m_tlasScratch.m_address + scratchAlign - 1) & ~(scratchAlign - 1);
}

void FrameContext::DeferredClearGPUBuffer(RD::Renderer_Buffer slot, Allocator& allocator)
{
	AllocatedBuffer old = m_gpuAddressTable.DetachGPUAddressBuffer(slot);
	if (!old.IsValid()) return;

	m_cpuDeletionQueue.PushFunction([old, &allocator]() mutable {
		allocator.FreeBuffer(old);
		});
}

void FrameContext::ClusterReset(Allocator& allocator)
{
	for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::ClusterCounts);
		i <= static_cast<size_t>(RD::Renderer_Buffer::VolClusterLightIDs);
		i++)
	{
		DeferredClearGPUBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}
}

void FrameContext::CreateRTRayListBuffer(const RTRayListLayout& sizes, Allocator& allocator)
{
	DeferredClearGPUBuffer(RD::Renderer_Buffer::RTRayList, allocator);
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::RTRayList,
		sizes.totalBytes,
		allocator);
}

void FrameContext::CreateDebugBuffers(Allocator& allocator)
{
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugCounts,
		GPU_BYTES_DEBUG_COUNTERS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugItems,
		GPU_BYTES_DEBUG_ITEMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugVertex,
		GPU_BYTES_DEBUG_VERTEX,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugDraw,
		RD::INDIRECT_CMD_SIZE,
		allocator);
}

void FrameContext::DestroyDebugBuffers(Allocator& allocator)
{
	for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::DebugCounts);
		i <= static_cast<size_t>(RD::Renderer_Buffer::DebugDraw);
		i++)
	{
		m_gpuAddressTable.ClearGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}
}

void FrameContext::CreateClusterBuffers(
	const ClusterBufferSizes& clusterBufSizes,
	Allocator& allocator)
{
	ClusterReset(allocator);

	// Uniform buffer updates
	if (m_clustered_UBO.IsValid())
	{
		m_cpuDeletionQueue.PushFunction([old = m_clustered_UBO, &allocator]() mutable {
			allocator.FreeBuffer(old);
			});
		m_clustered_UBO = {};
	}

	m_clusterData.tileCountX = clusterBufSizes.tileCountX;
	m_clusterData.tileCountY = clusterBufSizes.tileCountY;
	m_clusterData.clusterCount = clusterBufSizes.clusterCount;
	m_clustered_UBO = allocator.AllocateUniform(m_clusterData);
	m_bClusterUniformWriteNeeded = true; // Descriptor update needed

	// Ssbo updates

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterCounts,
		clusterBufSizes.clusterCountsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterOffsets,
		clusterBufSizes.clusterOffsetsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterCursors,
		clusterBufSizes.clusterCursorsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterLightIDs,
		clusterBufSizes.clusterLightIDsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterTileSliceRanges,
		clusterBufSizes.clusterTileSliceRangesBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterScanScratch,
		clusterBufSizes.clusterScanScratchBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterTileTransparentNear,
		clusterBufSizes.tileTransparentNearBytes,
		allocator);

	// Volumetric clusters
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VolClusterCounts,
		clusterBufSizes.clusterCountsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VolClusterOffsets,
		clusterBufSizes.clusterOffsetsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VolClusterLightIDs,
		clusterBufSizes.clusterLightIDsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VolClusterCursors,
		clusterBufSizes.clusterCursorsBytes,
		allocator);
}

void FrameContext::CollectTransferCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue)
{
	if (cmds.empty() || queue != QueueType::Transfer) return;

	m_transferCommands.insert(m_transferCommands.end(),
		std::make_move_iterator(cmds.begin()),
		std::make_move_iterator(cmds.end()));
}

void FrameContext::StashTransferCmds()
{
	m_transferCommandsToFree.insert(m_transferCommandsToFree.end(), m_transferCommands.begin(), m_transferCommands.end());
	m_transferCommands.clear();
}

void FrameContext::FreeStashedCmds(const DeviceContext& deviceCtx)
{
	if (!m_transferCommandsToFree.empty())
	{
		vkFreeCommandBuffers(
			deviceCtx.device,
			m_transferPool,
			static_cast<uint32_t>(m_transferCommandsToFree.size()),
			m_transferCommandsToFree.data());
		m_transferCommandsToFree.clear();
	}
}


void FrameContext::TickDescriptorWrites(DescriptorWriter& writer)
{
	// Most frequent writes
	writer.WriteBuffer(
		RD::FRAME_BINDING_SCENE,
		m_sceneInfo_UBO,
		m_frameSet);

	writer.WriteBuffer(
		RD::FRAME_BINDING_CSM,
		m_directionalCSM_UBO,
		m_frameSet);

	writer.WriteBuffer(
		RD::FRAME_BINDING_VOLUMETRIC,
		m_volumetricShadow_UBO,
		m_frameSet);

	// Less frequent writes

	if (m_gpuAddressTable.IsTableDirty())
	{
		writer.WriteBuffer(
			RD::ADDRESS_TABLE_BINDING,
			m_gpuAddressTable.GetTableBuffer(),
			m_frameSet);
	}

	if (m_bClusterUniformWriteNeeded)
	{
		writer.WriteBuffer(
			RD::FRAME_BINDING_CLUSTERED,
			m_clustered_UBO,
			m_frameSet);
		m_bClusterUniformWriteNeeded = false;
	}

	if (m_bTlasWriteNeeded && m_tlas != VK_NULL_HANDLE)
	{
		writer.WriteAccelerationStructure(
			RD::FRAME_BINDING_TLAS,
			m_tlas,
			m_frameSet);
		m_bTlasWriteNeeded = false;
	}
}

void FrameContext::AssignSceneUniform(AllocatedBuffer buffer, const Allocator& allocator)
{
	m_sceneInfo_UBO = std::move(buffer);
	m_cpuDeletionQueue.PushFunction([buffer = m_sceneInfo_UBO, &allocator]() mutable {
		allocator.FreeBuffer(buffer);
		});
}
void FrameContext::AssignCSMUniform(AllocatedBuffer buffer, const Allocator& allocator)
{
	m_directionalCSM_UBO = std::move(buffer);
	m_cpuDeletionQueue.PushFunction([buffer = m_directionalCSM_UBO, &allocator]() mutable {
		allocator.FreeBuffer(buffer);
		});
}
void FrameContext::AssignVolumetricShadowUniform(AllocatedBuffer buffer, const Allocator& allocator)
{
	m_volumetricShadow_UBO = std::move(buffer);
	m_cpuDeletionQueue.PushFunction([buffer = m_volumetricShadow_UBO, &allocator]() mutable {
		allocator.FreeBuffer(buffer);
		});
}

void FrameContext::Cleanup(const DeviceContext& deviceCtx, Allocator& allocator)
{
	m_cpuDeletionQueue.Flush(); // Memory frees for SceneInfo and DirectionalCSM uniforms occur in here
	allocator.FreeBuffer(m_clustered_UBO);

	if (m_statsMapped)
	{
		vmaUnmapMemory(allocator.GetVma(), m_statsReadback.m_allocation);
		m_statsMapped = nullptr;
	}
	allocator.FreeBuffer(m_statsReadback);

	m_gpuAddressTable.Shutdown(allocator);

	if (m_tlas != VK_NULL_HANDLE)
	{
		vkDestroyAccelerationStructureKHR(deviceCtx.device, m_tlas, nullptr);
		m_tlas = VK_NULL_HANDLE;
	}
	allocator.FreeBuffer(m_tlasStorage);
	allocator.FreeBuffer(m_tlasScratch);

	if (m_graphicsTimestampPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(deviceCtx.device, m_graphicsTimestampPool, nullptr);

	if (m_computeTimestampPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(deviceCtx.device, m_computeTimestampPool, nullptr);

	FreeStashedCmds(deviceCtx);

	m_secondaryArena.Cleanup();

	m_transferCommands.clear();

	if (m_graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_graphicsPool, nullptr);

	if (m_transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_transferPool, nullptr);

	if (m_computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_computePool, nullptr);
}