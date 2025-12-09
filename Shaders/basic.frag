#version 330 core

in vec4 channelCol;
out vec4 outCol;

uniform float uAlpha;

void main()
{
    outCol = vec4(channelCol.rgb, channelCol.a * uAlpha);
}
