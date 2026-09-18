#ifndef WORLD_PROBE_RT_GLSL
#define WORLD_PROBE_RT_GLSL

#include "rt_core.glsl"

struct WPHit { float distance; bool hit; bool backface; };

WPHit wpTrace(vec3 origin, vec3 direction, float tMin, float tMax)
{
	WPHit h = WPHit(tMax, false, false);
	if (tMax <= tMin) return h;
	rayQueryEXT q;

	rayQueryInitializeEXT(q, sceneTLAS, gl_RayFlagsOpaqueEXT,
		RT_MASK_OPAQUE | RT_MASK_ALPHA_TESTED, origin, tMin, direction, tMax);

	while (rayQueryProceedEXT(q)) {}

	h.hit = rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
	if (h.hit)
	{
		h.distance = rayQueryGetIntersectionTEXT(q, true);
		h.backface = !rayQueryGetIntersectionFrontFaceEXT(q, true);
	}
	return h;
}

#endif