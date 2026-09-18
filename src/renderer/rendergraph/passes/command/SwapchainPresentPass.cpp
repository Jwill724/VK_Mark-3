#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

void RegisterSwapchainPresentPass(RenderGraph& graph)
{
	graph.AddPass(
		"Swapchain_Present",
		{},
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Present)
				.ForceExecution()

				.ReadResource(
					RD::Renderer_RenderTarget::PostNonAAComposite,
					RD::ImageAccess::TransferSrc)

				.ReadResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::TransferSrc)

				.ReadResource(
					RD::Renderer_RenderTarget::SharpenedColor,
					RD::ImageAccess::TransferSrc)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& postNonAAComposite = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& sharpenedColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SharpenedColor);
						const auto& swapchain = ctx.swapchain;

						bool isNormalFrame =
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();

						const bool canUseSharpened =
							ctx.frameState->IsSharpeningOn() &&
							ctx.frameState->IsTemporalValid() &&
							isNormalFrame;

						AllocatedImage srcImage;
						if (canUseSharpened)
						{
							srcImage = sharpenedColor;
						}
						else if (ctx.frameState->IsChromaticAberrationOn() && isNormalFrame)
						{
							srcImage = postNonAAComposite;
						}
						else
						{
							srcImage = tonemap;
						}

						I::SwapchainPresentCopy(cmd, *swapchain, srcImage);
					});
		});
}
