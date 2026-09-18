#pragma once

#include <cstdint>

namespace RendererDefinitions
{
	// -------------
	// GPU Bindings
	// -------------
	// Descriptor info
	inline constexpr uint32_t GLOBAL_SET = 0u;
	inline constexpr uint32_t FRAME_SET  = 1u;
	inline constexpr uint32_t PUSH_SET   = 2u;

	// Shared between global and frame
	// ALL ssbos come from this table
	inline constexpr uint32_t ADDRESS_TABLE_BINDING           = 0u;

	inline constexpr uint32_t GLOBAL_ATMOSPHERE_BINDING       = 1u;

	// Global bindings
	inline constexpr uint32_t GLOBAL_BINDING_DEBUG_INLINE     = 2u;
	inline constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE     = 3u;
	inline constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 4u;

	// Frame bindings
	inline constexpr uint32_t FRAME_BINDING_SCENE      = 1u;
	inline constexpr uint32_t FRAME_BINDING_CSM        = 2u;
	inline constexpr uint32_t FRAME_BINDING_CLUSTERED  = 3u;
	inline constexpr uint32_t FRAME_BINDING_VOLUMETRIC = 4u;
	inline constexpr uint32_t FRAME_BINDING_TLAS       = 5u;

	// Push bindings
	inline constexpr uint32_t PUSH_BINDING_READ_1  = 0u;
	inline constexpr uint32_t PUSH_BINDING_READ_2  = 1u;
	inline constexpr uint32_t PUSH_BINDING_READ_3  = 2u;
	inline constexpr uint32_t PUSH_BINDING_READ_4  = 3u;
	inline constexpr uint32_t PUSH_BINDING_READ_5  = 4u;
	inline constexpr uint32_t PUSH_BINDING_READ_6  = 5u;
	inline constexpr uint32_t PUSH_BINDING_READ_7  = 6u;
	inline constexpr uint32_t PUSH_BINDING_READ_8  = 7u;
	inline constexpr uint32_t PUSH_BINDING_READ_9  = 8u;
	inline constexpr uint32_t PUSH_BINDING_READ_10 = 9u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_1 = 10u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_2 = 11u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_3 = 12u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_4 = 13u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_5 = 14u;

	// -------------------------
	// Indirect Dispatch Slots
	// -------------------------

	inline constexpr size_t DISPATCH_SLOT_STRIDE_BYTES      = 16u; // uvec4

	// Draw-build pipeline — stream dispatch args
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        = 0u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED = 1u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   = 2u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    = 3u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM0          = 4u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM1          = 5u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM2          = 6u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_VOLUMETRIC    = 7u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_SCATTER              = 8u;  // total-visible dispatch

	// Other systems
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          = 9u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_LIGHTS               = 10u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CLUSTERS             = 11u;

	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_REFLECT_RAYS         = 12u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_SHADOW_RAYS          = 13u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_TRANSPARENCY_RAYS    = 14u;

	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_COUNT                = 15u;

	// Byte offsets — multiply slot by stride
	inline constexpr uint64_t DISPATCH_STREAM_OPAQUE_BYTES            = INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_OPAQUE_MASKED_BYTES     = INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_TRANSPARENT_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_FLASHLIGHT_OFFSET_BYTES = INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM0_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM0          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM1_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM1          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM2_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM2          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_VOLUMETRIC_OFFSET_BYTES = INDIRECT_DISPATCH_SLOT_STREAM_VOLUMETRIC    * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_SCATTER_OFFSET_BYTES           = INDIRECT_DISPATCH_SLOT_SCATTER              * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_DEBUG_BUILD_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_LIGHTS_OFFSET_BYTES            = INDIRECT_DISPATCH_SLOT_LIGHTS               * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_CLUSTERS_OFFSET_BYTES          = INDIRECT_DISPATCH_SLOT_CLUSTERS             * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_REFLECT_RAYS_OFFSET_BYTES      = INDIRECT_DISPATCH_SLOT_REFLECT_RAYS         * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_SHADOW_RAYS_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_SHADOW_RAYS          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_TRANSPARENCY_RAYS_OFFSET_BYTES = INDIRECT_DISPATCH_SLOT_TRANSPARENCY_RAYS    * DISPATCH_SLOT_STRIDE_BYTES;

	// --------------
	// RT Ray slots
	// --------------
	inline constexpr uint32_t RT_RAY_SLOT_REFLECT      = 0u;
	inline constexpr uint32_t RT_RAY_SLOT_SHADOW       = 1u;
	inline constexpr uint32_t RT_RAY_SLOT_TRANSPARENCY = 2u;
	inline constexpr uint32_t RT_RAY_SLOT_COUNT        = 3u;

	// -------------------------
	// Visibility Stream Slots
	// -------------------------

	// Used during instance cull to fill indirect dispatch
	inline constexpr uint32_t VIS_SLOT_OPAQUE        = 0u;
	inline constexpr uint32_t VIS_SLOT_OPAQUE_MASKED = 1u;
	inline constexpr uint32_t VIS_SLOT_TRANSPARENT   = 2u;
	inline constexpr uint32_t VIS_SLOT_FLASHLIGHT    = 3u;
	inline constexpr uint32_t VIS_SLOT_CSM0          = 4u;
	inline constexpr uint32_t VIS_SLOT_CSM1          = 5u;
	inline constexpr uint32_t VIS_SLOT_CSM2          = 6u;
	inline constexpr uint32_t VIS_SLOT_VOLUMETRIC    = 7u;
	inline constexpr uint32_t VIS_SLOT_COUNT         = 8u;

