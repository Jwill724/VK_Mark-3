#ifndef SET_BINDINGS_GLSL
#define SET_BINDINGS_GLSL

// All Bindings are 1:1 with RendererDefinitions.cpp

// ================================
// === ADDRESS BUFFER TABLE IDs ===
// ================================

// --- Global (persistent across frames) ---
const uint ABT_InstanceInputs         = 0u;
const uint ABT_DrawBinKeys            = 1u;
const uint ABT_Mesh                   = 2u;
const uint ABT_Material               = 3u;
const uint ABT_Vertex                 = 4u;
const uint ABT_Index                  = 5u;
const uint ABT_Meshlet                = 6u;
const uint ABT_MeshletVertices        = 7u;
const uint ABT_MeshletTriangles       = 8u;
const uint ABT_StaticTransforms       = 9u;
const uint ABT_Luminance              = 10u;
const uint ABT_WorldProbes            = 11u;
const uint ABT_WorldProbeSchedule     = 12u;
const uint ABT_WorldProbeSummary      = 13u;
const uint ABT_BLASAddresses          = 14u;
const uint ABT_RTRows                 = 15u;

// --- Frame (written/reset each frame) ---

const uint ABT_WorldProbeFrameInfo    = 16u;
const uint ABT_WorldProbeCacheInfo    = 17u;
const uint ABT_DynamicTransforms      = 18u;
const uint ABT_MotionMatrices         = 19u;
const uint ABT_Lights                 = 20u;

const uint ABT_RTInstances            = 21u;
const uint ABT_RTRayList              = 22u;

const uint ABT_InstanceVisibility     = 23u;
const uint ABT_MeshletVisibilityA     = 24u;
const uint ABT_MeshletVisibilityB     = 25u;

const uint ABT_VisibleCount           = 26u;
const uint ABT_VisibleInstances       = 27u;
const uint ABT_InstanceCursors        = 28u;
const uint ABT_InstanceStreams        = 29u;
const uint ABT_DrawInstanceIDs        = 30u;
const uint ABT_IndirectDrawCounts     = 31u;
const uint ABT_DrawBins               = 32u;
const uint ABT_DrawBinCounters        = 33u;
const uint ABT_ShadowCullData         = 34u;
const uint ABT_DrawStats              = 35u;
const uint ABT_DispatchIndirectArgs   = 36u;
const uint ABT_TaskDispatch           = 37u;

const uint ABT_DebugCounts            = 38u;
const uint ABT_DebugItems             = 39u;
const uint ABT_DebugVertex            = 40u;
const uint ABT_DebugDraw              = 41u;

// Light culling

const uint ABT_VisibleLightCount      = 42u;
const uint ABT_VisibleLightIDs        = 43u;

// Clustered shading

const uint ABT_ClusterCounts              = 44u;
const uint ABT_ClusterOffsets             = 45u;
const uint ABT_ClusterCursors             = 46u;
const uint ABT_ClusterLightIDs            = 47u;
const uint ABT_ClusterTileSliceRanges     = 48u;
const uint ABT_ClusterScanScratch         = 49u;
const uint ABT_ClusterTileTransparentNear = 50u;

const uint ABT_VolClusterCounts           = 51u;
const uint ABT_VolClusterOffsets          = 52u;
const uint ABT_VolClusterCursors          = 53u;
const uint ABT_VolClusterLightIDs         = 54u;

const uint ABT_ShadowInvalidVolumes       = 55u;

const uint ABT_Count                      = 56u;

const uint MAX_SHADOW_INVALID_VOLUMES = 64u;

// =========================
// === RENDER TARGET IDs ===
// =========================

// All 2d types, any 3d can be accessed via push set
struct RenderTargetIDs
{
	uint worldProbeVisibilityID;
	uint worldProbeLightingID;
	uint worldProbeSkyMeanID;
	uint worldProbeReconstructedID;

	uint transparentAccumulationID;
	uint transparentRevealageID;
	uint transparentVelocityAccumID;

	uint hdrSceneID;
	uint tonemapID;

	uint depthResolvedID;
	uint prevDepthResolvedID;
	uint hiZID;
	uint linearizedHiZID;
	uint visibilityID;

	uint aoRawID;
	uint aoTempID;
	uint aoHistoryAID;
	uint aoHistoryBID;
	uint aoReconstructionID;
	uint aoEdgeInfoID;
	uint bentAOUpsampledID;
	uint bentNormalAOID;
	uint bentNormalAOHalfID;

	uint colorHistoryAID;
	uint colorHistoryBID;

	uint flareBrightID;
	uint lensFlareColorID;
	uint bloomMipchainID;

	uint velocityID;
	uint viewNormalsID;
	uint prevViewNormalsID;

