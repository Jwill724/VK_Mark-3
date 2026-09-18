#include "pch.h"

#include "RenderGraph.h"
#include "RenderPasses.h"
#include "../backend/pipelines/PipelineManager.h"
#include "../backend/Device.h"
#include "RenderGraphSchedule.h"
#include "RenderGraphResources.h"
#include "../frame/FrameContext.h"
#include "../backend/ImageUtils.h"
#include "../backend/memory/BindlessImageTable.h"
#include "../backend/memory/ImageSpecs.h"
#include "../../core/JobSystem.h"
#include "../../common/EngineTypes.h"

#include <cstdio>

const PipelineHandle& RenderPassExecutionContext::Pipe(RD::Renderer_Pipeline id) const
{
	const PipelineHandle& handle = pipelines->GetHandle(id);
	return handle;
}

RenderPassDesc& RenderGraph::CreatePass(
	std::string name,
	std::vector<RD::Renderer_Pipeline> pipelineIDs)
{
	RenderPassDesc desc{};

	desc.passName = std::move(name);
	desc.declaredPipelines = std::move(pipelineIDs);

	m_passes.push_back(std::move(desc));

	m_bGraphDirty = true;

	return m_passes.back();
}

void RenderGraph::Build(
	Extents2D renderExtent,
	Extents2D displayExtent,
	bool bHasDedicatedComputeQueue)
{
	m_passes.clear();

	SetRenderExtent(renderExtent);
	SetDisplayExtent(displayExtent);

	m_bHasDedicatedComputeQueue = bHasDedicatedComputeQueue;

	// G0: visibility and surface inputs
	RegisterTemporalCopyPass(*this);
	RegisterShadowBoundsPass(*this);
	RegisterInstanceCullPass(*this);
	RegisterDrawBuildPass(*this);

	RegisterThePrepass(*this);
	RegisterHiZGenerationPass(*this);
	RegisterThePrepassLate(*this);
	RegisterHiZGenerationLatePass(*this);
	RegisterVelocityResolvePass(*this);
	RegisterMaterialResolvePass(*this);

	RegisterWireframePass(*this);

	// G0: lighting inputs shared by graphics and compute
	RegisterClusteredLightsPass(*this);

	RegisterAtmosphereLUTUpdatePass(*this);
	RegisterAtmosphereSkyViewPass(*this);
	RegisterAtmosphereLightingPass(*this);

	// G1: graphics work independent of C0 results
	RegisterDirectionalCSMPass(*this);
	RegisterFlashlightShadowMapPass(*this);
	RegisterVolumetricShadowMapPass(*this);

	RegisterAtmosphereSkyRenderPass(*this);

	RegisterContactShadowsPass(*this);

	// C0: async compute chain

	RegisterSSGIPass(*this);
	RegisterWorldProbesUpdatePass(*this);
	RegisterWorldProbesResolvePass(*this);
	RegisterWorldProbesCachePass(*this);
	RegisterWorldProbesReconstructPass(*this);

	RegisterTLASBuildPass(*this);
	RegisterRTShadowsPass(*this);
	RegisterRTReflectionsPass(*this);
	RegisterNRDDenoisePass(*this);

	// G2: consumers of completed graphics and compute work
	RegisterOpaqueLightingPass(*this);
	RegisterTransparentForwardPass(*this);
	RegisterVolumetricFogPass(*this);
	RegisterHDRSceneCompositePass(*this);

	RegisterWorldProbesDebugPass(*this);

	RegisterDebugDrawBuildPass(*this);
	RegisterLineDebugPass(*this);

	// G2: temporal and post processing
	RegisterTAAPass(*this);
	RegisterLuminanceExposurePass(*this);
	RegisterBloomPass(*this);
	RegisterLensFlarePass(*this);
	RegisterFinalCompositePass(*this);

	RegisterGBufferDebugPass(*this);
	RegisterChromaticAberrationPass(*this);
	RegisterCASPass(*this);

	RegisterSwapchainPresentPass(*this);
	RegisterImguiDrawPass(*this);

	m_bGraphDirty = false;
}

void RenderGraph::Shutdown()
{
	const Device* device = m_device;

	m_passes.clear();
	m_bGraphDirty = true;
	*this = RenderGraph{};

	m_device = device;
}

void RenderGraph::InvalidateTrackedLayouts()
{
	for (size_t i = 0; i < m_trackedLayouts.size(); ++i)
	{
		if (ImageSpecs::kRenderTargets[i].group == ImageSpecs::ImageGroup::Resolution)
		{
			m_trackedLayouts[i] = RD::ImageAccess::Undefined;
		}
	}

	for (auto& pass : m_passes)
	{
		pass.pushWriter.Clear();
	}
}