	// Used to build draws
	inline constexpr uint32_t VIS_PRIMARY_OPAQUE         = 1u << 0;
	inline constexpr uint32_t VIS_PRIMARY_OPAQUE_MASKED  = 1u << 1;
	inline constexpr uint32_t VIS_PRIMARY_TRANSPARENT    = 1u << 2;
	inline constexpr uint32_t VIS_FLASHLIGHT             = 1u << 3;
	inline constexpr uint32_t VIS_CSM0                   = 1u << 4;
	inline constexpr uint32_t VIS_CSM1                   = 1u << 5;
	inline constexpr uint32_t VIS_CSM2                   = 1u << 6;
	inline constexpr uint32_t VIS_VOLUMETRIC             = 1u << 7;

	// -------------------------
	// Draw Region Offsets
	// -------------------------
	inline constexpr uint32_t INDIRECT_CMD_SIZE      = 20u; // VkDrawIndexedIndirectCommand

	inline constexpr uint32_t MAX_DRAWS_OPAQUE        = 32768u;
	inline constexpr uint32_t MAX_DRAWS_OPAQUE_MASKED = 12288u;
	inline constexpr uint32_t MAX_DRAWS_TRANSPARENT   = 3072u;
	inline constexpr uint32_t MAX_DRAWS_FLASHLIGHT    = 1024u;
	inline constexpr uint32_t MAX_DRAWS_CSM0          = 8192u;
	inline constexpr uint32_t MAX_DRAWS_CSM1          = 2048u;
	inline constexpr uint32_t MAX_DRAWS_CSM2          = 2048u;
	inline constexpr uint32_t MAX_DRAWS_VOLUMETRIC    = 8192u;

	// --------------------
	// Task Dispatch Slots
	// --------------------
	inline constexpr uint32_t TASK_GROUP_SIZE = 32u;

	inline constexpr uint32_t MAX_TASK_DISPATCHES_OPAQUE        = MAX_DRAWS_OPAQUE;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_OPAQUE_MASKED = MAX_DRAWS_OPAQUE_MASKED;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_TRANSPARENT   = MAX_DRAWS_TRANSPARENT;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_FLASHLIGHT    = MAX_DRAWS_FLASHLIGHT;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_CSM0          = MAX_DRAWS_CSM0;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_CSM1          = MAX_DRAWS_CSM1;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_CSM2          = MAX_DRAWS_CSM2;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_VOLUMETRIC    = MAX_DRAWS_VOLUMETRIC;

	inline constexpr uint32_t TASK_OFFSET_OPAQUE        = 0u;
	inline constexpr uint32_t TASK_OFFSET_OPAQUE_MASKED = TASK_OFFSET_OPAQUE        + MAX_TASK_DISPATCHES_OPAQUE;
	inline constexpr uint32_t TASK_OFFSET_TRANSPARENT   = TASK_OFFSET_OPAQUE_MASKED + MAX_TASK_DISPATCHES_OPAQUE_MASKED;
	inline constexpr uint32_t TASK_OFFSET_FLASHLIGHT    = TASK_OFFSET_TRANSPARENT   + MAX_TASK_DISPATCHES_TRANSPARENT;
	inline constexpr uint32_t TASK_OFFSET_CSM0          = TASK_OFFSET_FLASHLIGHT    + MAX_TASK_DISPATCHES_FLASHLIGHT;
	inline constexpr uint32_t TASK_OFFSET_CSM1          = TASK_OFFSET_CSM0          + MAX_TASK_DISPATCHES_CSM0;
	inline constexpr uint32_t TASK_OFFSET_CSM2          = TASK_OFFSET_CSM1          + MAX_TASK_DISPATCHES_CSM1;
	inline constexpr uint32_t TASK_OFFSET_VOLUMETRIC    = TASK_OFFSET_CSM2          + MAX_TASK_DISPATCHES_CSM2;
	inline constexpr uint32_t TASK_OFFSET_TOTAL         = TASK_OFFSET_VOLUMETRIC    + MAX_TASK_DISPATCHES_VOLUMETRIC;

	inline constexpr uint32_t TASK_BYTE_OFFSET_BY_SLOT[VIS_SLOT_COUNT] = {
		TASK_OFFSET_OPAQUE        * TASK_GROUP_SIZE,
		TASK_OFFSET_OPAQUE_MASKED * TASK_GROUP_SIZE,
		TASK_OFFSET_TRANSPARENT   * TASK_GROUP_SIZE,
		TASK_OFFSET_FLASHLIGHT    * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM0          * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM1          * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM2          * TASK_GROUP_SIZE,
		TASK_OFFSET_VOLUMETRIC    * TASK_GROUP_SIZE
	};

	inline constexpr uint32_t MAX_TASK_DISPATCHES_BY_SLOT[VIS_SLOT_COUNT] = {
		MAX_TASK_DISPATCHES_OPAQUE,
		MAX_TASK_DISPATCHES_OPAQUE_MASKED,
		MAX_TASK_DISPATCHES_TRANSPARENT,
		MAX_TASK_DISPATCHES_FLASHLIGHT,
		MAX_TASK_DISPATCHES_CSM0,
		MAX_TASK_DISPATCHES_CSM1,
		MAX_TASK_DISPATCHES_CSM2,
		MAX_TASK_DISPATCHES_VOLUMETRIC,
	};

