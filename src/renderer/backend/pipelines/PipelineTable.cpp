#include "pch.h"

#include "PipelineTable.h"

using RP = RD::Renderer_Pipeline;
using SS = Vulkan_ShaderStage;

namespace
{
	std::array<PipelineDef, RD::PIPELINE_COUNT> g_defs{};

	struct Reg
	{
		PipelineDef* d;

		Reg& Stage(const char* path, SS stage)
		{
			for (uint32_t i = 0; i < d->shaderCount; ++i)
			{
				if (d->shaders[i].stage == stage)
				{
					d->shaders[i].path = path;
					return *this;
				}
			}
			ASSERT(d->shaderCount < MAX_PIPELINE_STAGES);
			d->shaders[d->shaderCount++] = { path, stage };
			return *this;
		}

		Reg& Task(const char* p) { return Stage(p, SS::TASK_STAGE); }
		Reg& Mesh(const char* p) { return Stage(p, SS::MESH_STAGE); }
		Reg& Vert(const char* p) { return Stage(p, SS::VERTEX_STAGE); }
		Reg& Frag(const char* p) { return Stage(p, SS::FRAGMENT_STAGE); }

		template<typename... F>
		Reg& Color(F... formats)
		{
			static_assert(sizeof...(F) > 0 && sizeof...(F) <= MAX_COLOR_ATTACH);
			const Fmt list[] = { formats... };
			d->colorCount = 0u;
			for (Fmt f : list) d->color[d->colorCount++] = f;
			return *this;
		}

		template<typename... B>
		Reg& Blend(B... blends)
		{
			static_assert(sizeof...(B) > 0 && sizeof...(B) <= MAX_COLOR_ATTACH);
			const BlendDef list[] = { blends... };
			uint32_t i = 0u;
			for (BlendDef b : list) d->blend[i++] = b;
			return *this;
		}

		Reg& DepthWrite(Fmt format, Cmp compare)
		{
			d->depth = format; d->depthCompare = ToVk(compare);
			d->depthTest = true; d->depthWrite = true;
			return *this;
		}

		Reg& DepthRead(Fmt format, Cmp compare)
		{
			d->depth = format; d->depthCompare = ToVk(compare);
			d->depthTest = true; d->depthWrite = false;
			return *this;
		}

		Reg& Cull(CullMode m) { d->cull = ToVk(m); return *this; }
		Reg& Front(VkFrontFace f) { d->frontFace = f;       return *this; }
		Reg& Topology(VkPrimitiveTopology t) { d->topology = t;       return *this; }
		Reg& Polygon(VkPolygonMode m) { d->polygon = m;       return *this; }

		Reg& DepthBias(float constantFactor, float slopeFactor)
		{
			d->biasConstant = constantFactor;
			d->biasSlope = slopeFactor;
			return *this;
		}

		Reg& Like(RP source)
		{
			const PipelineDef& src = g_defs[static_cast<size_t>(source)];
			ASSERT(src.IsDeclared() && "Like() source must be registered above this entry");
			*d = src;
			return *this;
		}
	};

	Reg Graphics(RP id)
	{
		PipelineDef& d = g_defs[static_cast<size_t>(id)];
		ASSERT(!d.IsDeclared() && "pipeline registered twice");
		d.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		return Reg{ &d };
	}

	void Compute(RP id, const char* path)
	{
		PipelineDef& d = g_defs[static_cast<size_t>(id)];
		ASSERT(!d.IsDeclared() && "pipeline registered twice");
		d.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		d.shaders[0] = { path, SS::COMPUTE_STAGE };
		d.shaderCount = 1u;
	}
}

