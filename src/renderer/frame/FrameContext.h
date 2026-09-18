#pragma once

#include "../backend/VulkanForward.h"
#include "../RendererDefinitions.h"
#include "../backend/memory/BindlessBDATable.h"
#include "EngineTypes.h"
#include "FrameResources.h"
#include "SecondaryCmdArena.h"

namespace RD = RendererDefinitions;

class Device;
struct DeviceContext;
class DescriptorManager;
class DescriptorWriter;
class Allocator;
struct ClusterBufferSizes;

enum class QueueType;

class Renderer;
class Profiler;

class FrameContext
{
	friend class Renderer;
	friend class Profiler;
public:
	void Init(
		uint32_t frameIndex,
		uint32_t threadSlotCount,
		Device& device,
		DescriptorManager& descriptorsManager,
		Allocator& allocator,
		VkDescriptorSet frameSet);
	void Cleanup(const DeviceContext& deviceCtx, Allocator& allocator);

	BindlessBDATable& GetBindlessBDATable() { return m_gpuAddressTable; }

	void SetPendingAddrTableVersion(uint32_t version) { m_pendingAddressTableVersion = version; }
	uint32_t GetPendingAddrTableVersion() const noexcept { return m_pendingAddressTableVersion; }

	std::vector<VkCommandBuffer>& GetTransferCommands() { return m_transferCommands; }

