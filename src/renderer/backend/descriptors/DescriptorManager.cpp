#include "pch.h"

#include "../descriptors/DescriptorManager.h"

// Align up to 4 bytes
static constexpr uint32_t Align4(uint32_t x) noexcept { return (x + 3u) & ~3u; }
// Size of inline block in bytes (multiple of 4)
static constexpr uint32_t kDebugInlineBytes = Align4(sizeof(RD::RenderToggles));

void DescriptorManager::InitDescriptors(VkDevice device)
{
	ASSERT(device != VK_NULL_HANDLE);

	using enum Vulkan_DescriptorType;
	using enum Vulkan_ShaderStage;

	std::vector<PoolSizeRatio> poolSizes
	{
		{ SSBO,             1.0f },
		{ UNIFORM,          5.0f },
		{ ACCEL_STRUCT,     1.0f },
		{ COMBINED_SAMPLER, static_cast<float>(RD::MAX_SAMPLER_CUBE_IMAGES + RD::MAX_COMBINED_SAMPLERS_IMAGES)
							/ static_cast<float>(RD::MAX_FRAMES_IN_FLIGHT) },
		{ INLINE,           static_cast<float>(kDebugInlineBytes) },
	};
	InitSetPools(device, RD::MAX_FRAMES_IN_FLIGHT, poolSizes);

	// ===================
	// === UNIFIED SET ===
	// ===================
	// Unified descriptor set stores transient resources that is shared between frames

	ClearBinding();

	AddBinding(RD::ADDRESS_TABLE_BINDING, SSBO, ALL_STAGES);
	AddBinding(RD::GLOBAL_ATMOSPHERE_BINDING, UNIFORM, ALL_STAGES);
	AddBinding(RD::GLOBAL_BINDING_DEBUG_INLINE, INLINE, ALL_STAGES, kDebugInlineBytes);

	AddBinding(
		RD::GLOBAL_BINDING_SAMPLER_CUBE,
		COMBINED_SAMPLER,
		IMAGE_STAGES,
		RD::MAX_SAMPLER_CUBE_IMAGES
	);
	AddBinding(
		RD::GLOBAL_BINDING_COMBINED_SAMPLER,
		COMBINED_SAMPLER,
		IMAGE_STAGES,
		RD::MAX_COMBINED_SAMPLERS_IMAGES
	);

	VkDescriptorSetLayout unifiedLayout = CreateSetLayout(device);

	size_t globalSetID = static_cast<size_t>(RD::DescriptorSlot::Unified);
	m_descriptorDefs[globalSetID].descriptorSet = AllocateDescriptor(device, unifiedLayout, nullptr, RD::MAX_COMBINED_SAMPLERS_IMAGES, true);
	m_descriptorDefs[globalSetID].descriptorLayout = unifiedLayout;


	// =================
	// === FRAME SET ===
	// =================
	// Per frame descriptors for dynamic data

	ClearBinding();

	AddBinding(RD::ADDRESS_TABLE_BINDING, SSBO, ALL_STAGES);
	AddBinding(RD::FRAME_BINDING_SCENE, UNIFORM, ALL_STAGES);
	AddBinding(RD::FRAME_BINDING_CSM, UNIFORM, ALL_STAGES);
	AddBinding(RD::FRAME_BINDING_CLUSTERED, UNIFORM, ALL_STAGES);
	AddBinding(RD::FRAME_BINDING_VOLUMETRIC, UNIFORM, ALL_STAGES);
	AddBinding(RD::FRAME_BINDING_TLAS, ACCEL_STRUCT, ALL_STAGES);

	size_t frameSetID = static_cast<size_t>(RD::DescriptorSlot::Frame);
	m_descriptorDefs[frameSetID].descriptorLayout = CreateSetLayout(device);


	// =================
	// === PUSH SET ===
	// =================
	// Push descriptor bindings for dynamic updated images

	ClearBinding();

	// Readable inputs
	for (uint32_t i = RD::PUSH_BINDING_READ_1; i <= RD::PUSH_BINDING_READ_10; i++) {
		AddBinding(i, COMBINED_SAMPLER, IMAGE_STAGES);
	}

	// Writable outputs
	for (uint32_t i = RD::PUSH_BINDING_WRITE_1; i <= RD::PUSH_BINDING_WRITE_5; i++) {
		AddBinding(i, STORAGE, IMAGE_STAGES);
	}

	m_descriptorDefs[static_cast<size_t>(RD::DescriptorSlot::Push)].descriptorLayout = CreatePushSetLayout(device);
}

