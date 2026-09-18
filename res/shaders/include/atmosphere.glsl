#ifndef ATMOSPHERE_GLSL
#define ATMOSPHERE_GLSL

#include "atmosphere_math.glsl"

vec3 sampleAtmosphereTransmittanceHM(AtmosphereParameters p, float h, float mu)
{
	if (h < 0.0) return vec3(0.0);
	h = clamp(h, 0.0, p.geometry.y);
	if (mu < atmosphereHorizonCosine(p, h)) return vec3(0.0);

	AtmosphereResource resources = getAtmosphereResourceUBO();
	vec2 uv = atmosphereTransmittanceUV(p, h, clamp(mu, -1.0, 1.0),
		vec2(resources.transmittanceExtent.xy));
	return SampleTextureLod(resources.textureIDs.x, uv, 0.0).rgb;
}

float atmosphereHeight(AtmosphereParameters p, vec3 offsetKm)
{
	float R = p.geometry.x;
	float radius = length(offsetKm + vec3(0.0, R, 0.0));
	return (dot(offsetKm, offsetKm) + 2.0 * R * offsetKm.y) / (radius + R);
}

// Camera must be inside the atmosphere shell. World +Y is up at the sea-level anchor.
void atmosphereCamera(AtmosphereParameters p, vec3 cameraWS, vec4 placement,
	out float h, out vec3 up)
{
	vec3 offset = (cameraWS - placement.xyz) * placement.w;
	up = normalize(offset + vec3(0.0, p.geometry.x, 0.0));
	h = clamp(atmosphereHeight(p, offset), 0.00001, p.geometry.y - 0.00001);
}

