#include "pch.h"

#include "Device.h"

static bool s_crashMarkersActive = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	(void)messageType;
	(void)pUserData;

	if (messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		return VK_FALSE;

	if (s_crashMarkersActive &&
		pCallbackData->pMessageIdName &&
		strstr(pCallbackData->pMessageIdName, "SYNC-HAZARD") != nullptr &&
		pCallbackData->pMessage &&
		strstr(pCallbackData->pMessage, "CrashMarkerBuffer") != nullptr)
	{
		return VK_FALSE;
	}

	const char* severity =
		(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARN";

	fmt::println("[Vulkan {}] {} : {}",
		severity,
		pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "",
		pCallbackData->pMessage);

	for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
	{
		const auto& obj = pCallbackData->pObjects[i];
		fmt::println("    object[{}] type={} handle=0x{:x} name={}",
			i,
			static_cast<uint32_t>(obj.objectType),
			obj.objectHandle,
			obj.pObjectName ? obj.pObjectName : "<unnamed>");
	}

	for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i)
	{
		fmt::println("    label[{}] {}", i, pCallbackData->pCmdBufLabels[i].pLabelName);
	}

	return VK_FALSE;
}

void Device::Cleanup()
{
	m_graphicsQueue.CleanupFencePools();
	m_presentQueue.CleanupFencePools();
	m_transferQueue.CleanupFencePools();
	m_graphicsQueue.DestroyTimelineSemaphore();
	m_transferQueue.DestroyTimelineSemaphore();
	m_computeQueue.CleanupFencePools();
	m_computeQueue.DestroyTimelineSemaphore();

	CleanupCrashMarkers();

	m_threadCmdPoolManager.Cleanup(*this);

	vkDestroySurfaceKHR(m_context.instance, m_surface, nullptr);

	vkDestroyDevice(m_context.device, nullptr);

	if (Debugging.IsValidationLayerEnabled())
	{
		DestroyDebugUtilsMessengerEXT(m_context.instance, nullptr);
	}
	vkDestroyInstance(m_context.instance, nullptr);
}

void Device::CreateInstance()
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Mark-3";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 4, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_4;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto reqExtensions = GetRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(reqExtensions.size());
	createInfo.ppEnabledExtensionNames = reqExtensions.data();

	if (Debugging.IsValidationLayerEnabled())
	{
		if (CheckValidationLayerSupport())
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
			createInfo.ppEnabledLayerNames = m_validationLayers.data();

			VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
			PopulateDebugMessengerCreateInfo(debugCreateInfo);

			if (Debugging.IsGpuAssistedValidationEnabled())
			{
				fmt::println("////GPU ASSISTED VALIDATION LAYERS ON////");

				static VkValidationFeatureEnableEXT gpuEnableFeatures[]
				{
					VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
					VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT
				};

				static VkValidationFeaturesEXT gpuValidationFeatures{
					.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
					.pNext = &debugCreateInfo,
					.enabledValidationFeatureCount = 2,
					.pEnabledValidationFeatures = gpuEnableFeatures
				};

				createInfo.pNext = &gpuValidationFeatures;
			}
			else
			{
				fmt::println("////SYNCHRONIZATION VALIDATION ON////");

				static VkValidationFeatureEnableEXT syncEnableFeatures[]
				{
					VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
				};

				static VkValidationFeaturesEXT syncValidationFeatures{
					.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
					.pNext = &debugCreateInfo,
					.enabledValidationFeatureCount = 1,
					.pEnabledValidationFeatures = syncEnableFeatures
				};

				createInfo.pNext = &syncValidationFeatures;
			}
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
			fmt::println("Validation layers requested, but not available!");
		}
	}

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_context.instance));

	LoadInstanceExtensionFunctions(m_context.instance);

	SetupDebugMessenger();
}

// ------------------------
// Glfw specific functions
void Device::CreateSurface(GLFWwindow* windowHandle)
{
	VK_CHECK(glfwCreateWindowSurface(m_context.instance, windowHandle, nullptr, &m_surface));
}

std::vector<const char*> Device::GetRequiredExtensions() const
{
	uint32_t glfwExtensionsCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

	ASSERT(glfwExtensions && glfwExtensionsCount > 0 && "GLFW failed to return instance extensions");

	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionsCount);

	extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	if (Debugging.IsValidationLayerEnabled())
	{
		extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	return extensions;
}
// ------------------------


