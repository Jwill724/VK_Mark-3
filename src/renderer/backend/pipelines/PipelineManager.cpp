#include "pch.h"

#include "PipelineManager.h"
#include "PipelineBuilder.h"
#include "../shaders/ShaderCache.h"
#include <fstream>
#include <filesystem>

using PM = PipelineManager;

static const char* kPipelineCachePath = "res/shaders/cache/pipeline.bin";

void PM::CreatePipelineLayout(
	VkDevice device,
	const std::vector<VkDescriptorSetLayout>& descriptorLayouts)
{
	ASSERT(descriptorLayouts.size() == static_cast<size_t>(RD::DescriptorSlot::Count));

	PushConstantDef pcDef{ 0, RD::MAX_PUSH_CONSTANT_SIZE,
		static_cast<VkShaderStageFlags>(Vulkan_ShaderStage::ALL_STAGES) };

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = pcDef.stageFlags;
	pushConstantRange.offset = pcDef.offset;
	pushConstantRange.size = pcDef.size;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	m_globalLayout.pipelineLayout = pipelineLayout;
	m_globalLayout.pushConstantDef = pcDef;
}

void PM::LoadVkPipelineCache(VkDevice device)
{
	std::vector<char> blob;

	if (std::ifstream f(kPipelineCachePath, std::ios::binary | std::ios::ate); f.is_open())
	{
		blob.resize(static_cast<size_t>(f.tellg()));
		f.seekg(0);
		f.read(blob.data(), static_cast<std::streamsize>(blob.size()));
	}

	VkPipelineCacheCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	info.initialDataSize = blob.size();
	info.pInitialData = blob.empty() ? nullptr : blob.data();

	if (vkCreatePipelineCache(device, &info, nullptr, &m_vkPipelineCache) != VK_SUCCESS)
		m_vkPipelineCache = VK_NULL_HANDLE;
}

void PM::SaveVkPipelineCache(VkDevice device) const
{
	if (m_vkPipelineCache == VK_NULL_HANDLE) return;

	size_t size = 0u;
	if (vkGetPipelineCacheData(device, m_vkPipelineCache, &size, nullptr) != VK_SUCCESS || size == 0u)
		return;

	std::vector<char> blob(size);
	if (vkGetPipelineCacheData(device, m_vkPipelineCache, &size, blob.data()) != VK_SUCCESS)
		return;

	std::error_code ec;
	std::filesystem::create_directories(
		std::filesystem::path(kPipelineCachePath).parent_path(), ec);

	std::ofstream f(kPipelineCachePath, std::ios::binary | std::ios::trunc);
	if (f.is_open()) f.write(blob.data(), static_cast<std::streamsize>(size));
}

void PM::ConfigureGraphics(PipelineBuilder& builder, const PipelineDef& def)
{
	builder.InputAssemblyConfig(def.topology);

	if (def.biasConstant != 0.0f || def.biasSlope != 0.0f)
		builder.DepthBiasConfig(def.biasConstant, def.biasSlope);

	builder.RasterizerConfig(def.polygon, def.cull, def.frontFace);
	builder.MultisamplingConfig();
	builder.AttachmentConfig(def.color, def.blend, def.colorCount, def.depth);
	builder.DepthStencilConfig(def.depthTest, def.depthWrite, def.depthCompare);
}