void PipelineTable::Build()
{
	g_defs = {};

	// === Graphics ===

	Graphics(RP::PrepassMesh)
		.Task("visibility/meshlet_cull.spv")
		.Mesh("visibility/prepass_mesh.spv")
		.Frag("core/prepassFS.spv")
		.Color(Fmt::RG32U, Fmt::RG8unorm)
		.DepthWrite(Fmt::D32, Cmp::Greater)
		.Cull(CullMode::Back);

	Graphics(RP::PrepassMaskedMesh)
		.Like(RP::PrepassMesh)
		.Mesh("visibility/prepass_masked_mesh.spv")
		.Frag("core/prepassFS_masked.spv")
		.Cull(CullMode::None);

	Graphics(RP::ShadowMesh)
		.Task("shadows/shadowT.spv")
		.Mesh("shadows/shadowM.spv")
		.DepthWrite(Fmt::D32, Cmp::Less)
		.Cull(CullMode::None);

	Graphics(RP::ShadowMeshMaskedD32)
		.Like(RP::ShadowMesh)
		.Mesh("shadows/shadow_masked.spv")
		.Frag("shadows/shadow_maskedFS.spv");

	Graphics(RP::ShadowMeshMaskedD16)
		.Like(RP::ShadowMeshMaskedD32)
		.DepthWrite(Fmt::D16, Cmp::Less);

	Graphics(RP::WireframeMesh)
		.Task("visibility/meshlet_cull.spv")
		.Mesh("debug/wireframeM.spv")
		.Frag("debug/wireframeFS.spv")
		.Color(Fmt::RGBA16F)
		.DepthWrite(Fmt::D32, Cmp::Greater)
		.Cull(CullMode::Back)
		.Polygon(VK_POLYGON_MODE_LINE)
		.DepthBias(1.75f, 1.25f);

	Graphics(RP::TransparentForward)
		.Task("core/transparent_cull.spv")
		.Mesh("core/transparent_draw.spv")
		.Frag("core/transparent_forward.spv")
		.Color(Fmt::RGBA16F, Fmt::R16F, Fmt::RG16F)
		.Blend(Add(), InvSrc(Mask::R), Add(Mask::RG))
		.DepthRead(Fmt::D32, Cmp::Greater);

	Graphics(RP::LineDebug)
		.Vert("debug/line_debugVS.spv")
		.Frag("debug/line_debugFS.spv")
		.Color(Fmt::RGBA16F)
		.DepthRead(Fmt::D32, Cmp::Greater)
		.Topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
		.Polygon(VK_POLYGON_MODE_LINE);

	// === Compute ===

	Compute(RP::MaterialResolve, "core/material_resolve.spv");
	Compute(RP::VelocityResolve, "core/velocity_resolve.spv");
	Compute(RP::OpaqueLighting, "core/opaque_lighting.spv");
	Compute(RP::HDRSceneComposite, "core/hdr_scene_composite.spv");
	Compute(RP::HiZGen, "core/hi_z_gen.spv");
	Compute(RP::NRDPrepare, "core/nrd_prepare.spv");
	Compute(RP::TlasInstances, "core/tlas_instances.spv");
	Compute(RP::RTRayArgs, "core/rt_ray_args.spv");

	Compute(RP::InstanceCull, "visibility/instance_cull.spv");
	Compute(RP::DrawArgs, "visibility/draw_args.spv");
	Compute(RP::DrawScatter, "visibility/draw_scatter.spv");
	Compute(RP::DrawEmit, "visibility/draw_emit.spv");
	Compute(RP::DrawPlace, "visibility/draw_place.spv");

	Compute(RP::ShadowBounds, "shadows/shadow_bounds.spv");
	Compute(RP::ScreenSpaceContactShadows, "shadows/bend_sss.spv");
	Compute(RP::RTShadowVolumeBuild, "shadows/rt_shadow_volume_build.spv");
	Compute(RP::RTShadowInvalidMask, "shadows/rt_shadow_invalid_mask.spv");
	Compute(RP::RTShadowClassify, "shadows/rt_shadow_classify.spv");
	Compute(RP::RTShadowTrace, "shadows/rt_shadow_trace.spv");

	Compute(RP::ReflectClassify, "reflections/reflect_classify.spv");
	Compute(RP::RTReflectTrace, "reflections/rt_reflect_trace.spv");

	Compute(RP::HiZPrefilter, "ssgi/hi_z_prefilter.spv");
	Compute(RP::VBGI, "ssgi/vbgi_main.spv");
	Compute(RP::GIAccumulate, "ssgi/gi_accumulate.spv");
	Compute(RP::BilateralUpsample, "ssgi/bilateral_upsample.spv");
	Compute(RP::AODenoise, "ssgi/ao_denoise.spv");
	Compute(RP::GIDenoise, "ssgi/gi_denoise.spv");
	Compute(RP::AOAccumulate, "ssgi/ao_temporal.spv");

	Compute(RP::LightCull, "clustered/light_culling.spv");
	Compute(RP::TransparentClusterBounds, "clustered/transparent_cluster_bounds.spv");
	Compute(RP::ClusterTileSliceRanges, "clustered/cluster_tile_slice_ranges.spv");
	Compute(RP::IndirectArgsLight, "clustered/lights_indirect_args.spv");
	Compute(RP::ClusterCount, "clustered/cluster_count.spv");
	Compute(RP::ClusterScanOffsets, "clustered/cluster_scan_offsets.spv");
	Compute(RP::ClusterScatterIDs, "clustered/cluster_scatter_ids.spv");

	Compute(RP::FlareBright, "post_process/flare_bright.spv");
	Compute(RP::FlareGen, "post_process/flare_gen.spv");
	Compute(RP::BloomDownsample, "post_process/bloom_downsample.spv");
	Compute(RP::BloomUpsample, "post_process/bloom_upsample.spv");
	Compute(RP::ShadingSignalReduce, "post_process/shading_signal_reduce.spv");
	Compute(RP::TAA, "post_process/taa.spv");
	Compute(RP::CAS, "post_process/cas.spv");
	Compute(RP::ChromaticAberration, "post_process/chromatic_aberration.spv");
	Compute(RP::ExposureReduce, "post_process/exposure_reduce.spv");
	Compute(RP::ExposureFinalize, "post_process/exposure_finalize.spv");
	Compute(RP::FinalComposite, "post_process/final_composite.spv");

	Compute(RP::FroxelInject, "volumetric_fog/froxel_inject.spv");
	Compute(RP::FroxelReproject, "volumetric_fog/froxel_reproject.spv");
	Compute(RP::FroxelIntegrate, "volumetric_fog/froxel_integrate.spv");

	//Compute(RP::HDRToCubemap, "environment/hdr2cubemap.spv");
	//Compute(RP::SpecularPrefilter, "environment/specular_prefilter.spv");
	//Compute(RP::SHIrradiance, "environment/sh_irradiance.spv");
	Compute(RP::BRDFLUT, "environment/brdf_lut.spv");

	Compute(RP::WorldProbesRelocate, "world_probes/wp_relocate.spv");
	Compute(RP::WorldProbesRelight, "world_probes/wp_relight.spv");
	Compute(RP::WorldProbesPrepare, "world_probes/wp_prepare.spv");
	Compute(RP::WorldProbesSkyTrace, "world_probes/wp_sky_trace.spv");
	Compute(RP::WorldProbesInject, "world_probes/wp_inject.spv");
	Compute(RP::WorldProbesResolve, "world_probes/wp_resolve.spv");
	Compute(RP::WorldProbesReconstruct, "world_probes/wp_reconstruct.spv");
	Compute(RP::WorldProbesCacheResolve, "world_probes/wp_cache_resolve.spv");
	Compute(RP::WorldProbesCacheFallback, "world_probes/wp_cache_fallback.spv");
	Compute(RP::WorldProbesSkyMean, "world_probes/wp_sky_mean.spv");
	Compute(RP::WorldProbesDebug, "world_probes/wp_debug.spv");

	Compute(RP::DebugCount, "debug/debug_count.spv");
	Compute(RP::DebugArgs, "debug/debug_args.spv");
	Compute(RP::DebugBuild, "debug/debug_build.spv");
	Compute(RP::GBufferDebug, "debug/gbuffer_view.spv");

	Compute(RP::AtmosphereTransmittance, "sky/atmosphere_transmittance.spv");
	Compute(RP::AtmosphereLighting, "sky/atmosphere_lighting.spv");
	Compute(RP::AtmosphereSky, "sky/sky_render.spv");
	Compute(RP::AtmosphereSkyView, "sky/sky_view.spv");

	for (size_t i = 0; i < RD::PIPELINE_COUNT; ++i)
	{
		INVARIANT(g_defs[i].IsDeclared() && g_defs[i].shaderCount > 0u);
		INVARIANT(!g_defs[i].IsGraphics() ||
			g_defs[i].colorCount > 0u ||
			g_defs[i].depth != Fmt::Undefined);
	}
}

const PipelineDef& PipelineTable::Get(RP id)
{
	const size_t i = static_cast<size_t>(id);
	ASSERT(i < RD::PIPELINE_COUNT && g_defs[i].IsDeclared());
	return g_defs[i];
}