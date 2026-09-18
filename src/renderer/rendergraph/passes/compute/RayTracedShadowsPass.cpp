#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/nrd/NRDContext.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/ImageUtils.h"
#include "../../../frame/FrameContext.h"
#include "../../../backend/BufferBarriers.h"

namespace B = BufferBarriers;
namespace I = ImageUtils;

void RegisterRTShadowsPass(RenderGraph& graph)
{
	graph.AddPass(
		"RT_Shadows",
		{ RP::NRDPrepare, RP::RTShadowVolumeBuild, RP::RTShadowInvalidMask,
		RP::RTShadowClassify, RP::RTRayArgs, RP::RTShadowTrace },
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.NRDShadow != nullptr && ctx.NRDShadow->IsValid() &&
							ctx.frameState->IsNRDActive() &&
							ctx.frameState->RTShadowsEnabled();
					})

				.ReadResource(RD::Renderer_RenderTarget::DepthResolved, RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::PrevDepthResolved, RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::GBufferAlbedoRough, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::GBufferNormalMaterial, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::ViewNormals, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Velocity, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::RTShadowDenoised, RD::ImageAccess::Read)

				.InternalResource(RD::Renderer_RenderTarget::ShadowInvalidMask, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)

				.WriteResource(RD::Renderer_RenderTarget::RTShadowPenumbra,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.WriteResource(RD::Renderer_RenderTarget::NRDShadowNormalRoughness,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)
				.WriteResource(RD::Renderer_RenderTarget::NRDShadowViewZ,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::RTShadows,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;

						pass.scope = ComputeScope{ graph.GetRenderExtent(), WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& indirectArgs = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& rtRayList = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::RTRayList);
						const auto& shadowVolumes = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ShadowInvalidVolumes);

						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& viewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMaterial = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& penumbra = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::RTShadowPenumbra);
						const auto& shadowPrev = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::RTShadowDenoised);
						const auto& invalidMask = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ShadowInvalidMask);

						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						const auto& drawExtent = graph.GetRenderExtent();

						const Extents2D eighthExtent = {
							(drawExtent.Width() + 7u) / 8u,
							(drawExtent.Height() + 7u) / 8u };

						// =========
						// NRD Prep
						// =========

						pso.SetPush(ctx.profiler->nrdShadowPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, normalMaterial, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, albedoRough, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, velocity, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1,
							ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::DummyVelocity));

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowNormalRoughness));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowViewZ));

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::NRDPrepare), pass.pushWriter);

						// =======================
						// Dynamic caster volumes
						// =======================

						pso.FillGpuBuffer(cmd, shadowVolumes, 0u, 0, RD::SHADOW_INVALID_VOLUME_HEADER_BYTES);
						B::CmdFillToComputeRW(cmd, shadowVolumes);

						pso.SetPush(ctx.frameState->GetInstanceCount());

						pso.UpdateExtent({ ctx.frameState->GetInstanceCount(), 1u });
						pso.UpdateWorkgroups(WORKGROUP_256);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTShadowVolumeBuild), pass.pushWriter);

						B::ComputeWriteToRead(cmd, shadowVolumes);

						// ====================
						// Invalidation mask
						// ====================

						pso.SetPush(ctx.profiler->rtShadowPush);

						pso.UpdateExtent(eighthExtent);
						pso.UpdateWorkgroups(WORKGROUP_8x8);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, invalidMask);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTShadowInvalidMask), pass.pushWriter);
						I::TransitionLayoutCompute(cmd, invalidMask, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// =============
						// Ray classify
						// =============

						pso.UpdateExtent(drawExtent);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, viewNormals, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, velocity, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, prevDepth, linearClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, shadowPrev, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, invalidMask, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, penumbra);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTShadowClassify), pass.pushWriter);

						B::ComputeWriteToRW(cmd, rtRayList);

						// ==============
						// Indirect args
						// ==============
						pso.SetPush(RTArgsPush{
							RD::RT_RAY_SLOT_SHADOW,
							RD::INDIRECT_DISPATCH_SLOT_SHADOW_RAYS,
							64u,
							ctx.profiler->rtShadowPush.rayCapacity });

						pso.UpdateWorkgroups(WORKGROUP_1, true);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTRayArgs), pass.pushWriter);

						B::ComputeWriteToIndirectRead(cmd, indirectArgs);
						B::ComputeWriteToRead(cmd, rtRayList);

						// ==============
						// Ray intersect
						// ==============

						pso.SetPush(ctx.profiler->rtShadowPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, viewNormals, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, penumbra);

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_SHADOW_RAYS_OFFSET_BYTES);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTShadowTrace), pass.pushWriter);
					});
		});
}