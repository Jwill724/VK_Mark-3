#ifndef WORLD_PROBE_SURFACE_GLSL
#define WORLD_PROBE_SURFACE_GLSL

#include "world_probe.glsl"
#include "depth.glsl"
#include "pbr.glsl"

// Shared by half-resolution evaluation and full-resolution fallback.
// This keeps normal-map/bent-normal handling identical in both paths.
WPLighting wpEvaluateSurface(ivec2 pixel, ivec2 fullExtent,
	float rawDepth, vec3 geometricNormalWS)
{
	SceneData scene = getSceneData();
	vec2 uv = (vec2(pixel) + 0.5) / vec2(fullExtent);
	vec3 position = reconstructWorldPosFromDepth(
		uv, rawDepth, scene.invProj, scene.invView).pos;
	uint packedMat = SampleTexelFetch(
		scene.renderTargetIDs.gBufferNormalMaterialID, pixel, 0).r;
	vec3 N;
	float metal;
	unpackNormalMetal(packedMat, N, metal);
	DebugToggles debug = getDebugToggles();
	float aoTerm = 1.0;
	vec3 bentWS = N;
	float bentConf = 0.0;
	if (debug.giMode != OFF)
	{
		vec4 bentAo = SampleTextureFetch(
			scene.renderTargetIDs.bentNormalAOID, pixel, 0);
		aoTerm *= bentAo.a;
		if (debug.giMode == VBGI)
		{
			bentWS = normalize(bentAo.rgb * 2.0 - 1.0);
			bentConf = 1.0 - aoTerm;
		}
	}
	vec3 irradianceN = bentConf > 0.0
		? normalize(mix(N, bentWS, bentConf)) : N;
	return sampleWorldProbes(position, irradianceN, geometricNormalWS);
}

ivec2 wpResolveSourcePixel(ivec2 hp, ivec2 lowExtent, ivec2 fullExtent)
{
	return clamp(ivec2((vec2(hp) + 0.5) * vec2(fullExtent) / vec2(lowExtent)),
		ivec2(0), fullExtent - 1);
}

#endif
