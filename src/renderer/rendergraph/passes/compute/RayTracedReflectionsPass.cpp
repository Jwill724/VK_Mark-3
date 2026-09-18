#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../../backend/ImageUtils.h"
#include "../../../backend/nrd/NRDContext.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;
namespace I = ImageUtils;

void RegisterRTReflectionsPass(RenderGraph& graph)
{
	graph.AddPass(
		"RT_Reflections",
		{ RP::NRDPrepare, RP::ReflectClassify, RP::RTRayArgs, RP::RTReflectTrace },
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.NRDReflect != nullptr && ctx.NRDReflect->IsValid() &&
							ctx.frameState->IsNRDActive() &&
							ctx.frameState->RTReflectionsEnabled();
					})

				.RequireResource(RD::Renderer_RenderTarget::AtmosphereTransmittance, RD::ImageAccess::Read)
				.RequireResource(RD::Renderer_RenderTarget::AtmosphereSkyView, RD::ImageAccess::Read)
				.RequireResource(RD::Renderer_RenderTarget::AtmosphereLighting, RD::ImageAccess::Read)

				.ReadResource(RD::Renderer_RenderTarget::DepthResolved, RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::HiZ, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::GBufferAlbedoRough, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::GBufferNormalMaterial, RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Velocity, RD::ImageAccess::Read)

				.InternalResource(RD::Renderer_RenderTarget::ReflectRoughness,
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead)

				.WriteResource(RD::Renderer_RenderTarget::ReflectRadiance,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.WriteResource(RD::Renderer_RenderTarget::NRDMotion,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)
				.WriteResource(RD::Renderer_RenderTarget::NRDNormalRoughness,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)
				.WriteResource(RD::Renderer_RenderTarget::NRDViewZ,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::RTReflections,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;

						const auto& drawExtent = graph.GetRenderExtent();
						const Extents2D halfExtent = {
							(drawExtent.Width() + 1u) / 2u,
							(drawExtent.Height() + 1u) / 2u };

						pass.scope = ComputeScope{ {halfExtent}, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& indirectArgs = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& rtRayList = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::RTRayList);

						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMaterial = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);

						const auto& radiance = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ReflectRadiance);
						const auto& roughness = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ReflectRoughness);

						const auto depthPyramidSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);
						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						// =========
						// NRD Prep
						// =========

						pso.SetPush(ctx.profiler->nrdReflectPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, normalMaterial, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, albedoRough, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, velocity, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDMotion));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDNormalRoughness));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDViewZ));

						pso.DispatchComputePass(ctx.commandBuffer, ctx.Pipe(RP::NRDPrepare), pass.pushWriter);

						// =========
						// Classify
						// =========

						pso.SetPush(ctx.profiler->reflectPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, albedoRough, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, roughness);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, radiance);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::ReflectClassify), pass.pushWriter);

						B::ComputeWriteToRW(cmd, rtRayList);
						I::TransitionLayoutCompute(cmd, roughness,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// ==============
						// Indirect args
						// ==============
						pso.SetPush(RTArgsPush{
							RD::RT_RAY_SLOT_REFLECT,
							RD::INDIRECT_DISPATCH_SLOT_REFLECT_RAYS,
							64u,
							ctx.profiler->reflectPush.rayCapacity});

						pso.UpdateWorkgroups(WORKGROUP_1, true);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTRayArgs), pass.pushWriter);

						B::ComputeWriteToIndirectRead(cmd, indirectArgs);
						B::ComputeWriteToRead(cmd, rtRayList);

						pso.SetPush(ctx.profiler->reflectPush);

						// ==============
						// Ray intersect
						// ==============

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, roughness, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, normalMaterial, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, hiZ, depthPyramidSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, radiance);

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_REFLECT_RAYS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::RTReflectTrace), pass.pushWriter);
					});
		});
}
