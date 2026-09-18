#include "pch.h"

#include "../../RenderPasses.h"
#include "../../RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../scene/AtmosphereTypes.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

void RegisterHDRSceneCompositePass(RenderGraph& graph)
{
	graph.AddPass(
		"HDR_Scene_Composite",
		{ RP::HDRSceneComposite },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return !ctx.frameState->DebugRenderFastPath();
					})

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentAccumulation,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereHDR,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::HDRSceneComposite, pass.passName);

						pass.scope = ComputeScope{ { graph.GetRenderExtent() } };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto nearest = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation), nearest);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage), nearest);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AtmosphereHDR), nearest);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved), nearest,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene));

						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::HDRSceneComposite), pass.pushWriter);
					});
		});
}
