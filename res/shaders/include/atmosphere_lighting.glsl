#ifndef ATMOSPHERE_LIGHTING_GLSL
#define ATMOSPHERE_LIGHTING_GLSL

#include "atmosphere.glsl"

const int ATM_LIGHT_WIDTH     = 128;
const int ATM_LIGHT_HEIGHT    = 64;
const int ATM_SPEC_LEVELS     = 6;
const int ATM_DIFFUSE_LAYER   = 6;
const int ATM_LIGHT_LAYERS    = 7;

AtmosphereParameters getSceneAtmosphereParameters()
{
	SceneData scene = getSceneData();

	AtmosphereParameters p;
	p.rayleigh    = scene.atmosphereRayleigh;
	p.mie         = scene.atmosphereMie;
	p.absorption  = scene.atmosphereAbsorption;
	p.geometry    = scene.atmosphereGeometry;
	p.integration = scene.atmosphereIntegration;

	return p;
}

vec3 atmosphereSolarIlluminance()
{
	SceneData scene = getSceneData();

	return max(
		scene.sunlightColor.rgb
		* scene.sunlightColor.a
		* scene.atmosphereSun.y,
		vec3(0.0));
}

// Unexposed direct solar illuminance after atmospheric attenuation.
// Scene geometry shadowing remains the responsibility of the caller.
vec3 atmosphereSunAtWorld(vec3 worldPos)
{
	SceneData scene = getSceneData();
	AtmosphereParameters p = getSceneAtmosphereParameters();

	float h;
	vec3 up;
	atmosphereCamera(
		p, worldPos, scene.atmospherePlacement, h, up);

	vec3 sunDir = normalize(scene.sunlightDirection.xyz);

	return atmosphereSolarIlluminance()
		* sampleAtmosphereTransmittanceHM(
			p, h, dot(up, sunDir));
}

// Camera-altitude sky environment, excluding the explicit sun disc.
// Result is unexposed radiance.
vec3 atmosphereSkyRadiance(vec3 dir)
{
	SceneData scene = getSceneData();
	AtmosphereParameters p = getSceneAtmosphereParameters();

	float h;
	vec3 up;
	atmosphereCamera(
		p, scene.cameraPos.xyz, scene.atmospherePlacement, h, up);

	uint skyID = getAtmosphereResourceUBO().textureIDs.y;
	vec2 extent = vec2(textureSize(TEX2D(skyID), 0));

	vec2 uv = atmosphereSkyViewUV(
		p, h, normalize(dir), up,
		normalize(scene.sunlightDirection.xyz), extent);

	return max(SampleTextureLod(skyID, uv, 0.0).rgb, vec3(0.0));
}

vec3 atmosphereLatLongDirection(vec2 uv)
{
	float phi = (uv.x - 0.5) * (2.0 * PI);
	float theta = uv.y * PI;

	float s = sin(theta);

	return vec3(s * cos(phi), cos(theta), s * sin(phi));
}

vec2 atmosphereDirectionLatLong(vec3 dir)
{
	dir = normalize(dir);

	return vec2(
		atan(dir.z, dir.x) / (2.0 * PI) + 0.5,
		acos(clamp(dir.y, -1.0, 1.0)) / PI);
}

vec3 atmosphereFetchLighting(ivec2 cell, int layer)
{
	cell.x = (cell.x % ATM_LIGHT_WIDTH + ATM_LIGHT_WIDTH)
		% ATM_LIGHT_WIDTH;

	cell.y = clamp(cell.y, 0, ATM_LIGHT_HEIGHT - 1);

	uint id = getAtmosphereResourceUBO().textureIDs.z;

	return texelFetch(
		TEX2D(id),
		ivec2(cell.x, cell.y + layer * ATM_LIGHT_HEIGHT),
		0).rgb;
}

vec3 atmosphereSampleLighting(vec3 dir, int layer)
{
	vec2 uv = atmosphereDirectionLatLong(dir);
	vec2 pixel = uv * vec2(ATM_LIGHT_WIDTH, ATM_LIGHT_HEIGHT) - 0.5;

	ivec2 base = ivec2(floor(pixel));
	vec2 f = fract(pixel);

	vec3 c00 = atmosphereFetchLighting(base, layer);
	vec3 c10 = atmosphereFetchLighting(base + ivec2(1, 0), layer);
	vec3 c01 = atmosphereFetchLighting(base + ivec2(0, 1), layer);
	vec3 c11 = atmosphereFetchLighting(base + ivec2(1, 1), layer);

	return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// Integral of sky radiance * max(dot(N, direction), 0) over the sphere.
// Units: illuminance. Does not include direct sunlight.
vec3 atmosphereDiffuseIrradiance(vec3 N)
{
	return max(
		atmosphereSampleLighting(normalize(N), ATM_DIFFUSE_LAYER),
		vec3(0.0));
}

// Standard split-sum environment prefilter, excluding direct sunlight.
vec3 atmospherePrefilteredRadiance(vec3 R, float roughness)
{
	float level = clamp(roughness, 0.0, 1.0)
		* float(ATM_SPEC_LEVELS - 1);

	int lo = int(floor(level));
	int hi = min(lo + 1, ATM_SPEC_LEVELS - 1);

	return max(mix(
		atmosphereSampleLighting(R, lo),
		atmosphereSampleLighting(R, hi),
		fract(level)), vec3(0.0));
}

// Unexposed radiance along an RT miss direction.
// The view direction itself determines sun-disc coverage.
vec3 atmosphereSunDiscRadiance(vec3 dir)
{
	SceneData scene = getSceneData();
	vec3 sunDir = normalize(scene.sunlightDirection.xyz);

	float radius = max(scene.atmosphereSun.x, 1e-5);

	if (dot(normalize(dir), sunDir) < cos(radius))
		return vec3(0.0);

	AtmosphereParameters p = getSceneAtmosphereParameters();

	float h;
	vec3 up;
	atmosphereCamera(
		p, scene.cameraPos.xyz, scene.atmospherePlacement, h, up);

	float sinRadius = sin(radius);

	return atmosphereSolarIlluminance()
		* sampleAtmosphereTransmittanceHM(p, h, dot(up, normalize(dir)))
		/ (PI * sinRadius * sinRadius);
}

vec3 atmosphereGroundAlbedo()
{
	return clamp(getSceneData().atmosphereGround.rgb, vec3(0.0), vec3(1.0));
}

vec3 atmosphereGroundSkylight(vec3 up)
{
	float scale = getSceneData().atmosphereGround.w;

	return scale > 0.0
		? atmosphereDiffuseIrradiance(up) * scale
		: vec3(0.0);
}

#endif