#version 460

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
	DebugVertex v = getDebugVertexBuffer().vertices[gl_VertexIndex];
	SceneData scene = getSceneData();
	gl_Position = scene.viewProjUnjittered * vec4(v.position, 1.0);
	outColor = unpackDebugColor(v.colorPacked);
}
