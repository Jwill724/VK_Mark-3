#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../scene/Scene.h"

void RegisterVolumetricShadowMapPass(RenderGraph& graph)
{
	graph.AddPass(
		"Volumetric_Shadow",
		{ RP::ShadowMeshMaskedD16 },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::AsyncWindow)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->VolumetricFogActive();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::VolumetricShadowMap,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::VolumetricShadowMap,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& volShadow = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricShadowMap);

						AttachmentDesc depth{};
						depth.imageView = volShadow.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1);

						pso.UpdateRenderInfo(
							{ volShadow.Width(), volShadow.Height() },
							{ depth });

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;
						const auto taskDispatchBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;

						pso.BeginRendering(cmd);

						DepthTaskPush push{};
						push.viewproj = ctx.scene->GetVolumetricShadowInfo().cascadeVP;
						push.eye      = glm::vec4(glm::vec3(ctx.scene->GetSceneData().sunlightDirection), 0.0f);
						push.slot     = RD::VIS_SLOT_VOLUMETRIC;

						pso.SetPush(push);

						pso.DrawMeshTasksIndirectCount(
							cmd,
							RD::VIS_SLOT_VOLUMETRIC,
							taskDispatchBuffer,
							indirectCountBuffer,
							ctx.Pipe(RP::ShadowMeshMaskedD16),
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
