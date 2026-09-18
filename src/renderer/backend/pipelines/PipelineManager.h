#pragma once

#include "PipelineTable.h"
#include <vector>
#include <array>
#include <string_view>

class PipelineBuilder;
class ShaderCache;

class PipelineManager final
{
public:
	void CreatePipelineLayout(
		VkDevice device,
		const std::vector<VkDescriptorSetLayout>& descriptorLayouts);

	void InitPipelines(VkDevice device, const ShaderCache& cache);

	// currentFrame = frame about to be recorded; caller guarantees
	// every frame <= currentFrame - framesInFlight is complete.
	void TickFrame(VkDevice device, uint32_t currentFrame, uint32_t framesInFlight);

	// Render thread. Swaps the live handle and retires the old object.
	void Adopt(RD::Renderer_Pipeline id, VkPipeline built);

	// Any thread. Returns VK_NULL_HANDLE on failure, never aborts.
	static VkPipeline BuildPipelineObject(
		const PipelineDef& def,
		const std::array<const std::vector<uint32_t>*, MAX_PIPELINE_STAGES>& blobs,
		VkPipelineLayout                                            layout,
		VkPipelineCache                                             cache,
		VkDevice                                                    device,
		std::string& outLog);

	void Shutdown(VkDevice device);

	const PipelineLayoutConst& GetGlobalLayout() const noexcept { return m_globalLayout; }

	const PipelineHandle& GetHandle(RD::Renderer_Pipeline id) const noexcept
	{
		return m_pipelines[static_cast<size_t>(id)].Handle();
	}

	uint32_t GetRetiredCount() const noexcept
	{
		return static_cast<uint32_t>(m_retiredPipelines.size());
	}

private:
	class Pipeline
	{
	public:
		void Init(RD::Renderer_Pipeline id) { m_id = id; }

		PipelineHandle& Handle() { return m_handle; }
		const PipelineHandle& Handle() const { return m_handle; }
		RD::Renderer_Pipeline ID()     const { return m_id; }

	private:
		RD::Renderer_Pipeline m_id = RD::Renderer_Pipeline::Count;
		PipelineHandle        m_handle{};
	};

	std::array<Pipeline, RD::PIPELINE_COUNT> m_pipelines;

	PipelineLayoutConst m_globalLayout;
	VkPipelineCache     m_vkPipelineCache = VK_NULL_HANDLE;

	uint32_t m_currentFrame = 0u;

	struct RetiredPipeline
	{
		VkPipeline pipeline;
		uint32_t   frameIndex;
	};

	std::vector<RetiredPipeline> m_retiredPipelines;

	static void ConfigureGraphics(PipelineBuilder& builder, const PipelineDef& def);

	void LoadVkPipelineCache(VkDevice device);
	void SaveVkPipelineCache(VkDevice device) const;
};