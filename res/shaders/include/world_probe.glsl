#ifndef WORLD_PROBE_GLSL
#define WORLD_PROBE_GLSL

#include "common.glsl"
#include "world_probe_types.glsl"
#include "atmosphere_lighting.glsl"

const uint WP_COUNT = 10240u;
const uint WP_PER_CASCADE = 2048u;
const uint WP_QUEUE_SCROLL_QUARTER = 0u;
const uint WP_QUEUE_FULL_TRACE = 1u;
const uint WP_QUEUE_RELIGHT = 2u;
const uint WP_QUEUE_COUNT = 3u;
const int WP_FLAG_MOVED = 1;
const int WP_FLAG_RELOCATED = 2;
const int WP_FLAG_INSIDE = 4;
const int WP_FLAG_NEEDS_FULL_TRACE = 8;
const int WP_FLAG_QUARTER_TRACE = 16;
const uint WP_QUARTER_DIRECTION = 0x80000000u;
const vec3 WP_AXES[6] = vec3[6](vec3(1,0,0), vec3(-1,0,0),
	vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));

struct WPCascadeDesc
{
	uvec4 dimsCount;
	vec4 originSpacing;
	ivec4 baseCell;
	vec4 trace;
	uvec4 schedule;
};

struct WPHeader
{
	uvec4 layoutInfo;
	uvec4 revisions;
	vec4 gather;
	vec4 blend;
	vec4 relocation0;
	vec4 relocation1;
	uvec4 atlas;
	uvec4 schedule;
	WPCascadeDesc cascades[5];
};

struct WorldProbe
{
	vec4 position;
	uvec4 state;
	ivec4 cell;
	uvec4 meta;
	vec4 sky[6];
	vec4 bounce[6];
	uvec4 rayClassMask;
};

struct WPTraceSummary
{
	vec4 distance;
	uvec4 direction;
	uvec4 counts;
};

layout(buffer_reference, scalar) buffer WorldProbeBuffer
{
	WPHeader header;
	WorldProbe probes[];
};

layout(buffer_reference, scalar) readonly buffer WPFrameInfoBuffer
{
	WPHeader header;
};

WPFrameInfoBuffer getWorldProbeFrameInfo()
{
	return WPFrameInfoBuffer(getABTFrameAddress(ABT_WorldProbeFrameInfo));
};

layout(buffer_reference, scalar) buffer WorldProbeScheduleBuffer
{
	uint counts[4];
	uint ids[30720];
	uint commands[9];
};

layout(buffer_reference, scalar) buffer WPTraceSummaryBuffer
{
	WPTraceSummary summaries[];
};

WorldProbeBuffer getWorldProbeBuffer()
{
	return WorldProbeBuffer(getABTGlobalAddress(ABT_WorldProbes));
}
WorldProbeScheduleBuffer getWorldProbeScheduleBuffer()
{
	return WorldProbeScheduleBuffer(getABTGlobalAddress(ABT_WorldProbeSchedule));
}
WPTraceSummaryBuffer getWorldProbeSummaryBuffer()
{
	return WPTraceSummaryBuffer(getABTGlobalAddress(ABT_WorldProbeSummary));
}

uint wpIndex(uvec3 c, uvec3 d) { return c.x + d.x * (c.y + d.y * c.z); }
uvec3 wpCoord(uint i, uvec3 d) { return uvec3(i % d.x, (i / d.x) % d.y, i / (d.x * d.y)); }
ivec3 wpFloorMod(ivec3 a, ivec3 b) { return ((a % b) + b) % b; }

uint wpPhysicalIndex(ivec3 cell, WPCascadeDesc c)
{
	return c.dimsCount.w + wpIndex(uvec3(wpFloorMod(cell, ivec3(c.dimsCount.xyz))), c.dimsCount.xyz);
}

ivec3 wpSlotCell(uint localId, WPCascadeDesc c)
{
	return c.baseCell.xyz + wpFloorMod(ivec3(wpCoord(localId, c.dimsCount.xyz)) - c.baseCell.xyz,
		ivec3(c.dimsCount.xyz));
}

bool wpScheduledProbe(uint queue, uint slot, out uint id)
{
	id = 0u;
	if (queue >= WP_QUEUE_COUNT) return false;
	WorldProbeScheduleBuffer s = getWorldProbeScheduleBuffer();
	if (slot >= min(s.counts[queue], WP_COUNT)) return false;
	id = s.ids[queue * WP_COUNT + slot];
	return id < getWorldProbeBuffer().header.layoutInfo.y;
}