	// -------------------
	// Renderer constants
	// -------------------
	inline constexpr uint32_t MAX_FRAME_INSTANCES_TOTAL     = 1000000;
	inline constexpr uint32_t MAX_FRAME_DRAW_COMMANDS_TOTAL = TASK_OFFSET_TOTAL;
	inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT          = 3u;
	//inline constexpr uint32_t MAX_LIGHTS                    = 16384u;
	inline constexpr uint32_t MAX_LIGHTS                    = 2048u;
	//inline constexpr uint32_t MAX_VISIBLE_LIGHTS            = 2048u;
	inline constexpr uint32_t MAX_PUSH_CONSTANT_SIZE        = 256u;
	inline constexpr uint32_t MAX_INSTANCES_PER_STREAM      = 262144u;
	inline constexpr uint32_t MAX_DRAW_BINS                 = 65536u;
	inline constexpr uint32_t BIN_TABLE_SIZE                = MAX_DRAW_BINS * 2;
	inline constexpr uint32_t INVALID_U32                   = 0xFFFFFFFFu;
	inline constexpr uint32_t MAX_GRAPHICS_PRIMARIES        = 3u;
	inline constexpr uint32_t MAX_MESHLET_VISIBILITY_BITS   = 33554432u;
	inline constexpr uint32_t TAA_SAMPLE_COUNT              = 11u;

	inline constexpr uint32_t TRANSFORM_DYNAMIC_BIT = 1u << 31;
	inline constexpr uint32_t TRANSFORM_INDEX_MASK  = ~TRANSFORM_DYNAMIC_BIT;
	inline constexpr uint32_t MAX_STATIC_TRANSFORMS = MAX_FRAME_INSTANCES_TOTAL;
	inline constexpr uint32_t MAX_DYNAMIC_TRANSFORMS = 32768u;

	inline constexpr uint32_t MAX_RT_INSTANCES = 32000u;

	inline constexpr uint32_t DEBUG_VERTS_PER_OBB     = 24u;
	inline constexpr uint32_t DEBUG_VERTS_PER_SPHERE  = 72u;     // 3 rings x 12 segments x 2
	inline constexpr uint32_t DEBUG_MAX_ITEMS         = 65536u;  // max debug items per frame
	inline constexpr uint32_t DEBUG_MAX_VERTS         = DEBUG_MAX_ITEMS * DEBUG_VERTS_PER_OBB;

	inline constexpr uint32_t MAX_FLT_UINT = 0x7F7FFFFFu;

	// Static lights in global list
	inline constexpr uint32_t LIGHT_LIST_STATIC_COUNT     = 1u;
	//inline constexpr uint32_t LIGHT_LIST_SLOT_DIRECTIONAL = 0u;
	inline constexpr uint32_t LIGHT_LIST_SLOT_FLASHLIGHT  = 0u;

	inline constexpr uint32_t LIGHT_FLAG_DIRECTIONAL        = 1u << 0;
	inline constexpr uint32_t LIGHT_FLAG_POINT              = 1u << 1;
	inline constexpr uint32_t LIGHT_FLAG_SPOT               = 1u << 2;
	inline constexpr uint32_t LIGHT_FLAG_CASTS_SPOT_SHADOW  = 1u << 3;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT         = 1u << 4;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT_OFF     = 1u << 5;
	inline constexpr uint32_t LIGHT_FLAG_MASK_ONLY          = 1u << 6;

	inline constexpr uint32_t CLUSTERS_TILE_SLICE_X         = 64u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Y         = 64u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Z         = 24u;
	//inline constexpr size_t MAX_VISIBLE_LIGHT_ID_GPU_BYTES = MAX_LIGHTS * sizeof(uint32_t);
	inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER        = 256u;
	inline constexpr uint32_t MAX_VISIBLE_LIGHTS            = MAX_LIGHTS - LIGHT_LIST_STATIC_COUNT;

	inline constexpr uint32_t FROXEL_GRID_X = 160u;
	inline constexpr uint32_t FROXEL_GRID_Y = 90u;
	inline constexpr uint32_t FROXEL_GRID_Z = 64u;

	inline constexpr uint32_t ATMOSPHERE_TRANSMITTANCE_X = 512u;
	inline constexpr uint32_t ATMOSPHERE_TRANSMITTANCE_Y = 512u;

	inline constexpr uint32_t ATMOSPHERE_SKY_VIEW_X = 256u;
	inline constexpr uint32_t ATMOSPHERE_SKY_VIEW_Y = 128u;

	// Fixed allocation. Changing origin or spacing never changes the probe count.
	inline constexpr uint32_t WORLD_PROBE_CASCADE_COUNT = 5u;
	inline constexpr uint32_t WORLD_PROBE_GRID_X = 16u;
	inline constexpr uint32_t WORLD_PROBE_GRID_Y = 8u;
	inline constexpr uint32_t WORLD_PROBE_GRID_Z = 16u;
	inline constexpr uint32_t WORLD_PROBES_PER_CASCADE = 2048u;
	inline constexpr uint32_t WORLD_PROBE_COUNT = 10240u;
	inline constexpr uint32_t WORLD_PROBE_RAYS_FULL = 64u;
	inline constexpr uint32_t WORLD_PROBE_RAYS_QUARTER = 16u;
	inline constexpr uint32_t WORLD_PROBE_VISIBILITY_SIZE = 8u;
	inline constexpr uint32_t WORLD_PROBE_VISIBILITY_TILE_SIZE = 10u;
	inline constexpr uint32_t WORLD_PROBE_RESOLVE_DIVISOR = 2u;

	// For target hardware 16x is practically free
	inline constexpr float MAX_ANISOTROPY_LEVEL = 16.0f;

