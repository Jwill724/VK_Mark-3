#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../backend/BufferBarriers.h"
#include "../../../backend/ImageUtils.h"
#include "../../../frame/FrameResources.h"
#include "../../../scene/WorldProbeTypes.h"

namespace B = BufferBarriers;
namespace I = ImageUtils;

void RegisterWorldProbesUpdatePass(RenderGraph& graph)
{
	graph.AddPass(
		"World_Probes_Update",
		{
			RP::WorldProbesPrepare,
			RP::WorldProbesSkyTrace,
			RP::WorldProbesRelight,
			RP::WorldProbesInject,
			RP::WorldProbesRelocate,
			RP::WorldProbesSkyMean
		},
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()
				.ForceExecution()

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)
				.RequireResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::ComputeRead)
				.ReadResource(
					RD::Renderer_RenderTarget::GIDenoisePing,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::ComputeRead)
				.ReadResource(
					RD::Renderer_RenderTarget::BentAOUpsampled,
					RD::ImageAccess::ComputeRead)

				.WriteResource(RD::Renderer_RenderTarget::WorldProbeVisibility,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::WorldProbeSkyMean,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::WorldProbesUpdate,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;
						WorldProbePush push = *ctx.worldProbePush;

						// ============
						// Clear stage
						// ============

						pass.scope = ComputeScope{ { 64u, 1u }, WORKGROUP_64 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						const auto& schedule = ctx.bufferTable->GetGPUBuffer(RD::Renderer_Buffer::WorldProbeSchedule);
						B::ComputeStorageRW(cmd);
						push.update.x = 0u;
						pso.SetPush(push);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::WorldProbesPrepare), pass.pushWriter);

						// ===============
						// Classify stage
						// ===============

						B::ComputeStorageRW(cmd);
						push.update.x = 1u;
						pso.SetPush(push);
						pso.UpdateExtent({ RD::WORLD_PROBE_COUNT, 1u });
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::WorldProbesPrepare), pass.pushWriter);
						B::ComputeStorageRW(cmd);

						// ===============
						// Finalize stage
						// ===============

						push.update.x = 2u;
						pso.SetPush(push);
						pso.UpdateExtent({ 64u, 1u });
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::WorldProbesPrepare), pass.pushWriter);
						B::ComputeStorageRW(cmd);
						B::ComputeWriteToIndirectRead(cmd, schedule);

						auto dispatchQueue = [&](RP pipeline, uint32_t queue)
							{
								push.update.x = queue;
								pso.SetPush(push);
								pso.SetIndirect(schedule.m_buffer, GetWorldProbeIndirectOffset(queue));
								pso.DispatchComputePass(cmd, ctx.Pipe(pipeline), pass.pushWriter);
							};

						// ===============
						// Sky Trace
						// ===============

						const auto& wpVisAtlas = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::WorldProbeVisibility);
						// Rebind before both dispatches: PushDescriptorWriter starts a new batch after each push.
						for (uint32_t queue = 0u; queue < 2u; ++queue)
						{
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, wpVisAtlas);
							dispatchQueue(RP::WorldProbesSkyTrace, queue);
							B::ComputeStorageRW(cmd);
						}

						// ===============
						// Relight
						// ===============

						dispatchQueue(RP::WorldProbesRelight, WORLD_PROBE_RELIGHT_QUEUE);
						B::ComputeStorageRW(cmd);

						// ===============
						// Inject
						// ===============

						if (push.update.z != 0u)
						{
							const auto nearest = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
							for (uint32_t queue = 0u; queue < WORLD_PROBE_QUEUE_COUNT; ++queue)
							{
								dispatchQueue(RP::WorldProbesInject, queue);
							}
							B::ComputeStorageRW(cmd);
						}

						// ===============
						// Relocate
						// ===============

						for (uint32_t queue = 0u; queue < 2u; ++queue)
							dispatchQueue(RP::WorldProbesRelocate, queue);

						B::ComputeStorageRW(cmd);

						// ===============
						// Sky mean
						// ===============

						pso.ClearIndirect();

						const auto& wpSkyMean = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::WorldProbeSkyMean);
						pso.UpdateExtent({ wpSkyMean.Width(), wpSkyMean.Height() });
						pso.UpdateWorkgroups(WORKGROUP_8x8);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, wpSkyMean);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::WorldProbesSkyMean), pass.pushWriter);
					});
		});
}

void RegisterWorldProbesResolvePass(RenderGraph& graph)
{
	graph.AddPass("World_Probes_Resolve",
		{ RP::WorldProbesResolve },
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()
				.ForceExecution()

				.RequireResource(RD::Renderer_RenderTarget::WorldProbeVisibility, RD::ImageAccess::ComputeRead)

				.RequireResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::BentNormalAO,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::ComputeRead)

				.WriteResource(RD::Renderer_RenderTarget::WorldProbeLighting,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)

				.SetRecord([](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& s = ctx.profiler->worldProbeSettings;
						auto profile = ctx.profiler->ProfilePass(*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::WorldProbesResolve, pass.passName, ctx.threadSlot, ctx.scheduleInfo->queue);

						const auto& wpLightingAtlas = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::WorldProbeLighting);
						pass.scope = ComputeScope{ { wpLightingAtlas.Width(), wpLightingAtlas.Height() / 2u }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, wpLightingAtlas);
						pso.DispatchComputePass(ctx.commandBuffer, ctx.Pipe(RP::WorldProbesResolve), pass.pushWriter);
					});
		});
}