void Device::InitLogical(const PhysicalDeviceCandidate& candidate)
{
	ASSERT(candidate.pDevice != VK_NULL_HANDLE);

	m_pDeviceName = candidate.name;
	m_context.physicalDevice = candidate.pDevice;
	m_props = candidate.props;
	m_context.queueIndices = candidate.queueIndices;
	m_swapchainSupportDetails = candidate.swapchainSupport;

	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	props2.pNext = &asProps;
	vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);

	// -------------------------
	// Device queues assignment
	//--------------------------

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_context.physicalDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(
		m_context.physicalDevice,
		&queueFamilyCount,
		queueFamilyProps.data()
	);

	const auto qIndices = m_context.queueIndices;

	if (qIndices.graphicsFamily.has_value())
	{
		uint32_t gFamilyIndex = qIndices.graphicsFamily.value();
		uint32_t gTimestampBits = queueFamilyProps[gFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(gFamilyIndex);
		m_graphicsQueue.Setup(gFamilyIndex, gTimestampBits, QueueType::Graphics);
	}
	if (qIndices.presentFamily.has_value())
	{
		uint32_t pFamilyIndex = qIndices.presentFamily.value();
		uint32_t pTimestampBits = queueFamilyProps[pFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(pFamilyIndex);
		m_presentQueue.Setup(pFamilyIndex, pTimestampBits, QueueType::Present);
	}
	if (qIndices.transferFamily.has_value())
	{
		uint32_t tFamilyIndex = qIndices.transferFamily.value();
		uint32_t tTimestampBits = queueFamilyProps[tFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(tFamilyIndex);
		m_transferQueue.Setup(tFamilyIndex, tTimestampBits, QueueType::Transfer);
	}
	if (qIndices.computeFamily.has_value())
	{
		uint32_t cFamilyIndex = qIndices.computeFamily.value();
		uint32_t cTimestampBits = queueFamilyProps[cFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(cFamilyIndex);
		m_computeQueue.Setup(cFamilyIndex, cTimestampBits, QueueType::Compute);
	}

	const float queuePriority = 1.0f;
	for (auto queueFamily : uniqueQueueFamilies)
	{
		queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
			.queueCount = 1,
			.pQueuePriorities = &queuePriority
			});
	}

	// ----------------
	// Device features
	//-----------------

	VkPhysicalDeviceFeatures2 baseFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	baseFeatures.features.fillModeNonSolid = VK_TRUE;
	baseFeatures.features.samplerAnisotropy = VK_TRUE;
	baseFeatures.features.multiDrawIndirect = VK_TRUE;
	baseFeatures.features.shaderInt64 = VK_TRUE;
	//baseFeatures.features.tessellationShader                      = VK_TRUE;
	baseFeatures.features.geometryShader = VK_TRUE;
	baseFeatures.features.depthBiasClamp = VK_TRUE;
	baseFeatures.features.depthClamp = VK_TRUE;
	baseFeatures.features.drawIndirectFirstInstance = VK_TRUE;
	baseFeatures.features.imageCubeArray = VK_TRUE;
	baseFeatures.features.shaderStorageImageExtendedFormats = VK_TRUE;
	baseFeatures.features.robustBufferAccess = VK_TRUE;
	baseFeatures.features.shaderInt16 = VK_TRUE;
	baseFeatures.features.independentBlend = VK_TRUE;
	baseFeatures.features.fullDrawIndexUint32 = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features11.shaderDrawParameters = VK_TRUE;
	features11.variablePointers = VK_TRUE;
	features11.variablePointersStorageBuffer = VK_TRUE;
	//features11.multiview                                          = VK_TRUE;
	features11.storageBuffer16BitAccess = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;
	features12.timelineSemaphore = VK_TRUE;
	features12.scalarBlockLayout = VK_TRUE;
	features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
	features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingPartiallyBound = VK_TRUE;
	features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	features12.runtimeDescriptorArray = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
	features12.drawIndirectCount = VK_TRUE;
	features12.shaderFloat16 = VK_TRUE;
	features12.shaderInt8 = VK_TRUE;
	features12.storageBuffer8BitAccess = VK_TRUE;
	features12.shaderBufferInt64Atomics = VK_TRUE;
	features12.hostQueryReset = VK_TRUE;

	VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	features13.maintenance4 = VK_TRUE;
	features13.shaderDemoteToHelperInvocation = VK_TRUE;
	features13.subgroupSizeControl = VK_TRUE;
	features13.inlineUniformBlock = VK_TRUE;
	features13.descriptorBindingInlineUniformBlockUpdateAfterBind = VK_TRUE;

	VkPhysicalDeviceVulkan14Features features14{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
	features14.pushDescriptor = VK_TRUE;
	features14.maintenance5 = VK_TRUE;
	features14.maintenance6 = VK_TRUE;

	// =======================
	// Ray tracing extensions
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
	};
	accelerationStructureFeatures.accelerationStructure = VK_TRUE;
	accelerationStructureFeatures.accelerationStructureCaptureReplay = VK_TRUE;
	accelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;

	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR
	};
	rayQueryFeatures.rayQuery = VK_TRUE;

	VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR positionFetchFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR
	};
	positionFetchFeatures.rayTracingPositionFetch = VK_TRUE;

	// =======================
	// Mesh shaders extension
	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT
	};
	meshShaderFeatures.taskShader = VK_TRUE;
	meshShaderFeatures.meshShader = VK_TRUE;
	meshShaderFeatures.meshShaderQueries = VK_TRUE;
	//meshShaderFeatures.multiviewMeshShader                    = VK_TRUE;

	// =======================
	// Device fault reporting
	VkPhysicalDeviceFaultFeaturesEXT faultFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT
	};
	faultFeatures.deviceFault = VK_TRUE;

	features14.pNext = &accelerationStructureFeatures;
	accelerationStructureFeatures.pNext = &positionFetchFeatures;
	positionFetchFeatures.pNext = &rayQueryFeatures;
	rayQueryFeatures.pNext = &meshShaderFeatures;
	meshShaderFeatures.pNext = &faultFeatures;
	faultFeatures.pNext = nullptr;

	features13.pNext = &features14;
	features12.pNext = &features13;
	features11.pNext = &features12;

	baseFeatures.pNext = &features11;

	m_enabledDeviceExtensions = BuildEnabledExtensionList();

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &baseFeatures;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(m_enabledDeviceExtensions.size());
	createInfo.ppEnabledExtensionNames = m_enabledDeviceExtensions.data();
	createInfo.enabledLayerCount = Debugging.IsValidationLayerEnabled() ? static_cast<uint32_t>(m_validationLayers.size()) : 0;
	createInfo.ppEnabledLayerNames = Debugging.IsValidationLayerEnabled() ? m_validationLayers.data() : nullptr;

	VK_CHECK(vkCreateDevice(m_context.physicalDevice, &createInfo, nullptr, &m_context.device));

	LoadDeviceExtensionFunctions(m_context.device);

	ASSERT(!DidExtensionLoadFail() && "Extension entry point failed to load");

	SetObjectName(m_context.device, VK_OBJECT_TYPE_DEVICE, "MainDevice");

	// --------------------
	// Initialize VkQueues
	// --------------------
	if (qIndices.graphicsFamily.has_value())
	{
		m_graphicsQueue.GetDeviceQueue(m_context);
		m_graphicsQueue.InitTimelineSemaphore();
		SetObjectName(m_graphicsQueue.GetQueue(), VK_OBJECT_TYPE_QUEUE, "GraphicsQueue");
	}

	if (qIndices.presentFamily.has_value())
	{
		m_presentQueue.GetDeviceQueue(m_context);
		SetObjectName(m_presentQueue.GetQueue(), VK_OBJECT_TYPE_QUEUE, "PresentQueue");
	}

	if (qIndices.transferFamily.has_value())
	{
		m_transferQueue.GetDeviceQueue(m_context);
		m_transferQueue.InitTimelineSemaphore();
		SetObjectName(m_transferQueue.GetQueue(), VK_OBJECT_TYPE_QUEUE, "TransferQueue");
	}

	if (qIndices.computeFamily.has_value())
	{
		m_computeQueue.GetDeviceQueue(m_context);
		m_computeQueue.InitTimelineSemaphore();
		SetObjectName(m_computeQueue.GetQueue(), VK_OBJECT_TYPE_QUEUE, "ComputeQueue");
	}
}

