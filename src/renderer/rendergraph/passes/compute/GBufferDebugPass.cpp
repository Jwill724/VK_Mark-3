#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"

void RegisterGBufferDebugPass(RenderGraph& graph)
{
	graph.AddPass(
		"GBuffer_Debug",
		{ RP::GBufferDebug },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsShadedOverlayOn();
					})

				.ReadResource(RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::GBufferAlbedoRough,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::BentNormalAO,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = ComputeScope{{ graph.GetDisplayExtent() }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMat = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& vsNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& bentAo = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormalAO);
						const auto& visibility = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, visibility, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, albedoRough, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, normalMat, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, depthResolved, nearestClampSampler, UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, bentAo, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, contactShadows, nearestClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, tonemap);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							ctx.Pipe(RP::GBufferDebug),
							pass.pushWriter);
					});
		});
}
