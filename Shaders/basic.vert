#version 330 core

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inCol;

out vec4 channelCol;

uniform mat4 uModel;
uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * uModel * vec4(inPos, 0.0, 1.0);
    channelCol = inCol;
}
