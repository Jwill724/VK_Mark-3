#ifndef DEBUG_VIEWS_GLSL
#define DEBUG_VIEWS_GLSL

// View space
const uint DBG_OFF            = 0u;
const uint DBG_ALBEDO         = 1u;
const uint DBG_NORMALS        = 2u;
const uint DBG_ROUGHNESS      = 3u;
const uint DBG_METALLIC       = 4u;
const uint DBG_SSAO           = 5u;
const uint DBG_SS_SHADOWS     = 6u;
const uint DBG_CASCADES       = 7u;
const uint DBG_VIS_INSTANCE   = 8u;
const uint DBG_VIS_TRIANGLE   = 9u;
const uint DBG_VIS_LOD        = 10u;
const uint DBG_MESHLETS       = 11u;
//const uint DBG_MESHLET_FACING = 12u;
const uint DBG_COUNT          = 12u;

#define DBG_BIT(v) (1u << (v))

const uint DBG_CAPS_TEMPORAL =
	DBG_BIT(DBG_OFF) | DBG_BIT(DBG_SSAO);

bool debugViewTemporal(uint view)
{
	return view < DBG_COUNT && (DBG_CAPS_TEMPORAL & DBG_BIT(view)) != 0u;
}

bool debugViewSupported(uint caps, uint view)
{
	return view < DBG_COUNT && (caps & DBG_BIT(view)) != 0u;
}

vec3 debugHashColor(uint v)
{
	float f = float(v);
	return vec3(hash(vec2(f, 17.0)), hash(vec2(f, 71.0)), hash(vec2(f, 131.0)));
}

vec3 debugLodColor(uint lodIdx)
{
	if (lodIdx == LOD_IDX_LOD0) return vec3(0.0, 1.0, 0.0);
	if (lodIdx == LOD_IDX_LOD1) return vec3(0.6, 1.0, 0.0);
	if (lodIdx == LOD_IDX_LOD2) return vec3(1.0, 0.7, 0.0);
	if (lodIdx == LOD_IDX_LOD3) return vec3(1.0, 0.2, 0.0);
	if (lodIdx == LOD_IDX_BASE) return vec3(0.2, 0.5, 1.0);
	return vec3(1.0, 0.0, 1.0);
}

vec3 debugCascadeOverlay(vec3 albedo, uint cascadeIdx)
{
	return mix(albedo, cascadeColor(cascadeIdx), 0.6);
}

#endif
