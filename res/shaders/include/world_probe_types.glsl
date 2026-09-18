#ifndef WORLD_PROBE_TYPES_GLSL
#define WORLD_PROBE_TYPES_GLSL

// Consumer-only types: deliberately no probe buffers or gather functions.
struct WPLighting
{
	vec3 sky;
	vec3 bounce;
	float skyVisibility;
	float coverage;
	bool valid;
};

WPLighting wpEmptyLighting()
{
	return WPLighting(vec3(0), vec3(0), 0.0, 0.0, false);
}

struct WPVolume
{
	vec3 sky;
	float skyVisibility;
	bool valid;
	float coverage;
};

#endif
