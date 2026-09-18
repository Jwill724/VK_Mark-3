#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../Renderer.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../../scene/Scene.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

void RegisterLensFlarePass(RenderGraph& graph)
{
	graph.AddPass(
		"Lens_Flare",
		{ RP::FlareBright, RP::FlareGen },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableLensFlare &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Read)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.ReadResource(
					RD::Renderer_RenderTarget::FroxelIntegrated,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::LensFlareColor,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.InternalResource(
					RD::Renderer_RenderTarget::FlareBright,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::LensFlare,
							pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& volumetricFog = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FroxelIntegrated);
						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto& flareBright = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlareBright);
						const auto& lensflareColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						bool taaEnabled = (ctx.frameState->IsTaaOn() && ctx.frameState->IsTemporalValid());

						const auto& hdrScene = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene)
							: ctx.imageTable->GetRenderTarget(TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex()).write);

						pass.scope = ComputeScope{{ flareBright.Width(), flareBright.Height() }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						auto& lensFlarePush = ctx.profiler->lensFlareSettings;

						lensFlarePush.outputRes = { static_cast<float>(flareBright.Width()), static_cast<float>(flareBright.Height()) };
						lensFlarePush.invOutputRes = 1.0f / lensFlarePush.outputRes;

						const auto& sceneData = ctx.scene->GetSceneData();

						glm::vec3 sunDirWorld = glm::normalize(
							glm::vec3(sceneData.sunlightDirection));

						glm::vec3 sunDirView = glm::mat3(sceneData.view) * sunDirWorld;

						glm::vec4 clip = sceneData.projUnjittered
							* glm::vec4(sunDirView, 0.0f);

						lensFlarePush.sunUv = glm::vec2(0.5f);
						lensFlarePush.sunVisible = 0.0f;
						lensFlarePush.sunJitterScale = 0.0f;

						if (clip.w > 1e-6f)
						{
							glm::vec2 ndc = glm::vec2(clip) / clip.w;

							glm::vec2 uv{
								ndc.x * 0.5f + 0.5f,
								0.5f - ndc.y * 0.5f
							};

							constexpr float kEdgeMargin = 0.06f;

							glm::vec2 outside = glm::max(
								glm::vec2(0.0f),
								glm::max(-uv, uv - glm::vec2(1.0f)));

							float edgeFade = 1.0f - glm::clamp(
								glm::length(outside) / kEdgeMargin,
								0.0f, 1.0f);

							edgeFade = edgeFade * edgeFade * (3.0f - 2.0f * edgeFade);

							lensFlarePush.sunUv = uv;
							lensFlarePush.sunVisible = edgeFade;
						}

						lensFlarePush.froxelClips = ctx.profiler->compositePush.froxelClips;
						lensFlarePush.fogEnabled = ctx.frameState->VolumetricFogActive();

						pso.SetPush(lensFlarePush);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							flareBright);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hdrScene,
							linearClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							volumetricFog,
							linearClampSampler);

						// =============
						// Flare bright
						// =============
						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::FlareBright),
							pass.pushWriter);
						I::TransitionLayout(cmd, flareBright, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ==========
						// Flare gen
						// ==========
						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							lensflareColor);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiZ,
							hiZSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							flareBright,
							linearClampSampler);

						pso.DispatchComputePass(
							cmd,
							ctx.Pipe(RP::FlareGen),
							pass.pushWriter);
					});
		});
}
