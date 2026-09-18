#ifndef WORLD_PROBE_RESOLVE_GLSL
#define WORLD_PROBE_RESOLVE_GLSL

#include "common.glsl"
#include "textures.glsl"
#include "world_probe_packing.glsl"

// Full-resolution opaque consumer. No depth/normal reads and no probe fallback.
WPLighting wpReadReconstructedPixel(ivec2 pixel)
{
	uint id = getSceneData().renderTargetIDs.worldProbeReconstructedID;
	ivec2 extent = SampleTextureSize(id, 0) / ivec2(1, 2);
	pixel = clamp(pixel, ivec2(0), extent - 1);
	return wpUnpackLighting(SampleTextureFetch(id, pixel, 0),
		SampleTextureFetch(id, pixel + ivec2(0, extent.y), 0));
}

// Existing opaque call sites can retain their signature.
// The other arguments are intentionally unused: the producer already evaluated them.
WPLighting sampleWorldProbesResolved(vec2 uv, vec3 position,
	vec3 normal, vec3 geometricNormal)
{
	uint id = getSceneData().renderTargetIDs.worldProbeReconstructedID;
	ivec2 extent = SampleTextureSize(id, 0) / ivec2(1, 2);
	return wpReadReconstructedPixel(ivec2(uv * vec2(extent)));
}

#endif
