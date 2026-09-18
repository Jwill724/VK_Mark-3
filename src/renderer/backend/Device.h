#pragma once

#include "VulkanTypes.h"
#include "Queue.h"
#include "Debugging.h"
#include "string"
#include "functional"

struct GLFWwindow;

class Device final
{
public:
	const DeviceContext& GetContext() const { return m_context; }
	VkSurfaceKHR         GetSurface() const { return m_surface; }

	Debugging Debugging;

	std::string GetPhysicalDeviceName()  const { return m_pDeviceName; }

	const DeviceProperties& GetProperties() const noexcept { return m_props; }
	size_t      GetNonCoherentAtomSize() const { return m_props.limits.nonCoherentAtomSize; }
	uint32_t    GetMaxPushConstantSize() const { return m_props.limits.maxPushConstantsSize; }
	uint32_t    GetMaxMemoryAllocation() const { return m_props.limits.maxMemoryAllocationCount; }
	uint32_t    GetMaxAnisotropy()       const { return static_cast<uint32_t>(m_props.limits.maxSamplerAnisotropy); }
	float       GetTimestampPeriod()     const { return m_props.limits.timestampPeriod; }

	VkDeviceSize GetMinASScratchAlignment() const noexcept
	{
		return m_props.accelStruct.minAccelerationStructureScratchOffsetAlignment;
	}
	uint64_t GetMaxASInstanceCount() const noexcept
	{
		return m_props.accelStruct.maxInstanceCount;
	}

	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