bool wpRoundRobinContains(uint id, uint start, uint count)
{
	return ((id + WP_PER_CASCADE - start) % WP_PER_CASCADE) < count;
}

vec2 wpSign(vec2 v) { return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0); }

vec3 wpOctVector(vec2 uv)
{
	vec2 f = uv * 2.0 - 1.0;
	vec3 v = vec3(f, 1.0 - abs(f.x) - abs(f.y));
	if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * wpSign(v.xy);
	return v;
}

vec2 wpOctUV(vec3 d)
{
	d /= max(abs(d.x) + abs(d.y) + abs(d.z), 1e-8);
	if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * wpSign(d.xy);
	return d.xy * 0.5 + 0.5;
}

vec2 wpRayUV(uint index)
{
	if ((index & WP_QUARTER_DIRECTION) != 0u)
	{
		uint q = index & ~WP_QUARTER_DIRECTION;
		return (vec2(uvec2(q & 3u, q >> 2u) * 2u) + 1.0) / 8.0;
	}
	return (vec2(index & 7u, index >> 3u) + 0.5) / 8.0;
}

vec3 wpRayDirection(uint index) { return normalize(wpOctVector(wpRayUV(index))); }
uint wpLogicalRayIndex(uint logical, bool quarter)
{
	return quarter ? WP_QUARTER_DIRECTION | ((logical & 7u) / 2u + ((logical >> 3u) / 2u) * 4u) : logical;
}

uint wpRayClass(uvec4 masks, uint ray)
{
	return (masks[ray / 16u] >> (2u * (ray % 16u))) & 3u;
}

uint wpHash(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16);
}

float wpRandom(uint x) { return float(wpHash(x) >> 8) / 16777216.0; }

float wpNormalWeight(vec3 n, uint axis)
{
	vec3 w = abs(n); w *= w; w *= w; w *= w;
	float sum = w.x + w.y + w.z;
	uint component = axis >> 1u;
	bool match = (axis & 1u) == 0u ? n[component] > 0.0 : n[component] < 0.0;
	return match && sum > 1e-12 ? w[component] / sum : 0.0;
}

ivec2 wpInteriorBase(uint id) { return ivec2(uvec2(id % 128u, id / 128u) * 10u) + 1; }

// Tile coordinates [0,9] -> folded interior coordinates [0,7].
ivec2 wpFoldBorder(ivec2 p)
{
	if (p.x == 0 && p.y == 0) return ivec2(7,7);
	if (p.x == 9 && p.y == 0) return ivec2(0,7);
	if (p.x == 0 && p.y == 9) return ivec2(7,0);
	if (p.x == 9 && p.y == 9) return ivec2(0,0);
	if (p.x == 0) return ivec2(0, 8 - p.y);
	if (p.x == 9) return ivec2(7, 8 - p.y);
	if (p.y == 0) return ivec2(8 - p.x, 0);
	return ivec2(8 - p.x, 7);
}

vec2 wpVisibilityMoments(uint id, vec3 direction)
{
	vec2 pixel = vec2(wpInteriorBase(id)) + wpOctUV(direction) * 8.0;
	SceneData scene = getSceneData();

	return SampleTextureLod(
		scene.renderTargetIDs.worldProbeVisibilityID,
		pixel / vec2(1280.0, 800.0), 0.0).rg;
}

float wpVisibility(uint id, vec3 delta, WPCascadeDesc c, WPHeader h)
{
	float dist = length(delta);
	if (dist < 1e-5) return 1.0;
	vec2 m = wpVisibilityMoments(id, delta / dist);
	float z = clamp(max(dist - c.trace.w, 0.0) / c.trace.y, 0.0, 1.0);
	if (z <= m.x) return 1.0;
	float variance = max(m.y - m.x*m.x, h.relocation1.z);
	float d = z - m.x;
	float p = variance / (variance + d*d);
	return p*p*p;
}

