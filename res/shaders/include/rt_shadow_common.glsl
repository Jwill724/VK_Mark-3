#ifndef RTSHADOW_COMMON_GLSL
#define RTSHADOW_COMMON_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_params.glsl"
#include "depth.glsl"

// 8 = balanced; 4 = shorter cycle; 1 = static screen-space pattern.
const uint RT_SHADOW_NOISE_FRAMES = 8u;

// 4 = refresh each eligible lit pixel once per four frames.
// 2 = more responsive reuse; 1 = trace every front-facing surface pixel.
const uint RT_SHADOW_LIT_REFRESH_FRAMES = 4u;

// Conservative endpoint threshold in the stored SIGMA shadow encoding.
// The CPU saturationEps can tighten this further, but cannot loosen it.
const float RT_SHADOW_MAX_REUSE_EPS = 0.001;

layout(push_constant) uniform RTShadowPush
{
	vec2 resolution;
	vec2 invResolution;

	RTShadowParams shadow;

	uint rayBase;
	uint rayCapacity;
	uint shadowStbnID;

	float saturationEps;
	float disocclusionScale;
	uint pad0[3];
} sp;


// NVIDIA uniform vec2 STBN: 128x128 pixels, 64 frames.
// The asset packs frames into an 8x8 grid of tiles in one 1024x1024 RG8 atlas.
// Preserve frame order and pixel coordinates: no R2 offset or per-frame shuffle.
vec2 rtShadowSTBN(ivec2 px, uint atlasID, uint frameIndex)
{
	uvec2 localPx = uvec2(px) & uvec2(127u);
	uint frame = frameIndex & 63u;
	uvec2 tile = uvec2(frame & 7u, frame >> 3u);
	ivec2 atlasPx = ivec2(tile * 128u + localPx);

	// RG8_UNORM data, sampled exactly at mip 0. Never sample this as an integer
	// image, sRGB image, or filtered/interpolated sequence.
	vec2 encoded = texelFetch(TEX2D(atlasID), atlasPx, 0).rg;

	// Decode to centers of the 256 quantization bins, strictly inside (0, 1).
	return (encoded * 255.0 + 0.5) * (1.0 / 256.0);
}

#endif
