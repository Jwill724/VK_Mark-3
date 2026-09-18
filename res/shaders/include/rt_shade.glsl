#ifndef RT_SHADE_GLSL
#define RT_SHADE_GLSL

#extension GL_GOOGLE_include_directive : require

#include "world_probe.glsl"
#include "rt_params.glsl"
#include "pbr.glsl"
#include "atmosphere_lighting.glsl"
#include "lighting.glsl"

float rtMaxComp(vec3 v)
{
	return max(v.r, max(v.g, v.b));
}

vec3 rtSampleSky(vec3 dir, float pathRough, uint skyboxID)
{
	vec3 sky = atmosphereSkyRadiance(dir);

	float sunWeight =
		1.0 - smoothstep(0.05, 0.25, pathRough);

	vec3 sun =
		atmosphereSunDiscRadiance(dir) * sunWeight;

	return sky + sun;
}

vec3 rtShadeAnalytic(HitSurface s, vec3 rayDir, RTShadeParams p)
{
	SceneData    sd    = getSceneData();
	DebugToggles debug = getDebugToggles();

	vec3 V = -rayDir;

	Surface hs = makeSurface(
		s.position, s.normal, s.position + V,
		s.albedo, s.metallic, s.roughness, s.mat, p.brdfID);

	vec3 L        = p.shadow.sunDirectionWS.xyz;
	vec3 sunColor = atmosphereSunAtWorld(s.position);

	LightSample sun = evaluateDirectional(hs, L, sunColor);

	vec3 direct = vec3(0.0);

	if (sun.NdotL > 0.0)
	{
		float bias = rtSurfaceBias(
			p.shadow,
			distance(sd.cameraPos.xyz, s.position));

		float sunVisibility = rtSunVisibleOpaque(
			s.position + s.normal * bias,
			L,
			p.shadow.rayTMin,
			p.shadow.rayTMax);

		direct =
			(sun.diffuse + sun.specular) *
			sunVisibility;
	}

	vec3 local = vec3(0.0);

	if (debug.activeLightCount > 0u)
	{
		LightBuffer lightBuf = getLightBuffer();
		uint evaluated = 0u;

		for (uint i = 0u; i < debug.activeLightCount && evaluated < p.maxLights; ++i)
		{
			LocalLight light = lightBuf.lights[i];

			if ((light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u) continue;

			vec3  d  = light.position - s.position;
			float d2 = dot(d, d);
			if (d2 > light.radius * light.radius) continue;

			LightSample ls = evaluateLocalLight(light, hs, sd.cameraPos.xyz);
			if (ls.NdotL <= 0.0) continue;

			++evaluated;
			local += ls.diffuse + ls.specular;
		}
	}

	// Diffuse lighting at the reflected hit position.
	WPLighting wpDiffuse = sampleWorldProbes(
		s.position,
		hs.N,
		s.normal);

	vec3 irradiance = probeSkyIrradiance(wpDiffuse, hs.N);
	vec3 bounce     = probeBounceLighting(wpDiffuse);

	vec3 ambientDiffuse =
		hs.kD *
		hs.diffuseAlbedo *
		(irradiance * (1.0 / PI) + bounce);

	// Approximate sky visibility for the terminal specular lobe.
	vec3 R = reflect(-hs.V, hs.N);

	WPLighting wpSpecular = sampleWorldProbes(
		s.position,
		R,
		s.normal);

	float probeCoverage = wpSpecular.valid
		? clamp(wpSpecular.coverage, 0.0, 1.0)
		: 0.0;

	float skyVis =
		clamp(wpSpecular.skyVisibility, 0.0, 1.0) *
		probeCoverage;

	float horizon = clamp(
		1.0 + dot(R, s.geoNormal),
		0.0, 1.0);

	skyVis *= horizon * horizon;

	vec3 atmosphereSpecular = sampleAtmosphereSpecular(
		hs.V,
		hs.N,
		hs.rough,
		hs.F0,
		hs.brdf);

	const float RT_TERMINAL_SKY_SPECULAR_SCALE = 0.5;

	vec3 ambientSpecular =
		atmosphereSpecular *
		hs.multiScatter *
		skyVis *
		RT_TERMINAL_SKY_SPECULAR_SCALE;

	return direct
		+ local
		+ (ambientDiffuse + ambientSpecular) * p.ambientScale
		+ s.emissive;
}

#endif