void Device::InitThreadCommandPool(uint32_t threadCount)
{
	ASSERT(m_context.device != VK_NULL_HANDLE);
	// --------------------------------
	// Mulithreaded command pool setup
	// --------------------------------
	m_threadCmdPoolManager.Init(*this, threadCount);
}

VkCommandPool Device::CreateCommandPool(QueueType qType)
{
	uint32_t qFamilyIndex = UINT32_MAX;
	switch(qType)
	{
	case QueueType::Graphics:
		qFamilyIndex = m_graphicsQueue.GetFamilyIndex();
		break;
	case QueueType::Transfer:
		qFamilyIndex = m_transferQueue.GetFamilyIndex();
		break;
	case QueueType::Compute:
		qFamilyIndex = m_computeQueue.GetFamilyIndex();
		break;
	}
	ASSERT(qFamilyIndex != UINT32_MAX);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = qFamilyIndex;

	VkCommandPool commandPool;
	VK_CHECK(vkCreateCommandPool(m_context.device, &poolInfo, nullptr, &commandPool));

	return commandPool;
}

VkCommandBuffer Device::CreateCommandBuffer(VkCommandPool commandPool) const
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(m_context.device, &allocInfo, &commandBuffer));

	return commandBuffer;
}

void Device::CreateCommandBuffers(VkCommandPool commandPool, VkCommandBuffer* commands, uint32_t count) const
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = count;

	VK_CHECK(vkAllocateCommandBuffers(m_context.device, &allocInfo, commands));
}

