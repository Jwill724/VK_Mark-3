#ifndef FROXEL_GLSL
#define FROXEL_GLSL

#include "clustered.glsl"

const uint FROXEL_X = 160u;
const uint FROXEL_Y = 90u;
const uint FROXEL_Z = 64u;

float froxelSliceToDepth(float slice, float nearZ, float farZ)
{
	return nearZ * pow(farZ / nearZ, slice / float(FROXEL_Z));
}

float froxelDepthToSlice(float viewDepth, float nearZ, float farZ)
{
	return log(max(viewDepth, nearZ) / nearZ) / log(farZ / nearZ) * float(FROXEL_Z);
}

float phaseHenyeyGreenstein(float cosTheta, float g)
{
	float g2    = g * g;
	float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001);
	return 1.0 / (4.0 * PI) * (1.0 - g2) / (denom * sqrt(denom));
}

vec3 applyFroxelFog(
	sampler3D froxelIntegrated,
	vec2 uv,
	float viewDepth,
	vec3 color,
	float nearZ,
	float farZ)
{
	if (viewDepth <= nearZ) return color;

	float depth = min(viewDepth, farZ);
	float firstEnd = froxelSliceToDepth(1.0, nearZ, farZ);

	vec4 fog;

	if (depth < firstEnd)
	{
		vec4 first = textureLod(
			froxelIntegrated,
			vec3(uv, 0.5 / float(FROXEL_Z)),
			0.0);

		float fraction = clamp(
			(depth - nearZ) / (firstEnd - nearZ),
			0.0, 1.0);

		float fullTransmit = clamp(first.a, 0.0, 1.0);
		float partialTransmit = pow(fullTransmit, fraction);

		float fullLoss = 1.0 - fullTransmit;

		float scatterFraction = fullLoss > 1e-4
			? (1.0 - partialTransmit) / fullLoss
			: fraction;

		fog = vec4(first.rgb * scatterFraction, partialTransmit);
	}
	else
	{
		float sliceF = froxelDepthToSlice(depth, nearZ, farZ);

		float z = clamp(
			(sliceF - 0.5) / float(FROXEL_Z),
			0.5 / float(FROXEL_Z),
			(float(FROXEL_Z) - 0.5) / float(FROXEL_Z));

		fog = textureLod(
			froxelIntegrated,
			vec3(uv, z),
			0.0);
	}

	return color * fog.a + fog.rgb;
}


#endif