	void CollectTransferCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue);
	void StashTransferCmds();
	void FreeStashedCmds(const DeviceContext& deviceCtx);

	void ClusterReset(Allocator& allocator);
	void CreateClusterBuffers(
		const ClusterBufferSizes& clusterBufSizes,
		Allocator& allocator);

	void DestroyDebugBuffers(Allocator& allocator);
	void CreateDebugBuffers(Allocator& allocator);

	const AllocatedBuffer& GetGPUBuffer(RD::Renderer_Buffer buffer) const { return m_gpuAddressTable.GetGPUBuffer(buffer); }

	const RD::PassTimestampRange& GetTimestampRange(RD::Renderer_Pass pass) const
	{
		return m_passTimestampRanges[static_cast<uint32_t>(pass)];
	}

	bool IsTemporalValid() const noexcept { return m_bIsTemporalValid; }
	bool IsHiZValid()      const noexcept { return m_bIsHiZValid; }

	VkCommandPool  GetTransferPool()  const { return m_transferPool; }
	uint64_t&      GetTransferWaitValue()   { return transferWaitValue; }

	bool IsInstanceInputsUploadNeeded() const noexcept { return m_bInstanceInputUploadNeeded; }
	bool IsTransformsUploadNeeded()     const noexcept { return m_bTransformsBufferUploadNeeded; }
	bool IsLightsUploadNeeded()         const noexcept { return m_bLightsBufferUploadNeeded; }

	void ClearTransformsUploadFlag() noexcept { m_bTransformsBufferUploadNeeded = false; }
	void ClearLightsUploadFlag()     noexcept { m_bLightsBufferUploadNeeded = false; }

	bool DoesCachedExtentNeedUpdate(uint32_t width, uint32_t height) noexcept
	{
		if (m_cachedDrawExtent.Width() != width || m_cachedDrawExtent.Height() != height)
		{
			m_cachedDrawExtent.Width() = width;
			m_cachedDrawExtent.Height() = height;
			return true;
		}
		return false;
	}

	void ResetDrawExtentCache() { m_cachedDrawExtent = {}; }

	void AssignSceneUniform(AllocatedBuffer buffer, const Allocator& allocator);
	void AssignCSMUniform(AllocatedBuffer buffer, const Allocator& allocator);
	void AssignVolumetricShadowUniform(AllocatedBuffer buffer, const Allocator& allocator);

	void IsFlashlightStateVersionOld(uint32_t version) noexcept
	{
		if (m_uploadedFlashlightVersion != version)
		{
			m_bLightsBufferUploadNeeded = true;
			m_uploadedFlashlightVersion = version;
		}
	}

	// Requires update when count hasn't changed besides dynamic and static states
	void EvaluatePossibleLightUpdateStatus() noexcept
	{
		if (m_bRecentDynamicLightsTransform && !m_bLightsBufferUploadNeeded)
		{
			m_bLightsBufferUploadNeeded = true;
			m_bRecentDynamicLightsTransform = false;
		}
	}
	void MarkDynamicLightTransforms() { m_bRecentDynamicLightsTransform = true; }


	void EvaluateLightListSizeChanges(size_t listSize)
	{
		auto lightListSize = static_cast<uint32_t>(listSize);
		if (m_recentLightListCount != lightListSize)
		{
			m_recentLightListCount = lightListSize;
			m_bLightsBufferUploadNeeded = true;
		}
	}

	void IsFirstLightsUpload(bool lightsExist)
	{
		if (lightsExist && !m_bLightsInitialized)
		{
			m_bLightsInitialized = true;
		}
	}

	void MarkLightUpload()
	{
		m_bLightsBufferUploadNeeded = true;
	}

	void EvaluateTransformsStatus(bool uploadNeeded)
	{
		if (uploadNeeded || !m_bTransformsInitialized)
		{
			m_bTransformsBufferUploadNeeded = true;
			m_bTransformsInitialized = true;
		}
	}

	void FlagInstanceInputUpload(bool flag) { m_bInstanceInputUploadNeeded = flag; }
	void ClearInstanceInputUploadFlag() { m_bInstanceInputUploadNeeded = false; }

	const LightClustersData& GetClusterData() const { return m_clusterData; }

	const AllocatedBuffer& GetStatsReadbackBuffer() const { return m_statsReadback; }

	VkCommandBuffer GetPrimaryCommandBuffer() const noexcept
	{
		return m_graphicsPrimaries[0];
	}

	VkCommandBuffer GetGraphicsPrimary(uint32_t batchIdx) const noexcept
	{
		ASSERT(batchIdx < RD::MAX_GRAPHICS_PRIMARIES);
		return m_graphicsPrimaries[batchIdx];
	}

	VkCommandBuffer GetAsyncComputePrimary() const noexcept
	{
		return m_asyncComputeCmd;
	}

	SecondaryCmdArena& GetSecondaryArena() { return m_secondaryArena; }

	void SwapMeshletVisibility()
	{
		m_gpuAddressTable.SwapBufferSlots(
			RD::Renderer_Buffer::MeshletVisibilityA,
			RD::Renderer_Buffer::MeshletVisibilityB);
	}

	bool IsMeshletVisibilityBufferInitialized() const noexcept
	{
		return m_bMeshletVisInitialized;
	}
	void MeshletVisibilityBufferValid() { m_bMeshletVisInitialized = true; }
	void InvalidateMeshletVisibility() noexcept { m_bMeshletVisInitialized = false; }

	void MarkTlasDirty()  noexcept { m_bTlasDirty = true; }
	void ClearTlasFlag()  noexcept { m_bTlasDirty = false; }
	bool IsTlasDirty()    const noexcept { return m_bTlasDirty; }

	VkAccelerationStructureKHR GetTLAS() const noexcept { return m_tlas; }
	VkDeviceAddress GetTlasScratchAddress() const noexcept { return m_tlasScratchAddress; }

	void CreateRTRayListBuffer(const RTRayListLayout& sizes, Allocator& allocator);

	VkDescriptorSet GetFrameSet() const noexcept { return m_frameSet; }

	void DeferredClearGPUBuffer(RD::Renderer_Buffer slot, Allocator& allocator);

	uint32_t GetFrameIndex() const noexcept { return m_frameIndex; }

	bool IsLuminanceResetNeeded() const noexcept { return m_bLuminanceResetNeeded; }
	void MarkLuminanceReset()     noexcept { m_bLuminanceResetNeeded = true; }
	void ClearLuminanceResetFlag() noexcept { m_bLuminanceResetNeeded = false; }