	VkFormat FindDepthFormat();
	VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature);
	bool HasStencilComponent(VkFormat format);

	std::vector<uint32_t> FindSupportedSampleCounts() const;

	void CreateInstance();
	void CreateSurface(GLFWwindow* windowHandle);

	const std::vector<const char*>& GetDeviceExtensions() const { return m_deviceExtensions; }

	void InitLogical(const PhysicalDeviceCandidate& candidate);

	void Cleanup();

	// -----------------------------
	// Diagnostics
	// -----------------------------

	bool IsDeviceFaultSupported()  const noexcept { return pfn_vkGetDeviceFaultInfoEXT != nullptr; }
	bool AreCheckpointsSupported() const noexcept { return pfn_vkCmdSetCheckpointNV != nullptr; }
	bool IsObjectNamingSupported() const noexcept { return pfn_vkSetDebugUtilsObjectNameEXT != nullptr; }

	enum class MarkerBackend { None, NvCheckpoints, AmdBufferMarker };

	MarkerBackend GetMarkerBackend() const noexcept { return m_markerBackend; }

	void InitCrashMarkers(uint32_t framesInFlight, uint32_t maxPassesPerBatch);
	void CleanupCrashMarkers();
	void ResetCrashMarkers(uint32_t frameIndex) const;

	void MarkPassBegin(VkCommandBuffer cmd, QueueType qType, uint32_t frameIndex, uint32_t passIndex) const;
	void MarkPassEnd(VkCommandBuffer cmd, QueueType qType, uint32_t frameIndex, uint32_t passIndex) const;

	void DumpDeviceState(const char* context) const;

	void ReportDeviceFault(const char* context) const;
	void ReportCheckpoints(const char* context) const;
	void ReportMemoryBudget(const char* context) const;

	void SetObjectName(uint64_t handle, VkObjectType type, const char* name) const;

	template<typename T>
	void SetObjectName(T handle, VkObjectType type, const char* name) const
	{
		SetObjectName(reinterpret_cast<uint64_t>(handle), type, name);
	}

	void SetCheckpoint(VkCommandBuffer cmd, const char* marker) const;

	bool AreDebugLabelsSupported() const noexcept { return pfn_vkCmdBeginDebugUtilsLabelEXT != nullptr; }
	bool AreDebugLabelsEnabled()   const noexcept { return m_debugLabelsEnabled; }
	void SetDebugLabelsEnabled(bool enabled) noexcept { m_debugLabelsEnabled = enabled; }

	void BeginDebugLabel(VkCommandBuffer cmd, const char* name, const float rgba[4] = nullptr) const;
	void EndDebugLabel(VkCommandBuffer cmd) const;
	void InsertDebugLabel(VkCommandBuffer cmd, const char* name, const float rgba[4] = nullptr) const;

	void BeginQueueLabel(VkQueue queue, const char* name, const float rgba[4] = nullptr) const;
	void EndQueueLabel(VkQueue queue) const;

	// -----------------------------
	// Command buffer/pool creation
	// -----------------------------

	VkCommandPool CreateCommandPool(QueueType qType);
	VkCommandBuffer CreateCommandBuffer(VkCommandPool commandPool) const;

	void CreateCommandBuffers(VkCommandPool commandPool, VkCommandBuffer* commands, uint32_t count) const;
	VkCommandBuffer CreateSecondaryCommand(VkCommandPool pool, VkCommandBufferInheritanceInfo& inheritance) const;
	void RecordCommand(
		std::function<void(VkCommandBuffer)>&& function,
		VkCommandBuffer commandBuffer,
		VkCommandBufferUsageFlags usageFlags);

	class DeferredCommands
	{
		friend class Device;
	public:
		std::vector<VkCommandBuffer> CollectGraphics()
		{
			std::scoped_lock lock(m_submitMutex);
			std::vector<VkCommandBuffer> collected = std::move(m_recordedGraphicsCmds);
			m_recordedGraphicsCmds.clear();
			return collected;
		}
		std::vector<VkCommandBuffer> CollectTransfer()
		{
			std::scoped_lock lock(m_submitMutex);
			std::vector<VkCommandBuffer> collected = std::move(m_recordedTransferCmds);
			m_recordedTransferCmds.clear();
			return collected;
		}
		std::vector<VkCommandBuffer> CollectCompute()
		{
			std::scoped_lock lock(m_submitMutex);
			std::vector<VkCommandBuffer> collected = std::move(m_recordedComputeCmds);
			m_recordedComputeCmds.clear();
			return collected;
		}

		void ClearAll()
		{
			m_recordedGraphicsCmds.clear();
			m_recordedTransferCmds.clear();
			m_recordedComputeCmds.clear();
		}
	private:
		std::mutex m_submitMutex;
		std::vector<VkCommandBuffer> m_recordedGraphicsCmds;
		std::vector<VkCommandBuffer> m_recordedTransferCmds;
		std::vector<VkCommandBuffer> m_recordedComputeCmds;
	};
	DeferredCommands DeferredCmds;

	void RecordDeferredCommand(
		std::function<void(VkCommandBuffer)>&& function,
		VkCommandPool cmdPool,
		QueueType qType);

	void SubmitDeferredCommands(QueueType qType);

	// Call after device setup
	void InitThreadCommandPool(uint32_t threadCount);

	VkCommandPool GetThreadCommandPool(uint32_t threadID, QueueType type)
	{
		return m_threadCmdPoolManager.GetPool(threadID, type);
	}

	SwapchainSupportDetails GetSwapchainSupportDetails() const;

	bool IsTransferQueueSupported() const noexcept { return m_transferQueue.IsValid(); }
	bool IsComputeQueueSupported() const noexcept { return m_computeQueue.IsValid(); }

	GraphicsQueue& GetGraphicsQueue() { return m_graphicsQueue; }
	const GraphicsQueue& GetGraphicsQueue() const { return m_graphicsQueue; }
	PresentQueue& GetPresentQueue() { return m_presentQueue; }
	const PresentQueue& GetPresentQueue() const { return m_presentQueue; }
	TransferQueue& GetTransferQueue() { return m_transferQueue; }
	const TransferQueue& GetTransferQueue() const { return m_transferQueue; }
	ComputeQueue& GetComputeQueue() { return m_computeQueue; }
	const ComputeQueue& GetComputeQueue() const { return m_computeQueue; }