VkCommandBuffer Device::CreateSecondaryCommand(VkCommandPool pool, VkCommandBufferInheritanceInfo& inheritance) const
{
	VkCommandBufferAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer secondaryCmd;
	vkAllocateCommandBuffers(m_context.device, &allocInfo, &secondaryCmd);

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &inheritance
	};

	vkBeginCommandBuffer(secondaryCmd, &beginInfo);

	return secondaryCmd;
}

void Device::RecordCommand(
	std::function<void(VkCommandBuffer)>&& function,
	VkCommandBuffer commandBuffer,
	VkCommandBufferUsageFlags usageFlags)
{
	ASSERT(commandBuffer != VK_NULL_HANDLE && "[RecordCommandBuffer] Null command buffer.");

	VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = usageFlags;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
	function(commandBuffer);
	VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void Device::RecordDeferredCommand(
	std::function<void(VkCommandBuffer)>&& function,
	VkCommandPool cmdPool,
	QueueType qType)
{
	VkCommandBuffer cmd = CreateCommandBuffer(cmdPool);
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	bool validQueueType = false;
	switch (qType) {
	case(QueueType::Graphics):
		PushGraphics(cmd);
		validQueueType = true;
		break;
	case(QueueType::Transfer):
		PushTransfer(cmd);
		validQueueType = true;
		break;
	case(QueueType::Compute):
		PushCompute(cmd);
		validQueueType = true;
		break;
	default:
		ASSERT(validQueueType);
	}
}

void Device::SubmitDeferredCommands(QueueType qType)
{
	bool validQueueType = false;
	std::vector<VkCommandBuffer> cmds;
	switch (qType)
	{
	case(QueueType::Graphics):
		cmds = DeferredCmds.CollectGraphics();
		m_graphicsQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	case(QueueType::Transfer):
		cmds = DeferredCmds.CollectTransfer();
		m_transferQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	case(QueueType::Compute):
		cmds = DeferredCmds.CollectCompute();
		m_computeQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	default:
		ASSERT(validQueueType);
	}
}

// Thread command pools
void Device::ThreadCommandPoolManager::Init(Device& device, uint32_t threadCount)
{
	m_perThreadPools.resize(threadCount);
	for (uint32_t i = 0; i < threadCount; ++i)
	{
		auto& pool = m_perThreadPools[i];
		pool.graphicsPool = device.CreateCommandPool(QueueType::Graphics);
		pool.transferPool = device.CreateCommandPool(QueueType::Transfer);
		pool.computePool = device.CreateCommandPool(QueueType::Compute);
	}
}
void Device::ThreadCommandPoolManager::Cleanup(Device& device)
{
	for (uint32_t i = 0; i < m_perThreadPools.size(); ++i)
	{
		auto& pool = m_perThreadPools[i];

		if (pool.graphicsPool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.graphicsPool, nullptr);
		}

		if (pool.transferPool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.transferPool, nullptr);
		}

		if (pool.computePool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.computePool, nullptr);
		}
	}
}

uint32_t Device::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	return 0;
}

std::vector<uint32_t> Device::FindSupportedSampleCounts() const
{
	std::vector<uint32_t> sampleCounts;

	VkSampleCountFlags counts = m_props.limits.framebufferColorSampleCounts & m_props.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { sampleCounts.push_back(8); }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { sampleCounts.push_back(4); }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { sampleCounts.push_back(2); }
	sampleCounts.push_back(1); // Always allow no MSAA

	return sampleCounts;
}


