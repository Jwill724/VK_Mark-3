#pragma once

#include "PipelineDefs.h"

class PipelineBuilder final
{
public:
	void InitCreateInfoStructs() noexcept
	{
		m_inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		m_rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		m_multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		m_depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		m_renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		m_colorFormats.clear();
		m_blendAttachments.clear();
	}

	void SetPipelineLayout(VkPipelineLayout layout) noexcept { m_pipelineLayout = layout; }

	void InputAssemblyConfig(VkPrimitiveTopology topology) noexcept
	{
		m_inputAssembly.topology = topology;
		m_inputAssembly.primitiveRestartEnable = VK_FALSE;
	}

	void DepthBiasConfig(float constantFactor, float slopeFactor) noexcept
	{
		m_rasterizer.depthBiasEnable = VK_TRUE;
		m_rasterizer.depthBiasConstantFactor = constantFactor;
		m_rasterizer.depthBiasSlopeFactor = slopeFactor;
		m_rasterizer.depthBiasClamp = 0.0f;
	}

	void RasterizerConfig(
		VkPolygonMode   mode,
		VkCullModeFlags cullMode,
		VkFrontFace     frontFace) noexcept
	{
		m_rasterizer.polygonMode = mode;
		m_rasterizer.lineWidth = 1.0f;
		m_rasterizer.cullMode = cullMode;
		m_rasterizer.frontFace = frontFace;
	}

	// No msaa support currently
	void MultisamplingConfig() noexcept
	{
		m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		m_multisampling.sampleShadingEnable = VK_FALSE;
		m_multisampling.minSampleShading = 1.0f;
		m_multisampling.pSampleMask = nullptr;
		m_multisampling.alphaToCoverageEnable = VK_FALSE;
		m_multisampling.alphaToOneEnable = VK_FALSE;
	}

	void AttachmentConfig(
		const std::array<Fmt, MAX_COLOR_ATTACH>& colorFormats,
		const std::array<BlendDef, MAX_COLOR_ATTACH>& blends,
		uint32_t                                      colorCount,
		Fmt                                           depthFormat)
	{
		m_colorFormats.clear();
		m_blendAttachments.clear();
		m_colorFormats.reserve(colorCount);
		m_blendAttachments.reserve(colorCount);

		for (uint32_t i = 0; i < colorCount; ++i)
		{
			ASSERT(colorFormats[i] != Fmt::Undefined);
			m_colorFormats.push_back(static_cast<VkFormat>(colorFormats[i]));
			m_blendAttachments.push_back(ToVk(blends[i]));
		}

		m_renderInfo.colorAttachmentCount = colorCount;
		m_renderInfo.pColorAttachmentFormats = colorCount > 0u ? m_colorFormats.data() : nullptr;
		m_renderInfo.depthAttachmentFormat = static_cast<VkFormat>(depthFormat);
	}

	void DepthStencilConfig(
		bool        depthTestEnabled,
		bool        depthWriteEnabled,
		VkCompareOp depthCompare) noexcept
	{
		m_depthStencil.depthTestEnable = depthTestEnabled;
		m_depthStencil.depthWriteEnable = depthWriteEnabled;
		m_depthStencil.depthCompareOp = depthCompare;
		m_depthStencil.depthBoundsTestEnable = VK_FALSE;
		m_depthStencil.stencilTestEnable = VK_FALSE;
		m_depthStencil.front = {};
		m_depthStencil.back = {};
		m_depthStencil.minDepthBounds = 0.0f;
		m_depthStencil.maxDepthBounds = 1.0f;
	}

	// Returns the raw result so runtime rebuilds can reject a bad shader
	// without aborting. Init path asserts on the return value instead.
	VkResult CreatePipeline(
		PipelineHandle& handle,
		const std::vector<VkPipelineShaderStageCreateInfo>& stages,
		VkPipelineCache                                     cache,
		VkDevice                                            device)
	{
		if (stages.empty()) return VK_ERROR_INITIALIZATION_FAILED;

		handle.pipeline = VK_NULL_HANDLE;

		if (handle.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
		{
			VkComputePipelineCreateInfo computeInfo{
				.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.pNext = nullptr,
				.stage = stages.back(),
				.layout = m_pipelineLayout,
			};

			return vkCreateComputePipelines(device, cache, 1, &computeInfo, nullptr, &handle.pipeline);
		}

		VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineColorBlendStateCreateInfo colorBlending{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
		colorBlending.attachmentCount = static_cast<uint32_t>(m_blendAttachments.size());
		colorBlending.pAttachments = m_blendAttachments.empty() ? nullptr : m_blendAttachments.data();

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

		VkDynamicState state[]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dynamicInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicInfo.pDynamicStates = &state[0];
		dynamicInfo.dynamicStateCount = 2;

		VkGraphicsPipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &m_renderInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &m_inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &m_rasterizer;
		pipelineInfo.pMultisampleState = &m_multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDepthStencilState = &m_depthStencil;
		pipelineInfo.pDynamicState = &dynamicInfo;
		pipelineInfo.layout = m_pipelineLayout;

		return vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &handle.pipeline);
	}

private:
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

	VkPipelineInputAssemblyStateCreateInfo m_inputAssembly{};
	VkPipelineRasterizationStateCreateInfo m_rasterizer{};
	VkPipelineMultisampleStateCreateInfo   m_multisampling{};
	VkPipelineDepthStencilStateCreateInfo  m_depthStencil{};
	VkPipelineRenderingCreateInfo          m_renderInfo{};

	std::vector<VkFormat>                            m_colorFormats;
	std::vector<VkPipelineColorBlendAttachmentState> m_blendAttachments;
};