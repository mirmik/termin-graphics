#version 330 core

in float v_linear_depth;
out vec4 FragColor;

void main()
{
    float d = clamp(v_linear_depth, 0.0, 1.0);
    FragColor = vec4(d, 0.0, 0.0, 1.0);
}
