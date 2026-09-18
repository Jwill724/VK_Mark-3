#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

void RegisterMaterialResolvePass(RenderGraph& graph)
{
	graph.AddPass(
		"Material_Resolve",
		{ RP::MaterialResolve },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::GBufferAlbedoRough,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::MaterialResolve,
							pass.passName);

						const auto& drawExtent = graph.GetRenderExtent();
						pass.scope = ComputeScope{{ drawExtent }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMat = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& hdrScene = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						const auto& visibility = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							visibility,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							albedoRough);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_2,
							normalMat);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_3,
							hdrScene);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							ctx.Pipe(RP::MaterialResolve),
							pass.pushWriter);
					});
		});
}