void RenderGraph::FlushBakedBarriers(
	VkCommandBuffer cmd,
	PassQueue queue,
	const std::vector<BakedImageBarrier>& barriers,
	BindlessImageTable& imageTable) const
{
	for (const BakedImageBarrier& b : barriers)
	{
		const AllocatedImage& img = imageTable.GetRenderTarget(b.target);

		if (queue == PassQueue::AsyncCompute)
		{
			// Filters stage/access down to the compute-legal subset:
			// GetImageSyncScope returns fragment-stage and attachment
			// bits that are illegal to submit on a compute queue.
			ImageUtils::TransitionLayoutCompute(
				cmd, img, b.oldAccess, b.newAccess, b.baseMip, b.mipCount);
		}
		else
		{
			ImageUtils::TransitionLayout(
				cmd, img, b.oldAccess, b.newAccess, b.baseMip, b.mipCount);
		}
	}
}

namespace
{
	constexpr float kLabelBatchGraphics[4] = { 0.30f, 0.55f, 0.85f, 1.0f };
	constexpr float kLabelBatchCompute[4] = { 0.90f, 0.60f, 0.20f, 1.0f };
	constexpr float kLabelBarrier[4] = { 0.55f, 0.55f, 0.60f, 1.0f };
	constexpr float kLabelSecondary[4] = { 0.60f, 0.80f, 0.45f, 1.0f };

	// Slot order must match BatchId. Graphics slots that fall outside the
	// table degrade to the generic name rather than reading out of bounds.
	const char* BatchLabelName(uint32_t slot)
	{
		static const char* kNames[] = { "G0 Visibility", "C0 AsyncCompute", "G1 AsyncWindow", "G2 Shading" };

		return (slot < std::size(kNames)) ? kNames[slot] : "Batch";
	}

	void FormatPassLabel(char(&out)[128], const char* passName, const char* suffix)
	{
		std::snprintf(out, sizeof(out), "%s : %s", passName, suffix);
	}

	void BeginSecondary(VkCommandBuffer cmd)
	{
		VkCommandBufferInheritanceInfo inherit{};
		inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin.pInheritanceInfo = &inherit;

		VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
	}

	void BeginPrimary(VkCommandBuffer cmd)
	{
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
	}
}

void RenderGraph::RecordFrame(
	RenderPassExecutionContext& baseCtx,
	JobSystem& jobSystem,
	FrameContext& frameCtx,
	const RecordHooks& hooks)
{
	ASSERT(m_schedule.bValid && "Sync() must run before RecordFrame()");
	ASSERT(baseCtx.imageTable != nullptr);
	ASSERT(m_device != nullptr && "SetDevice() must run before RecordFrame()");

	BindlessImageTable& imageTable = *baseCtx.imageTable;

	if (m_schedule.bUsesAsyncCompute)
		frameCtx.GetSecondaryArena().BeginFrame();

	RecordAsyncSecondaries(baseCtx, jobSystem, frameCtx, hooks);

	// Which graphics batch is last decides where onFrameEnd goes.
	uint32_t lastGraphicsBatchSlot = UINT32_MAX;
	for (uint32_t b = 0; b < MAX_SUBMIT_BATCHES; ++b)
	{
		const SubmitBatch& batch = m_schedule.batches[b];
		if (batch.bActive && batch.queue == PassQueue::Graphics)
			lastGraphicsBatchSlot = b;
	}
	ASSERT(lastGraphicsBatchSlot != UINT32_MAX && "Frame has no graphics work.");

	uint32_t graphicsPrimaryIdx = 0u;

	for (uint32_t b = 0; b < MAX_SUBMIT_BATCHES; ++b)
	{
		SubmitBatch& batch = m_schedule.batches[b];
		if (!batch.bActive) continue;

		if (batch.queue == PassQueue::AsyncCompute)
		{
			AssembleComputeBatch(
				batch,
				frameCtx.GetAsyncComputePrimary(),
				imageTable,
				hooks,
				BatchLabelName(b));
		}
		else
		{
			AssembleGraphicsBatch(
				batch,
				frameCtx.GetGraphicsPrimary(graphicsPrimaryIdx),
				baseCtx,
				hooks,
				BatchLabelName(b),
				graphicsPrimaryIdx == 0u,
				b == lastGraphicsBatchSlot);

			++graphicsPrimaryIdx;
		}
	}

	ASSERT(graphicsPrimaryIdx == m_schedule.graphicsBatchCount);
}

void RenderGraph::RecordAsyncSecondaries(
	RenderPassExecutionContext& baseCtx,
	JobSystem& jobSystem,
	FrameContext& frameCtx,
	const RecordHooks& hooks)
{
	if (m_schedule.asyncRecordList.empty()) return;

	SubmitBatch& c0 = m_schedule.Get(BatchId::C0);
	SecondaryCmdArena& arena = frameCtx.GetSecondaryArena();

	auto recordJob = [&](ThreadContext& threadCtx, uint32_t jobIndex)
		{
			const uint32_t slot = m_schedule.asyncRecordList[jobIndex];

			PassScheduleInfo& info = c0.passes[slot];
			RenderPassDesc& pass = m_passes[info.passIndex];

			VkCommandBuffer cmd = arena.Acquire(threadCtx.threadID);

			BeginSecondary(cmd);

			{
				char label[128];
				FormatPassLabel(label, pass.passName.c_str(), "record");

				ScopedDebugLabel secondaryLabel(*m_device, cmd, label, kLabelSecondary);

				if (hooks.bindPrologue)
					hooks.bindPrologue(cmd, PassQueue::AsyncCompute);

				RenderPassExecutionContext ctx = baseCtx;
				ctx.commandBuffer = cmd;
				ctx.scheduleInfo = &info;
				ctx.threadSlot = threadCtx.threadID;

				pass.record(ctx, pass);
			}

			VK_CHECK(vkEndCommandBuffer(cmd));

			info.recordedCmd = cmd;
		};

	jobSystem.RunParallel(
		static_cast<uint32_t>(m_schedule.asyncRecordList.size()),
		recordJob);
}

