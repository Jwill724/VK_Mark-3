#ifndef SHADOW_GLSL
#define SHADOW_GLSL

#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

const float FLASHLIGHT_TEXEL_SIZE = 1.0 / 1024.0;
const float FLASHLIGHT_OFFSET_TEXELS = 1.5;

const float CASCADE_BLEND_FRACTION     = 0.9;
const float CASCADE_LAST_FADE_FRACTION = 0.98;

// The only number that works
const float MIN_SHADOW_BIAS = 0.0001;

const float SHADOW_BASE_BIAS_TEXELS  = 0.5;
const float SHADOW_SLOPE_BIAS_TEXELS = 0.8;

const float PCSS_MIN_OFFSET_TEXELS = 0.25;

const float PCSS_TAPS_PER_TEXEL = 1.75;

const int PCF_FILTER_TAPS      = 16;
const int PCSS_SEARCH_TAPS     = 12;
const int PCSS_FILTER_TAPS_MIN = 6;
const int PCSS_FILTER_TAPS_MAX = 24;


struct CascadeProj
{
	vec2  atlasUV;
	vec2  atlasMin;
	vec2  atlasMax;
	vec2  depthGrad;
	vec3  worldPos;
	vec3  normalWS;
	vec3  L;
	float depth;
	float maxPlaneBias;
	float radius;
	float searchRadius;
	float worldTexel;
	float ndcPerWorld;
	uint  index;
	uint  filterMode;
	bool  valid;
};

// 8-tap Poisson disk in texels
const vec2 poisson8[8] = vec2[](
  vec2( 0.0, -0.5), vec2( 0.5,  0.0), vec2( 0.0,  0.5), vec2(-0.5,  0.0),
  vec2( 0.35, 0.35), vec2(-0.35, 0.35), vec2( 0.35,-0.35), vec2(-0.35,-0.35)
);

float gaussianWeight(vec2 diskPos)
{
	float d2 = dot(diskPos, diskPos);
	return exp(-d2 * 2.0);
}

// Pushes the receiver along its normal to escape self-shadowing acne.
// Offset grows with grazing angle and is scaled into world units by texel size.
vec3 computeNormalOffset(vec3 normalWS, vec3 L, float texelSizeWorld)
{
	float nDotL = dot(normalWS, L);
	float slope = sqrt(clamp(1.0 - nDotL * nDotL, 0.0, 1.0));
	float offsetAmount = (SHADOW_BASE_BIAS_TEXELS + slope * SHADOW_SLOPE_BIAS_TEXELS) * texelSizeWorld;
	return normalWS * offsetAmount;
}

// Ortho => w==1, so row-2 of the linear part is the gradient; length is 1/depthRange.
float ndcDepthPerWorld(mat4 cascadeVP)
{
	return length(vec3(cascadeVP[0][2], cascadeVP[1][2], cascadeVP[2][2]));
}

float computeMaxPlaneBias(float radiusTexels, float worldPerTexel, float ndcPerWorld)
{
	return (radiusTexels * worldPerTexel) * ndcPerWorld;
}

float spotWorldTexel(float distToLight, float outerCos, float texel)
{
	float tanHalf = sqrt(max(1.0 - outerCos * outerCos, 1e-6)) / max(outerCos, 1e-3);
	return 2.0 * distToLight * tanHalf * texel;
}

vec2 vogelSample(int i, int count, float phi)
{
	float r     = sqrt(float(i) + 0.5) / sqrt(float(count));
	float theta = float(i) * 2.4 + phi;
	return vec2(r * cos(theta), r * sin(theta));
}

vec2 vogelSample(int i, int count, float phi, float jitter)
{
	float r     = sqrt(float(i) + 0.5 + jitter) / sqrt(float(count) + jitter);
	float theta = float(i) * 2.4 + phi;
	return vec2(r * cos(theta), r * sin(theta));
}

float rotationPhase(mat2 rot)
{
	return atan(rot[0][1], rot[0][0]);
}

// Used for volumetric directional and flashlight
float PCFPoissonLow(
	mat2  poissonRotation,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float bias,
	float texel)
{
	float sum      = 0.0;
	float depthPos = receiverDepth + bias;
	for (int i = 0; i < 8; ++i) {
		vec2 offset = (poissonRotation * poisson8[i]) * texel;
		float depthSample = SampleTexture(shadowMapID, shadowUV + offset).r;
		sum += float(depthPos < depthSample);
	}

	return sum * (1.0 / 8.0);
}

