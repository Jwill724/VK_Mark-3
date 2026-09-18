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

void RegisterOpaqueLightingPass(RenderGraph& graph)
{
	graph.AddPass(
		"Opaque_Lighting",
		{ RP::OpaqueLighting },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.RequireResourceIf(
					RD::Renderer_RenderTarget::DirectionalCSMAtlas,
					RD::ImageAccess::DepthRead,
					[](const RD::RenderStateInfo& state)
					{
						return state.IsShadowsOn() && !state.RTShadowsEnabled();
					})

				.RequireResourceIf(
					RD::Renderer_RenderTarget::FlashlightShadowMap,
					RD::ImageAccess::DepthRead,
					[](const RD::RenderStateInfo& state)
					{
						return state.IsFlashlightOn();
					})

				.ReadResource(RD::Renderer_RenderTarget::WorldProbeReconstructed,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferAlbedoRough,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::BentNormalAO,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::RTReflectDenoised,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::RTShadowDenoised,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::IndirectSSGI,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::ComputeReadWrite,
					RD::ImageAccess::ComputeWrite)

				.WriteResource(
					RD::Renderer_RenderTarget::ShadingSignalHalf,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.HistoryResource(RADIANCE_RESOLVED_A, RADIANCE_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OpaqueLighting,
							pass.passName);

						const auto& drawExtent = graph.GetRenderExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						pso.SetPush(ctx.profiler->forwardPush);

						const uint32_t frameIndex = ctx.frameState->GetTemporalIndex();
						const auto diffSlots = TemporalHistory::GetDiffuseRadianceSlots(frameIndex);
						const auto& diffuseRadiance = ctx.imageTable->GetRenderTarget(diffSlots.write);
						const auto& hdrScene = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						const auto& shadingSignalHalf = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ShadingSignalHalf);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							hdrScene,
							UINT32_MAX,
							RD::ImageAccess::ComputeReadWrite);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, diffuseRadiance);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3, shadingSignalHalf);

						I::TransitionLayout(cmd, diffuseRadiance,
							RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::OpaqueLighting), pass.pushWriter);

						I::TransitionLayout(cmd, diffuseRadiance,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
					});
		});
}
