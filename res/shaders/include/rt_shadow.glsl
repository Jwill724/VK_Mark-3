#ifndef RT_SHADOW_GLSL
#define RT_SHADOW_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_geometry.glsl"
#include "rt_sampling.glsl"

struct RTVisibility
{
	float visibility;
	float occluderDist;
	float hits;
};

float rtSunVisibleOpaque(vec3 origin, vec3 dir, float tMin, float tMax)
{
	rayQueryEXT q;
	rayQueryInitializeEXT(q, sceneTLAS,
		gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsCullBackFacingTrianglesEXT,
		RT_MASK_OPAQUE, origin, tMin, dir, tMax);

	while (rayQueryProceedEXT(q)) {}

	return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT
		? 1.0 : 0.0;
}

vec3 rtSampleSunCone(vec3 dir, float tanAngle, vec2 rnd)
{
	float phi = TWO_PI * rnd.x;
	float r = tanAngle * sqrt(rnd.y);

	vec3 up = abs(dir.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 t = normalize(cross(up, dir));
	vec3 b = cross(dir, t);

	return normalize(dir + t * (r * cos(phi)) + b * (r * sin(phi)));
}

// Fast visibility query retained for existing non-SIGMA callers.
// Returned distance is NOT guaranteed to be the nearest blocker.
float rtShadowFirstHit(vec3 origin, vec3 dir, float tMin, float tMax, float mipBias)
{
	rayQueryEXT q;
	rayQueryInitializeEXT(q, sceneTLAS,
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		RT_MASK_SHADOW_CASTERS, origin, tMin, dir, tMax);

	while (rayQueryProceedEXT(q))
	{
		if (rayQueryGetIntersectionTypeEXT(q, false) != gl_RayQueryCandidateIntersectionTriangleEXT)
			continue;

		uint instanceID = rayQueryGetIntersectionInstanceCustomIndexEXT(q, false);
		uint primID = rayQueryGetIntersectionPrimitiveIndexEXT(q, false);
		vec2 bary = rayQueryGetIntersectionBarycentricsEXT(q, false);

		if (rtAlphaTestPasses(instanceID, primID, bary, mipBias))
			rayQueryConfirmIntersectionEXT(q);
	}

	return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT
		? NRD_FP16_MAX : rayQueryGetIntersectionTEXT(q, true);
}

// SIGMA uses distance to determine its penumbra/filter footprint.
// Continue traversal to obtain the closest accepted blocker, including alpha test.
// This is still ONE ray query per traced pixel.
float rtShadowClosestHit(vec3 origin, vec3 dir, float tMin, float tMax, float mipBias)
{
	rayQueryEXT q;
	rayQueryInitializeEXT(q, sceneTLAS,
		gl_RayFlagsCullBackFacingTrianglesEXT,
		RT_MASK_SHADOW_CASTERS, origin, tMin, dir, tMax);

	while (rayQueryProceedEXT(q))
	{
		if (rayQueryGetIntersectionTypeEXT(q, false) != gl_RayQueryCandidateIntersectionTriangleEXT)
			continue;

		uint instanceID = rayQueryGetIntersectionInstanceCustomIndexEXT(q, false);
		uint primID = rayQueryGetIntersectionPrimitiveIndexEXT(q, false);
		vec2 bary = rayQueryGetIntersectionBarycentricsEXT(q, false);

		if (rtAlphaTestPasses(instanceID, primID, bary, mipBias))
			rayQueryConfirmIntersectionEXT(q);
	}

	return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT
		? NRD_FP16_MAX : rayQueryGetIntersectionTEXT(q, true);
}

RTVisibility rtConeVisibilityBasis(
	vec3 origin, mat3 basis, float tanRadius,
	float phi, float jitter, int taps,
	float tMin, float tMax, float mipBias)
{
	RTVisibility r;
	r.visibility = 1.0;
	r.occluderDist = 0.0;
	r.hits = 0.0;

	if (taps <= 0) return r;

	float vis = 0.0;
	float distSum = 0.0;
	float hits = 0.0;

	for (int i = 0; i < taps; ++i)
	{
		vec3 dir = rtConeDirection(basis, rtVogelDisk(i, taps, phi, jitter), tanRadius);
		float t = rtShadowFirstHit(origin, dir, tMin, tMax, mipBias);

		if (t >= NRD_FP16_MAX) vis += 1.0;
		else { distSum += t; hits += 1.0; }
	}

	r.visibility = vis / float(taps);
	r.occluderDist = hits > 0.0 ? distSum / hits : 0.0;
	r.hits = hits;
	return r;
}

RTVisibility rtConeVisibility(
	vec3 origin, vec3 L, float tanRadius,
	vec2 pixel, int taps, float tMin, float tMax, float mipBias)
{
	uint frameIndex = getSceneData().temporal.x;

	float phi = createPhase(pixel) + fract(float(frameIndex) * 0.7548776662);
	float jitter = fract(temporalInterleavedGradientNoise(pixel + 37.0, 0, 1.0)
		+ float(frameIndex) * 0.5698402909);

	return rtConeVisibilityBasis(origin, buildTBN(L), tanRadius,
		phi, jitter, taps, tMin, tMax, mipBias);
}

RTVisibility rtConeVisibilityRnd(
	vec3 origin, vec3 L, float tanRadius,
	vec2 rnd, int taps, float tMin, float tMax, float mipBias)
{
	return rtConeVisibilityBasis(origin, buildTBN(L), tanRadius,
		rnd.x * TWO_PI, rnd.y, taps, tMin, tMax, mipBias);
}

#endif
