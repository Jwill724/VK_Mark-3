#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;

void RegisterClusteredLightsPass(RenderGraph& graph)
{
	graph.AddPass(
		"Clustered_Lights",
		{ RP::LightCull, RP::TransparentClusterBounds, RP::ClusterTileSliceRanges,
		RP::IndirectArgsLight, RP::ClusterCount, RP::ClusterScanOffsets, RP::ClusterScatterIDs},
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::ComputeRead,
					0u, VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ClusteredLights,
							pass.passName);

						pass.scope = ComputeScope{{ ctx.frameState->GetLightCount(), 1u }, { WORKGROUP_256 }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& indirectArgs        = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& tileSliceRanges     = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileSliceRanges);
						const auto& clusterCounts       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCounts);
						const auto& clusterCursors      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCursors);
						const auto& clusterScanScratch  = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterScanScratch);
						const auto& clusterOffsets      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterOffsets);
						const auto& volClusterCounts    = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VolClusterCounts);
						const auto& volClusterCursors   = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VolClusterCursors);

						const auto& lightCountBuf            = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightCount);
						const auto& visibleLightIdsBuf       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightIDs);
						const auto& transparentClusterBounds = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileTransparentNear);

						const auto& hiZ       = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						// Buffers reset to zero
						pso.FillGpuBuffer(cmd, clusterCursors);
						pso.FillGpuBuffer(cmd, clusterCounts);
						pso.FillGpuBuffer(cmd, volClusterCounts);
						pso.FillGpuBuffer(cmd, volClusterCursors);
						pso.FillGpuBuffer(cmd, clusterScanScratch);
						pso.FillGpuBuffer(cmd, transparentClusterBounds, RD::MAX_FLT_UINT);

						pso.FillGpuBuffer(cmd, lightCountBuf);
						B::CmdFillToComputeRW(cmd, lightCountBuf);

						// ==============
						// Light culling
						// ==============

						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::LightCull),
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, lightCountBuf);
						B::ComputeWriteToRead(cmd, visibleLightIdsBuf);


						// ===========================
						// Transparent Cluster Bounds
						// ===========================

						pso.UpdateExtent({ctx.frameState->GetInstanceCount(), 1});
						pso.UpdateWorkgroups(WORKGROUP_64);

						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::TransparentClusterBounds),
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, transparentClusterBounds);


						// ==========================
						// Cluster Tile slice ranges
						// ==========================
						Extents2D clusterExtent = { ctx.frameCtx->GetClusterData().tileCountX, ctx.frameCtx->GetClusterData().tileCountY };
						pso.UpdateExtent(clusterExtent);
						pso.UpdateWorkgroups(WORKGROUP_8x8);

						// Only needed for tile slice ranges
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiZ,
							hiZSampler);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::ClusterTileSliceRanges), pass.pushWriter);
						B::ComputeWriteToRead(cmd, tileSliceRanges);

						// =====================
						// Indirect arg compute
						// =====================

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						B::CmdFillToComputeRW(cmd, indirectArgs);
						pso.ClearIndirect();

						pso.UpdateWorkgroups(WORKGROUP_1, true);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::IndirectArgsLight), pass.pushWriter);
						B::ComputeWriteToIndirectRead(cmd, indirectArgs);

						// ===============
						// Cluster counts
						// ===============
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::ClusterCount), pass.pushWriter);
						B::ComputeWriteToRead(cmd, clusterCounts);

						// ================
						// Cluster offsets
						// ================
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_CLUSTERS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::ClusterScanOffsets), pass.pushWriter);
						B::ComputeWriteToRead(cmd, clusterOffsets);

						// ====================
						// Cluster scatter ids
						// ====================
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::ClusterScatterIDs), pass.pushWriter);

						B::ComputeWriteToRead(cmd, clusterScanScratch);
					});
		});
}
