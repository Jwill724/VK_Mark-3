#ifndef TEXTURES_GLSL
#define TEXTURES_GLSL

// ==============================
// === GLOBAL BINDLESS IMAGES ===
// ==============================
layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE)
uniform samplerCube envMaps[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER)
uniform sampler2D combinedSamplers[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER)
uniform usampler2D combinedSamplersU[];

#define TEX2D(id) combinedSamplers[nonuniformEXT(id)]
#define TEXU2D(id) combinedSamplersU[nonuniformEXT(id)]
#define TEXCUBE(id) envMaps[nonuniformEXT(id)]

#define INVALID_TEXTURE_ID 0xFFFFFFFFu

vec4 SampleTexture(uint id, vec2 uv) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return texture(TEX2D(id), uv);
}

// Exact float/normalized/depth texel read; no UV conversion or filtering.
vec4 SampleTextureFetch(uint id, ivec2 pixel, int lod) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return texelFetch(TEX2D(id), pixel, lod);
}

// Dimensions of a sampled 2D image at the requested mip.
ivec2 SampleTextureSize(uint id, int lod) {
	if (id == INVALID_TEXTURE_ID) {
		return ivec2(0);
	}
	return textureSize(TEX2D(id), lod);
}

uvec4 SampleTexelFetch(uint id, ivec2 uv, int lod) {
	if (id == INVALID_TEXTURE_ID) {
		return uvec4(1u);
	}
	return texelFetch(TEXU2D(id), uv, lod);
}

vec4 SampleTextureLod(uint id, vec2 uv, float lod) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return textureLod(TEX2D(id), uv, lod);
}

vec4 SampleTextureGrad(uint id, vec2 uv, vec2 dx, vec2 dy) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return textureGrad(TEX2D(id), uv, dx, dy);
}

vec4 SampleTextureGradTAA(uint id, vec2 uv, vec2 dx, vec2 dy, float artBias) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}

	vec4  taa   = getSceneData().taaMipParams;
	float scale = exp2(artBias);

	if (taa.x != 0.0) {
		vec2  texSize = vec2(SampleTextureSize(id, 0));
		float rho     = max(length(dx * texSize), length(dy * texSize));
		float lod     = log2(max(rho, 1e-6)) + artBias;
		float fade    = 1.0 - saturate((lod - taa.y) * taa.z);
		scale = exp2(artBias + taa.x * fade);
	}

	return textureGrad(TEX2D(id), uv, dx * scale, dy * scale);
}

vec4 SampleTextureBiasTAA(uint id, vec2 uv, float artBias) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
#if defined(GL_FRAGMENT_SHADER) || defined(FRAGMENT_SHADER)
	vec4  taa  = getSceneData().taaMipParams;
	float lod  = textureQueryLod(TEX2D(id), uv).y + artBias;
	float fade = 1.0 - saturate((lod - taa.y) * taa.z);
	return texture(TEX2D(id), uv, artBias + taa.x * fade);
#else
	return textureLod(TEX2D(id), uv, 0.0);
#endif
}

vec4 SampleCube(uint id, vec3 dir) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(0.0);
	}
	return texture(TEXCUBE(id), dir);
}

vec4 SampleCubeLod(uint id, vec3 dir, float lod) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(0.0);
	}
	return textureLod(TEXCUBE(id), dir, lod);
}

int SampleCubeQueryLevels(uint id) {
	if (id == INVALID_TEXTURE_ID) {
		return 0;
	}
	return textureQueryLevels(TEXCUBE(id));
}

vec4 SampleTextureBias(uint id, vec2 uv, float bias) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
// Only the fragment path is used, allows compilation in compute shader
#if defined(GL_FRAGMENT_SHADER) || defined(FRAGMENT_SHADER)
	return texture(TEX2D(id), uv, bias);
#else
	return textureLod(TEX2D(id), uv, 0.0);
#endif
}

vec2 SpatioTemporalNoise(ivec2 pixCoord, uint texId, uint noiseIndex) {
	uint index = SampleTexelFetch(texId, ivec2(pixCoord % 64), 0).r;
	index += 288u * noiseIndex;
	return vec2(fract(0.5 + index * vec2(0.75487766624669276005, 0.5698402909980532659114)));
}

#endif // TEXTURES_GLSL