	uint shadowInvalidMaskID;
	uint rtShadowPenumbraID;
	uint rtShadowDenoisedID;
	uint nrdShadowNormalRoughnessID;
	uint nrdShadowViewZID;

	uint diffuseRadianceAID;
	uint diffuseRadianceBID;
	uint giHistoryAID;
	uint giHistoryBID;
	uint indirectSSGIID;
	uint giDenoisePingID;

	uint reflectRadianceID;
	uint reflectRoughnessID;

	uint atmosphereTransmittanceID;
	uint atmosphereSkyViewID;
	uint atmosphereHDRID;
	uint atmosphereLightingID;

	uint nrdMotionID;
	uint nrdNormalRoughnessID;
	uint nrdViewZID;
	uint rtReflectDenoisedID;

	uint gBufferAlbedoRoughID;
	uint gBufferNormalMaterialID;

	uint postNonAACompositeID;
	uint sharpenedColorID;
	uint shadingSignalHalfID;

	uint shadingLowAID;
	uint shadingLowBID;

	uint ssContactShadowsID;

	uint directionalCSMAtlasID;
	uint flashlightShadowMapID;
	uint volumetricShadowMapID;
};

// =============================
// === SET_BINDINGS_BINDINGS ===
// =============================

const uint GLOBAL_SET = 0u;
const uint FRAME_SET  = 1u;
const uint PUSH_SET   = 2u;

// both global and frame owned
const uint ADDRESS_TABLE_BINDING            = 0u;

// Uniform
const uint GLOBAL_BINDING_ATMOSPHERE        = 1u;

// global set specific
const uint GLOBAL_BINDING_DEBUG_INLINE      = 2u;
const uint GLOBAL_BINDING_SAMPLER_CUBE      = 3u;
const uint GLOBAL_BINDING_COMBINED_SAMPLER  = 4u;

// Frame set specific UBOs
const uint FRAME_BINDING_SCENE      = 1u;
const uint FRAME_BINDING_CSM        = 2u;
const uint FRAME_BINDING_CLUSTERED  = 3u;
const uint FRAME_BINDING_VOLUMETRIC = 4u;
const uint FRAME_BINDING_TLAS       = 5u;

// Push bindings for images
const uint PUSH_BINDING_READ_1   = 0u;
const uint PUSH_BINDING_READ_2   = 1u;
const uint PUSH_BINDING_READ_3   = 2u;
const uint PUSH_BINDING_READ_4   = 3u;
const uint PUSH_BINDING_READ_5   = 4u;
const uint PUSH_BINDING_READ_6   = 5u;
const uint PUSH_BINDING_READ_7   = 6u;
const uint PUSH_BINDING_READ_8   = 7u;
const uint PUSH_BINDING_READ_9   = 8u;
const uint PUSH_BINDING_READ_10  = 9u;
const uint PUSH_BINDING_WRITE_1  = 10u;
const uint PUSH_BINDING_WRITE_2  = 11u;
const uint PUSH_BINDING_WRITE_3  = 12u;
const uint PUSH_BINDING_WRITE_4  = 13u;
const uint PUSH_BINDING_WRITE_5  = 14u;

// Indirect dispatch args
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        = 0u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED = 1u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   = 2u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    = 3u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM0          = 4u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM1          = 5u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM2          = 6u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_VOLUMETRIC    = 7u;
const uint INDIRECT_DISPATCH_SLOT_SCATTER              = 8u;
const uint INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          = 9u;
const uint INDIRECT_DISPATCH_SLOT_LIGHTS               = 10u;
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS             = 11u;
const uint INDIRECT_DISPATCH_SLOT_REFLECT_RAYS         = 12u;
const uint INDIRECT_DISPATCH_SLOT_SHADOW_RAYS          = 13u;
const uint INDIRECT_DISPATCH_SLOT_TRANSPARENCY_RAYS    = 14u;
const uint INDIRECT_DISPATCH_SLOT_COUNT                = 15u;

// Visibility/Draw slots
const uint VIS_SLOT_OPAQUE        = 0u;
const uint VIS_SLOT_OPAQUE_MASKED = 1u;
const uint VIS_SLOT_TRANSPARENT   = 2u;
const uint VIS_SLOT_FLASHLIGHT    = 3u;
const uint VIS_SLOT_CSM0          = 4u;
const uint VIS_SLOT_CSM1          = 5u;
const uint VIS_SLOT_CSM2          = 6u;
const uint VIS_SLOT_VOLUMETRIC    = 7u;

const uint VIS_SLOT_COUNT         = 8u;

// Ray tracing ray slots
const uint RT_RAY_SLOT_REFLECT      = 0u;
const uint RT_RAY_SLOT_SHADOW       = 1u;
const uint RT_RAY_SLOT_TRANSPARENCY = 2u;
const uint RT_RAY_SLOT_COUNT        = 3u;

#endif
