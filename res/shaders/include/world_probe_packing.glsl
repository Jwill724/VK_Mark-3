#ifndef WORLD_PROBE_PACKING_GLSL
#define WORLD_PROBE_PACKING_GLSL

#include "common.glsl"
#include "world_probe_types.glsl"

// Both textures store nonnegative, pre-exposed, coverage-premultiplied RGB.
// Sky alpha: coverage * sky visibility. Bounce alpha: coverage.
// Decode returns the original unexposed, non-premultiplied WPLighting API.
void wpPackLighting(WPLighting wp, out vec4 sky, out vec4 bounce)
{
	float coverage = wp.valid ? clamp(wp.coverage, 0.0, 1.0) : 0.0;
	float scale = coverage * max(getPreExposure(), 1e-8);
	sky = vec4(max(wp.sky, vec3(0)) * scale,
		clamp(wp.skyVisibility, 0.0, 1.0) * coverage);
	bounce = vec4(max(wp.bounce, vec3(0)) * scale, coverage);
}

WPLighting wpUnpackLighting(vec4 sky, vec4 bounce)
{
	WPLighting result = wpEmptyLighting();
	float coverage = clamp(bounce.a, 0.0, 1.0);
	if (coverage <= 1e-6) return result;
	float inverseScale = 1.0 / (coverage * max(getPreExposure(), 1e-8));
	result.sky = sky.rgb * inverseScale;
	result.bounce = bounce.rgb * inverseScale;
	result.skyVisibility = clamp(sky.a / coverage, 0.0, 1.0);
	result.coverage = coverage;
	result.valid = true;
	return result;
}

#endif