	// FPS targets
	inline constexpr float TARGET_FPS_60       = 60.0f;
	inline constexpr float TARGET_FPS_90       = 90.0f;
	inline constexpr float TARGET_FPS_100      = 100.0f;
	inline constexpr float TARGET_FPS_120      = 120.0f;
	inline constexpr float TARGET_FPS_144      = 144.0f;
	inline constexpr float TARGET_FPS_165      = 165.0f;
	inline constexpr float TARGET_FPS_180      = 180.0f;
	inline constexpr float TARGET_FPS_200      = 200.0f;
	inline constexpr float TARGET_FPS_240      = 240.0f;
	inline constexpr float TARGET_FPS_300      = 300.0f;
	inline constexpr float TARGET_FPS_360      = 360.0f;
	inline constexpr float TARGET_FPS_480      = 480.0f;

	// Resource Limits
	inline constexpr uint32_t MAX_MIP_LEVELS          = 12u;
	inline constexpr uint32_t MAX_SHADOW_CASCADES     = 3u;
	inline constexpr uint32_t VOL_CASCADE_COUNT       = 1u;
	inline constexpr uint32_t MAX_LUMINANCE_GROUPS    = 65536u;
	inline constexpr uint32_t HI_Z_MIP_COUNT          = 11u;
	inline constexpr uint32_t HI_Z_MIN_MIP_COUNT      = 5u;

	inline constexpr uint32_t RADIANCE_MIP_COUNT      = 4u;

	inline constexpr uint32_t MAX_ENVIRONMENT_SETS            = 2u;
	inline constexpr uint32_t SPECULAR_PREFILTERED_MIP_LEVELS = 6;
	inline constexpr uint32_t PREFILTER_SAMPLE_COUNT          = 2048;
	inline constexpr uint32_t BRDF_EXTENT                     = 128;

	// Unused currently.
	inline constexpr uint32_t SKYBOX_EXTENT = 512;
	inline constexpr uint32_t SPECULAR_EXTENT = 256;

	inline constexpr uint32_t MAX_SHADOW_INVALID_VOLUMES = 64u;
	inline constexpr size_t SHADOW_INVALID_VOLUME_HEADER_BYTES = 16u;
	inline constexpr size_t SHADOW_INVALID_VOLUME_BYTES =
		SHADOW_INVALID_VOLUME_HEADER_BYTES + MAX_SHADOW_INVALID_VOLUMES * 24u;

	// Image array sizes
	inline constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES      = 100u;
	inline constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 10000u;

	inline constexpr uint32_t CSM_QUALITY_HIGH   = 4096u;
	inline constexpr uint32_t CSM_QUALITY_MEDIUM = 3072u;
	inline constexpr uint32_t CSM_QUALITY_LOW    = 2048u;

	inline constexpr uint32_t FLASHLIGHT_SHADOW_QUALITY = 1024u;

	inline constexpr uint32_t FLASHLIGHT_SHADOW_MAP_X = FLASHLIGHT_SHADOW_QUALITY;
	inline constexpr uint32_t FLASHLIGHT_SHADOW_MAP_Y = FLASHLIGHT_SHADOW_QUALITY;

	// The volumetric shadow map pass doesn't scale with resolution,
	// vertex throughput and alpha tested fragment shader are true cost.
	// 2048 provides highest quality god ray beams and d16 format minimizes some memory cost.
	// Single projection, so every texel is the same world size across the whole
	// fog volume. Alpha tested foliage is what drives this: each froxel records a
	// coverage fraction, and sub-texel gaps between leaves don't blur, they vanish.
	// Dropping resolution biases froxels toward shadowed and beams read as flat fog.
	inline constexpr uint32_t VOL_SHADOW_MAP_X = 2048u;
	inline constexpr uint32_t VOL_SHADOW_MAP_Y = 2048u;

	enum class SunShadowFilter : uint32_t
	{
		PCF,
		PCSS,
		RT_SOFT,
	};
	enum class ShadowQuality
	{
		Low,
		Medium,
		High,
	};

	inline uint32_t EvaluateShadowQuality(ShadowQuality quality)
	{
		switch(quality)
		{
			case ShadowQuality::Low:    return CSM_QUALITY_LOW;
			case ShadowQuality::Medium: return CSM_QUALITY_MEDIUM;
			case ShadowQuality::High:   return CSM_QUALITY_HIGH;
		}
	}

	enum class Renderer_Pass
	{
		ShadowBounds,
		InstanceCull,
		DrawBuild,
		Prepass,
		HiZGeneration,
		PrepassLate,
		HiZGenerationLate,
		VelocityResolve,
		MaterialResolve,
		ClusteredLights,
		AtmosphereLUTUpdate,
		AtmosphereSkyView,
		AtmosphereLighting,
		DirectionalCSMAtlas,
		FlashlightShadow,
		VolumetricShadowMap,
		AtmosphereSky,
		ScreenSpaceContactShadows,
		SSGI,
		WorldProbesUpdate,
		WorldProbesResolve,
		WorldProbesCache,
		WorldProbesReconstruct,
		TlasBuild,
		RTShadows,
		RTReflections,
		NRDDenoise,
		OpaqueLighting,
		TransparentForward,
		VolumetricFog,
		HDRSceneComposite,
		WorldProbesDebug,
		DebugDrawBuild,
		DebugLineDraw,
		TAA,
		LuminanceExposure,
		Bloom,
		LensFlare,
		FinalComposite,
		ChromaticAberration,
		CAS,

		Count
	};

	struct PassTimestampRange
	{
		uint32_t beginQuery = UINT32_MAX;
		uint32_t endQuery = UINT32_MAX;
	};

