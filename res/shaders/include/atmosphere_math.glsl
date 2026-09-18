#ifndef ATMOSPHERE_MATH_GLSL
#define ATMOSPHERE_MATH_GLSL

struct AtmosphereParameters
{
	vec4 rayleigh;
	vec4 mie;
	vec4 absorption;
	vec4 geometry;
	uvec4 integration;
};

// h in km above the bottom sphere. Factoring avoids subtracting planet radii squared.
float atmosphereHorizonCosine(AtmosphereParameters p, float h)
{
	float r = p.geometry.x + h;
	return -sqrt(max(h * (2.0 * p.geometry.x + h), 0.0)) / r;
}

float atmosphereDistanceToTop(AtmosphereParameters p, float h, float mu)
{
	float r = p.geometry.x + h;
	float q = max((p.geometry.y - h) * (2.0 * p.geometry.x + p.geometry.y + h), 0.0);
	float b = r * mu;
	float root = sqrt(max(b * b + q, 0.0));
	return b >= 0.0 ? q / max(root + b, 1e-8) : root - b;
}

// Explicit M1 mapping, not a claim to reproduce the book's unspecified mapping.
// x: square-root distance in cosine from the geometric horizon to zenith.
// y: square-root altitude. Both physical endpoints lie at texel centers.
vec2 atmosphereTransmittanceUV(AtmosphereParameters p, float h, float mu, vec2 extent)
{
	h = clamp(h, 0.0, p.geometry.y);
	float horizon = atmosphereHorizonCosine(p, h);
	vec2 x = sqrt(clamp(vec2((mu - horizon) / (1.0 - horizon), h / p.geometry.y), 0.0, 1.0));
	return (0.5 + x * (extent - 1.0)) / extent;
}

void atmosphereTransmittanceDecode(AtmosphereParameters p, vec2 uv, vec2 extent, out float h, out float mu)
{
	vec2 x = clamp((uv * extent - 0.5) / (extent - 1.0), 0.0, 1.0);
	h = x.y * x.y * p.geometry.y;
	float horizon = atmosphereHorizonCosine(p, h);
	mu = mix(horizon, 1.0, x.x * x.x);
}

vec3 atmosphereExtinction(AtmosphereParameters p, float h)
{
	h = max(h, 0.0);
	float rhoR = exp(-h / p.rayleigh.w);
	float rhoM = exp(-h / p.mie.w);
	float rhoA = max(1.0 - abs(h - p.geometry.z) / p.geometry.w, 0.0) * p.absorption.w;
	return p.rayleigh.rgb * rhoR + p.mie.rgb * rhoM + p.absorption.rgb * rhoA;
}

vec3 integrateAtmosphereTransmittance(AtmosphereParameters p, float h, float mu)
{
	float distanceToTop = atmosphereDistanceToTop(p, h, mu);
	uint samples = clamp(p.integration.x, 32u, 1024u);
	float stepLength = distanceToTop / float(samples);
	float R = p.geometry.x;
	float r = R + h;
	vec3 opticalDepth = vec3(0.0);

	for (uint i = 0u; i < samples; ++i)
	{
		float t = (float(i) + 0.5) * stepLength;
		float delta = h * (2.0 * R + h) + t * (2.0 * r * mu + t);
		float sampleRadius = sqrt(max(R * R + delta, 0.0));
		float sampleHeight = max(delta / (sampleRadius + R), 0.0);
		opticalDepth += atmosphereExtinction(p, sampleHeight) * stepLength;
	}

	return exp(-opticalDepth);
}

#endif
