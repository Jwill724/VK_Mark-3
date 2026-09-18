#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) flat in uint inMaterialID;
layout(location = 3) flat in uint inPackedID;

layout(location = 0) out uvec2 outVisibility;
layout(location = 1) out vec2 outViewSpaceNormal;

void main()
{
	Material mat = getMaterialBuffer().materials[inMaterialID];
	SceneData scene = getSceneData();
	float mipBias = scene.taaMipParams.x;
	float alpha = SampleTextureBiasTAA(mat.albedoID, inUV, mipBias).a * mat.colorFactor.a;
	if (alpha < mat.alphaCutoff) discard;
	outVisibility = uvec2(inPackedID, uint(gl_PrimitiveID) | uint(gl_FrontFacing));

	vec3 n = inViewNormal;
	if (!gl_FrontFacing) n = -n;
	outViewSpaceNormal = octEncode(n);
}