void RegisterWorldProbesCachePass(RenderGraph& graph)
{
	graph.AddPass("World_Probes_Cache",
		{ RP::WorldProbesCacheFallback, RP::WorldProbesCacheResolve },
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()
				.ForceExecution()

				.RequireResource(RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::Read)

				.RequireResource(RD::Renderer_RenderTarget::WorldProbeSkyMean,
					RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::WorldProbeVisibility,
					RD::ImageAccess::ComputeRead)
				.WriteResource(RD::Renderer_RenderTarget::WorldProbeSpatialCache,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)
				.SetRecord([](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto scope = ctx.profiler->ProfilePass(*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::WorldProbesCache, pass.passName,
							ctx.threadSlot, ctx.scheduleInfo->queue);
						pass.scope = ComputeScope{ { 1u, 1u }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						const auto& settings = ctx.profiler->worldProbeSettings;
						WorldProbeCachePush push{};
						push.flags = (settings.cacheFog ? 1u : 0u)
							| (settings.cacheTransparency ? 2u : 0u);
						pso.SetPush(push);
						// 1 group; this shader's own local size is 1.

						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::WorldProbesCacheFallback), pass.pushWriter);

						BufferBarriers::ComputeStorageRW(ctx.commandBuffer);
						const auto& output = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::WorldProbeSpatialCache);

						// Physical image = (gridX, gridY, gridZ * 13).
						// Keep this value identical to the allocation's logical Z resolution.
						constexpr uint32_t cacheGridZ = 32u;
						pso.UpdateExtent({ output.Width(), output.Height() * cacheGridZ });
						pso.SetPush(push);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, output);
						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::WorldProbesCacheResolve), pass.pushWriter);
					});
		});
}


void RegisterWorldProbesReconstructPass(RenderGraph& graph)
{
	graph.AddPass(
		"World_Probes_Reconstruct",
		{ RP::WorldProbesReconstruct },
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()
				.ForceExecution()

				.ReadResource(RD::Renderer_RenderTarget::WorldProbeLighting,
					RD::ImageAccess::ComputeRead)
				.RequireResource(RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::BentAOUpsampled,
					RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::WorldProbeVisibility,
					RD::ImageAccess::ComputeRead)
				.WriteResource(RD::Renderer_RenderTarget::WorldProbeReconstructed,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)
				.SetRecord([](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto scope = ctx.profiler->ProfilePass(*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::WorldProbesReconstruct, pass.passName,
							ctx.threadSlot, ctx.scheduleInfo->queue);
						const auto& output = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::WorldProbeReconstructed);
						pass.scope = ComputeScope{
							{ output.Width(), output.Height() / 2u }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						const auto& settings = ctx.profiler->worldProbeSettings;
						WorldProbeReconstructPush push{};
						push.directFallback = settings.reconstructDirectFallback ? 1u : 0u;
						push.debugView = settings.reconstructDebugView ? 1u : 0u;
						pso.SetPush(push);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, output);
						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::WorldProbesReconstruct), pass.pushWriter);
					});
		});
}

void RegisterWorldProbesDebugPass(RenderGraph& graph)
{
	graph.AddPass("World_Probes_Debug",
		{ RP::WorldProbesDebug },
		[&](RenderPassBuilder& builder)
		{
			builder.
				SetExecutionCondition([](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->IsWorldProbesDebugOn();
					})

				.RequireResource(RD::Renderer_RenderTarget::DepthResolved, RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::WorldProbeVisibility, RD::ImageAccess::ComputeRead)
				.WriteResource(RD::Renderer_RenderTarget::HDRScene, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)
				.SetRecord([](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& s = ctx.profiler->worldProbeSettings;
						auto profile = ctx.profiler->ProfilePass(*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::WorldProbesDebug, pass.passName, ctx.threadSlot, ctx.scheduleInfo->queue);

						const auto& hdr = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						pass.scope = ComputeScope{ { hdr.Width(), hdr.Height() }, WORKGROUP_8x8 };

						auto& pso = std::get<ComputeScope>(pass.scope);
						WorldProbeDebugPush push{};

						push.params = glm::vec4(s.debugRadius, s.debugMaxDistance, s.debugIntensity, 0.0f);
						push.mode = glm::uvec4(s.debugMode, s.debugShowInactive ? 1u : 0u,
							ctx.worldProbePush->update.w, uint32_t(s.debugCascade + 1));

						pso.SetPush(push);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, hdr);
						pso.DispatchComputePass(ctx.commandBuffer, ctx.Pipe(RP::WorldProbesDebug), pass.pushWriter);
					});
		});
}