float PCFVogel(
	float phi,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float texel,
	float radius,
	vec2  atlasMin,
	vec2  atlasMax,
	vec2  depthGradient,
	float maxPlaneBias,
	float jitter,
	int   taps)
{
	float samplePos = texel * radius;
	float sum       = 0.0;
	float weightSum = 0.0;

	for (int i = 0; i < taps; ++i) {
		vec2 diskPos      = vogelSample(i, taps, phi, jitter);
		vec2 sampleUV     = clamp(shadowUV + diskPos * samplePos, atlasMin, atlasMax);
		vec2 actualOffset = sampleUV - shadowUV;

		float planeBias = clamp(dot(actualOffset, depthGradient), -maxPlaneBias, maxPlaneBias);
		float depthPos  = receiverDepth + planeBias + MIN_SHADOW_BIAS;

		float depthSample = SampleTexture(shadowMapID, sampleUV).r;
		float shadow      = depthSample >= depthPos ? 1.0 : 0.0;

		float w    = gaussianWeight(diskPos);
		sum       += shadow * w;
		weightSum += w;
	}

	return weightSum > 1e-6 ? sum / weightSum : 1.0;
}

float blockerSearch(
	CascadeProj p,
	float phi,
	float jitter,
	uint  shadowMapID,
	float texel,
	out float avgBlockerDepth)
{
	float samplePos = texel * p.searchRadius;

	float sum   = 0.0;
	float count = 0.0;

	for (int i = 0; i < PCSS_SEARCH_TAPS; ++i) {
		vec2 diskPos      = vogelSample(i, PCSS_SEARCH_TAPS, phi, jitter);
		vec2 sampleUV     = clamp(p.atlasUV + diskPos * samplePos, p.atlasMin, p.atlasMax);
		vec2 actualOffset = sampleUV - p.atlasUV;

		float planeBias = clamp(dot(actualOffset, p.depthGrad), -p.maxPlaneBias, p.maxPlaneBias);
		float depthPos  = p.depth + planeBias + MIN_SHADOW_BIAS;

		float depthSample = SampleTexture(shadowMapID, sampleUV).r;
		float isBlocker   = float(depthSample < depthPos);

		sum   += depthSample * isBlocker;
		count += isBlocker;
	}

	avgBlockerDepth = count > 0.0 ? sum / count : 0.0;
	return count;
}

// NDC depth gap -> world distance along the light axis -> penumbra radius -> texels.
float penumbraRadiusTexels(CascadeProj p, float avgBlockerDepth, vec4 pcss, out float gapWorld)
{
	float deltaNDC = max(p.depth - avgBlockerDepth, 0.0);
	gapWorld       = deltaNDC / max(p.ndcPerWorld, 1e-9);

	float penumbraWorld = gapWorld * pcss.x;
	return clamp(penumbraWorld / max(p.worldTexel, 1e-9), pcss.y, p.searchRadius);
}

// Offset keyed to measured blocker distance, not kernel radius. Floors at the
// contact value so a touching blocker biases exactly like the PCF path.
float pcssNormalOffsetTexels(float radius, float gapWorld, float worldTexel, vec4 pcss, vec4 pcssBias)
{
	float contact  = max(pcssBias.x, PCSS_MIN_OFFSET_TEXELS);
	float wanted   = clamp(radius, contact, max(pcss.w, contact));
	float gapLimit = (gapWorld * pcssBias.y) / max(worldTexel, 1e-9);

	return min(wanted, max(gapLimit, contact));
}

// Right handed view looks down -Z
uint cascadeViewDepthSplit(float viewDepth, uint cascadeCount, vec4 cascadeSplits)
{
	uint cascadeIdx = cascadeCount - 1u;
	for (uint i = 0u; i < cascadeCount; ++i) {
		if (viewDepth < cascadeSplits[i]) {
			cascadeIdx = i;
			break;
		}
	}

	return cascadeIdx;
}

vec3 cascadeColor(uint i)
{
	const vec3 C[4] = vec3[](
		vec3(1, 0, 0),
		vec3(0, 1, 0),
		vec3(0, 0, 1),
		vec3(1, 1, 0)
	);
	return C[min(i, 3u)];
}