private:
	DeviceContext              m_context;
	VkSurfaceKHR               m_surface = VK_NULL_HANDLE;
	std::string                m_pDeviceName;

	DeviceProperties m_props{};

	SwapchainSupportDetails    m_swapchainSupportDetails{};

	VkDebugUtilsMessengerEXT   m_debugMessenger = VK_NULL_HANDLE;

	std::vector<const char*>   m_extensions;

	// Thread pools
	class ThreadCommandPoolManager
	{
		friend class Device;
	private:
		VkCommandPool GetPool(uint32_t threadID, QueueType type) const noexcept
		{
			VkCommandPool selected = VK_NULL_HANDLE;
			switch (type)
			{
			case QueueType::Graphics:
				selected = m_perThreadPools[threadID].graphicsPool;
				break;
			case QueueType::Transfer:
				selected = m_perThreadPools[threadID].transferPool;
				break;
			case QueueType::Compute:
				selected = m_perThreadPools[threadID].computePool;
				break;
			}

			return selected;
		}
		void Init(Device& device, uint32_t threadCount);
		void Cleanup(Device& device);

		std::vector<ThreadCommandPool> m_perThreadPools;
	};

	ThreadCommandPoolManager m_threadCmdPoolManager;

	GraphicsQueue m_graphicsQueue;
	PresentQueue  m_presentQueue;
	TransferQueue m_transferQueue;
	ComputeQueue  m_computeQueue;

	void PushGraphics(VkCommandBuffer cmd)
	{
		std::scoped_lock lock(DeferredCmds.m_submitMutex);
		DeferredCmds.m_recordedGraphicsCmds.push_back(cmd);
	}
	void PushTransfer(VkCommandBuffer cmd)
	{
		std::scoped_lock lock(DeferredCmds.m_submitMutex);
		DeferredCmds.m_recordedTransferCmds.push_back(cmd);
	}
	void PushCompute(VkCommandBuffer cmd)
	{
		std::scoped_lock lock(DeferredCmds.m_submitMutex);
		DeferredCmds.m_recordedComputeCmds.push_back(cmd);
	}

	VkResult CreateDebugUtilsMessengerEXT(
		const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator);

	void DestroyDebugUtilsMessengerEXT(
		VkInstance instance,
		const VkAllocationCallbacks* pAllocator) const;

	void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

	void SetupDebugMessenger();
	bool CheckValidationLayerSupport();

	const std::vector<const char*> m_validationLayers
	{
		"VK_LAYER_KHRONOS_validation"
	};

	const std::vector<const char*> m_deviceExtensions
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_EXT_MESH_SHADER_EXTENSION_NAME,
		VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		//VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
		VK_EXT_DEVICE_FAULT_EXTENSION_NAME
	};

	std::vector<const char*> GetRequiredExtensions() const;

	const std::vector<const char*> m_optionalDeviceExtensions
	{
		VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,
		VK_AMD_BUFFER_MARKER_EXTENSION_NAME,
	};

	std::vector<const char*> m_enabledDeviceExtensions;

	std::vector<const char*> BuildEnabledExtensionList() const;

	MarkerBackend m_markerBackend = MarkerBackend::None;

	std::vector<std::string> m_markerNamesGraphics;
	std::vector<std::string> m_markerNamesCompute;

	VkBuffer           m_markerBuffer = VK_NULL_HANDLE;
	VkDeviceMemory     m_markerMemory = VK_NULL_HANDLE;
	volatile uint32_t* m_markerMapped = nullptr;
	uint32_t           m_markerFramesInFlight = 0;
	uint32_t           m_markerPassesPerBatch = 0;

	bool m_debugLabelsEnabled = true;

	uint32_t MarkerSlot(QueueType qType, uint32_t frameIndex, uint32_t passIndex, bool end) const noexcept;
	const char* MarkerName(QueueType qType, uint32_t value) const;
};

class ScopedDebugLabel
{
public:
	ScopedDebugLabel(const Device& device, VkCommandBuffer cmd, const char* name, const float rgba[4] = nullptr)
		: m_device(&device), m_cmd(cmd)
	{
		m_device->BeginDebugLabel(m_cmd, name, rgba);
	}

	ScopedDebugLabel(const ScopedDebugLabel&) = delete;
	ScopedDebugLabel& operator=(const ScopedDebugLabel&) = delete;

	~ScopedDebugLabel() { m_device->EndDebugLabel(m_cmd); }

private:
	const Device* m_device;
	VkCommandBuffer m_cmd;
};
