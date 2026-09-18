#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

void RegisterTransparentForwardPass(RenderGraph& graph)
{
	graph.AddPass(
		"Transparent_Forward",
		{ RP::TransparentForward },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->InstancesActive();
					})

				.RequireResourceIf(
					RD::Renderer_RenderTarget::DirectionalCSMAtlas,
					RD::ImageAccess::DepthRead,
					[](const RD::RenderStateInfo& state)
					{
						return state.IsShadowsOn() && !state.RTShadowsEnabled();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::WorldProbeSpatialCache,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::Read)

				.RequireResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::RTShadowDenoised,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentAccumulation,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentVelocityAccum,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::ComputeRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::TransparentForward,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;
						const auto taskDispatchBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& transparentAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation);
						const auto& transparentReveal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage);
						const auto& transparentVelocityAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityAccum);
						const auto& probeCache = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::WorldProbeSpatialCache);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						AttachmentDesc tAccumAttach{};
						tAccumAttach.imageView = transparentAccum.m_imageView;
						tAccumAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						tAccumAttach.SetColor({0.0f});

						AttachmentDesc tRevealAttach{};
						tRevealAttach.imageView = transparentReveal.m_imageView;
						tRevealAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						tRevealAttach.SetColor({ 1.0f, 0.0f, 0.0f, 0.0f });

						AttachmentDesc tVelocityAccumAttach{};
						tVelocityAccumAttach.imageView = transparentVelocityAccum.m_imageView;
						tVelocityAccumAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						tVelocityAccumAttach.SetColor({ 0.0f });

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
						depthAttach.SetDepth(0);

						pso.UpdateRenderInfo({ depthResolved.Width(), depthResolved.Height() }, // All images are the same size as renderer drawExtent
							{ tAccumAttach, tRevealAttach, tVelocityAccumAttach, depthAttach });

						pso.SetPush(ctx.profiler->forwardPush);

						pso.BeginRendering(cmd);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_10,
							probeCache,
							linearClampSampler);

						pso.DrawMeshTasksIndirectCount(
							cmd,
							RD::VIS_SLOT_TRANSPARENT,
							taskDispatchBuffer,
							indirectCountBuffer,
							ctx.Pipe(RP::TransparentForward),
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