bool casterOverlapsReceivers(
	vec3 casterCenterWS, vec3 casterExtentWS,
	mat4 lightView,
	vec3 receiverLSMin, vec3 receiverLSMax,
	float lsEpsilon)
{
	mat3 rot = mat3(lightView);
	mat3 absRot = mat3(abs(rot[0]), abs(rot[1]), abs(rot[2]));

	vec3 centerLS = (lightView * vec4(casterCenterWS, 1.0)).xyz;
	vec3 extentLS = absRot * casterExtentWS;

	vec3 cMin = centerLS - extentLS;
	vec3 cMax = centerLS + extentLS;

	// X/Y: shadow projects down light-space Z, need footprint overlap
	if (cMin.x > receiverLSMax.x + lsEpsilon || cMax.x < receiverLSMin.x - lsEpsilon) return false;
	if (cMin.y > receiverLSMax.y + lsEpsilon || cMax.y < receiverLSMin.y - lsEpsilon) return false;

	// Z: keep everything toward the light, reject only fully behind receivers
	if (cMax.z < receiverLSMin.z - lsEpsilon) return false;

	return true;
}

bool casterOverlapsVolumetricReceivers(
	vec3 casterCenterWS,
	vec3 casterExtentWS,
	mat4 lightView,
	vec3 receiverLSMin,
	vec3 receiverLSMax,
	float casterLSMaxZ,
	float lsEpsilon)
{
	mat3 rot = mat3(lightView);
	mat3 absRot = mat3(abs(rot[0]), abs(rot[1]), abs(rot[2]));

	vec3 centerLS = (lightView * vec4(casterCenterWS, 1.0)).xyz;
	vec3 extentLS = absRot * casterExtentWS;

	vec3 cMin = centerLS - extentLS;
	vec3 cMax = centerLS + extentLS;

	if (cMin.x > receiverLSMax.x + lsEpsilon ||
		cMax.x < receiverLSMin.x - lsEpsilon) return false;

	if (cMin.y > receiverLSMax.y + lsEpsilon ||
		cMax.y < receiverLSMin.y - lsEpsilon) return false;

	// Behind receiver volume
	if (cMax.z < receiverLSMin.z - lsEpsilon) return false;

	// Farther toward the light than the shadow projection supports
	if (cMin.z > casterLSMaxZ + lsEpsilon) return false;

	return true;
}

bool cascadeIsActive(uvec4 activeC, uint c)
{
	uint flag = (c == 0u) ? activeC.x
			  : (c == 1u) ? activeC.y
			  : (c == 2u) ? activeC.z
						  : activeC.w;
	return flag != 0u;
}

// Receiver-plane depth gradient. Ortho cascades are affine, so the
// inverse-transpose maps the world normal straight to clip space.
vec2 analyticDepthGradient(vec3 nWS, mat4 invTransVP, vec4 atlas)
{
	vec3 nClip = mat3(invTransVP) * nWS;

	if (abs(nClip.z) < 1e-6) return vec2(0.0);

	float dzdx = -nClip.x / nClip.z;
	float dzdy = -nClip.y / nClip.z;

	return vec2(dzdx * ( 2.0 / atlas.x),
				dzdy * (-2.0 / atlas.y));
}

void projectToCascade(vec3 worldPos, mat4 cascadeVP, vec4 atlas, out vec2 atlasUV, out float depth01)
{
	vec4 lsPos      = cascadeVP * vec4(worldPos, 1.0);
	vec3 projCoords = lsPos.xyz / lsPos.w;
	vec2 uv         = projCoords.xy * 0.5 + 0.5;
	uv.y            = 1.0 - uv.y;
	atlasUV         = uv * atlas.xy + atlas.zw;
	depth01         = projCoords.z;
}

void projectToLightSpace(vec3 worldPos, mat4 light, out vec2 outUV, out float outDepth)
{
	vec4 lsPos      = light * vec4(worldPos, 1.0);
	vec3 projCoords = lsPos.xyz / lsPos.w;
	vec2 uv         = projCoords.xy * 0.5 + 0.5;
	uv.y            = 1.0 - uv.y;
	outUV           = uv;
	outDepth        = projCoords.z;
}

