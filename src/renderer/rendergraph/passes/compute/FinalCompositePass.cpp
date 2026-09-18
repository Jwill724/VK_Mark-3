#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../scene/Scene.h"

void RegisterFinalCompositePass(RenderGraph& graph)
{
	graph.AddPass(
		"Final_Composite",
		{ RP::FinalComposite },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)
				.ForceExecution()

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Read)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.ReadResource(
					RD::Renderer_RenderTarget::FroxelIntegrated,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::BloomMipchain,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.ReadResource(
					RD::Renderer_RenderTarget::LensFlareColor,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)


				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FinalComposite,
							pass.passName);

						pass.scope = ComputeScope{{ graph.GetDisplayExtent() }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						ctx.profiler->compositePush.fogEnabled =
							ctx.frameState->VolumetricFogActive() ? 1u : 0u;

						pso.SetPush(ctx.profiler->compositePush);

						bool taaEnabled = (ctx.frameState->IsTaaOn() && ctx.frameState->IsTemporalValid() && !ctx.frameState->DebugRendering());

						const auto& hdrScene = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene)
							: ctx.imageTable->GetRenderTarget(TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex()).write);

						const auto& froxelIntegrated = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FroxelIntegrated);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& lensflare = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor);
						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							tonemap);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hdrScene,
							linearSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							depthResolved,
							linearSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							froxelIntegrated,
							linearClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_4,
							lensflare,
							linearClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_5,
							bloom,
							linearClampSampler,
							0);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							ctx.Pipe(RP::FinalComposite),
							pass.pushWriter);
					});
		});
}
