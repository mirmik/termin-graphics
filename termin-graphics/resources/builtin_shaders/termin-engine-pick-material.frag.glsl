#version 330 core

uniform vec3 u_pickColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(u_pickColor, 1.0);
}