	inline constexpr size_t PASS_COUNT = static_cast<size_t>(Renderer_Pass::Count);

	enum class Renderer_Pipeline
	{
		TransparentForward,

		HDRSceneComposite,

		VelocityResolve,
		WireframeMesh,

		PrepassMesh,
		PrepassMaskedMesh,
		ShadowMesh,
		ShadowMeshMaskedD32,
		ShadowMeshMaskedD16,

		ShadowBounds,

		RTShadowVolumeBuild,
		RTShadowInvalidMask,
		RTShadowClassify,
		RTShadowTrace,
		InstanceCull,
		DrawArgs,
		DrawScatter,
		DrawEmit,
		DrawPlace,

		MaterialResolve,
		OpaqueLighting,

		ExposureReduce,
		ExposureFinalize,
		FinalComposite,

		DebugCount,
		DebugArgs,
		DebugBuild,
		LineDebug,

		//HDRToCubemap,
		//SpecularPrefilter,
		//SHIrradiance,
		BRDFLUT,

		WorldProbesPrepare,
		WorldProbesRelight,
		WorldProbesSkyTrace,
		WorldProbesInject,
		WorldProbesRelocate,
		WorldProbesSkyMean,

		WorldProbesResolve,

		WorldProbesReconstruct,
		WorldProbesDebug,

		WorldProbesCacheResolve,
		WorldProbesCacheFallback,

		AtmosphereTransmittance,
		AtmosphereSkyView,
		AtmosphereSky,
		AtmosphereLighting,

		NRDPrepare,

		HiZGen,

		HiZPrefilter,
		VBGI,
		GIAccumulate,
		BilateralUpsample,
		AODenoise,
		GIDenoise,
		AOAccumulate,

		FroxelInject,
		FroxelReproject,
		FroxelIntegrate,

		FlareBright,
		FlareGen,

		BloomDownsample,
		BloomUpsample,

		LightCull,

		TransparentClusterBounds,
		ClusterTileSliceRanges,
		IndirectArgsLight,
		ClusterCount,
		ClusterScanOffsets,
		ClusterScatterIDs,

		ShadingSignalReduce,
		TAA,

		CAS,

		ScreenSpaceContactShadows,

		ChromaticAberration,

		GBufferDebug,

		TlasInstances,

		RTRayArgs,

		ReflectClassify,
		RTReflectTrace,

		Count
	};

	inline constexpr size_t PIPELINE_COUNT = static_cast<size_t>(Renderer_Pipeline::Count);

	enum class Renderer_RenderTarget
	{
		WorldProbeVisibility,
		WorldProbeLighting,
		WorldProbeSkyMean,
		WorldProbeReconstructed,
		WorldProbeSpatialCache,
		TransparentAccumulation,
		TransparentRevealage,
		TransparentVelocityAccum,
		HDRScene,
		Tonemap,
		DepthResolved,
		PrevDepthResolved,
		HiZ,
		LinearizedHiZ,
		Visibility,
		AORaw,
		AOTemp,
		AOHistoryA,
		AOHistoryB,
		AOReconstruction,
		AoEdgeInfo,
		BentAOUpsampled,
		BentNormalAO,
		BentNormalAOHalf,
		ColorHistoryA,
		ColorHistoryB,
		FlareBright,
		LensFlareColor,
		BloomMipchain,
		Velocity,
		ViewNormals,
		PrevViewNormals,

		FroxelScatterExtA,
		FroxelScatterExtB,
		FroxelIntegrated,

		ShadowInvalidMask,
		RTShadowPenumbra,
		RTShadowDenoised,
		NRDShadowNormalRoughness,
		NRDShadowViewZ,

		DiffuseRadianceA,
		DiffuseRadianceB,
		GIHistoryA,
		GIHistoryB,
		IndirectSSGI,
		GIDenoisePing,

		ReflectRadiance,
		ReflectRoughness,

		AtmosphereTransmittance,
		AtmosphereSkyView,
		AtmosphereHDR,
		AtmosphereLighting,

		NRDMotion,
		NRDNormalRoughness,
		NRDViewZ,
		RTReflectDenoised,

		// Deferred outputs
		GBufferAlbedoRough,
		GBufferNormalMaterial, // N, metal, matID

		// Used as chromatic aberration output
		PostNonAAComposite,

		// CAS
		SharpenedColor,

		ShadingSignalHalf,

		ShadingLowA, // 1/8 render extent
		ShadingLowB,

		SSContactShadows,
		DirectionalCSMAtlas,
		FlashlightShadowMap,
		VolumetricShadowMap,
		Count
	};
	inline constexpr size_t RENDER_TARGET_COUNT = static_cast<size_t>(Renderer_RenderTarget::Count);

