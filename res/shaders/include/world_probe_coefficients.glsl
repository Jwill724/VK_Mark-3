#ifndef WORLD_PROBE_COEFFICIENTS_GLSL
#define WORLD_PROBE_COEFFICIENTS_GLSL

#include "world_probe.glsl"

// Normal-independent spatial gather for the optional transparent cache.
// Unlike exact surface sampling, no geometric-normal front weighting is used.
// All six lobes share one probe neighborhood and one set of visibility fetches.
struct WPCoefficients
{
	vec4 sky[6];       // RGB sky, A sky visibility
	vec3 bounce[6];    // confidence has already attenuated RGB
	float coverage;
};

WPCoefficients wpEmptyCoefficients()
{
	WPCoefficients r;
	for (uint a = 0u; a < 6u; ++a)
	{
		r.sky[a] = vec4(0);
		r.bounce[a] = vec3(0);
	}
	r.coverage = 0.0;
	return r;
}

WPCoefficients wpGatherCascadeCoefficients(uint cascade, vec3 position)
{
	WPCoefficients r = wpEmptyCoefficients();
	WorldProbeBuffer b = getWorldProbeBuffer();
	WPHeader h = b.header;
	WPCascadeDesc c = h.cascades[cascade];
	ivec3 base;
	vec3 f;
	if (!wpGridCell(c, position, base, f)) return r;
	uvec3 dims = c.dimsCount.xyz;
	ivec3 worldBase = c.baseCell.xyz + base;
	uvec3 physicalBase = uvec3(wpFloorMod(worldBase, ivec3(dims)));
	float sum = 0.0;
	for (uint k = 0u; k < 8u; ++k)
	{
		uvec3 o = uvec3(k & 1u, (k >> 1u) & 1u, (k >> 2u) & 1u);
		vec3 bw = mix(1.0 - f, f, vec3(o));
		float w = bw.x * bw.y * bw.z;
		if (w <= 0.0) continue;
		uvec3 physical = physicalBase + o;
		if (physical.x >= dims.x) physical.x -= dims.x;
		if (physical.y >= dims.y) physical.y -= dims.y;
		if (physical.z >= dims.z) physical.z -= dims.z;
		uint id = c.dimsCount.w + wpIndex(physical, dims);
		uvec4 state = b.probes[id].state;
		if (state.x == 0u || state.y != h.revisions.y) continue;
		if (any(notEqual(b.probes[id].cell.xyz, worldBase + ivec3(o)))) continue;
		w *= wpVisibility(id, position - b.probes[id].position.xyz, c, h);
		if (w < 1e-8) continue;
		for (uint a = 0u; a < 6u; ++a)
		{
			r.sky[a] += b.probes[id].sky[a] * w;
			vec4 bounce = b.probes[id].bounce[a];
			r.bounce[a] += bounce.rgb * (w * clamp(bounce.a, 0.0, 1.0));
		}
		sum += w;
	}
	if (sum <= 1e-8) return wpEmptyCoefficients();
	for (uint a = 0u; a < 6u; ++a)
	{
		r.sky[a] /= sum;
		r.bounce[a] /= sum;
	}
	r.coverage = min(sum / 0.05, 1.0);
	return r;
}

WPCoefficients wpGatherCoefficients(vec3 position)
{
	WPCoefficients r = wpEmptyCoefficients();
	WPHeader h = getWorldProbeBuffer().header;
	if (h.layoutInfo.y == 0u) return r;
	float remaining = 1.0;
	float used = 0.0;
	uint gathers = 0u;
	for (uint c = 0u; c < min(h.layoutInfo.x, 5u); ++c)
	{
		if (remaining <= 1e-5 || gathers >= WP_MAX_SAMPLE_GATHERS) break;
		float edge = wpCascadeEdgeFade(h.cascades[c], position);
		if (edge <= 0.0) continue;
		++gathers;
		WPCoefficients s = wpGatherCascadeCoefficients(c, position);
		float support = clamp(s.coverage * edge, 0.0, 1.0);
		float w = remaining * support;
		for (uint a = 0u; a < 6u; ++a)
		{
			r.sky[a] += s.sky[a] * w;
			r.bounce[a] += s.bounce[a] * w;
		}
		used += w;
		remaining *= 1.0 - support;
	}
	if (used > 1e-8)
	{
		for (uint a = 0u; a < 6u; ++a)
		{
			r.sky[a] /= used;
			r.bounce[a] /= used;
		}
		r.coverage = used;
	}
	return r;
}

#endif