CascadeProj buildCascadeProj(
	ShadowCSM csm, uvec4 activeC, uint c,
	vec3 worldPos, vec3 geometricNormalWS, vec3 L, float texel, uint filterMode)
{
	CascadeProj o;
	o.valid = false;

	if (!cascadeIsActive(activeC, c)) return o;

	const vec4 atlas   = csm.atlasUV[c];
	const mat4 vp      = csm.cascadeVP[c];
	const bool isPCSS  = (filterMode == SUN_SHADOW_FILTER_PCSS);

	o.index        = c;
	o.filterMode   = filterMode;
	o.worldPos     = worldPos;
	o.normalWS     = geometricNormalWS;
	o.L            = L;
	o.worldTexel   = csm.cascadeWorldTexels[c];
	o.ndcPerWorld  = ndcDepthPerWorld(vp);
	o.searchRadius = isPCSS ? csm.maxPcssFilterRadiusTexels[c] * csm.pcss.z
							: csm.maxPcfFilterRadiusTexels[c];
	o.radius       = o.searchRadius;
	o.atlasMin     = atlas.zw;
	o.atlasMax     = atlas.zw + atlas.xy;
	o.depthGrad    = analyticDepthGradient(geometricNormalWS, csm.cascadeInvTransVP[c], atlas);
	o.maxPlaneBias = computeMaxPlaneBias(o.searchRadius, o.worldTexel, o.ndcPerWorld);

	float offsetTexels = isPCSS ? max(csm.pcssBias.x, PCSS_MIN_OFFSET_TEXELS) : o.radius;
	vec3  offsetPos    = worldPos + computeNormalOffset(geometricNormalWS, L, o.worldTexel * offsetTexels);
	projectToCascade(offsetPos, vp, atlas, o.atlasUV, o.depth);

	vec2 shadowUV = (o.atlasUV - atlas.zw) / atlas.xy;
	vec2 margin   = (texel * o.searchRadius) / atlas.xy;

	o.valid = all(greaterThanEqual(shadowUV, margin))
		   && all(lessThanEqual(shadowUV, vec2(1.0) - margin))
		   && o.depth >= 0.0 && o.depth <= 1.0;

	return o;
}

int pcssFilterTaps(float radius)
{
	int taps = PCSS_FILTER_TAPS_MIN + int(ceil(radius * PCSS_TAPS_PER_TEXEL));
	return clamp(taps, PCSS_FILTER_TAPS_MIN, PCSS_FILTER_TAPS_MAX);
}

// PCSS: search -> penumbra estimate -> reproject with a radius-matched normal offset -> filter.
float sampleCascade(
	CascadeProj p, ShadowCSM csm,
	float phiStable, float phiTemporal, float jitter,
	uint shadowMapID, float texel)
{
	if (p.filterMode == SUN_SHADOW_FILTER_PCF)
	{
		return PCFVogel(
			phiTemporal, shadowMapID, p.atlasUV, p.depth, texel, p.radius,
			p.atlasMin, p.atlasMax, p.depthGrad, p.maxPlaneBias, jitter, PCF_FILTER_TAPS);
	}

	float avgBlockerDepth;
	if (blockerSearch(p, phiStable, jitter, shadowMapID, texel, avgBlockerDepth) == 0.0) return 1.0;

	float gapWorld;
	p.radius       = penumbraRadiusTexels(p, avgBlockerDepth, csm.pcss, gapWorld);
	p.maxPlaneBias = computeMaxPlaneBias(p.radius, p.worldTexel, p.ndcPerWorld);

	float offsetTexels = pcssNormalOffsetTexels(p.radius, gapWorld, p.worldTexel, csm.pcss, csm.pcssBias);
	vec3  offsetPos    = p.worldPos + computeNormalOffset(p.normalWS, p.L, p.worldTexel * offsetTexels);
	projectToCascade(offsetPos, csm.cascadeVP[p.index], csm.atlasUV[p.index], p.atlasUV, p.depth);

	return PCFVogel(
		phiTemporal, shadowMapID, p.atlasUV, p.depth, texel, p.radius,
		p.atlasMin, p.atlasMax, p.depthGrad, p.maxPlaneBias, jitter, pcssFilterTaps(p.radius));
}