VkFormat Device::FindDepthFormat()
{
	return FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT },
		VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkFormat Device::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature)
{
	for (const auto& format : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & feature) == feature)
		{
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & feature) == feature)
		{
			return format;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

bool Device::HasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkResult Device::CreateDebugUtilsMessengerEXT(
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator)
{
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_context.instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr)
	{
		return func(m_context.instance, pCreateInfo, pAllocator, &m_debugMessenger);
	}
	else
	{
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void Device::DestroyDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkAllocationCallbacks* pAllocator) const
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr)
		func(instance, m_debugMessenger, pAllocator);
}

void Device::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
	createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallback;
}

void Device::SetupDebugMessenger()
{
	if (!Debugging.IsValidationLayerEnabled()) return;

	ASSERT(m_debugMessenger == VK_NULL_HANDLE && "debugMessenger should not be initialized yet");

	VkDebugUtilsMessengerCreateInfoEXT createInfo;
	PopulateDebugMessengerCreateInfo(createInfo);

	// Instance and debugMessenger are declared before this function is called
	CreateDebugUtilsMessengerEXT(&createInfo, nullptr);
}

bool Device::CheckValidationLayerSupport()
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : m_validationLayers)
	{
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound) return false;
	}

	return true;
}

SwapchainSupportDetails Device::GetSwapchainSupportDetails() const
{
	SwapchainSupportDetails details = m_swapchainSupportDetails;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		m_context.physicalDevice,
		m_surface,
		&details.capabilities);
	return details;
}

// =======================
// Device fault extension
// =======================
static const char* FaultAddressTypeName(VkDeviceFaultAddressTypeEXT type)
{
	switch (type)
	{
	case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:                        return "None";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:                return "Invalid read";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:               return "Invalid write";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:             return "Invalid execute";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "IP unknown";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "IP invalid";
	case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:   return "IP fault";
	default:                                                           return "Unknown";
	}
}

std::vector<const char*> Device::BuildEnabledExtensionList() const
{
	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(m_context.physicalDevice, nullptr, &count, nullptr);

	std::vector<VkExtensionProperties> available(count);
	vkEnumerateDeviceExtensionProperties(m_context.physicalDevice, nullptr, &count, available.data());

	std::vector<const char*> enabled = m_deviceExtensions;

	for (const char* opt : m_optionalDeviceExtensions)
	{
		for (const auto& ext : available)
		{
			if (strcmp(opt, ext.extensionName) == 0)
			{
				enabled.push_back(opt);
				fmt::println("[Device] optional extension enabled: {}", opt);
				break;
			}
		}
	}

	return enabled;
}

uint32_t Device::MarkerSlot(QueueType qType, uint32_t frameIndex, uint32_t passIndex, bool end) const noexcept
{
	const uint32_t queueSlot = (qType == QueueType::Compute) ? 1u : 0u;
	const uint32_t base = ((frameIndex * 2u + queueSlot) * m_markerPassesPerBatch + passIndex) * 2u;
	return base + (end ? 1u : 0u);
}

const char* Device::MarkerName(QueueType qType, uint32_t passIndex) const
{
	const auto& names =
		(qType == QueueType::Compute) ? m_markerNamesCompute : m_markerNamesGraphics;

	return (passIndex < names.size()) ? names[passIndex].c_str() : "<out of range>";
}

