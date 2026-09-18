#version 460

#extension GL_ARB_separate_shader_objects : require

layout(location = 0) in  vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() { outColor = inColor; }