bool wpGridCell(WPCascadeDesc c, vec3 position, out ivec3 base, out vec3 f)
{
	vec3 g = (position - c.originSpacing.xyz) / c.originSpacing.w;
	base = ivec3(0); f = vec3(0);
	if (any(lessThan(g, vec3(0))) || any(greaterThan(g, vec3(c.dimsCount.xyz) - 1.0))) return false;
	base = min(ivec3(floor(g)), ivec3(c.dimsCount.xyz) - 2);
	f = clamp(g - vec3(base), 0.0, 1.0);
	return true;
}

float wpCascadeEdgeFade(WPCascadeDesc c, vec3 position)
{
	vec3 g = (position - c.originSpacing.xyz) / c.originSpacing.w;
	vec3 d = min(g, vec3(c.dimsCount.xyz) - 1.0 - g);
	vec3 fade = clamp(d / max(c.trace.z, 0.001), 0.0, 1.0);
	return fade.x * fade.y * fade.z;
}

// Maximum number of resident cascades whose probe data is gathered.
const uint WP_MAX_SAMPLE_GATHERS = 2u;

WPLighting wpSampleCascade(
	uint cascade,
	vec3 position,
	vec3 normal,
	vec3 geometricNormal,
	bool volume)
{
	WPLighting r = wpEmptyLighting();

	WorldProbeBuffer b = getWorldProbeBuffer();
	WPHeader h = b.header;
	WPCascadeDesc c = h.cascades[cascade];

	ivec3 base;
	vec3 f;

	if (!wpGridCell(c, position, base, f))
		return r;

	vec3 biased = volume
		? position
		: position + geometricNormal * c.trace.w;

	// Wrap the base corner once. Other corners need only a conditional
	// subtraction, since each coordinate increases by at most one.
	uvec3 dims = c.dimsCount.xyz;
	ivec3 worldBase = c.baseCell.xyz + base;
	uvec3 physicalBase = uvec3(
		wpFloorMod(worldBase, ivec3(dims)));

	// Exactly one signed lobe per component can contribute.
	uvec3 axes = uvec3(
		normal.x < 0.0 ? 1u : 0u,
		normal.y < 0.0 ? 3u : 2u,
		normal.z < 0.0 ? 5u : 4u);

	vec3 skyWeights = normal * normal;

	// Same abs(normal)^8 weighting as wpNormalWeight(),
	// calculated once per gather instead of once per lobe per corner.
	vec3 bounceWeights = skyWeights * skyWeights;
	bounceWeights *= bounceWeights;

	float normalWeightSum =
		bounceWeights.x +
		bounceWeights.y +
		bounceWeights.z;

	bounceWeights = normalWeightSum > 1e-12
		? bounceWeights / normalWeightSum
		: vec3(0.0);

	float sum = 0.0;
	float bounceSupport = 0.0;

	for (uint k = 0u; k < 8u; ++k)
	{
		uvec3 o = uvec3(
			k & 1u,
			(k >> 1u) & 1u,
			(k >> 2u) & 1u);

		vec3 bw = mix(1.0 - f, f, vec3(o));
		float spatialWeight = bw.x * bw.y * bw.z;

		if (spatialWeight <= 0.0)
			continue;

		ivec3 cell = worldBase + ivec3(o);

		uvec3 physical = physicalBase + o;
		if (physical.x >= dims.x) physical.x -= dims.x;
		if (physical.y >= dims.y) physical.y -= dims.y;
		if (physical.z >= dims.z) physical.z -= dims.z;

		uint id = c.dimsCount.w + wpIndex(physical, dims);

		uvec4 state = b.probes[id].state;

		if (state.x == 0u || state.y != h.revisions.y)
			continue;

		if (any(notEqual(b.probes[id].cell.xyz, cell)))
			continue;

		vec3 p = b.probes[id].position.xyz;

		float front = 1.0;

		if (!volume)
		{
			vec3 toProbe = p - position;
			float distanceSquared = dot(toProbe, toProbe);

			if (distanceSquared >= 1e-10)
			{
				front = max(
					dot(
						toProbe * inversesqrt(distanceSquared),
						geometricNormal),
					0.0);
			}
		}

		float w = spatialWeight * front;

		// Reject back-facing corners before the visibility texture lookup.
		if (w <= 0.0)
			continue;

		w *= wpVisibility(id, biased - p, c, h);

		if (w < 1e-8)
			continue;

		if (volume)
		{
			// wp_sky_mean.comp refreshes this after the last probe mutation.
			// Match its fixed 128 x 80 layout; alpha is mean sky visibility.
			vec4 skyMean = SampleTextureFetch(
				getSceneData().renderTargetIDs.worldProbeSkyMeanID,
				ivec2(id % 128u, id / 128u), 0);

			r.sky += skyMean.rgb * w;
			r.skyVisibility += skyMean.a * w;
		}
		else
		{
			// Three sky + three bounce lobes instead of six + six.
			for (uint component = 0u; component < 3u; ++component)
			{
				float sw = skyWeights[component];
				float nw = bounceWeights[component];

				if (sw <= 0.0 && nw <= 0.0)
					continue;

				uint axis = axes[component];

				vec4 sky = b.probes[id].sky[axis];
				r.sky += sky.rgb * (sw * w);
				r.skyVisibility += sky.a * (sw * w);

				if (nw > 0.0)
				{
					vec4 cachedBounce = b.probes[id].bounce[axis];

					float support = w * nw;
					float confidence = clamp(
						cachedBounce.a, 0.0, 1.0);

					r.bounce +=
						cachedBounce.rgb * (support * confidence);

					bounceSupport += support;
				}
			}
		}

		sum += w;
	}

	if (sum <= 1e-8)
		return r;

	float inverseSum = 1.0 / sum;

	r.sky *= inverseSum;
	r.skyVisibility *= inverseSum;
	r.coverage = min(sum / 0.05, 1.0);
	r.valid = true;

	// Keep confidence in the resulting signal.
	// The caller applies spatial coverage.
	r.bounce = bounceSupport > 1e-8
		? r.bounce / bounceSupport
		: vec3(0.0);

	return r;
}