void Device::InitCrashMarkers(uint32_t framesInFlight, uint32_t maxPassesPerBatch)
{
	ASSERT(framesInFlight > 0u && "InitCrashMarkers called before frames-in-flight is known");
	ASSERT(maxPassesPerBatch > 0u);

	m_markerFramesInFlight = framesInFlight;
	m_markerPassesPerBatch = maxPassesPerBatch;

	m_markerNamesGraphics.resize(maxPassesPerBatch);
	m_markerNamesCompute.resize(maxPassesPerBatch);

	for (uint32_t i = 0; i < maxPassesPerBatch; ++i)
	{
		m_markerNamesGraphics[i] = fmt::format("G_pass_{:02}", i);
		m_markerNamesCompute[i] = fmt::format("C_pass_{:02}", i);
	}

	if (pfn_vkCmdSetCheckpointNV && pfn_vkGetQueueCheckpointData2NV)
	{
		m_markerBackend = MarkerBackend::NvCheckpoints;
		fmt::println("[Device] crash markers: NV checkpoints");
		return;
	}

	if (!pfn_vkCmdWriteBufferMarker2AMD)
	{
		m_markerBackend = MarkerBackend::None;
		fmt::println("[Device] crash markers: unavailable (debug labels only)");
		return;
	}

	const VkDeviceSize slotCount =
		static_cast<VkDeviceSize>(framesInFlight) * 2ull * maxPassesPerBatch * 2ull;
	const VkDeviceSize bytes = slotCount * sizeof(uint32_t);

	VkBufferCreateInfo bufInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = bytes,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VK_CHECK(vkCreateBuffer(m_context.device, &bufInfo, nullptr, &m_markerBuffer));

	VkMemoryRequirements memReq{};
	vkGetBufferMemoryRequirements(m_context.device, m_markerBuffer, &memReq);

	VkMemoryAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memReq.size,
		.memoryTypeIndex = FindMemoryType(
			memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	};

	VK_CHECK(vkAllocateMemory(m_context.device, &allocInfo, nullptr, &m_markerMemory));
	VK_CHECK(vkBindBufferMemory(m_context.device, m_markerBuffer, m_markerMemory, 0));

	void* mapped = nullptr;
	VK_CHECK(vkMapMemory(m_context.device, m_markerMemory, 0, bytes, 0, &mapped));

	m_markerMapped = static_cast<volatile uint32_t*>(mapped);
	std::memset(mapped, 0, static_cast<size_t>(bytes));

	SetObjectName(m_markerBuffer, VK_OBJECT_TYPE_BUFFER, "CrashMarkerBuffer");

	m_markerBackend = MarkerBackend::AmdBufferMarker;
	s_crashMarkersActive = true;

	fmt::println("[Device] crash markers: AMD buffer markers ({} slots, {} bytes)",
		slotCount, bytes);
}

void Device::CleanupCrashMarkers()
{
	if (m_markerMapped)
	{
		vkUnmapMemory(m_context.device, m_markerMemory);
		m_markerMapped = nullptr;
	}

	if (m_markerBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_context.device, m_markerBuffer, nullptr);
		m_markerBuffer = VK_NULL_HANDLE;
	}

	if (m_markerMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_context.device, m_markerMemory, nullptr);
		m_markerMemory = VK_NULL_HANDLE;
	}

	s_crashMarkersActive = false;
}

void Device::ResetCrashMarkers(uint32_t frameIndex) const
{
	if (m_markerBackend != MarkerBackend::AmdBufferMarker || !m_markerMapped) return;

	const uint32_t perQueue = m_markerPassesPerBatch * 2u;

	for (uint32_t q = 0; q < 2u; ++q)
	{
		const uint32_t base = (frameIndex * 2u + q) * perQueue;
		for (uint32_t i = 0; i < perQueue; ++i)
		{
			m_markerMapped[base + i] = 0u;
		}
	}
}

void Device::MarkPassBegin(
	VkCommandBuffer cmd, QueueType qType, uint32_t frameIndex, uint32_t passIndex) const
{
	switch (m_markerBackend)
	{
	case MarkerBackend::NvCheckpoints:
		pfn_vkCmdSetCheckpointNV(cmd, const_cast<char*>(MarkerName(qType, passIndex)));
		break;

	case MarkerBackend::AmdBufferMarker:
		if (passIndex >= m_markerPassesPerBatch) return;
		pfn_vkCmdWriteBufferMarker2AMD(
			cmd,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			m_markerBuffer,
			MarkerSlot(qType, frameIndex, passIndex, false) * sizeof(uint32_t),
			1u);
		break;

	default:
		break;
	}
}

void Device::MarkPassEnd(
	VkCommandBuffer cmd, QueueType qType, uint32_t frameIndex, uint32_t passIndex) const
{
	if (m_markerBackend != MarkerBackend::AmdBufferMarker) return;
	if (passIndex >= m_markerPassesPerBatch) return;

	pfn_vkCmdWriteBufferMarker2AMD(
		cmd,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		m_markerBuffer,
		MarkerSlot(qType, frameIndex, passIndex, true) * sizeof(uint32_t),
		1u);
}

void Device::SetObjectName(uint64_t handle, VkObjectType type, const char* name) const
{
	if (!pfn_vkSetDebugUtilsObjectNameEXT || handle == 0) return;

	VkDebugUtilsObjectNameInfoEXT info
	{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = type,
		.objectHandle = handle,
		.pObjectName = name
	};

	pfn_vkSetDebugUtilsObjectNameEXT(m_context.device, &info);
}

