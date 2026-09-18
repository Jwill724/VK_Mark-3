#version 460

#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) flat in uint inMaterialID;

void main()
{
	Material mat = getMaterialBuffer().materials[inMaterialID];

	float cutoff = (mat.alphaCutoff < 1.0) ? mat.alphaCutoff : 0.0;
	if (cutoff <= 0.0) return;

	float a = SampleTextureLod(mat.albedoID, inUV, 0.0).a * mat.colorFactor.a;
	if (a < cutoff) discard;
}