void DescriptorManager::CleanupDescriptors(VkDevice device)
{
	DestroyPools(device);

	for (auto& d : m_descriptorDefs)
	{
		if (d.descriptorLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(device, d.descriptorLayout, nullptr);
	}
}

VkDescriptorSet DescriptorManager::AllocateFrameDescriptorSet(VkDevice device)
{
	VkDescriptorSetLayout frameLayout =  m_descriptorDefs[static_cast<size_t>(RD::DescriptorSlot::Frame)].descriptorLayout;
	ASSERT(frameLayout != VK_NULL_HANDLE && "Create the frame layout first!");

	return AllocateDescriptor(device, frameLayout);
}

void DescriptorManager::BindGlobalSetGraphics(
	VkCommandBuffer cmd,
	const PipelineLayoutConst& globalLayout)
{
	const VkDescriptorSet set[1] { GetGlobalSet() };

	vkCmdBindDescriptorSets(cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		globalLayout.pipelineLayout, 0, 1, set, 0, nullptr);
}

void DescriptorManager::BindGlobalSetCompute(
	VkCommandBuffer cmd,
	const PipelineLayoutConst& globalLayout)
{
	const VkDescriptorSet set[1] { GetGlobalSet() };

	vkCmdBindDescriptorSets(cmd,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		globalLayout.pipelineLayout, 0, 1, set, 0, nullptr);
}

void DescriptorManager::BindDescriptorSetsGraphics(
	VkCommandBuffer cmd,
	VkDescriptorSet frameSet,
	const PipelineLayoutConst& globalLayout)
{
	const VkDescriptorSet sets[2] { GetGlobalSet(), frameSet };

	vkCmdBindDescriptorSets(cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		globalLayout.pipelineLayout, 0, 2, sets, 0, nullptr);
}

void DescriptorManager::BindDescriptorSetsCompute(
	VkCommandBuffer cmd,
	VkDescriptorSet frameSet,
	const PipelineLayoutConst& globalLayout)
{
	const VkDescriptorSet sets[2] { GetGlobalSet(), frameSet };

	vkCmdBindDescriptorSets(cmd,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		globalLayout.pipelineLayout, 0, 2, sets, 0, nullptr);
}

void DescriptorManager::InitSetPools(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	m_ratios.clear();

	for (auto& r : poolRatios) { m_ratios.push_back(r); }

	VkDescriptorPool newPool = CreateDescriptorPool(device, maxSets, poolRatios);

	m_setsPerPool = static_cast<uint32_t>(maxSets * 1.5);

	m_readyPools.push_back(newPool);
}

VkDescriptorPool DescriptorManager::GetPool(VkDevice device)
{
	VkDescriptorPool newPool;
	if (m_readyPools.size() != 0)
	{
		newPool = m_readyPools.back();
		m_readyPools.pop_back();
	}
	else
	{
		newPool = CreateDescriptorPool(device, m_setsPerPool, m_ratios);

		m_setsPerPool = static_cast<uint32_t>(m_setsPerPool * 1.5);
		if (m_setsPerPool > 4092)
		{
			m_setsPerPool = 4092;
		}
	}

	return newPool;
}

VkDescriptorPool DescriptorManager::CreateDescriptorPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (auto& ratio : poolRatios)
	{
		poolSizes.emplace_back(VkDescriptorPoolSize{
			.type = static_cast<VkDescriptorType>(ratio.type),
			.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount)
		});
	}

	const uint32_t inlineBytesTotal = kDebugInlineBytes * setCount;
	if (inlineBytesTotal)
	{
		poolSizes.emplace_back(VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
			.descriptorCount = inlineBytesTotal
		});
	}

	VkDescriptorPoolInlineUniformBlockCreateInfo inlineInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO,
		.pNext = nullptr,
		.maxInlineUniformBlockBindings = setCount // one inline binding per m_frameSet
	};

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = &inlineInfo,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
		VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = setCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};

	VkDescriptorPool descriptorPool;
	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

	return descriptorPool;
}

