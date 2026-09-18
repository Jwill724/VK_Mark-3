#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

void RegisterSSGIPass(RenderGraph& graph)
{
	graph.AddPass(
		"SSGI",
		{ RP::HiZPrefilter,
			RP::VBGI,
			RP::GIAccumulate,
			RP::AODenoise,
			RP::GIDenoise,
			RP::BilateralUpsample,
			RP::AOAccumulate
		},
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.profiler->debugToggles.giMode != static_cast<uint32_t>(RD::GIMethod::OFF) &&
							!ctx.frameState->IsWireframeOn();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevViewNormals,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::Velocity,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::BentNormalAO,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::IndirectSSGI,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.HistoryResource(RADIANCE_RESOLVED_A, RADIANCE_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, false)

				.HistoryResource(GI_RESOLVED_A, GI_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.HistoryResource(AO_RESOLVED_A, AO_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.InternalResource(
					RD::Renderer_RenderTarget::BentAOUpsampled,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AOReconstruction,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AORaw,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AoEdgeInfo,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AOTemp,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::BentNormalAOHalf,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::GIDenoisePing,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::LinearizedHiZ,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead,
					0,
					VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::SSGI,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& drawExtent = graph.GetRenderExtent();
						pass.scope = ComputeScope{ drawExtent };
						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.SetPush(ctx.profiler->ssgiSettings);

						const bool bGIActive =
							ctx.profiler->debugToggles.giMode == static_cast<uint32_t>(RD::GIMethod::VBGI);

						const uint64_t frameIndex = static_cast<uint64_t>(ctx.frameState->GetTemporalIndex());

						const auto diffSlots = TemporalHistory::GetDiffuseRadianceSlots(frameIndex);
						const auto giSlots = TemporalHistory::GetGIHistorySlots(frameIndex);
						const auto aoSlots = TemporalHistory::GetAOHistorySlots(frameIndex);
						const auto& aoHistoryWrite = ctx.imageTable->GetRenderTarget(aoSlots.write);
						const auto& bentAoUpsampled = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentAOUpsampled);
						const auto& aoReconstruction = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AOReconstruction);
						const auto& linearizedHiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LinearizedHiZ);
						const auto& rawAO = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AORaw);
						const auto& aoTemp = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AOTemp);
						const auto& edgeInfo = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AoEdgeInfo);
						const auto& bentNormalAoHalf = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormalAOHalf);
						const auto& bentNormalAo = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormalAO);
						const auto& viewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& prevViewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevViewNormals);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& giDenoisePing = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GIDenoisePing);
						const auto& indirectSSGI = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::IndirectSSGI);
						const auto& prevRadiance = ctx.imageTable->GetRenderTarget(diffSlots.read);
						const auto& giHistoryRead = ctx.imageTable->GetRenderTarget(giSlots.read);
						const auto& giHistoryWrite = ctx.imageTable->GetRenderTarget(giSlots.write);

						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto depthPyramidSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);
						const auto aoSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearLodClamp);
						const auto nearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Nearest);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);


						// ======================
						// HiZ Prefilter
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						uint32_t pushWriteBinding = RD::PUSH_BINDING_WRITE_1;
						for (uint32_t i = 0u; i < RD::HI_Z_MIN_MIP_COUNT; i++)
						{
							ASSERT(pushWriteBinding <= RD::PUSH_BINDING_WRITE_5);

							pso.BindWriteImage(
								pass.pushWriter,
								pushWriteBinding,
								linearizedHiZ,
								i);
							pushWriteBinding++;
						}

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::HiZPrefilter), pass.pushWriter);

						I::TransitionLayoutCompute(cmd, linearizedHiZ,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead,
							0, VK_REMAINING_MIP_LEVELS);

						// ======================
						// VBGI trace
						// ======================

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, linearizedHiZ, depthPyramidSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, viewNormals, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, prevRadiance, aoSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, prevDepth, nearestClampSampler,
							UINT32_MAX, RD::ImageAccess::DepthRead);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, rawAO);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, edgeInfo);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3, bentNormalAoHalf);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_4, giDenoisePing);

						pso.UpdateWorkgroups({ WORKGROUP_32x32 });
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::VBGI), pass.pushWriter);

						I::TransitionLayoutCompute(cmd, rawAO, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						I::TransitionLayoutCompute(cmd, edgeInfo, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						I::TransitionLayoutCompute(cmd, giDenoisePing, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// =======================
						// GI temporal accumulate
						// =======================

						I::TransitionLayoutCompute(cmd, giHistoryWrite,
							RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, giDenoisePing, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, giHistoryRead, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, linearizedHiZ, depthPyramidSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, velocity, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, prevDepth, nearestClampSampler,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, viewNormals, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_7, prevViewNormals, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_8, rawAO, linearClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, giHistoryWrite);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, aoTemp);

						pso.UpdateWorkgroups({ WORKGROUP_32x32 });
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::GIAccumulate), pass.pushWriter);

						I::TransitionLayoutCompute(cmd, giHistoryWrite,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						I::TransitionLayoutCompute(cmd, rawAO, RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);
						I::TransitionLayoutCompute(cmd, aoTemp, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// ======================
						// AO Denoise Pass 1
						// ======================

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, aoTemp, aoSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, edgeInfo, nearSampler);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, rawAO);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, bentNormalAoHalf);

						pso.UpdateWorkgroups({ 64u, 32u, 1u });
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::AODenoise), pass.pushWriter);

						I::TransitionLayoutCompute(cmd, rawAO, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						I::TransitionLayoutCompute(cmd, aoTemp, RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);

						// ======================
						// AO Denoise Pass 2
						// ======================

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, rawAO, aoSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, edgeInfo, nearSampler);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, aoTemp);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, bentNormalAoHalf);

						pso.EditPush<SSGIPush>([](SSGIPush& push) {
							push.isFinalPass = 1u;
							});

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::AODenoise), pass.pushWriter);

						I::TransitionLayoutCompute(cmd, aoTemp, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						I::TransitionLayoutCompute(cmd, bentNormalAoHalf, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						if (bGIActive)
						{
							// ======================
							// GI Denoise
							// ======================

							I::TransitionLayoutCompute(cmd, giDenoisePing,
								RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);

							pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, giHistoryWrite, nearestClampSampler);
							pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, edgeInfo, nearSampler);
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, giDenoisePing);

							pso.UpdateWorkgroups({ WORKGROUP_32x32 });
							pso.DispatchComputePass(cmd, ctx.Pipe(RP::GIDenoise), pass.pushWriter);

							I::TransitionLayoutCompute(cmd, giDenoisePing,
								RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						}

						// ======================
						// Bilateral upsample
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							linearizedHiZ,
							depthPyramidSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							bentNormalAoHalf,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							viewNormals,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_4,
							giDenoisePing,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							bentAoUpsampled);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_2,
							indirectSSGI);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_3,
							aoReconstruction);

						pso.UpdateWorkgroups({ WORKGROUP_16x16 });

						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::BilateralUpsample),
							pass.pushWriter);

						I::TransitionLayoutCompute(
							cmd,
							bentAoUpsampled,
							RD::ImageAccess::ComputeWrite,
							RD::ImageAccess::ComputeRead);

						I::TransitionLayoutCompute(
							cmd,
							aoReconstruction,
							RD::ImageAccess::ComputeWrite,
							RD::ImageAccess::ComputeRead);

						// ================================
						// Full-resolution AO accumulation
						// ================================

						I::TransitionLayoutCompute(
							cmd,
							aoHistoryWrite,
							RD::ImageAccess::ComputeRead,
							RD::ImageAccess::ComputeWrite);

						pso.SetPush(ctx.profiler->ssgiSettings);

						// All reads are bindless. Only outputs are pushed.
						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							aoHistoryWrite);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_2,
							bentNormalAo);

						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::AOAccumulate),
							pass.pushWriter);

						I::TransitionLayoutCompute(
							cmd,
							aoHistoryWrite,
							RD::ImageAccess::ComputeWrite,
							RD::ImageAccess::ComputeRead);
					});
		});
}
