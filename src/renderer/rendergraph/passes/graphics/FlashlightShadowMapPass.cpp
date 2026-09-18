#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../scene/Scene.h"
#include "../../../scene/LightingSystem.h"

void RegisterFlashlightShadowMapPass(RenderGraph& graph)
{
	graph.AddPass(
		"Flashlight_Shadow",
		{ RP::ShadowMeshMaskedD32 },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::AsyncWindow)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsFlashlightOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::FlashlightShadowMap,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FlashlightShadow,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd  = ctx.commandBuffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;
						const auto taskDispatchBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;

						const auto& shadowmap =
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap);

						AttachmentDesc depth{};
						depth.imageView   = shadowmap.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1);

						pso.UpdateRenderInfo(
							{ shadowmap.Width(), shadowmap.Height() },
							{ depth });

						const glm::mat4& flashlightVP = ctx.scene->GetSceneData().flashlightVP;

						const glm::vec3 lightPos = LightingSystem::_mainFlashLight.position;

						DepthTaskPush push{};
						push.viewproj = flashlightVP;
						push.eye = glm::vec4(lightPos, 1.0f);
						push.slot = RD::VIS_SLOT_FLASHLIGHT;
						push.cullDistance = LightingSystem::_mainFlashLight.radius;
						pso.SetPush(push);

						pso.BeginRendering(cmd);

						pso.DrawMeshTasksIndirectCount(
							cmd,
							RD::VIS_SLOT_FLASHLIGHT,
							taskDispatchBuffer,
							indirectCountBuffer,
							ctx.Pipe(RP::ShadowMeshMaskedD32),
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