	struct RenderTargetIDs
	{
		uint32_t worldProbeVisibilityID = UINT32_MAX;
		uint32_t worldProbeLightingID = UINT32_MAX;
		uint32_t worldProbeSkyMeanID = UINT32_MAX;
		uint32_t worldProbeReconstructedID = UINT32_MAX;
		uint32_t transparentAccumulationID = UINT32_MAX;
		uint32_t transparentRevealageID = UINT32_MAX;
		uint32_t transparentVelocityAccumID = UINT32_MAX;
		uint32_t hdrSceneID = UINT32_MAX;
		uint32_t tonemapID = UINT32_MAX;
		uint32_t depthResolvedID = UINT32_MAX;
		uint32_t prevDepthResolvedID = UINT32_MAX;
		uint32_t hiZID = UINT32_MAX;
		uint32_t linearizedHiZID = UINT32_MAX;
		uint32_t visibilityID = UINT32_MAX;
		uint32_t aoRawID = UINT32_MAX;
		uint32_t aoTempID = UINT32_MAX;
		uint32_t aoHistoryAID = UINT32_MAX;
		uint32_t aoHistoryBID = UINT32_MAX;
		uint32_t aoReconstructionID = UINT32_MAX;
		uint32_t aoEdgeInfoID = UINT32_MAX;
		uint32_t bentAOUpsampledID = UINT32_MAX;
		uint32_t bentNormalAOID = UINT32_MAX;
		uint32_t bentNormalAOHalfID = UINT32_MAX;
		uint32_t colorHistoryAID = UINT32_MAX;
		uint32_t colorHistoryBID = UINT32_MAX;
		uint32_t flareBrightID = UINT32_MAX;
		uint32_t lensFlareColorID = UINT32_MAX;
		uint32_t bloomMipchainID = UINT32_MAX;
		uint32_t velocityID = UINT32_MAX;
		uint32_t viewNormalsID = UINT32_MAX;
		uint32_t prevViewNormalsID = UINT32_MAX;
		uint32_t shadowInvalidMaskID = UINT32_MAX;
		uint32_t rtShadowPenumbraID = UINT32_MAX;
		uint32_t rtShadowDenoisedID = UINT32_MAX;
		uint32_t nrdShadowNormalRoughnessID = UINT32_MAX;
		uint32_t nrdShadowViewZID = UINT32_MAX;
		uint32_t diffuseRadianceAID = UINT32_MAX;
		uint32_t diffuseRadianceBID = UINT32_MAX;
		uint32_t giHistoryAID = UINT32_MAX;
		uint32_t giHistoryBID = UINT32_MAX;
		uint32_t indirectSSGIID = UINT32_MAX;
		uint32_t giDenoisePingID = UINT32_MAX;
		uint32_t reflectRadianceID = UINT32_MAX;
		uint32_t reflectRoughnessID = UINT32_MAX;
		uint32_t atmosphereTransmittanceID = UINT32_MAX;
		uint32_t atmosphereSkyViewID = UINT32_MAX;
		uint32_t atmosphereHDRID = UINT32_MAX;
		uint32_t atmosphereLightingID = UINT32_MAX;
		uint32_t nrdMotionID = UINT32_MAX;
		uint32_t nrdNormalRoughnessID = UINT32_MAX;
		uint32_t nrdViewZID = UINT32_MAX;
		uint32_t rtReflectDenoisedID = UINT32_MAX;
		uint32_t gBufferAlbedoRoughID = UINT32_MAX;
		uint32_t gBufferNormalMaterialID = UINT32_MAX;
		uint32_t postNonAACompositeID = UINT32_MAX;
		uint32_t sharpenedColorID = UINT32_MAX;
		uint32_t shadingSignalHalfID = UINT32_MAX;
		uint32_t shadingLowAID = UINT32_MAX;
		uint32_t shadingLowBID = UINT32_MAX;
		uint32_t ssContactShadowsID = UINT32_MAX;
		uint32_t directionalCSMAtlasID = UINT32_MAX;
		uint32_t flashlightShadowMapID = UINT32_MAX;
		uint32_t volumetricShadowMapID = UINT32_MAX;
	};

	enum class Renderer_Texture
	{
		RainbowLut,
		CookieGobo,
		HilbertCurveLut,
		ShadowSTBN,
		Dummy,
		DummyU8,
		DummyVelocity,
		Brdf,
		White,
		MetalRough,
		Normal,
		Checkerboard,

		Count
	};

	inline constexpr size_t STATIC_TEXTURE_COUNT = static_cast<size_t>(Renderer_Texture::Count);

	enum class Renderer_Sampler
	{
		NearestClamp,
		LinearClamp,
		HiZ,
		LinearLodClamp,
		PointBorder,
		TaaHistory,
		Noise,
		ShadowMap,
		Linear,
		Nearest,
		Brdf,
		Specular,
		Skybox,
		Equirect,
		Count
	};

	inline constexpr size_t SAMPLER_COUNT = static_cast<size_t>(Renderer_Sampler::Count);

	// ssbo buffers inside the bindless address table
	enum class Renderer_Buffer
	{
		// Global — persistent
		InstanceInputs,
		DrawBinKeys,
		Mesh,
		Material,
		Vertex,
		Index,
		Meshlet,
		MeshletVertices,
		MeshletTriangles,
		StaticTransforms,
		Luminance,
		WorldProbes,
		WorldProbeSchedule,
		WorldProbeSummary,
		BLASAddresses,
		RTRows,

		// Frame — transient
		WorldProbeFrameInfo,
		WorldProbeCacheInfo,
		DynamicTransforms,
		MotionMatrices,
		Lights,
		RTInstances,
		RTRayList,
		InstanceVisibility,
		MeshletVisibilityA,
		MeshletVisibilityB,
		VisibleCount,
		VisibleInstances,
		InstanceCursors,
		InstanceStreams,
		DrawInstanceIDs,
		IndirectDrawCounts,
		DrawBins,
		DrawBinCounters,
		ShadowCullData,
		DrawStats,
		DispatchIndirectArgs,
		TaskDispatch,

		DebugCounts,
		DebugItems,
		DebugVertex,
		DebugDraw,

		VisibleLightCount,
		VisibleLightIDs,

		ClusterCounts,
		ClusterOffsets,
		ClusterCursors,
		ClusterLightIDs,
		ClusterTileSliceRanges,
		ClusterScanScratch,
		ClusterTileTransparentNear,