VkPipeline PM::BuildPipelineObject(
	const PipelineDef& def,
	const std::array<const std::vector<uint32_t>*, MAX_PIPELINE_STAGES>& blobs,
	VkPipelineLayout                                                     layout,
	VkPipelineCache                                                      cache,
	VkDevice                                                             device,
	std::string& outLog)
{
	std::array<VkShaderModule, MAX_PIPELINE_STAGES> modules{};
	std::vector<VkPipelineShaderStageCreateInfo> stages;
	stages.reserve(def.shaderCount);

	VkPipeline result = VK_NULL_HANDLE;
	bool ok = true;

	for (uint32_t s = 0; s < def.shaderCount && ok; ++s)
	{
		const std::vector<uint32_t>* blob = blobs[s];
		if (blob == nullptr || blob->empty())
		{
			outLog = fmt::format("missing spir-v for stage {}", s);
			ok = false;
			break;
		}

		VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		createInfo.codeSize = blob->size() * sizeof(uint32_t);
		createInfo.pCode = blob->data();

		if (vkCreateShaderModule(device, &createInfo, nullptr, &modules[s]) != VK_SUCCESS)
		{
			outLog = fmt::format("vkCreateShaderModule failed for stage {}", s);
			ok = false;
			break;
		}

		stages.push_back(VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = static_cast<VkShaderStageFlagBits>(def.shaders[s].stage),
			.module = modules[s],
			.pName = "main" });
	}

	if (ok)
	{
		PipelineBuilder builder;
		builder.InitCreateInfoStructs();
		builder.SetPipelineLayout(layout);

		if (def.IsGraphics()) ConfigureGraphics(builder, def);

		PipelineHandle scratch{};
		scratch.bindPoint = def.bindPoint;

		const VkResult vr = builder.CreatePipeline(scratch, stages, cache, device);
		if (vr != VK_SUCCESS)
			outLog = fmt::format("pipeline creation returned {}", vkResultToString(vr));
		else
			result = scratch.pipeline;
	}

	for (uint32_t s = 0; s < def.shaderCount; ++s)
		if (modules[s] != VK_NULL_HANDLE)
			vkDestroyShaderModule(device, modules[s], nullptr);

	return result;
}

void PM::InitPipelines(VkDevice device, const ShaderCache& cache)
{
	ASSERT(m_globalLayout.pipelineLayout != VK_NULL_HANDLE);
	ASSERT(device != VK_NULL_HANDLE);

	LoadVkPipelineCache(device);

	for (size_t i = 0; i < RD::PIPELINE_COUNT; ++i)
	{
		const auto id = static_cast<RD::Renderer_Pipeline>(i);
		const PipelineDef& def = PipelineTable::Get(id);

		Pipeline& pipeline = m_pipelines[i];
		pipeline.Init(id);

		PipelineHandle& handle = pipeline.Handle();
		handle.layout = m_globalLayout;
		handle.bindPoint = def.bindPoint;
		handle.debugName = def.DebugName();
		if (def.IsGraphics()) handle.topology = def.topology;

		std::array<const std::vector<uint32_t>*, MAX_PIPELINE_STAGES> blobs{};
		for (uint32_t s = 0; s < def.shaderCount; ++s)
			blobs[s] = &cache.GetSpirvUnlocked(def.shaders[s].path);

		std::string log;
		handle.pipeline = BuildPipelineObject(
			def, blobs, m_globalLayout.pipelineLayout, m_vkPipelineCache, device, log);

		if (handle.pipeline == VK_NULL_HANDLE)
			fmt::println(stderr, "[Pipeline] {}: {}", def.DebugName(), log);

		INVARIANT(handle.pipeline != VK_NULL_HANDLE);
	}

	SaveVkPipelineCache(device);
}

void PM::TickFrame(VkDevice device, uint32_t currentFrame, uint32_t framesInFlight)
{
	m_currentFrame = currentFrame;

	m_retiredPipelines.erase(
		std::remove_if(m_retiredPipelines.begin(), m_retiredPipelines.end(),
			[&](const RetiredPipeline& r)
			{
				if (r.frameIndex + framesInFlight <= currentFrame)
				{
					vkDestroyPipeline(device, r.pipeline, nullptr);
					return true;
				}
				return false;
			}),
		m_retiredPipelines.end());
}

void PM::Adopt(RD::Renderer_Pipeline id, VkPipeline built)
{
	if (built == VK_NULL_HANDLE) return;

	PipelineHandle& handle = m_pipelines[static_cast<size_t>(id)].Handle();

	if (handle.pipeline != VK_NULL_HANDLE)
		m_retiredPipelines.push_back({ handle.pipeline, m_currentFrame });

	handle.pipeline = built;
}

void PM::Shutdown(VkDevice device)
{
	SaveVkPipelineCache(device);

	if (m_vkPipelineCache != VK_NULL_HANDLE)
		vkDestroyPipelineCache(device, m_vkPipelineCache, nullptr);

	for (auto& pipeline : m_pipelines)
	{
		if (pipeline.Handle().pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, pipeline.Handle().pipeline, nullptr);
			pipeline.Handle().pipeline = VK_NULL_HANDLE;
		}
	}

	for (auto& r : m_retiredPipelines)
		vkDestroyPipeline(device, r.pipeline, nullptr);
	m_retiredPipelines.clear();

	vkDestroyPipelineLayout(device, m_globalLayout.pipelineLayout, nullptr);
}