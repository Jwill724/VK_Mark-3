#include "pch.h"

#include "../../RenderPasses.h"
#include "../../RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../scene/AtmosphereState.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

void RegisterAtmosphereLUTUpdatePass(RenderGraph& graph)
{
	graph.AddPass(
		"Atmosphere_LUT",
		{ RP::AtmosphereTransmittance },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[&](const RenderPassExecutionContext& ctx)
					{
						return ctx.atmosphereState->NeedsBuild();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[&](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::AtmosphereLUTUpdate,
							pass.passName);

						const auto& lut = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::AtmosphereTransmittance);

						pass.scope = ComputeScope{ { lut.Width(), lut.Height() }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.atmosphereState->Parameters());
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, lut);
						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::AtmosphereTransmittance), pass.pushWriter);

						// Recording is not submission; keep dirty until MarkSubmitted().
						ctx.atmosphereState->MarkRecorded();
					});
		});
}

void RegisterAtmosphereSkyViewPass(RenderGraph& graph)
{
	graph.AddPass(
		"Atmosphere_Sky_View",
		{ RP::AtmosphereSkyView },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return !ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[&](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::AtmosphereSkyView, pass.passName);

						const auto& target = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::AtmosphereSkyView);
						pass.scope = ComputeScope{ { target.Width(), target.Height() }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(MakeAtmosphereSkyPush(
							ctx.atmosphereState->Parameters(),
							ctx.profiler->atmosphereSkySettings));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, target);
						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::AtmosphereSkyView), pass.pushWriter);
					});
		});
}


void RegisterAtmosphereLightingPass(RenderGraph& graph)
{
	graph.AddPass(
		"Atmosphere_Lighting",
		{ RP::AtmosphereLighting },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return !ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::AtmosphereLighting,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::AtmosphereLighting,
							pass.passName);

						const auto& target = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::AtmosphereLighting);

						pass.scope = ComputeScope{
							{ target.Width(), target.Height() },
							WORKGROUP_8x8 };

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.ClearPush();

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							target);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							ctx.Pipe(RP::AtmosphereLighting),
							pass.pushWriter);
					});
		});
}

void RegisterAtmosphereSkyRenderPass(RenderGraph& graph)
{
	graph.AddPass(
		"Atmosphere_Sky",
		{ RP::AtmosphereSky },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::AsyncWindow)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return !ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereTransmittance,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::AtmosphereSkyView,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::AtmosphereHDR,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[&](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer,
							RD::Renderer_Pass::AtmosphereSky, pass.passName);

						const auto& target = ctx.imageTable->GetRenderTarget(
							RD::Renderer_RenderTarget::AtmosphereHDR);
						pass.scope = ComputeScope{ { target.Width(), target.Height() }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(MakeAtmosphereSkyPush(ctx.atmosphereState->Parameters(),
							ctx.profiler->atmosphereSkySettings));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, target);
						pso.DispatchComputePass(ctx.commandBuffer,
							ctx.Pipe(RP::AtmosphereSky), pass.pushWriter);
					});
		});
}
