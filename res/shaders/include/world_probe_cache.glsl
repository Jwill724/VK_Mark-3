#ifndef WORLD_PROBE_CACHE_GLSL
#define WORLD_PROBE_CACHE_GLSL

#include "world_probe_cache_common.glsl"
#include "world_probe_packing.glsl"

layout(set = PUSH_SET, binding = PUSH_BINDING_READ_10) uniform sampler3D wpSpatialCacheTexture;

ivec3 wpSpatialCacheExtent()
{
	return textureSize(wpSpatialCacheTexture, 0) / ivec3(1, 1, WP_CACHE_SLABS);
}

vec4 wpFetchCacheSlab(vec3 unitUVW, ivec3 extent, int slab)
{
	return textureLod(wpSpatialCacheTexture, wpCacheTextureUVW(unitUVW, extent, slab), 0.0);
}

// One filtered texture instruction. RGB remains coverage-premultiplied until
// composed with the fallback, so there is no division by tiny coverage.
// Returns UNEXPOSED ambient radiance for the existing scatter shader.
vec3 sampleWorldProbeFogAmbient(vec3 worldPosition)
{
	WPSpatialCacheInfoBuffer info = getWPSpatialCacheInfo();
	vec3 fallback = max(info.fallbackSky.rgb, vec3(0));
	vec3 uvw;
	if ((info.options.x & 1u) == 0u || !wpCacheProject(worldPosition, uvw)) return fallback;
	ivec3 extent = wpSpatialCacheExtent();
	vec4 value = wpFetchCacheSlab(uvw, extent, WP_CACHE_MEAN_SLAB);
	float edge = wpCacheEdgeCoverage(uvw, extent);
	float coverage = clamp(value.a, 0.0, 1.0) * edge;
	return max(value.rgb, vec3(0)) * (edge / max(getPreExposure(), 1e-8))
		+ fallback * (1.0 - coverage);
}

// Optional transparent-surface approximation: six filtered texture instructions,
// with no probe metadata/visibility traversal in the material shader.
// Normal-independent cache construction cannot retain the original per-surface front weights.
WPLighting sampleWorldProbesCachedSurface(vec3 position,
	vec3 normal, vec3 geometricNormal)
{
	WPSpatialCacheInfoBuffer info = getWPSpatialCacheInfo();
	if ((info.options.x & 2u) == 0u) return wpEmptyLighting();
	vec3 uvw;
	vec3 query = position + geometricNormal * max(info.depthBias.z, 0.0);
	if (!wpCacheProject(query, uvw)) return wpEmptyLighting();
	float n2 = dot(normal, normal);
	if (n2 <= 1e-12) return wpEmptyLighting();
	vec3 N = normal * inversesqrt(n2);
	ivec3 axes = ivec3(N.x < 0.0 ? 1 : 0, N.y < 0.0 ? 3 : 2, N.z < 0.0 ? 5 : 4);
	vec3 sw = N * N;
	vec3 bw = sw * sw;
	bw *= bw;
	bw /= max(bw.x + bw.y + bw.z, 1e-12);
	ivec3 extent = wpSpatialCacheExtent();
	vec4 sky = vec4(0), bounce = vec4(0);
	for (int a = 0; a < 3; ++a)
	{
		sky += wpFetchCacheSlab(uvw, extent, axes[a]) * sw[a];
		bounce += wpFetchCacheSlab(uvw, extent, axes[a] + 6) * bw[a];
	}
	WPLighting result = wpUnpackLighting(sky, bounce);
	result.coverage *= wpCacheEdgeCoverage(uvw, extent);
	result.valid = result.valid && result.coverage > 1e-6;
	return result;
}

#endif
