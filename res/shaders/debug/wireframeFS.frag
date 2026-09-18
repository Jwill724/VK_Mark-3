#version 460

#extension GL_ARB_separate_shader_objects : require

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(0.0f, 1.0f, 0.0f, 1.0f); // green debug color
}
