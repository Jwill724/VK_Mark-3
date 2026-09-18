#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/ImageUtils.h"

namespace I = ImageUtils;

void RegisterCASPass(RenderGraph& graph)
{
	graph.AddPass(
		"CAS",
		{ RP::CAS },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsSharpeningOn() &&
							ctx.frameState->IsTemporalValid() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::PostNonAAComposite,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::SharpenedColor,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::TransferSrc)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer, RD::Renderer_Pass::CAS, pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& postNonAAComposite = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite);
						const auto& sharpenedColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SharpenedColor);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const AllocatedImage& srcImage =
							ctx.frameState->IsChromaticAberrationOn() ? postNonAAComposite : tonemap;

						pass.scope = ComputeScope{ { graph.GetDisplayExtent() }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.SetPush(ctx.profiler->casSettings);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							srcImage,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							sharpenedColor);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::CAS), pass.pushWriter);
					});
		});
}