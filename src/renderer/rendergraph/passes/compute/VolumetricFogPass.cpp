#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace I = ImageUtils;

void RegisterVolumetricFogPass(RenderGraph& graph)
{
	graph.AddPass(
		"Volumetric_Fog",
		{ RP::FroxelInject, RP::FroxelReproject, RP::FroxelIntegrate },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->VolumetricFogActive();
					})

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::WorldProbeSpatialCache,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::VolumetricShadowMap,
					RD::ImageAccess::DepthRead)

				.InternalResource(
					RD::Renderer_RenderTarget::FroxelIntegrated,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.HistoryResource(FROXEL_SCATTER_RESOLVED_A, FROXEL_SCATTER_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::VolumetricFog,
							pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						pass.scope = ComputeScope{{ RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.profiler->froxelSettings);

						const auto froxelSlots =
							TemporalHistory::GetFroxelScatterSlots(ctx.frameState->GetTemporalIndex());

						const auto& historyRead = ctx.imageTable->GetRenderTarget(froxelSlots.read);
						const auto& historyWrite = ctx.imageTable->GetRenderTarget(froxelSlots.write);
						const auto& integrated = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::FroxelIntegrated);

						const auto& probeCache = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::WorldProbeSpatialCache);

						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						// ==============
						// Inject (raw)
						// ==============

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_10,
							probeCache,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							integrated);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::FroxelInject), pass.pushWriter);
						I::TransitionLayout(cmd, integrated, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ==========================
						// Temporal reprojection
						// ==========================

						I::TransitionLayout(cmd, historyWrite, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							integrated,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							historyRead,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							historyWrite);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::FroxelReproject), pass.pushWriter);
						I::TransitionLayout(cmd, historyWrite, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ==========================
						// Front-to-back integration
						// ==========================

						I::TransitionLayout(cmd, integrated, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							historyWrite,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							integrated);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::FroxelIntegrate), pass.pushWriter);
						I::TransitionLayout(cmd, integrated, RD::ImageAccess::Write, RD::ImageAccess::Read);
					});
		});
}