		VolClusterCounts,
		VolClusterOffsets,
		VolClusterCursors,
		VolClusterLightIDs,

		ShadowInvalidVolumes,

		Count
	};

	inline constexpr size_t ADDRESS_TABLE_BUFFER_COUNT = static_cast<size_t>(Renderer_Buffer::Count);

	inline constexpr uint32_t DEBUG_MASK_OBB     = 1u << 0;
	inline constexpr uint32_t DEBUG_MASK_SPHERE  = 1u << 1; // unused
	inline constexpr uint32_t DEBUG_MASK_AABB    = 1u << 2; // unused

	// Instance drawing counts and transforms
	enum class InstancingMethod
	{
		DrawStatic,      // single baked instance
		DrawMultiStatic, // many baked instances
		DrawDynamic,     // single instance, dynamic transform
		DrawMultiDynamic // many instances, dynamic transforms
	};

	enum class GIMethod
	{
		OFF,
		VBAO, // Visibility Bitmask Ambient Occlusion
		VBGI
	};

	enum class AntiAliasingMethod
	{
		AA_OFF,
		//AA_CMAA2,   // Conservative Morphological Anti-Aliasing 2
		//AA_SMAA,    // Sub-Pixel Morphological Anti-Aliasing
		//AA_FXAA,    // Fast Approximate Anti-Aliasing
		AA_TAA,       // Temporal Anti-Aliasing
		AA_TAA_CAS,   // Contrast Adaptive Sharpening
	};

	// Define this somewhere
	struct alignas(4) RenderToggles
	{
		uint32_t enableLensFlare           = 0;
		uint32_t enableChromaticAberration = 0;
		uint32_t enableSSS                 = 0;
		uint32_t enableFlashlight          = 0;

		uint32_t aaMode                    = 0;
		uint32_t giMode                    = 0;
		uint32_t enableShadows             = 0;
		uint32_t enableVolumetrics         = 0;

		uint32_t activeEnvMap              = 0;
		uint32_t disableOcclusionCull      = 0;
		uint32_t renderingMode             = 0;
		uint32_t debugView                 = 0;

		float depthScale                   = 0.0f;
		uint32_t enableWireframe           = 0;
		uint32_t sunShadowFilter           = 0;
		uint32_t enableRTReflections       = 0;

		uint32_t enableProfilerView        = 0;
		uint32_t enableSettings            = 0;
		uint32_t enableBloom               = 0;
		float bloomIntensity               = 0.0;

		uint32_t showOpaqueOBBs            = 0;
		uint32_t showTransparentOBBs       = 0;
		uint32_t activeInstanceCount       = 0;
		uint32_t activeLightCount          = 0;

		uint32_t activeRTInstances         = 0;
		uint32_t csmAtlasCached            = 0;
		uint32_t worldProbesDebugView      = 0;
		uint32_t pad0;
	};

	enum class ImageAccess
	{
		Undefined,

		TransferSrc,
		TransferDst,

		ComputeRead,
		ComputeWrite,
		ComputeReadWrite,
		ComputeReadStorage,

		Read,
		Write,

		MeshShaderRead,

		GraphicsColorWrite,
		GraphicsDepthWrite,

		Present,

		DepthRead
	};

	enum class BufferAccess
	{
		Undefined,

		TransferWrite,
		TransferRead,

		Read,
		ComputeWrite,
		ComputeRWTransfer,

		VertexRead,
		IndexRead,
		IndirectRead,

		FragmentRead,
		GraphicsRead,

		ASBuildRead,
		ASWrite,
		ASRead
	};

	enum class ResourceLifetime
	{
		Persistent,     // engine lifetime, freed at shutdown only
		FrameVolatile,  // freed after N frames-in-flight
		RenderTarget,   // freed and rebuilt on resize/setting change
		Asset           // freed when scene or asset owner dies
	};

	enum class DescriptorSlot
	{
		Unified,
		Frame,
		Push,
		Count
	};

	enum class DebugState : uint32_t
	{
		Off,
		Wireframe,
		OBBLine,
		ShadedOverlay,
	};


	enum class DebugView : uint32_t
	{
		Off = 0,
		Albedo,
		Normals,
		Roughness,
		Metallic,
		SSGI,
		SSShadows,
		Cascades,
		VisInstance,
		VisTriangle,
		VisLod,
		Meshlets,
		//MeshletFacing,
		Count
	};

	inline constexpr uint32_t DebugViewBit(DebugView v)
	{
		return 1u << static_cast<uint32_t>(v);
	}

	inline constexpr uint32_t DBG_CAPS_TEMPORAL =
		DebugViewBit(DebugView::Off) |
		DebugViewBit(DebugView::SSGI);

	inline constexpr bool DebugViewTemporal(uint32_t view)
	{
		return view < static_cast<uint32_t>(DebugView::Count)
			&& (DBG_CAPS_TEMPORAL & (1u << view)) != 0u;
	}

	inline constexpr bool DebugViewSupported(uint32_t caps, uint32_t view)
	{
		return view < static_cast<uint32_t>(DebugView::Count)
			&& (caps & (1u << view)) != 0u;
	}

	struct DebugViewEntry
	{
		DebugView   view;
		const char* label;
	};

