#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) flat in uint inPackedID;

layout(location = 0) out uvec2 outVisibility;
layout(location = 1) out vec2 outViewSpaceNormal;

void main()
{
	outVisibility = uvec2(inPackedID, uint(gl_PrimitiveID) | uint(gl_FrontFacing));

	vec3 n = inViewNormal;
	if (!gl_FrontFacing) n = -n;
	outViewSpaceNormal = octEncode(n);
}