void atmosphereBasis(vec3 up, vec3 sunDir, out vec3 right, out vec3 forward)
{
	vec3 projectedSun = sunDir - up * dot(sunDir, up);
	if (dot(projectedSun, projectedSun) > 1e-8)
		right = normalize(projectedSun);
	else
	{
		vec3 axis = abs(up.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		right = normalize(cross(axis, up));
	}
	forward = cross(up, right);
}

float atmosphereRayleighPhase(float mu)
{
	return (3.0 / (16.0 * PI)) * (1.0 + mu * mu);
}

float atmosphereMiePhase(float mu, float g)
{
	g = clamp(g, -0.95, 0.95);
	float d = max(1.0 + g * g - 2.0 * g * mu, 1e-5);
	return (1.0 - g * g) / (4.0 * PI * d * sqrt(d));
}

// Limit is ds as extinction approaches zero; no artificial vacuum extinction.
vec3 atmosphereSegmentIntegral(vec3 sigmaT, float ds)
{
	vec3 tau = sigmaT * ds;
	vec3 exact = (vec3(1.0) - exp(-tau)) / max(sigmaT, vec3(1e-20));
	vec3 series = ds * (vec3(1.0) - tau * 0.5 + tau * tau / 6.0);
	return mix(exact, series, lessThan(tau, vec3(1e-3)));
}

float atmosphereGroundDistance(AtmosphereParameters p, float h, float mu)
{
	float q = h * (2.0 * p.geometry.x + h);
	float b = (p.geometry.x + h) * mu;
	float root = sqrt(max(b * b - q, 0.0));
	return q / max(-b + root, 1e-8);
}

// Split horizon rows prevent interpolation across the planet silhouette.
void atmosphereSkyViewDecode(AtmosphereParameters p, float h, ivec2 cell, ivec2 extent,
	vec3 up, vec3 sunDir, out vec3 dir, out bool hitsGround)
{
	float phi = PI * float(cell.x) / float(extent.x - 1);
	int halfRows = extent.y / 2;
	hitsGround = cell.y < halfRows;
	float t = hitsGround
		? 1.0 - float(cell.y) / float(halfRows - 1)
		: float(cell.y - halfRows) / float(halfRows - 1);
	float horizon = atmosphereHorizonCosine(p, h);
	float mu = hitsGround ? horizon - (1.0 + horizon) * t * t
		: horizon + (1.0 - horizon) * t * t;
	vec3 right;
	vec3 forward;
	atmosphereBasis(up, sunDir, right, forward);
	dir = up * mu + (right * cos(phi) + forward * sin(phi)) * sqrt(max(1.0 - mu * mu, 0.0));
}

vec2 atmosphereSkyViewUV(AtmosphereParameters p, float h, vec3 dir,
	vec3 up, vec3 sunDir, vec2 extent)
{
	float mu = clamp(dot(dir, up), -1.0, 1.0);
	float horizon = atmosphereHorizonCosine(p, h);
	vec3 right;
	vec3 forward;
	atmosphereBasis(up, sunDir, right, forward);
	vec3 flatDir = dir - up * mu;
	float phi = dot(flatDir, flatDir) > 1e-10
		? acos(clamp(dot(normalize(flatDir), right), -1.0, 1.0)) : 0.0;
	float halfRows = extent.y * 0.5;
	float y;
	if (mu < horizon)
	{
		float t = sqrt(clamp((horizon - mu) / (1.0 + horizon), 0.0, 1.0));
		y = (1.0 - t) * (halfRows - 1.0);
	}
	else
	{
		float t = sqrt(clamp((mu - horizon) / (1.0 - horizon), 0.0, 1.0));
		y = halfRows + t * (halfRows - 1.0);
	}
	return (vec2(phi / PI * (extent.x - 1.0), y) + 0.5) / extent;
}

vec3 atmosphereGroundRadiance(AtmosphereParameters p, float h, vec3 up, vec3 dir,
	float tGround, vec3 sunDir, vec3 solarIlluminance,
	vec3 groundAlbedo, vec3 skyIrradiance)
{
	vec3 groundUp = normalize(up * (p.geometry.x + h) + dir * tGround);
	float muSun = dot(groundUp, sunDir);

	vec3 direct = solarIlluminance
		* sampleAtmosphereTransmittanceHM(p, 0.0, muSun)
		* max(muSun, 0.0);

	return groundAlbedo * (1.0 / PI) * (direct + skyIrradiance);
}

vec3 integrateAtmosphereSky(AtmosphereParameters p, float h, vec3 up, vec3 dir,
	vec3 sunDir, vec3 solarIlluminance, float mieAlbedo, float mieG,
	vec3 groundAlbedo, vec3 skyIrradiance, uint samples, bool hitsGround)
{
	float mu = dot(up, dir);
	float distanceToBoundary = hitsGround ? atmosphereGroundDistance(p, h, mu)
		: atmosphereDistanceToTop(p, h, mu);
	float cosSun = clamp(dot(dir, sunDir), -1.0, 1.0);
	float phaseR = atmosphereRayleighPhase(cosSun);
	float phaseM = atmosphereMiePhase(cosSun, mieG);
	vec3 accum = vec3(0.0);
	vec3 transmit = vec3(1.0);
	float R = p.geometry.x;
	float r = R + h;

	samples = clamp(samples, 16u, 256u);
	for (uint i = 0u; i < samples; ++i)
	{
		float u0 = float(i) / float(samples);
		float u1 = float(i + 1u) / float(samples);
		float t0 = u0 * u0 * distanceToBoundary;
		float t1 = u1 * u1 * distanceToBoundary;
		float t = (t0 + t1) * 0.5;
		float ds = t1 - t0;
		float delta = h * (2.0 * R + h) + t * (2.0 * r * mu + t);
		float sampleRadius = sqrt(max(R * R + delta, 0.0));
		float sampleH = max(delta / (sampleRadius + R), 0.0);
		vec3 sampleUp = normalize(up * r + dir * t);
		vec3 sunT = sampleAtmosphereTransmittanceHM(p, sampleH, dot(sampleUp, sunDir));
		vec3 sigmaR = p.rayleigh.rgb * exp(-sampleH / p.rayleigh.w);
		vec3 sigmaM = p.mie.rgb * clamp(mieAlbedo, 0.0, 1.0) * exp(-sampleH / p.mie.w);
		vec3 sigmaT = atmosphereExtinction(p, sampleH);
		vec3 source = solarIlluminance * sunT * (sigmaR * phaseR + sigmaM * phaseM);
		accum += transmit * source * atmosphereSegmentIntegral(sigmaT, ds);
		transmit *= exp(-sigmaT * ds);
	}

	if (hitsGround)
		accum += transmit * atmosphereGroundRadiance(p, h, up, dir,
			distanceToBoundary, sunDir, solarIlluminance, groundAlbedo, skyIrradiance);

	return accum;
}

vec3 atmosphereViewRay(vec2 uv, mat4 invProj, mat4 invView)
{
	vec4 view = invProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
	return normalize(mat3(invView) * (view.xyz / view.w));
}

#endif