	inline constexpr DebugViewEntry DEBUG_VIEW_TABLE[] = {
		{ DebugView::Off,           "Complete"        },
		{ DebugView::Albedo,        "Albedo"          },
		{ DebugView::Normals,       "Normals"         },
		{ DebugView::Roughness,     "Roughness"       },
		{ DebugView::Metallic,      "Metallic"        },
		{ DebugView::SSGI,          "SSAO"            },
		{ DebugView::SSShadows,     "Contact Shadows" },
		{ DebugView::Cascades,      "Cascade Splits"  },
		{ DebugView::VisInstance,   "Vis: Instance"   },
		{ DebugView::VisTriangle,   "Vis: Triangle"   },
		{ DebugView::VisLod,        "Vis: LOD"        },
		{ DebugView::Meshlets,      "Meshlets"        },
		//{ DebugView::MeshletFacing, "MeshletFacing"   }
	};

	struct RenderStateInfo
	{
	public:
		void SetWorldProbeSSGIValid(bool value) noexcept { m_worldProbeSSGIValid = value; }
		bool WorldProbeSSGIValid() const noexcept { return m_worldProbeSSGIValid; }

		void ResetDebugMask() { m_debugState = DebugState::Off; }
		void SetDebugMask(DebugState mask) { m_debugState = mask; }

		bool IsWireframeOn() const noexcept { return m_debugState == DebugState::Wireframe; }
		bool IsObbLineOn() const noexcept { return m_debugState == DebugState::OBBLine; }
		bool IsShadedOverlayOn() const noexcept { return m_debugState == DebugState::ShadedOverlay; }

		bool IsNRDActive() const noexcept {
			return
				IsTemporalValid() &&
				InstancesActive() &&
				RTInstancesActive() &&
				(RTReflectionsEnabled() || RTShadowsEnabled()) &&
				!DebugRenderFastPath();
 		}

		bool InstancesActive() const noexcept { return m_renderToggles.activeInstanceCount > 0; }
		bool LightsActive() const noexcept { return m_renderToggles.activeLightCount > 0; }

		bool IsCSMAtlasCached() const noexcept { return m_renderToggles.csmAtlasCached; }

		// Cuts down minimum passes
		bool DebugRenderFastPath() const noexcept { return IsWireframeOn() || IsShadedOverlayOn(); }

		bool DebugRendering() const noexcept { return DebugRenderFastPath() || IsObbLineOn(); }

		bool TemporalAllowed() const noexcept
		{
			return !DebugRendering();
		}

		bool TemporalActive() const noexcept
		{
			return IsTaaOn() && TemporalAllowed();
		}

		bool RTReflectionsEnabled() const noexcept { return m_renderToggles.enableRTReflections; }

		void UpdateToggles(RenderToggles toggles) { m_renderToggles = toggles; }

		bool DrawImgui() const noexcept { return m_renderToggles.enableProfilerView || m_renderToggles.enableSettings; }
		bool FlashlightOn() const noexcept { return m_renderToggles.enableFlashlight; }

		void UpdateTemporal(bool temporalValid, bool hiZValid)
		{
			m_bTemporalValid = temporalValid;
			m_bHiZValid = hiZValid;
		}

		bool IsShadowsOn() const noexcept { return m_renderToggles.enableShadows; }

		bool IsFlashlightOn() const noexcept { return m_renderToggles.enableFlashlight; }

		bool RTShadowsEnabled() const noexcept
		{
			return m_renderToggles.sunShadowFilter == static_cast<uint32_t>(SunShadowFilter::RT_SOFT) && IsShadowsOn();
		}

		bool IsScreenSpaceShadowsOn() const noexcept { return m_renderToggles.enableSSS; }
		bool IsVolumetricsOn() const noexcept { return m_renderToggles.enableVolumetrics; }

		bool VolumetricFogActive() const noexcept
		{
			return IsVolumetricsOn() && InstancesActive() && !DebugRendering();
		}

		bool IsWorldProbesDebugOn() const noexcept
		{
			return m_renderToggles.worldProbesDebugView && InstancesActive() && !DebugRenderFastPath();
		}

		bool IsTemporalValid() const noexcept { return m_bTemporalValid; }
		bool IsHiZValid() const noexcept { return m_bHiZValid; }

		bool IsTaaOn() const noexcept
		{
			return m_renderToggles.aaMode == static_cast<uint32_t>(AntiAliasingMethod::AA_TAA)
				|| m_renderToggles.aaMode == static_cast<uint32_t>(AntiAliasingMethod::AA_TAA_CAS);
		}

		bool IsSharpeningOn() const noexcept
		{
			return m_renderToggles.aaMode == static_cast<uint32_t>(AntiAliasingMethod::AA_TAA_CAS);
		}

		bool IsChromaticAberrationOn() const noexcept
		{
			return m_renderToggles.enableChromaticAberration;
		}

		void ResetRenderToggles() { m_renderToggles = {}; }

		uint32_t GetLightCount() const noexcept { return m_renderToggles.activeLightCount; }
		uint32_t GetInstanceCount() const noexcept { return m_renderToggles.activeInstanceCount; }

		bool m_bStateChanged         = false;

		bool RTInstancesActive() const noexcept { return m_renderToggles.activeRTInstances > 0; }
		uint32_t GetRTInstanceCount() const noexcept { return m_renderToggles.activeRTInstances; }

		void SetTemporalIndex(uint64_t index) { m_temporalIndex = index; }
		uint64_t GetTemporalIndex() const noexcept { return m_temporalIndex; }

	private:
		bool m_bTemporalValid        = false;
		bool m_bHiZValid             = false;
		bool m_worldProbeSSGIValid   = false;

		uint64_t m_temporalIndex = UINT32_MAX;

		DebugState m_debugState = DebugState::Off;

		RenderToggles m_renderToggles;
	};
}