void Device::SetCheckpoint(VkCommandBuffer cmd, const char* marker) const
{
	if (!pfn_vkCmdSetCheckpointNV) return;
	pfn_vkCmdSetCheckpointNV(cmd, const_cast<char*>(marker));
}

void Device::ReportDeviceFault(const char* context) const
{
	if (!pfn_vkGetDeviceFaultInfoEXT)
	{
		fmt::println("[DeviceFault] ({}) vkGetDeviceFaultInfoEXT unavailable.", context);
		return;
	}

	VkDeviceFaultCountsEXT counts{ VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT };
	if (pfn_vkGetDeviceFaultInfoEXT(m_context.device, &counts, nullptr) != VK_SUCCESS)
	{
		fmt::println("[DeviceFault] ({}) Failed to query fault counts.", context);
		return;
	}

	std::vector<VkDeviceFaultAddressInfoEXT> addressInfos(counts.addressInfoCount);
	std::vector<VkDeviceFaultVendorInfoEXT>  vendorInfos(counts.vendorInfoCount);

	counts.vendorBinarySize = 0;

	VkDeviceFaultInfoEXT info{ VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT };
	info.pAddressInfos = addressInfos.empty() ? nullptr : addressInfos.data();
	info.pVendorInfos = vendorInfos.empty() ? nullptr : vendorInfos.data();

	const VkResult result = pfn_vkGetDeviceFaultInfoEXT(m_context.device, &counts, &info);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
	{
		fmt::println("[DeviceFault] ({}) Failed to query fault info: {}", context, vkResultToString(result));
		return;
	}

	fmt::println("---- FAULT ----");
	fmt::println("  {}", info.description);
	fmt::println("  addresses={} vendorEntries={}", counts.addressInfoCount, counts.vendorInfoCount);

	for (uint32_t i = 0; i < counts.addressInfoCount; ++i)
	{
		const VkDeviceFaultAddressInfoEXT& addr = addressInfos[i];
		const VkDeviceSize precision = addr.addressPrecision ? addr.addressPrecision : 1;
		const VkDeviceSize lower = addr.reportedAddress & ~(precision - 1);

		fmt::println("  [addr {}] {:<16} 0x{:016x}  page [0x{:016x} .. 0x{:016x}]",
			i,
			FaultAddressTypeName(addr.addressType),
			addr.reportedAddress,
			lower,
			lower + precision - 1);
	}

	for (uint32_t i = 0; i < counts.vendorInfoCount; ++i)
	{
		const VkDeviceFaultVendorInfoEXT& v = vendorInfos[i];
		fmt::println("  [vendor {}] {} code=0x{:x} data=0x{:x}",
			i, v.description, v.vendorFaultCode, v.vendorFaultData);
	}
}

void Device::ReportCheckpoints(const char* context) const
{
	(void)context;

	fmt::println("---- CHECKPOINTS ----");

	if (m_markerBackend == MarkerBackend::NvCheckpoints)
	{
		struct QueueEntry { const char* name; VkQueue handle; bool valid; };

		const QueueEntry targets[]
		{
			{ "Graphics", m_graphicsQueue.GetQueue(), m_graphicsQueue.IsValid() },
			{ "Compute",  m_computeQueue.GetQueue(),  m_computeQueue.IsValid()  },
			{ "Transfer", m_transferQueue.GetQueue(), m_transferQueue.IsValid() },
		};

		for (const auto& t : targets)
		{
			if (!t.valid || t.handle == VK_NULL_HANDLE) continue;

			uint32_t count = 0;
			pfn_vkGetQueueCheckpointData2NV(t.handle, &count, nullptr);
			if (count == 0) continue;

			std::vector<VkCheckpointData2NV> data(count,
				VkCheckpointData2NV{ VK_STRUCTURE_TYPE_CHECKPOINT_DATA_2_NV });

			pfn_vkGetQueueCheckpointData2NV(t.handle, &count, data.data());

			for (const auto& d : data)
			{
				fmt::println("  [{}] stage=0x{:x} marker={}",
					t.name,
					static_cast<uint64_t>(d.stage),
					d.pCheckpointMarker ? static_cast<const char*>(d.pCheckpointMarker) : "<null>");
			}
		}
		return;
	}

	if (m_markerBackend == MarkerBackend::AmdBufferMarker && m_markerMapped)
	{
		for (uint32_t f = 0; f < m_markerFramesInFlight; ++f)
		{
			for (uint32_t q = 0; q < 2u; ++q)
			{
				const QueueType qType = (q == 1u) ? QueueType::Compute : QueueType::Graphics;
				const char* qName = (q == 1u) ? "Compute " : "Graphics";

				uint32_t begun = 0u;
				uint32_t done = 0u;
				uint32_t inFlight = 0u;

				for (uint32_t p = 0; p < m_markerPassesPerBatch; ++p)
				{
					const uint32_t b = m_markerMapped[MarkerSlot(qType, f, p, false)];
					const uint32_t e = m_markerMapped[MarkerSlot(qType, f, p, true)];

					if (b == 0u) continue;
					++begun;

					if (e != 0u) { ++done; continue; }

					++inFlight;
					fmt::println("  frame[{}] {} {}   <-- IN FLIGHT",
						f, qName, MarkerName(qType, p));
				}

				if (begun == 0u) continue;

				if (inFlight == 0u)
				{
					fmt::println("  frame[{}] {} all {} passes completed", f, qName, done);
				}
				else
				{
					fmt::println("  frame[{}] {} {}/{} completed", f, qName, done, begun);
				}
			}
		}
		return;
	}

	fmt::println("  no marker backend available on this driver");
}