void DescriptorManager::ClearPools(VkDevice device)
{
	for (auto& p : m_readyPools)
	{
		vkResetDescriptorPool(device, p, 0);
	}
	for (auto& p : m_fullPools)
	{
		vkResetDescriptorPool(device, p, 0);
		m_readyPools.push_back(p);
	}

	m_fullPools.clear();
}

void DescriptorManager::DestroyPools(VkDevice device)
{
	for (auto& p : m_readyPools)
		vkDestroyDescriptorPool(device, p, nullptr);
	m_readyPools.clear();

	for (auto& p : m_fullPools)
		vkDestroyDescriptorPool(device, p, nullptr);
	m_fullPools.clear();
}

// ALWAYS ClearBinding() before a new layout is setup
void DescriptorManager::ClearBinding() { m_bindings.clear(); }
void DescriptorManager::AddBinding(uint32_t binding, Vulkan_DescriptorType type, Vulkan_ShaderStage stage, uint32_t count)
{
	m_bindings.emplace_back(VkDescriptorSetLayoutBinding{
		.binding = binding,
		.descriptorType = static_cast<VkDescriptorType>(type),
		.descriptorCount = count,
		.stageFlags = static_cast<VkShaderStageFlags>(stage)
	});
}

// Last binding is the largest size for variable binding count
VkDescriptorSetLayout DescriptorManager::CreateSetLayout(VkDevice device)
{
	std::sort(m_bindings.begin(), m_bindings.end(), [](auto& a, auto& b) {
		return a.binding < b.binding;
	});

	uint32_t highestBinding = 0;
	for (const auto& b : m_bindings)
		highestBinding = std::max(highestBinding, b.binding);

	std::vector<VkDescriptorBindingFlags> bindingFlags;
	for (const auto& binding : m_bindings)
	{
		VkDescriptorBindingFlags flags = 0;

		// Always allow updating while in use
		flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		if (binding.binding == highestBinding && binding.descriptorCount > 1)
			flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

		bindingFlags.push_back(flags);
	}

	ASSERT(highestBinding == m_bindings.back().binding && "Variable descriptor binding must be last");

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
		.pBindingFlags = bindingFlags.data()
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &bindingFlagsInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = static_cast<uint32_t>(m_bindings.size()),
		.pBindings = m_bindings.data()
	};

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &set));

	return set;
}

VkDescriptorSet DescriptorManager::AllocateDescriptor(
	VkDevice device,
	VkDescriptorSetLayout layout,
	void* pNext,
	uint32_t count,
	bool useVariableCount)
{
	VkDescriptorPool poolToUse = GetPool(device);

	void* finalPNext = pNext;
	VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
	if (useVariableCount)
	{
		countInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
			.descriptorSetCount = 1,
			.pDescriptorCounts = &count
		};
		finalPNext = &countInfo;
	}

	VkDescriptorSetAllocateInfo allocInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = finalPNext,
		.descriptorPool = poolToUse,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout
	};

	VkDescriptorSet ds;
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
	{
		m_fullPools.push_back(poolToUse);
		poolToUse = GetPool(device);
		allocInfo.descriptorPool = poolToUse;
		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	}
	else
	{
		VK_CHECK(result);
	}

	m_readyPools.push_back(poolToUse);
	return ds;
}

// Push layout
VkDescriptorSetLayout DescriptorManager::CreatePushSetLayout(VkDevice device)
{
	std::sort(m_bindings.begin(), m_bindings.end(), [](auto& a, auto& b) { return a.binding < b.binding; });

	VkDescriptorSetLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
		.bindingCount = static_cast<uint32_t>(m_bindings.size()),
		.pBindings = m_bindings.data()
	};

	VkDescriptorSetLayout set{};
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
	return set;
}