WPLighting wpSampleCascades(
	vec3 position,
	vec3 normal,
	vec3 geometricNormal,
	bool volume)
{
	WPLighting r = wpEmptyLighting();

	WPHeader h = getWorldProbeBuffer().header;

	if (h.layoutInfo.y == 0u)
		return r;

	uint cascadeCount = min(h.layoutInfo.x, 5u);
	uint gathers = 0u;

	float remaining = 1.0;
	float used = 0.0;

	for (uint c = 0u; c < cascadeCount; ++c)
	{
		if (remaining <= 1e-5 ||
			gathers >= WP_MAX_SAMPLE_GATHERS)
		{
			break;
		}

		float edge = wpCascadeEdgeFade(
			h.cascades[c], position);

		if (edge <= 0.0)
			continue;

		// Count attempted gathers, including unsupported neighborhoods.
		// Otherwise invalid probes could still trigger all five gathers.
		++gathers;

		WPLighting s = wpSampleCascade(
			c, position, normal, geometricNormal, volume);

		if (!s.valid)
			continue;

		float support = clamp(s.coverage * edge, 0.0, 1.0);
		float weight = remaining * support;

		r.sky += s.sky * weight;
		r.bounce += s.bounce * weight;
		r.skyVisibility += s.skyVisibility * weight;

		used += weight;
		remaining *= 1.0 - support;
	}

	if (used > 1e-8)
	{
		float inverseUsed = 1.0 / used;

		r.sky *= inverseUsed;
		r.bounce *= inverseUsed;
		r.skyVisibility *= inverseUsed;
		r.coverage = used;
		r.valid = true;
	}

	return r;
}

WPLighting sampleWorldProbes(vec3 position, vec3 normal, vec3 geometricNormal)
{
	return wpSampleCascades(position, normal, geometricNormal, false);
}


WPVolume sampleWorldProbesVolume(vec3 position)
{
	WPLighting r = wpSampleCascades(position, vec3(0), vec3(0), true);
	return WPVolume(r.sky, r.skyVisibility, r.valid, r.coverage);
}

vec3 wpSkyAt(vec3 pWS, vec3 dir, WPHeader header)
{
	SceneData s = getSceneData();
	AtmosphereParameters p = getSceneAtmosphereParameters();
	float h; vec3 up;
	atmosphereCamera(p, pWS, s.atmospherePlacement, h,up);
	bool ground = dot(up,dir) < atmosphereHorizonCosine(p,h);
	return max(integrateAtmosphereSky(p, h, up, dir, normalize(s.sunlightDirection.xyz),
		atmosphereSolarIlluminance(), header.gather.z, header.gather.w,
		atmosphereGroundAlbedo(), atmosphereGroundSkylight(up), 16u, ground), vec3(0));
}

#endif
