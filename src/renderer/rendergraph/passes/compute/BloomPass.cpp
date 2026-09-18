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

static constexpr uint32_t BLOOM_FIRST_DOWNSAMPLE_BIT = 1u;

void RegisterBloomPass(RenderGraph& graph)
{
	graph.AddPass(
		"Bloom",
		{ RP::BloomDownsample, RP::BloomUpsample },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableBloom &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Read)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.InternalResource(
					RD::Renderer_RenderTarget::BloomMipchain,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Bloom,
							pass.passName);

						pass.scope = ComputeScope{ graph.GetDisplayExtent(), WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.profiler->bloomPush);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto colorSlots = TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex());

						const bool useResolved =
							ctx.frameState->TemporalActive() &&
							ctx.frameState->IsTemporalValid();

						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& sceneHDR = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						const auto& resolved = ctx.imageTable->GetRenderTarget(colorSlots.write);

						const auto& source = useResolved ? resolved : sceneHDR;

						const auto linearClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const uint32_t mips = bloom.m_mipLevels;

						Extents2D dstExtent = { bloom.Width(), bloom.Height() };

						for (uint32_t mip = 0; mip < mips; ++mip)
						{
							Extents2D srcExtent;
							if (mip == 0)
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, source, linearClamp);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp, UINT32_MAX, RD::ImageAccess::DepthRead);
								srcExtent = { source.Width(), source.Height() };
							}
							else
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, bloom, linearClamp, mip - 1);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp, UINT32_MAX, RD::ImageAccess::DepthRead);
								srcExtent = { std::max(1u, bloom.Width() >> (mip - 1)),
											  std::max(1u, bloom.Height() >> (mip - 1)) };
							}

							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, mip);

							const uint32_t flags = (mip == 0) ? BLOOM_FIRST_DOWNSAMPLE_BIT : 0u;

							pso.EditPush<BloomPush>([srcExtent, dstExtent, flags](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes = { dstExtent.Width(), dstExtent.Height() };
								push.flags = flags;
								});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, ctx.Pipe(RP::BloomDownsample), pass.pushWriter);

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, mip, 1);

							dstExtent.Width() = std::max(1u, dstExtent.Width() >> 1);
							dstExtent.Height() = std::max(1u, dstExtent.Height() >> 1);
						}

						for (int i = int(mips) - 2; i >= 0; --i)
						{
							Extents2D dstExtent = { std::max(1u, bloom.Width() >> uint32_t(i)),
													std::max(1u, bloom.Height() >> uint32_t(i)) };
							Extents2D srcExtent = { std::max(1u, bloom.Width() >> uint32_t(i + 1)),
													std::max(1u, bloom.Height() >> uint32_t(i + 1)) };

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Read, RD::ImageAccess::Write, i, 1);

							pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, bloom, linearClamp, i + 1);
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, i);

							pso.EditPush<BloomPush>([srcExtent, dstExtent](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes = { dstExtent.Width(), dstExtent.Height() };
								});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, ctx.Pipe(RP::BloomUpsample), pass.pushWriter);

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, i, 1);
						}
					});
		});
}