private:
	uint32_t m_frameIndex = 0u;

	uint32_t m_swapchainImageIndex = 0u;

	VkCommandPool m_graphicsPool = VK_NULL_HANDLE;

	uint64_t transferWaitValue = UINT64_MAX;
	VkCommandPool m_transferPool;
	std::vector<VkCommandBuffer> m_transferCommands;

	// === async compute ===
	VkCommandPool m_computePool = VK_NULL_HANDLE;

	std::vector<VkCommandBuffer> m_transferCommandsToFree;

	std::array<VkCommandBuffer, RD::MAX_GRAPHICS_PRIMARIES> m_graphicsPrimaries{};

	VkCommandBuffer m_asyncComputeCmd = VK_NULL_HANDLE; // async compute primary (C0)
	SecondaryCmdArena m_secondaryArena;

	uint32_t m_recentLightListCount = 0u;
	uint32_t m_uploadedFlashlightVersion = 0u;

	VkQueryPool m_graphicsTimestampPool = VK_NULL_HANDLE;
	bool m_bHasTimestampResultsPending = false;

	VkQueryPool m_computeTimestampPool = VK_NULL_HANDLE;
	std::atomic<bool> m_bHasComputeTimestampsPending = false;
	std::array<bool, TIMESTAMP_PASS_COUNT> m_timestampPassUsedCompute{};

	std::array<RD::PassTimestampRange, TIMESTAMP_PASS_COUNT> m_passTimestampRanges{};
	std::array<uint64_t, PASS_TIMESTAMP_QUERY_COUNT> m_timestampResults{};
	std::array<bool, TIMESTAMP_PASS_COUNT> m_timestampPassUsed{};

	Extents2D m_cachedDrawExtent;

	// Takes renderer descriptor writer
	void TickDescriptorWrites(DescriptorWriter& writer);

	void SetTemporalResult(bool result) { m_bIsTemporalValid = result; }
	bool m_bIsTemporalValid = false;

	void SetHiZValidResult(bool result) { m_bIsHiZValid = result; }
	bool m_bIsHiZValid = false;

	LightClustersData m_clusterData;

	bool m_bDebugLineRendering = false;

	// Handles both draw bin keys and instance inputs since they are directly tied together
	bool m_bInstanceInputUploadNeeded = false;

	bool m_bTransformsInitialized = false;
	bool m_bTransformsBufferUploadNeeded = false;

	bool m_bLightsInitialized = false;
	bool m_bLightsBufferUploadNeeded = false;
	bool m_bRecentDynamicLightsTransform = false;

	bool m_bMeshletVisInitialized = false;

	// frame owned gpu buffers
	BindlessBDATable m_gpuAddressTable;
	uint32_t m_pendingAddressTableVersion = 0u;

	AllocatedBuffer m_statsReadback;
	const GPUStats* m_statsMapped = nullptr;

	AllocatedBuffer m_directionalCSM_UBO;
	AllocatedBuffer m_volumetricShadow_UBO;
	AllocatedBuffer m_sceneInfo_UBO;

	bool m_bClusterUniformWriteNeeded = false;
	AllocatedBuffer m_clustered_UBO;

	VkDescriptorSet m_frameSet = VK_NULL_HANDLE;\

	bool m_bLuminanceResetNeeded = false;

	void CreateTLAS(Device& device, Allocator& allocator);

	bool m_bTlasDirty = true;
	bool m_bTlasWriteNeeded = true;
	AllocatedBuffer            m_tlasStorage;
	VkAccelerationStructureKHR m_tlas = VK_NULL_HANDLE;
	AllocatedBuffer            m_tlasScratch;
	VkDeviceAddress            m_tlasScratchAddress = 0;

	DeletionQueue m_cpuDeletionQueue;
};