void Device::ReportMemoryBudget(const char* context) const
{
	(void)context;

	VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
	VkPhysicalDeviceMemoryProperties2 memProps{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
	memProps.pNext = &budget;

	vkGetPhysicalDeviceMemoryProperties2(m_context.physicalDevice, &memProps);

	fmt::println("---- MEMORY ----");

	for (uint32_t i = 0; i < memProps.memoryProperties.memoryHeapCount; ++i)
	{
		const bool deviceLocal =
			(memProps.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;

		fmt::println("  heap[{}] {} usage={} MB budget={} MB",
			i,
			deviceLocal ? "device" : "host  ",
			budget.heapUsage[i] / (1024ull * 1024ull),
			budget.heapBudget[i] / (1024ull * 1024ull));
	}
}

void Device::DumpDeviceState(const char* context) const
{
	fmt::println("======== DEVICE STATE DUMP: {} ========", context);
	ReportDeviceFault(context);
	ReportCheckpoints(context);
	ReportMemoryBudget(context);
	fmt::println("=======================================");
}

// =============
// Debug Labels
// =============

static void FillLabel(VkDebugUtilsLabelEXT& label, const char* name, const float rgba[4])
{
	label = VkDebugUtilsLabelEXT{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
	label.pLabelName = name;

	if (rgba)
		std::memcpy(label.color, rgba, sizeof(float) * 4);
	else
		label.color[3] = 1.0f;
}

void Device::BeginDebugLabel(VkCommandBuffer cmd, const char* name, const float rgba[4]) const
{
	if (!m_debugLabelsEnabled || !pfn_vkCmdBeginDebugUtilsLabelEXT) return;
	if (cmd == VK_NULL_HANDLE || name == nullptr) return;

	VkDebugUtilsLabelEXT label;
	FillLabel(label, name, rgba);
	vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
}

void Device::EndDebugLabel(VkCommandBuffer cmd) const
{
	if (!m_debugLabelsEnabled || !pfn_vkCmdEndDebugUtilsLabelEXT) return;
	if (cmd == VK_NULL_HANDLE) return;

	vkCmdEndDebugUtilsLabelEXT(cmd);
}

void Device::InsertDebugLabel(VkCommandBuffer cmd, const char* name, const float rgba[4]) const
{
	if (!m_debugLabelsEnabled || !pfn_vkCmdInsertDebugUtilsLabelEXT) return;
	if (cmd == VK_NULL_HANDLE || name == nullptr) return;

	VkDebugUtilsLabelEXT label;
	FillLabel(label, name, rgba);
	vkCmdInsertDebugUtilsLabelEXT(cmd, &label);
}

void Device::BeginQueueLabel(VkQueue queue, const char* name, const float rgba[4]) const
{
	if (!m_debugLabelsEnabled || !pfn_vkQueueBeginDebugUtilsLabelEXT) return;
	if (queue == VK_NULL_HANDLE || name == nullptr) return;

	VkDebugUtilsLabelEXT label;
	FillLabel(label, name, rgba);
	pfn_vkQueueBeginDebugUtilsLabelEXT(queue, &label);
}

void Device::EndQueueLabel(VkQueue queue) const
{
	if (!m_debugLabelsEnabled || !pfn_vkQueueEndDebugUtilsLabelEXT) return;
	if (queue == VK_NULL_HANDLE) return;

	pfn_vkQueueEndDebugUtilsLabelEXT(queue);
}