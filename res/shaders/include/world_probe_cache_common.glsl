#ifndef WORLD_PROBE_CACHE_COMMON_GLSL
#define WORLD_PROBE_CACHE_COMMON_GLSL

#include "common.glsl"
#include "world_probe_types.glsl"

layout(buffer_reference, scalar) readonly buffer WPSpatialCacheInfoBuffer
{
	mat4 viewProjection;
	mat4 inverseViewProjection;
	mat4 view;
	vec4 cameraPosition;
	vec4 depthBias;   // near, far, surface world-space bias, edge fade in cells
	vec4 fallbackSky; // RGB = unexposed mean atmosphere radiance
	uvec4 options;    // x: bit 0 fog enabled; bit 1 directional surface cache enabled
};

WPSpatialCacheInfoBuffer getWPSpatialCacheInfo()
{
	return WPSpatialCacheInfoBuffer(getABTFrameAddress(ABT_WorldProbeCacheInfo));
}

const int WP_CACHE_SLABS = 13;
const int WP_CACHE_MEAN_SLAB = 12;

// One image3D packs 13 separate volumes along Z. Always clamp within a slab
// before linear filtering, otherwise neighboring slabs contaminate each other.
vec3 wpCacheTextureUVW(vec3 unitUVW, ivec3 gridExtent, int slab)
{
	vec3 extent = vec3(gridExtent);
	vec3 texel = clamp(unitUVW * extent, vec3(0.5), extent - 0.5);
	texel.z += float(slab * gridExtent.z);
	return texel / (extent * vec3(1, 1, WP_CACHE_SLABS));
}

bool wpCacheProject(vec3 worldPosition, out vec3 unitUVW)
{
	WPSpatialCacheInfoBuffer info = getWPSpatialCacheInfo();
	unitUVW = vec3(0);
	float nearZ = info.depthBias.x;
	float farZ = info.depthBias.y;
	if (nearZ <= 0.0 || farZ <= nearZ) return false;
	vec4 clip = info.viewProjection * vec4(worldPosition, 1);
	float depth = -(info.view * vec4(worldPosition, 1)).z;
	if (clip.w <= 0.0 || depth < nearZ || depth > farZ) return false;
	unitUVW = vec3(clip.xy / clip.w * 0.5 + 0.5,
		log(depth / nearZ) / log(farZ / nearZ));
	return all(greaterThanEqual(unitUVW, vec3(0)))
		&& all(lessThanEqual(unitUVW, vec3(1)));
}

vec3 wpCacheWorldPosition(ivec3 cell, ivec3 gridExtent)
{
	WPSpatialCacheInfoBuffer info = getWPSpatialCacheInfo();
	vec3 unitUVW = (vec3(cell) + 0.5) / vec3(gridExtent);
	float depth = info.depthBias.x * pow(info.depthBias.y / info.depthBias.x, unitUVW.z);
	// Use an interior clip depth: unlike Z=0 it remains finite with reverse-Z
	// infinite-far projections. The resulting ray is rescaled to the desired view Z.
	vec4 p = info.inverseViewProjection * vec4(unitUVW.xy * 2.0 - 1.0, 0.5, 1);
	vec3 world = p.xyz / p.w;
	float sampleDepth = -(info.view * vec4(world, 1)).z;
	return info.cameraPosition.xyz
		+ (world - info.cameraPosition.xyz) * (depth / sampleDepth);
}

float wpCacheEdgeCoverage(vec3 unitUVW, ivec3 gridExtent)
{
	float fadeCells = max(getWPSpatialCacheInfo().depthBias.w, 0.001);
	vec3 cellsToEdge = min(unitUVW, 1.0 - unitUVW) * vec3(gridExtent);
	vec3 fade = clamp(cellsToEdge / fadeCells, 0.0, 1.0);
	return fade.x * fade.y * fade.z;
}

#endif
