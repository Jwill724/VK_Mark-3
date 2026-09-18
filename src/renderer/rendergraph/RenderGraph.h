#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphSchedule.h"

#include <string>
#include <vector>
#include <array>

class BindlessImageTable;
struct RenderPassExecutionContext;
struct PipelineHandle;
class PipelineManager;
class JobSystem;
class FrameContext;
class Device;

class GraphicsScope;
class ComputeScope;

class RenderGraph final
{
	friend class Renderer;
public:
	template<typename BuildFn>
	RenderPassDesc& AddPass(
		std::string name,
		std::vector<RD::Renderer_Pipeline> pipelineIDs,
		BuildFn&& buildFn)
	{
		RenderPassDesc& pass = CreatePass(
			std::move(name),
			std::move(pipelineIDs));

		RenderPassBuilder builder(pass);

		buildFn(builder);

		return pass;
	}

	void Sync(
		const RD::RenderStateInfo& frameState,
		const RenderPassExecutionContext& ctx);

	void RecordFrame(
		RenderPassExecutionContext& baseCtx,
		JobSystem& jobSystem,
		FrameContext& frameCtx,
		const RecordHooks& hooks);

	const CompiledSchedule& GetSchedule() const noexcept { return m_schedule; }

	// SubmitFrame uses these two to pick its submit path.
	bool     UsesAsyncCompute()    const noexcept { return m_schedule.bUsesAsyncCompute; }
	uint32_t GetGraphicsBatchCount() const noexcept { return m_schedule.graphicsBatchCount; }

	void SetAsyncComputeEnabled(bool enabled)
	{
		if (m_bAsyncComputeEnabled != enabled)
		{
			m_bAsyncComputeEnabled = enabled;
			m_bBatchesDirty = true;
		}
	}
	bool IsAsyncComputeEnabled() const noexcept { return m_bAsyncComputeEnabled; }

	bool HasDedicatedComputeQueue() const noexcept
	{
		return m_bHasDedicatedComputeQueue;
	}

	// Call after render targets are recreated. The tracker is persistent
	// across frames (transitioning FROM Undefined discards contents, which
	// would silently destroy history targets), so resize is the only time
	// it may be reset.
	void InvalidateTrackedLayouts();

	void NotifyLayout(RD::Renderer_RenderTarget target, RD::ImageAccess access)
	{
		m_trackedLayouts[static_cast<size_t>(target)] = access;
	}

	void SetRenderExtent(Extents2D extent) { m_renderExtent = extent; }
	void SetDisplayExtent(Extents2D extent) { m_displayExtent = extent; }
	const Extents2D& GetRenderExtent() const noexcept { return m_renderExtent; }
	const Extents2D& GetDisplayExtent() const noexcept { return m_displayExtent; }

	void SetDevice(const Device& device) noexcept { m_device = &device; }

	bool IsResourceActive(const RenderResourceUsage& res) const;

	bool FindImageUse(
		const SubmitBatch& batch,
		RD::Renderer_RenderTarget target,
		ImageUse& out) const;

private:
	RenderPassDesc& CreatePass(
		std::string name,
		std::vector<RD::Renderer_Pipeline> pipelineIDs);

	void Build(
		Extents2D renderExtent,
		Extents2D displayExtent,
		bool bHasDedicatedComputeQueue);

	void Shutdown();

	// --- compile stages (RenderGraphCompile.cpp) ---
	void EvaluateActivePasses(const RenderPassExecutionContext& ctx);
	void BuildBatches();
	void BakeBarriers();

	// Debug: C0 and G1 execute concurrently with nothing ordering them.
	// Asserts their declared target sets are disjoint.
	void ValidateConcurrentBatches() const;

	// --- execution (RenderGraphExecute.cpp) ---
	// Render targets are resolved through ctx.imageTable. The graph owns
	// no resource pointer and no device handle of its own — the arena
	// caches the VkDevice it needs.
	void RecordAsyncSecondaries(
		RenderPassExecutionContext& baseCtx,
		JobSystem& jobSystem,
		FrameContext& frameCtx,
		const RecordHooks& hooks);

	void AssembleGraphicsBatch(
		SubmitBatch& batch,
		VkCommandBuffer primary,
		RenderPassExecutionContext& baseCtx,
		const RecordHooks& hooks,
		const char* batchName,
		bool bFirstGraphicsBatch,
		bool bLastGraphicsBatch);

	void AssembleComputeBatch(
		SubmitBatch& batch,
		VkCommandBuffer primary,
		BindlessImageTable& imageTable,
		const RecordHooks& hooks,
		const char* batchName);

	void FlushBakedBarriers(
		VkCommandBuffer cmd,
		PassQueue queue,
		const std::vector<BakedImageBarrier>& barriers,
		BindlessImageTable& imageTable) const;

	// Persistent across frames
	std::array<RD::ImageAccess, RD::RENDER_TARGET_COUNT> m_trackedLayouts{};

	std::vector<RenderPassDesc> m_passes;

	CompiledSchedule m_schedule;

	uint64_t m_activeMask     = 0u;
	uint64_t m_prevActiveMask = ~0ull; // force first BuildBatches

	bool m_bAsyncComputeEnabled = true;
	bool m_bBatchesDirty        = true;

	// Set at Build() time: false when the device exposes no queue family
	// with COMPUTE that is distinct from the graphics family. Async then
	// degrades to the single-batch path, which is always correct.
	bool m_bHasDedicatedComputeQueue = false;

	RD::RenderStateInfo m_recentFrameState{};

	bool m_bGraphDirty = true;

	const Device* m_device = nullptr;

	Extents2D m_renderExtent;
	Extents2D m_displayExtent;
};