float sampleSunShadowCSM(
	vec3 worldPos, vec3 geometricNormalWS, vec3 L,
	float viewDepth, vec2 fragCoord, uint filterMode, bool stableNoise)
{
	ShadowCSM   csm          = getShadowCSM();
	const uint  shadowMapID  = uint(csm.params.x);
	const uint  cascadeCount = uint(csm.params.y);
	const float texel        = csm.params.z;

	const uvec4 activeCascades = getShadowCullDataBuffer().data.cascadeActive;

	uint  frameIndex = getSceneData().temporal.x;
	float phiStable  = createPhase(fragCoord);
	float phi        = stableNoise ? phiStable : createPhaseTemporal(fragCoord, frameIndex);
	float jitter     = stableNoise ? 0.0
					 : temporalInterleavedGradientNoise(fragCoord + 37.0, int(frameIndex), 1.0);

	uint cascadeIdx = cascadeViewDepthSplit(viewDepth, cascadeCount, csm.cascadeSplits);

	vec3 Nbias = (dot(geometricNormalWS, L) < 0.0) ? -geometricNormalWS : geometricNormalWS;

	CascadeProj cur;
	cur.valid = false;

	for (uint c = cascadeIdx; c < cascadeCount; ++c)
	{
		CascadeProj p = buildCascadeProj(csm, activeCascades, c,
			worldPos, Nbias, L, texel, filterMode);

		if (p.valid)
		{
			cur        = p;
			cascadeIdx = c;
			break;
		}
	}

	if (!cur.valid) return 1.0;

	float sA     = sampleCascade(cur, csm, phiStable, phi, jitter, shadowMapID, texel);
	float shadow = sA;

	CascadeProj nxt;
	nxt.valid = false;

	for (uint c = cascadeIdx + 1u; c < cascadeCount; ++c)
	{
		CascadeProj p = buildCascadeProj(csm, activeCascades, c,
			worldPos, geometricNormalWS, L, texel, filterMode);

		if (p.valid)
		{
			nxt = p;
			break;
		}
	}

	const float blendEnd = csm.cascadeSplits[cascadeIdx];

	if (nxt.valid)
	{
		float blendStart = blendEnd * CASCADE_BLEND_FRACTION;
		if (viewDepth >= blendStart)
		{
			float sB = sampleCascade(nxt, csm, phiStable, phi, jitter, shadowMapID, texel);
			shadow   = mix(sA, sB, smoothstep(blendStart, blendEnd, viewDepth));
		}
	}
	else
	{
		float blendStart = blendEnd * CASCADE_LAST_FADE_FRACTION;
		if (viewDepth >= blendStart)
			shadow = mix(sA, 1.0, smoothstep(blendStart, blendEnd, viewDepth));
	}

	return shadow;
}

vec3 worldToVolumetricShadowUvz(
	vec3 worldPos,
	mat4 cascadeVP,
	out bool valid)
{
	vec4 lightClip = cascadeVP * vec4(worldPos, 1.0);

	vec3 projCoords = lightClip.xyz / lightClip.w;

	vec2 uv = projCoords.xy * 0.5 + 0.5;

	uv.y = 1.0 - uv.y;

	float z = projCoords.z;

	valid =
		uv.x >= 0.0 &&
		uv.x <= 1.0 &&
		uv.y >= 0.0 &&
		uv.y <= 1.0 &&
		z >= 0.0 &&
		z <= 1.0;

	return vec3(uv, z);
}

float sampleVolumetricShadowMap(
	VolumetricShadowInfo volShadow,
	vec3 worldPos,
	mat2 poissonRotation)
{
	bool valid = false;

	vec3 uvz = worldToVolumetricShadowUvz(
			worldPos,
			volShadow.cascadeVP,
			valid);

	if (!valid) return 1.0;

	const uint shadowMapID = uint(volShadow.params.x);

	const float localTexel = volShadow.params.z;

	const float depthPos = uvz.z + MIN_SHADOW_BIAS;

	float sum = 0.0;

	for (int i = 0; i < 8; ++i)
	{
		vec2 localOffset = (poissonRotation * poisson8[i]) * localTexel;

		vec2 localUV = uvz.xy + localOffset;

		// Two-pixel atlas border safely absorbs the small kernel,
		// but reject anything that goes completely outside the tile.
		localUV = clamp(localUV, vec2(0.0), vec2(1.0));

		float depthSample = SampleTexture(shadowMapID, localUV).r;

		sum += float(depthPos < depthSample);
	}

	return sum * (1.0 / 8.0);
}

#endif
