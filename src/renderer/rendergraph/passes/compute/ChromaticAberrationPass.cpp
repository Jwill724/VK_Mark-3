#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

void RegisterChromaticAberrationPass(RenderGraph& graph)
{
	graph.AddPass(
		"Chromatic_Aberration",
		{ RP::ChromaticAberration },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsChromaticAberrationOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::PostNonAAComposite,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ChromaticAberration,
							pass.passName);

						pass.scope = ComputeScope{{ graph.GetDisplayExtent() }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.SetPush(ctx.profiler->caPush);

						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& postNonAA = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							postNonAA);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							ctx.Pipe(RP::ChromaticAberration),
							pass.pushWriter);
					});
		});
}