void RenderGraph::AssembleGraphicsBatch(
	SubmitBatch& batch,
	VkCommandBuffer primary,
	RenderPassExecutionContext& baseCtx,
	const RecordHooks& hooks,
	const char* batchName,
	bool bFirstGraphicsBatch,
	bool bLastGraphicsBatch)
{
	BindlessImageTable& imageTable = *baseCtx.imageTable;

	VK_CHECK(vkResetCommandBuffer(primary, 0));
	BeginPrimary(primary);

	{
		ScopedDebugLabel batchLabel(*m_device, primary, batchName, kLabelBatchGraphics);

		if (bFirstGraphicsBatch && hooks.onFrameBegin)
			hooks.onFrameBegin(primary);

		if (hooks.bindPrologue)
			hooks.bindPrologue(primary, PassQueue::Graphics);

		for (auto& info : batch.passes)
		{
			RenderPassDesc& pass = m_passes[info.passIndex];

			if (!info.enterBarriers.empty())
			{
				char label[128];
				FormatPassLabel(label, pass.passName.c_str(), "enter barriers");

				ScopedDebugLabel barrierLabel(*m_device, primary, label, kLabelBarrier);

				FlushBakedBarriers(
					primary, PassQueue::Graphics, info.enterBarriers, imageTable);
			}

			RenderPassExecutionContext ctx = baseCtx;
			ctx.commandBuffer = primary;
			ctx.scheduleInfo = &info;
			ctx.threadSlot = JobSystem::RENDER_THREAD;

			pass.pushWriter.Clear();

			pass.record(ctx, pass);

			if (!info.exitBarriers.empty())
			{
				char label[128];
				FormatPassLabel(label, pass.passName.c_str(), "exit barriers");

				ScopedDebugLabel barrierLabel(*m_device, primary, label, kLabelBarrier);

				FlushBakedBarriers(
					primary, PassQueue::Graphics, info.exitBarriers, imageTable);
			}
		}

		// Handoff transitions for the async batch waiting on this submit.
		// Emitted here, on the graphics queue, where the stage masks are
		// legal — the timeline signal makes them visible to the compute wait.
		if (!batch.tailBarriers.empty())
		{
			ScopedDebugLabel tailLabel(*m_device, primary, "Handoff barriers", kLabelBarrier);

			FlushBakedBarriers(
				primary, PassQueue::Graphics, batch.tailBarriers, imageTable);
		}

		if (bLastGraphicsBatch && hooks.onFrameEnd)
			hooks.onFrameEnd(primary);
	}

	VK_CHECK(vkEndCommandBuffer(primary));
}

void RenderGraph::AssembleComputeBatch(
	SubmitBatch& batch,
	VkCommandBuffer primary,
	BindlessImageTable& imageTable,
	const RecordHooks& hooks,
	const char* batchName)
{
	VK_CHECK(vkResetCommandBuffer(primary, 0));
	BeginPrimary(primary);

	{
		ScopedDebugLabel batchLabel(*m_device, primary, batchName, kLabelBatchCompute);

		for (auto& info : batch.passes)
		{
			ASSERT(info.recordedCmd != VK_NULL_HANDLE);

			RenderPassDesc& pass = m_passes[info.passIndex];

			if (!info.enterBarriers.empty())
			{
				char label[128];
				FormatPassLabel(label, pass.passName.c_str(), "enter barriers");

				ScopedDebugLabel barrierLabel(*m_device, primary, label, kLabelBarrier);

				FlushBakedBarriers(
					primary, PassQueue::AsyncCompute, info.enterBarriers, imageTable);
			}

			vkCmdExecuteCommands(primary, 1u, &info.recordedCmd);

			if (!info.exitBarriers.empty())
			{
				char label[128];
				FormatPassLabel(label, pass.passName.c_str(), "exit barriers");

				ScopedDebugLabel barrierLabel(*m_device, primary, label, kLabelBarrier);

				FlushBakedBarriers(
					primary, PassQueue::AsyncCompute, info.exitBarriers, imageTable);
			}

			info.recordedCmd = VK_NULL_HANDLE;
		}

		ASSERT(batch.tailBarriers.empty());

		if (hooks.onAsyncBatchEnd)
			hooks.onAsyncBatchEnd(primary);
	}

	VK_CHECK(vkEndCommandBuffer(primary));
}